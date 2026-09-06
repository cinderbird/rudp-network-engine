// RUDP 신뢰성 시각화 대시보드 프론트엔드. 실시간 집계 차트 + 커넥션별
// NetPacketNotify 실측 표(InSeq/OutSeq/InAckSeq/OutAckSeq, docs/reference/sequence.md
// 참고)를 보여준다.
//
// 행 라우팅 조인 키: 서버/클라이언트는 같은 논리적 연결에 서로 다른
// connectionId 공간을 쓴다(서버: 자체 할당자, 클라이언트: 자기 소켓의
// 로컬 포트, M24). 그 로컬 포트가 서버가 보는 remotePort와 같은 값이라
// 조인 키로 쓴다 -- 자세한 배경은 connection-table.js 상단 주석 참고.

const EVENT_LOG = document.getElementById('eventLog');
const CONN_STATUS = document.getElementById('connStatus');
const CONN_COUNT = document.getElementById('connCount');
const TABLE_BODY = document.getElementById('connTableBody');

// ---- WebSocket -------------------------------------------------------------

let ws;
function connect() {
	ws = new WebSocket(`ws://${location.host}`);
	ws.onopen = () => setStatus(true);
	ws.onclose = () => { setStatus(false); setTimeout(connect, 1000); };
	ws.onerror = () => ws.close();
	ws.onmessage = (msg) => handleEvent(JSON.parse(msg.data));
}
connect();

function setStatus(connected) {
	CONN_STATUS.textContent = connected ? '브릿지 연결됨' : '브릿지 연결 끊김 -- 재시도 중...';
	CONN_STATUS.className = 'status-pill ' + (connected ? 'status-connected' : 'status-disconnected');
}

function send(cmd) {
	if (ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(cmd));
}

// ---- 이벤트 로그 --------------------------------------------------------

function logEvent(cls, text) {
	const line = document.createElement('div');
	line.className = cls;
	line.textContent = text;
	EVENT_LOG.appendChild(line);
	while (EVENT_LOG.children.length > 200) EVENT_LOG.removeChild(EVENT_LOG.firstChild);
	EVENT_LOG.scrollTop = EVENT_LOG.scrollHeight;
}

// ---- 커넥션 표 (행 추적 로직은 connection-table.js) ---------------------

const connTable = ConnectionTable.create({ tableBody: TABLE_BODY, connCountEl: CONN_COUNT, onSend: send, onLog: logEvent });

// ---- 집계 카운터 (초당 1회 차트에 반영) ---------------------------------

const counters = { packetSend: 0, ack: 0, nack: 0, dropIn: 0, dropOut: 0, dupIn: 0, dupOut: 0, reorderIn: 0, reorderOut: 0 };
let maxOutUnAckedPct = 0; // gauge 이벤트로 실시간 갱신, 차트엔 초당 1회 반영

// 최신 "stats"(NetDriver::EmitStatsSnapshot, 서버 전용) 스냅샷 -- 다른
// 차트들과 같은 주기(초당)로 한꺼번에 다시 그린다.
let latestServerStats = null;

// ---- 이벤트 디스패치 ------------------------------------------------------

