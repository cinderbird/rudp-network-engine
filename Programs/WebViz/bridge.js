// RUDP 엔진의 TelemetrySink(Project/Network/src/telemetry/)와 브라우저를
// 잇는 로컬 실시간 브릿지. 전체 구조는 docs/web-visualization.md 참고.
//
// 역할:
//  1. GameServer.exe(1회)와 RemoteServer.exe(브라우저의 "connect_client"
//     요청마다)를 텔레메트리 포트를 켠 설정 오버라이드로 실행.
//  2. 각 프로세스의 텔레메트리 포트에 TCP로 붙어 줄바꿈 구분 JSON 이벤트를
//     파싱하고, 어느 프로세스에서 왔는지 태그해 모든 브라우저 탭에
//     WebSocket으로 재전송.
//  3. 반대 방향 제어 메시지(set_drop)는 같은 TCP 소켓에 JSON 한 줄로 기록.
//  4. 정적 프론트엔드(public/)를 WebSocket과 같은 포트에서 서빙.

const http = require('http');
const fs = require('fs');
const path = require('path');
const net = require('net');
const { spawn } = require('child_process');
const { WebSocketServer } = require('ws');

const HTTP_PORT = 8090;
const SERVER_TELEMETRY_PORT = 9100;
const CLIENT_TELEMETRY_PORT_BASE = 9101;

const REPO_ROOT = path.resolve(__dirname, '..', '..');

// Release를 우선 찾고, 없으면 Debug로 폴백(README 빌드 안내가 기본으로
// 만드는 건 Release라 클론 직후엔 Debug가 아예 없을 수 있다).
function resolveBinDir() {
	const releaseDir = path.join(REPO_ROOT, 'Bins', 'Release');
	const debugDir = path.join(REPO_ROOT, 'Bins', 'Debug');
	const hasExes = (dir) =>
		fs.existsSync(path.join(dir, 'GameServer.exe')) && fs.existsSync(path.join(dir, 'RemoteServer.exe'));

	if (hasExes(releaseDir)) {
		console.log('[bridge] using Bins/Release');
		return releaseDir;
	}
	if (hasExes(debugDir)) {
		console.log('[bridge] Bins/Release not found (or incomplete) -- using Bins/Debug');
		return debugDir;
	}
	console.error('[bridge] GameServer.exe/RemoteServer.exe not found in Bins/Release or Bins/Debug.');
	console.error('[bridge] Build Toska.sln (Release|x64 recommended) first -- see README.md.');
	process.exit(1);
}

const BIN_DIR = resolveBinDir();
const GAMESERVER_EXE = path.join(BIN_DIR, 'GameServer.exe');
const REMOTESERVER_EXE = path.join(BIN_DIR, 'RemoteServer.exe');
const GAMESERVER_BASE_CONFIG = path.join(REPO_ROOT, 'Project', 'GameServer', 'config', 'DefaultNetworkEngine.json');
const REMOTESERVER_BASE_CONFIG = path.join(REPO_ROOT, 'Project', 'RemoteServer', 'config', 'DefaultNetworkEngine.json');

const TMP_CONFIG_DIR = path.join(__dirname, '.tmp-config');
fs.mkdirSync(TMP_CONFIG_DIR, { recursive: true });

// -- 프로세스/텔레메트리 연결 상태 -------------------------------------------
// 살아있는 프로세스마다 하나씩: { source, proc, socket, buffer }. "server"는
// 항상 있고, "client:<id>"는 브라우저가 연결/해제할 때마다 생기고 없어진다.
const sources = new Map();
let nextClientId = 1;

function writeOverrideConfig(baseConfigPath, overrides, outPath) {
	const base = JSON.parse(fs.readFileSync(baseConfigPath, 'utf8'));
	Object.assign(base.Network, overrides);
	fs.writeFileSync(outPath, JSON.stringify(base, null, 4));
}