function handleEvent(ev) {
	if (ev.type === 'topology') {
		// 브릿지가 보내는 소스 목록 -- 끊김을 놓친 경우 대비 재동기화.
		connTable.handleTopology(ev);
		return;
	}

	const isServer = ev.source === 'server';

	// 드라이버/시뮬레이터 전역 이벤트라 connectionId가 없다(stats, 그리고
	// drop/duplicate/reorder는 PacketSimulator가 커넥션 단위가 아니라
	// 드라이버 위에서 발생시킴). 아래 행 매칭보다 먼저 처리해야, 아직 화면에
	// 행이 하나도 없을 때(서버 막 시작 직후 등) 조용히 버려지지 않는다.
	switch (ev.type) {
		case 'stats':
			if (isServer) {
				latestServerStats = ev;
			}
			return;

		case 'drop':
			if (ev.direction === 'inbound') counters.dropIn++; else counters.dropOut++;
			logEvent('ev-drop', `[drop] ${ev.source} (${ev.direction})`);
			return;

		case 'duplicate':
			if (ev.direction === 'inbound') counters.dupIn++; else counters.dupOut++;
			logEvent('ev-drop', `[duplicate] ${ev.source} (${ev.direction})`);
			return;

		case 'reorder':
			if (ev.direction === 'inbound') counters.reorderIn++; else counters.reorderOut++;
			logEvent('ev-drop', `[reorder] ${ev.source} (${ev.direction}, window=${ev.windowSize})`);
			return;

		case 'handshake':
			// 핸드셰이크 단계엔 connectionId가 없어(행에 못 붙임) 여기서
			// 바로 로그만 남긴다.
			logEvent('ev-handshake', `[handshake] ${ev.role} -> ${ev.stage}`);
			return;
	}

	if (ev.connectionId === undefined) return;

	// 이 이벤트가 속한 행을 찾는다(connection-table.js).
	const entry = connTable.resolveEntry(ev, isServer);
	if (!entry) return;

	switch (ev.type) {
		// handshake/drop/duplicate/reorder/stats는 connectionId가 없어
		// 위에서 이미 처리되고 여긴 안 옴.

		case 'packet_send':
			counters.packetSend++;
			break;

		case 'ack':
			counters.ack++;
			break;

		case 'nack':
			counters.nack++;
			logEvent('ev-nack', `[NACK] ${ev.source} packet=${ev.packetId} ch=${ev.channel} -- 재전송됨`);
			break;

		case 'gauge': {
			const pct = connTable.renderGauge(entry, isServer, ev);
			maxOutUnAckedPct = Math.max(maxOutUnAckedPct, pct);
			break;
		}

		case 'connection':
			connTable.handleConnectionLifecycle(entry, isServer, ev);
			break;
	}
}

// ---- 실시간 라인 차트 (롤링 윈도우) --------------------------------------

class LineChart {
	constructor(canvas, series, { windowPoints = 60, unit = '', legendEl = null } = {}) {
		this.canvas = canvas;
		this.ctx = canvas.getContext('2d');
		this.series = series; // [{key, color, label}]
		this.data = Object.fromEntries(series.map(s => [s.key, []]));
		this.windowPoints = windowPoints;
		this.unit = unit;
		this.legendEl = legendEl;
		this.resize();
		window.addEventListener('resize', () => this.resize());
		this.renderLegend(); // 첫 tick 전에도 범례가 비어있지 않게 "0"으로 초기화
	}

	resize() {
		const rect = this.canvas.getBoundingClientRect();
		const dpr = window.devicePixelRatio || 1;
		this.canvas.width = rect.width * dpr;
		this.canvas.height = rect.height * dpr;
		this.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
		this.w = rect.width;
		this.h = rect.height;
	}

	push(values) {
		for (const s of this.series) {
			const arr = this.data[s.key];
			arr.push(values[s.key] || 0);
			if (arr.length > this.windowPoints) arr.shift();
		}
		this.draw();
		this.renderLegend();
	}

	draw() {
		const { ctx, w, h } = this;
		ctx.clearRect(0, 0, w, h);

		// 범례는 캔버스 아래 별도 DOM(renderLegend())이라 이 영역은 그리드/
		// 라인 전용 -- 데이터와 겹칠 일이 없다.
		const padL = 42, padR = 8, padT = 8, padB = 8;
		const plotW = w - padL - padR;
		const plotH = h - padT - padB;

		let maxVal = 1;
		for (const s of this.series) {
			for (const v of this.data[s.key]) maxVal = Math.max(maxVal, v);
		}
		maxVal *= 1.15;

		// 그리드선 + y축 라벨(0, 중간, 최대)
		ctx.strokeStyle = '#232b36';
		ctx.fillStyle = '#a8b3c1';
		ctx.font = '13px sans-serif';
		ctx.textBaseline = 'middle';
		ctx.lineWidth = 1;
		for (const frac of [0, 0.5, 1]) {
			const y = padT + plotH * (1 - frac);
			ctx.beginPath();
			ctx.moveTo(padL, y);
			ctx.lineTo(w - padR, y);
			ctx.stroke();
			const label = (maxVal * frac).toFixed(maxVal < 3 ? 1 : 0);
			ctx.fillText(label, 2, y);
		}
		ctx.textBaseline = 'alphabetic';

		for (const s of this.series) {
			const arr = this.data[s.key];
			if (arr.length < 2) continue;
			ctx.strokeStyle = s.color;
			ctx.lineWidth = 2;
			ctx.beginPath();
			arr.forEach((v, i) => {
				const x = padL + (plotW * i) / (this.windowPoints - 1);
				const y = padT + plotH * (1 - v / maxVal);
				if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
			});
			ctx.stroke();

			// 오른쪽 끝에 현재값 점 표시
			const lastV = arr[arr.length - 1];
			const x = padL + (plotW * (arr.length - 1)) / (this.windowPoints - 1);
			const y = padT + plotH * (1 - lastV / maxVal);
			ctx.fillStyle = s.color;
			ctx.beginPath();
			ctx.arc(x, y, 3, 0, Math.PI * 2);
			ctx.fill();
		}
	}

	renderLegend() {
		if (!this.legendEl) return;
		this.legendEl.innerHTML = this.series.map(s => {
			const lastV = this.data[s.key][this.data[s.key].length - 1] || 0;
			return `<span class="legend-item"><span class="legend-swatch" style="background:${s.color}"></span>${s.label} <span class="legend-value">${lastV.toFixed(1)}${this.unit}</span></span>`;
		}).join('');
	}
}

const chartThroughput = new LineChart(document.getElementById('chartThroughput'), [
	{ key: 'send', color: '#4f8fef', label: '송신' },
], { legendEl: document.getElementById('legendThroughput') });
const chartAckNack = new LineChart(document.getElementById('chartAckNack'), [
	{ key: 'ack', color: '#3ecf8e', label: 'Ack' },
	{ key: 'nack', color: '#ef5b5b', label: 'Nack' },
], { legendEl: document.getElementById('legendAckNack') });
const chartDrop = new LineChart(document.getElementById('chartDrop'), [
	{ key: 'in', color: '#f0a94b', label: 'inbound' },
	{ key: 'out', color: '#b98eff', label: 'outbound' },
], { legendEl: document.getElementById('legendDrop') });
const chartBuffer = new LineChart(document.getElementById('chartBuffer'), [
	{ key: 'pct', color: '#ef5b5b', label: '최대 사용률' },
], { unit: '%', legendEl: document.getElementById('legendBuffer') });
const chartDupReorder = new LineChart(document.getElementById('chartDupReorder'), [
	{ key: 'dup', color: '#4fd0ef', label: '중복' },
	{ key: 'reorder', color: '#e0c34c', label: '재정렬' },
], { legendEl: document.getElementById('legendDupReorder') });
// 서버 전역 jobs/sec (NetDriver::EmitStatsSnapshot) -- 이전엔 PowerShell/CSV
// 출력에만 있고 화면엔 없던 워커 스케일링 실측치.
const chartJobs = new LineChart(document.getElementById('chartJobs'), [
	{ key: 'jobs', color: '#3ecf8e', label: 'jobs/sec' },
], { legendEl: document.getElementById('legendJobs') });

const WORKER_TABLE_BODY = document.getElementById('workerTableBody');
const STAT_CONNS = document.getElementById('statConns');
// TickLoop/IOCP 스레드 수 -- 아래 JobWorker 표와 합쳐 3종 스레드 역할을
// 완성한다.
const STAT_TICK_THREADS = document.getElementById('statTickThreads');
const STAT_IO_THREADS = document.getElementById('statIoThreads');
// JobWorker 수도 별도 줄로 표시(workers 배열 길이로 계산, 새 텔레메트리
// 불필요).
const STAT_JOB_THREADS = document.getElementById('statJobThreads');
const STAT_DGRAM = document.getElementById('statDgram');
const STAT_IO = document.getElementById('statIo');