function connectTelemetry(source, port, onEvent) {
	const attempt = () => {
		const socket = net.connect({ host: '127.0.0.1', port }, () => {
			console.log(`[bridge] telemetry connected: ${source} (port ${port})`);
			const entry = sources.get(source);
			if (entry) {
				entry.socket = socket;
			}
		});

		let buffer = '';
		socket.on('data', (chunk) => {
			buffer += chunk.toString('utf8');
			let idx;
			while ((idx = buffer.indexOf('\n')) !== -1) {
				const line = buffer.slice(0, idx);
				buffer = buffer.slice(idx + 1);
				if (!line) continue;
				try {
					const event = JSON.parse(line);
					event.source = source;
					onEvent(event);
				} catch (e) {
					console.warn(`[bridge] bad telemetry line from ${source}: ${e.message}`);
				}
			}
		});

		socket.on('error', () => { /* 무시 -- 아래 재시도 루프가 처리 */ });

		socket.on('close', () => {
			const entry = sources.get(source);
			if (entry && entry.socket === socket) {
				entry.socket = null;
			}
			// 엔진 프로세스가 아직 텔레메트리 포트를 안 열었을 수 있다(기동
			// 중) -- 프로세스 자체가 사라질 때까지(stopSource) 계속 재시도.
			if (sources.has(source)) {
				setTimeout(attempt, 500);
			}
		});
	};

	attempt();
}

function startGameServer() {
	const overridePath = path.join(TMP_CONFIG_DIR, 'GameServer.json');
	writeOverrideConfig(GAMESERVER_BASE_CONFIG, { TelemetryPort: SERVER_TELEMETRY_PORT }, overridePath);

	const proc = spawn(GAMESERVER_EXE, [], {
		cwd: BIN_DIR,
		env: {
			...process.env,
			LADELTA_CONFIG_PATH: overridePath,
			// WebViz 데모 자체의 운영 기본값(M28) -- 사용자가 직접 환경변수를
			// 안 잡아도 IOCP가 실제로 멀티스레드로 도는 걸 보여준다. 8/2는
			// 처리량이 증명된 값(워커)과 사용자가 실증 목적으로 선택한 값
			// (IOCP)이 섞여있다 -- 자세한 근거는 docs/decisions/0008-server-iocp-default.md 참고. 서버
			// 스폰에만 적용(클라이언트는 그대로), 셸에서 이미 값을 지정했으면
			// 그게 우선(||는 미설정일 때만 채움).
			NET_IO_THREADS: process.env.NET_IO_THREADS || '2',
			NET_JOBSYSTEM_WORKERS: process.env.NET_JOBSYSTEM_WORKERS || '8',
		},
		stdio: 'ignore',
	});
	proc.on('exit', (code) => console.log(`[bridge] GameServer.exe exited (code ${code})`));

	sources.set('server', { source: 'server', proc, socket: null });
	connectTelemetry('server', SERVER_TELEMETRY_PORT, broadcast);

	console.log(`[bridge] GameServer.exe launched (pid ${proc.pid}), telemetry port ${SERVER_TELEMETRY_PORT}`);
}

// connectionCount > 1이면 프로세스를 N개 띄우는 대신 M24의 RemoteServer.exe
// <N>(프로세스 하나가 N개 커넥션 호스팅)을 쓴다. TelemetrySink는 프로세스
// 전체 싱글턴이라 이 N개 커넥션의 이벤트가 전부 같은 source로 태그되지만,
// 프론트엔드(app.js)가 각 이벤트의 connectionId + remotePort 조인 키로
// 구분한다.
function startClient(connectionCount = 1) {
	const id = nextClientId++;
	const source = `client:${id}`;
	const port = CLIENT_TELEMETRY_PORT_BASE + id;
	const overridePath = path.join(TMP_CONFIG_DIR, `RemoteServer_${id}.json`);
	writeOverrideConfig(REMOTESERVER_BASE_CONFIG, { TelemetryPort: port }, overridePath);

	const args = connectionCount > 1 ? [String(connectionCount)] : [];
	const proc = spawn(REMOTESERVER_EXE, args, {
		cwd: BIN_DIR,
		env: { ...process.env, LADELTA_CONFIG_PATH: overridePath },
		stdio: 'ignore',
	});
	proc.on('exit', (code) => {
		console.log(`[bridge] RemoteServer.exe (${source}) exited (code ${code})`);
		sources.delete(source);
		broadcastTopology();
	});

	sources.set(source, { source, proc, socket: null });
	connectTelemetry(source, port, broadcast);

	console.log(`[bridge] RemoteServer.exe launched (pid ${proc.pid}), source=${source}, ${connectionCount} connection(s), telemetry port ${port}`);
	broadcastTopology();
	return source;
}

function stopSource(source) {
	const entry = sources.get(source);
	if (!entry) return;
	sources.delete(source); // 텔레메트리 재연결 루프도 같이 멈춤
	if (entry.socket) entry.socket.destroy();
	if (entry.proc && !entry.proc.killed) entry.proc.kill();
}

function stopAll() {
	for (const source of [...sources.keys()]) {
		stopSource(source);
	}
}