function renderServerStats() {
	if (!latestServerStats) return;
	const s = latestServerStats;
	STAT_CONNS.textContent = s.connections;
	STAT_TICK_THREADS.textContent = s.tickThreadCount ?? '-';
	STAT_IO_THREADS.textContent = s.ioThreadCount ?? '-';
	STAT_JOB_THREADS.textContent = s.workers ? s.workers.length : '-';
	STAT_DGRAM.textContent = `${s.datagramsRecvPerSec?.toFixed(1)} / ${s.datagramsSentPerSec?.toFixed(1)}`;
	STAT_IO.textContent = `${s.ioCompletionsPerSec?.toFixed(1)} / ${s.ioTimeoutsPerSec?.toFixed(1)}`;

	const workers = s.workers || [];
	WORKER_TABLE_BODY.innerHTML = workers.map(w => `
		<tr>
			<td>w${w.worker}</td>
			<td>${w.osThreadId}</td>
			<td>${w.jobsPerSec.toFixed(1)}</td>
			<td>${w.cpuCores.toFixed(4)}</td>
		</tr>
	`).join('') || '<tr><td colspan="4">아직 stats 이벤트 없음</td></tr>';
}

setInterval(() => {
	chartThroughput.push({ send: counters.packetSend });
	chartAckNack.push({ ack: counters.ack, nack: counters.nack });
	chartDrop.push({ in: counters.dropIn, out: counters.dropOut });
	chartBuffer.push({ pct: maxOutUnAckedPct });
	chartDupReorder.push({ dup: counters.dupIn + counters.dupOut, reorder: counters.reorderIn + counters.reorderOut });
	chartJobs.push({ jobs: latestServerStats ? latestServerStats.jobsPerSec : 0 });
	renderServerStats();

	counters.packetSend = 0; counters.ack = 0; counters.nack = 0; counters.dropIn = 0; counters.dropOut = 0;
	counters.dupIn = 0; counters.dupOut = 0; counters.reorderIn = 0; counters.reorderOut = 0;
	maxOutUnAckedPct = 0;
}, 1000);

// ---- 컨트롤 ---------------------------------------------------------------

document.getElementById('btnConnectClient').addEventListener('click', () => send({ cmd: 'connect_client' }));
document.getElementById('btnConnectClients100').addEventListener('click', () => send({ cmd: 'connect_clients_bulk', count: 100 }));
document.getElementById('btnConnectClients1000').addEventListener('click', () => send({ cmd: 'connect_clients_bulk', count: 1000 }));

// 슬라이더 4개 전부 매번 하나의 set_drop 커맨드로 보낸다 -- 화면에 보이는
// 값과 실제 엔진 설정이 어긋나지 않게.
function wireDropSlider(id, isPercent = true) {
	const slider = document.getElementById(id);
	const val = document.getElementById(id + 'Val');
	const apply = () => {
		val.textContent = slider.value + (isPercent ? '%' : '');
		send({
			cmd: 'set_drop',
			target: 'all',
			// 화면은 0-100%, 실제 프로토콜/엔진 설정은 permille(0-1000) --
			// 여기서 변환. reorderWindow는 비율이 아니라 개수라 변환 없음.
			inbound: parseInt(document.getElementById('dropInbound').value, 10) * 10,
			outbound: parseInt(document.getElementById('dropOutbound').value, 10) * 10,
			duplicate: parseInt(document.getElementById('dupRate').value, 10) * 10,
			reorderWindow: parseInt(document.getElementById('reorderWindow').value, 10),
		});
	};
	slider.addEventListener('input', apply);
}
wireDropSlider('dropInbound');
wireDropSlider('dropOutbound');
wireDropSlider('dupRate');
wireDropSlider('reorderWindow', false);

// ---- 커넥션 표 "크게 보기" (같은 페이지 안 확대) --------------------------
// 이 페이지가 이미 갖고 있는 DOM/행을 그대로 크게 보여줄 뿐이라, 버튼을
// 누르기 전에 이미 연결돼있던 커넥션도 전부 그대로 보인다(별도 페이지로
// 만들었던 첫 버전은 이 부분이 안 됐다).

const connTableSection = document.getElementById('connTableSection');
const btnExpandConnTable = document.getElementById('btnExpandConnTable');

function setConnTableFullscreen(on) {
	connTableSection.classList.toggle('fullscreen', on);
	btnExpandConnTable.textContent = on ? '⛶ 축소' : '⛶ 크게 보기';
	btnExpandConnTable.setAttribute('aria-label', on ? '축소' : '크게 보기');
}

btnExpandConnTable.addEventListener('click', () => {
	setConnTableFullscreen(!connTableSection.classList.contains('fullscreen'));
});

document.addEventListener('keydown', (e) => {
	if (e.key === 'Escape' && connTableSection.classList.contains('fullscreen')) {
		setConnTableFullscreen(false);
	}
});

// ---- 도움말 팝업 ("?" 버튼) ------------------------------------------------
// 버튼의 data-help 값이 아래 항목 키와 대응. 일반적인 설명이 아니라 실제
// 엔진 동작(docs/reference/engine-design.md / docs/reference/sequence.md)에 근거해 작성.

const HELP_TEXT = {
	throughput:
		'이 프로세스가 실제로 네트워크에 내보낸 패킷 수(초당)입니다. ' +
		'NetConnection::AssembleOutgoingPackets가 패킷을 봉인하고 시퀀스 번호(OutSeq)를 ' +
		'커밋하는 순간(packet_send 이벤트)을 집계합니다. 서버·클라이언트 각자의 송신량이며, ' +
		'상대가 받았는지 여부와는 무관합니다(그건 아래 Ack/Nack 차트가 보여줍니다).',
	ackNack:
		'상대방이 내가 보낸 패킷을 "받았다"(Ack, 초록) 또는 "확인 결과 못 받았다"(Nack, 빨강)고 ' +
		'알려온 건수(초당)입니다. Nack을 받으면 NetChannel::Nacked()가 해당 Bunch를 즉시 재전송 ' +
		'큐(OutBunchQueue) 맨 앞으로 되돌립니다 — 이게 이 엔진의 신뢰성 보장 메커니즘 그 자체입니다. ' +
		'손실률을 올렸을 때 Nack이 늘어나면서도 연결이 끊기지 않고 계속 간다면, 정확히 의도대로 ' +
		'동작하고 있다는 뜻입니다.',
	drop:
		'PacketSimulator가 실제로 버린 패킷 수(초당)입니다. inbound는 이 프로세스가 받을 예정이던 ' +
		'패킷이 도착 직전에 버려진 것, outbound는 이 프로세스가 보내려던 패킷이 나가기 직전에 버려진 ' +
		'것입니다. 아래 손실률 슬라이더가 이 값을 실시간으로 좌우합니다 -- 슬라이더를 올리면 이 차트와 ' +
		'Ack/Nack 차트가 거의 동시에 반응하는 걸 볼 수 있습니다.',
	buffer:
		'모든 활성 커넥션 중, 아직 Ack도 Nack도 받지 못해 재전송 대기 중인 Bunch 수(OutUnAckedBunches)의 ' +
		'최댓값을, 채널당 상한(NetChannel::RELIABLE_BUFFER = 512)에 대한 비율로 표시합니다. 이 값이 ' +
		'100%에 도달하면 그 커넥션은 즉시 강제 종료됩니다(Close("ReliableBufferOverflow"), M10). ' +
		'평소엔 0%에 가깝다가 손실률을 크게 올리면 서서히 올라가는 걸로, 신뢰성 버퍼가 실제로 압박받는 ' +
		'과정을 볼 수 있습니다.',
	packetNotify:
		'각 커넥션의 NetPacketNotify(패킷 레벨 신뢰성 추적기)와 ConnectionRttTracker(M26)가 실제로 ' +
		'들고 있는 수치입니다.\n\n' +
		'상태: EConnectionState -- Invalid/Closed/Pending/Open (PacketEnum.h).\n' +
		'InSeq(Ack): 상대방에게서 마지막으로 받은 패킷의 시퀀스 번호, 괄호는 내가 마지막으로 Ack를 ' +
		'보낸 상대방 시퀀스(InAckSeq).\n' +
		'OutSeq(Ack): 내가 마지막으로 보낸 패킷의 시퀀스 번호, 괄호는 상대방이 확인했다고 나도 인지한 ' +
		'마지막 시퀀스(OutAckSeq).\n' +
		'UnAcked: 아직 확인받지 못해 재전송 대기 중인 Bunch 수 (상한 512 -- 왼쪽 "신뢰성 버퍼 사용률" ' +
		'차트가 이 값의 전체 최댓값을 보여줍니다).\n' +
		'RTT: 최근 10초 동안 이 연결에서 실제로 관측된 왕복 시간(ms, 소수점 1자리) -- 패킷을 보낸 ' +
		'시각부터 상대방의 Ack가 도착한 시각까지입니다. 세 숫자는 p50 / p95 / p99(백분위수)로, 같은 ' +
		'평균이 아니라 "샘플들을 크기순으로 줄 세웠을 때 각 지점의 값"입니다.\n' +
		'  · p50(중앙값): 절반의 패킷이 이보다 빨리 도착했습니다 -- "보통"의 체감 지연.\n' +
		'  · p95: 100개 중 95개는 이 안에 들어옵니다 -- 가끔 튀는 지연까지 포함한 현실적인 상한.\n' +
		'  · p99: 100개 중 1개는 이보다 더 오래 걸립니다 -- 최악에 가까운 케이스, p50과 크게 벌어져 ' +
		'있으면 지터(순간적으로 튀는 지연)가 크다는 뜻입니다.\n' +
		'0.0/0.0/0.0ms는 아직 이 연결에서 확인된 Ack가 없다는 뜻입니다. 툴팁(마우스 올리기)에는 ' +
		'같은 10초 창의 최솟값/최댓값과 샘플 수가 더 나옵니다.\n\n' +
		'"서버 시점"과 "클라이언트 시점"은 같은 논리적 연결을 양쪽이 각자 독립적으로 추적한 결과입니다 -- ' +
		'정상 동작 중에는 서로 가깝게 따라가야 하고, 손실이 심해지면 서버의 OutSeq와 클라이언트의 InSeq ' +
		'사이 격차가 벌어졌다 Ack/Nack 사이클을 거쳐 다시 좁혀지는 걸 관찰할 수 있습니다.',
	dupReorder:
		'PacketSimulator가 실제로 중복시키거나(같은 패킷을 두 번 전달) 순서를 뒤섞은(재정렬 창 안에서 ' +
		'무작위로 하나를 골라 내보냄) 건수(초당)입니다. 아래 "중복 / 재정렬" 슬라이더로 서버 쪽 값을 ' +
		'실시간 조절할 수 있습니다(inbound 방향만 -- 이 엔진은 outbound 경로에 중복/재정렬 로직 자체가 ' +
		'없고 드롭만 지원합니다, PacketSimulator.cpp). "재정렬 창"은 확률이 아니라 개수입니다 -- 이만큼 ' +
		'패킷을 모았다가 그중 하나를 무작위 순서로 내보냅니다(0 = 재정렬 비활성).',
	jobsPerSec:
		'서버(GameServer.exe) 전체의 초당 처리 Job 수입니다(NetDriver::EmitStatsSnapshot, 초당 1회). ' +
		'JobSystem 워커가 실행한 모든 Job(패킷 수신 처리, Ack/Nack 반영, 송신 조립 등)을 합산한 값 -- ' +
		'M22/M23/M25가 "워커를 늘리면 처리량이 오르는가"를 답할 때 쓴 바로 그 지표입니다. 아래 표의 ' +
		'워커별 분포와 함께 보면, 부하가 워커 사이에 고르게 나뉘는지 특정 워커에 쏠리는지 알 수 있습니다.',
	serverStats:
		'서버 프로세스 전역 지표입니다(연결별이 아니라 GameServer.exe 프로세스 하나 전체) -- ' +
		'NetDriver::EmitStatsSnapshot이 초당 1회 내보내는 것을 그대로 보여줍니다.\n\n' +
		'TickLoop/IOCP 스레드: ThreadManager::Run()이 만드는 나머지 두 종류의 엔진 스레드 수입니다 -- ' +
		'JobWorker(아래 표)와 합쳐야 이 프로세스가 실제로 쓰는 엔진 스레드 전체가 됩니다. TickLoop은 ' +
		'커넥션 스윕 전용 스레드로 항상 정확히 1개입니다 -- 여러 개 돌리면 M3/M5가 겪은 중복 틱 버그가 ' +
		'그대로 재현되기 때문에, 이 값을 바꾸는 코드 경로 자체가 없습니다(설정이 아니라 구조적 상수). ' +
		'IOCP 스레드는 완료 포트를 드레인하는 IOLoop 스레드 수로, 컴파일타임 기본값 1을 런타임에 ' +
		'NET_IO_THREADS 환경변수로 조정할 수 있습니다(M23). M19 v8에서 NET_IO_THREADS=2로 실행해 ' +
		'프로세스의 총 OS 스레드 수가 정확히 +1 되는 것까지는 실측했지만 그땐 화면에 안 보였습니다 -- ' +
		'M27이 그 값을 여기 직접 노출합니다. JobWorker 스레드 수는 아래 워커별 표의 행 수와 항상 ' +
		'같습니다(workers 배열 길이) -- 3개 스레드 역할(TickLoop/IOCP/JobWorker)을 표 세지 않고도 ' +
		'한 줄에서 바로 비교할 수 있게 이 줄에도 따로 뽑아뒀습니다.\n\n' +
		'워커별 표: 각 JobSystem 워커 스레드의 jobs/sec과 CPU 코어 사용률(1.0 = 코어 하나 포화). ' +
		'M25의 워커 스케일링 실험이 정확히 이 숫자들로 "워커 1개가 0.976코어까지 포화됐다" 같은 ' +
		'결론을 냈습니다.\n' +
		'IO 완료/타임아웃: IOCP GetQueuedCompletionStatusEx 결과 -- 타임아웃이 0으로 수렴하면 IO ' +
		'스레드가 포트를 못 따라가기 시작했다는 뜻입니다(IpNetDriver.cpp의 PumpIO 주석 참고) -- 즉 ' +
		'위 IOCP 스레드 수를 늘려야 할지 판단하는 실측 신호입니다.',
};