// -- WebSocket 브로드캐스트 ---------------------------------------------------

const wsClients = new Set();

function broadcast(event) {
	const line = JSON.stringify(event);
	for (const ws of wsClients) {
		if (ws.readyState === ws.OPEN) ws.send(line);
	}
}

function broadcastTopology() {
	broadcast({
		type: 'topology',
		sources: [...sources.keys()],
	});
}

// -- HTTP + 정적 파일 서빙 ----------------------------------------------------

const PUBLIC_DIR = path.join(__dirname, 'public');
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css' };

const httpServer = http.createServer((req, res) => {
	let reqPath = req.url === '/' ? '/index.html' : req.url;
	const filePath = path.join(PUBLIC_DIR, reqPath);
	if (!filePath.startsWith(PUBLIC_DIR)) {
		res.writeHead(403);
		res.end();
		return;
	}
	fs.readFile(filePath, (err, data) => {
		if (err) {
			res.writeHead(404);
			res.end('not found');
			return;
		}
		res.writeHead(200, { 'Content-Type': MIME[path.extname(filePath)] || 'application/octet-stream' });
		res.end(data);
	});
});

const wss = new WebSocketServer({ server: httpServer });

wss.on('connection', (ws) => {
	wsClients.add(ws);
	ws.send(JSON.stringify({ type: 'topology', sources: [...sources.keys()] }));

	ws.on('message', (raw) => {
		let cmd;
		try {
			cmd = JSON.parse(raw.toString('utf8'));
		} catch {
			return;
		}

		switch (cmd.cmd) {
			case 'connect_client':
				startClient();
				break;

			case 'connect_clients_bulk': {
				// "+100"/"+1000" 버튼 -- 커넥션마다 프로세스 하나 대신 M24의
				// 다중 커넥션 RemoteServer.exe <N>을 쓴다(예전 방식은 프로세스
				// 700개 근방에서 Windows 데스크톱 힙 한계에 부딪혔다).
				// connectionsPerProcess 기본값(100)과 count 상한(5000)은
				// M25가 이미 안정성을 확인한 범위 안(750커넥션/6프로세스).
				// 프로세스는 20ms씩 스태거(한꺼번에 수백 행이 리플로우되는
				// 걸 완화) -- 프로세스 안의 개별 커넥션은 그대로 거의 동시에
				// 붙는다, connectionId 기반 행 연결이라 순서에 안 민감함.
				const count = Math.max(1, Math.min(5000, cmd.count | 0));
				const connectionsPerProcess = Math.max(1, Math.min(150, cmd.connectionsPerProcess | 0 || 100));
				const processCount = Math.ceil(count / connectionsPerProcess);
				let remaining = count;
				for (let i = 0; i < processCount; i++) {
					const thisProcessCount = Math.min(connectionsPerProcess, remaining);
					remaining -= thisProcessCount;
					setTimeout(() => startClient(thisProcessCount), i * 20);
				}
				break;
			}

			case 'disconnect_client':
				if (typeof cmd.source === 'string') stopSource(cmd.source);
				broadcastTopology();
				break;

			case 'set_drop': {
				// target: "server" | "client:<id>" | "all". duplicate/
				// reorderWindow는 선택 -- 안 보내면 엔진이 현재 값을 유지하므로
				// 슬라이더 4개를 한꺼번에 보내도, 바뀐 것만 보내도 된다.
				const line = JSON.stringify({
					cmd: 'set_drop', inbound: cmd.inbound | 0, outbound: cmd.outbound | 0,
					...(cmd.duplicate !== undefined ? { duplicate: cmd.duplicate | 0 } : {}),
					...(cmd.reorderWindow !== undefined ? { reorderWindow: cmd.reorderWindow | 0 } : {}),
				}) + '\n';
				const targets = cmd.target === 'all' ? [...sources.keys()] : [cmd.target];
				for (const t of targets) {
					const entry = sources.get(t);
					if (entry && entry.socket) {
						entry.socket.write(line);
					}
				}
				break;
			}

			default:
				console.warn(`[bridge] unknown ws command: ${cmd.cmd}`);
		}
	});

	ws.on('close', () => wsClients.delete(ws));
});

httpServer.listen(HTTP_PORT, () => {
	console.log(`[bridge] http://localhost:${HTTP_PORT}`);
	startGameServer();
});

process.on('SIGINT', () => {
	console.log('\n[bridge] shutting down...');
	stopAll();
	process.exit(0);
});