function setupHelpButtons() {
	document.querySelectorAll('.help-btn').forEach((btn) => {
		const pop = document.createElement('div');
		pop.className = 'help-popover';
		pop.textContent = HELP_TEXT[btn.dataset.help] || '(설명 없음)';
		document.body.appendChild(pop);

		btn.addEventListener('click', (e) => {
			e.stopPropagation();
			const willOpen = !pop.classList.contains('open');
			document.querySelectorAll('.help-popover.open').forEach((p) => p.classList.remove('open'));
			if (willOpen) {
				// 먼저 열어서(위치는 아직 이전 값) 브라우저가 실제 레이아웃을
				// 계산하게 한 뒤 실제 높이를 읽는다 -- 고정값 추정은 텍스트가
				// 길어지면 화면 밖으로 잘려나간다.
				pop.classList.add('open');
				const rect = btn.getBoundingClientRect();
				const popRect = pop.getBoundingClientRect();
				// 기본은 버튼 아래-왼쪽, 화면 아래로 넘치면 위로 뒤집는다.
				const top = (rect.bottom + popRect.height + 8 > window.innerHeight)
					? rect.top - popRect.height - 8
					: rect.bottom + 8;
				pop.style.top = `${Math.max(8, Math.min(top, window.innerHeight - popRect.height - 8))}px`;
				pop.style.left = `${Math.max(8, Math.min(rect.left, window.innerWidth - popRect.width - 16))}px`;
			}
		});
	});

	document.addEventListener('click', () => {
		document.querySelectorAll('.help-popover.open').forEach((p) => p.classList.remove('open'));
	});
}
setupHelpButtons();
