// 커넥션 행 추적 로직. app.js에서 분리해 대시보드가 공용으로 쓴다(비-모듈
// 전역 스크립트, 이 프로젝트의 기존 스타일과 일관 -- 페이지 자체 스크립트
// 앞에 <script> 태그로 로드).
//
// 라우팅 설계: 행은 서버 자신의 connectionId로 키를 잡는다(어느
// RemoteServer.exe 프로세스에서 왔는지가 아니라) -- M24의 RemoteServer.exe
// <N>이 프로세스 하나로 N개 커넥션을 호스팅할 수 있어서다. 서버/클라이언트
// 양쪽은 같은 논리적 연결에 서로 다른 connectionId 공간을 쓰지만(서버: 자체
// 할당자, 클라이언트: 자기 소켓의 로컬 포트, M24), 그 로컬 포트가 서버가
// 보는 remotePort와 정확히 같은 값이라 조인 키로 쓸 수 있다. 양쪽 이벤트가
// 각각 최소 한 번씩 도착해야 연결되고(linkClientConnection() 참고), 그
// 전까지 먼저 온 쪽은 작은 대기 맵에 보류된다 -- 레이스 상황에서 대략
// 1초 안에 자연히 맞춰지는 정도로 허용.

function createConnectionTable({ tableBody, connCountEl, onSend, onLog }) {
	const rows = new Map();                    // 서버 connectionId -> 행 정보
	const remotePortToSource = new Map();      // 클라이언트 로컬 포트 -> source
	const clientKeyToServerConnId = new Map(); // `${source}:${clientLocalPort}` -> 서버 connectionId (연결 완료 시)
	const pendingServerOpens = new Map();      // remotePort -> 서버 connectionId (클라이언트 쪽 대기 중)

	const HS_LABELS = {
		initial: '시작', challenge: 'Challenge', response: 'Response',
		ack: 'Ack', complete: '완료', invalid_cookie: '쿠키 불일치',
	};
	const STATE_LABELS = { 0: 'Invalid', 1: 'Closed', 2: 'Pending', 3: 'Open' }; // PacketEnum.h EConnectionState

	function updateConnCount() {
		if (connCountEl) connCountEl.textContent = `커넥션 ${rows.size}개`;
	}

	// 연결 끊기는 어느 클라이언트 프로세스인지 알아야 한다 -- 아직 모르면
	// 버튼을 비활성화한다(눌러도 조용히 무반응인 것보다 낫다). 행 생성 시와
	// linkClientConnection()에서 나중에 source가 밝혀질 때 둘 다 호출.
	function refreshDisconnectButton(entry) {
		entry.disconnectBtn.disabled = !entry.source;
		entry.disconnectBtn.title = entry.source
			? '이 연결이 속한 프로세스를 종료합니다 -- 다중 커넥션 프로세스라면 같은 소스의 다른 연결도 함께 끊깁니다'
			: '이 연결이 어느 클라이언트 프로세스에 속하는지 아직 알 수 없어 여기서는 끊을 수 없습니다.';
	}

	function updateHandshake(entry, stage) {
		entry.hs.textContent = HS_LABELS[stage] || stage;
		entry.hs.classList.toggle('hs-complete', stage === 'complete');
		entry.hs.classList.toggle('hs-progress', stage !== 'complete' && stage !== 'invalid_cookie');
	}

	function ensureRow(serverConnId, source) {
		if (rows.has(serverConnId)) return rows.get(serverConnId);

		const tr = document.createElement('tr');
		tr.innerHTML = `
			<td class="c-label">${source ? `${source} · #${serverConnId}` : `? · #${serverConnId}`}</td>
			<td><span class="badge hs">-</span></td>
			<td class="c-srv-state">-</td><td class="c-srv-in">-</td><td class="c-srv-out">-</td><td class="c-srv-unacked">-</td><td class="c-srv-rtt">-</td>
			<td class="c-cli-state">-</td><td class="c-cli-in">-</td><td class="c-cli-out">-</td><td class="c-cli-unacked">-</td><td class="c-cli-rtt">-</td>
			<td><button class="small">✕</button></td>
		`;
		tableBody.appendChild(tr);

		const entry = {
			tr, source,
			label: tr.querySelector('.c-label'),
			hs: tr.querySelector('.hs'),
			disconnectBtn: tr.querySelector('button'),
			srvState: tr.querySelector('.c-srv-state'), srvIn: tr.querySelector('.c-srv-in'), srvOut: tr.querySelector('.c-srv-out'), srvUnacked: tr.querySelector('.c-srv-unacked'), srvRtt: tr.querySelector('.c-srv-rtt'),
			cliState: tr.querySelector('.c-cli-state'), cliIn: tr.querySelector('.c-cli-in'), cliOut: tr.querySelector('.c-cli-out'), cliUnacked: tr.querySelector('.c-cli-unacked'), cliRtt: tr.querySelector('.c-cli-rtt'),
		};

		// "handshake"이벤트는 connectionId를 안 실어서(NetConnection이 생기기
		// 전 단계라) 행에 못 붙인다. ensureRow()는 항상 서버의 "opened"
		// 이벤트로만 호출되고, 이건 핸드셰이크가 끝난 뒤에만 발생하므로 행이
		// 존재한다는 것 자체가 "완료" 신호다.
		updateHandshake(entry, 'complete');

		// 활성화 여부는 생성 시점이 아니라 현재 source 유무를 반영 -- 처음엔
		// 모르다가 linkClientConnection()에서 나중에 밝혀지는 경우도 있다.
		refreshDisconnectButton(entry);
		entry.disconnectBtn.addEventListener('click', () => {
			if (entry.source && onSend) onSend({ cmd: 'disconnect_client', source: entry.source });
		});

		rows.set(serverConnId, entry);
		updateConnCount();
		return entry;
	}

	function removeRow(serverConnId) {
		const entry = rows.get(serverConnId);
		if (!entry) return;
		entry.tr.remove();
		rows.delete(serverConnId);
		if (entry.source) {
			clientKeyToServerConnId.delete(`${entry.source}:${entry.clientLocalId}`);
		}
		updateConnCount();
	}

	// 클라이언트 쪽 연결(source + 자기 로컬 포트 기반 connectionId, M24)을
	// 서버 쪽 행에 연결한다 -- 서버 "opened"와 클라이언트 이벤트 중 먼저 온
	// 쪽에 맞춰 조인. 연결되면 서버 connectionId, 아직이면 null(다음 이벤트,
	// 늦어도 게이지 주기로 약 1초 뒤에 성공).
	function linkClientConnection(source, clientLocalId) {
		remotePortToSource.set(clientLocalId, source);
		const key = `${source}:${clientLocalId}`;

		const linked = clientKeyToServerConnId.get(key);
		if (linked !== undefined) return linked;

		const pendingConnId = pendingServerOpens.get(clientLocalId);
		if (pendingConnId === undefined) return null;

		pendingServerOpens.delete(clientLocalId);
		clientKeyToServerConnId.set(key, pendingConnId);
		const entry = rows.get(pendingConnId);
		if (entry) {
			entry.source = source;
			entry.clientLocalId = clientLocalId;
			entry.label.textContent = `${source} · #${pendingConnId}`;
			refreshDisconnectButton(entry);
		}
		return pendingConnId;
	}

	// 커넥션 단위 이벤트(gauge/connection 등, connectionId가 있는 것)가 속한
	// 행을 찾는다. 아직 못 본 connectionId면 서버 쪽 이벤트라면 무엇이든
	// 행을 새로 만든다("opened"가 아니어도 -- 커넥션이 이미 붙어있는 상태에서
	// 화면을 새로고침하는 경우까지 대응). 다만 remotePort(클라이언트 조인
	// 키)는 "opened"에만 있어서, 다른 이벤트로 생긴 행은 클라이언트 소스를
	// 못 밝혀 "? · #id"로 표시되고 클라이언트 시점 칸은 빈 채로 남는다.
	function resolveEntry(ev, isServer) {
		if (isServer) {
			let entry = rows.get(ev.connectionId);
			if (!entry) {
				const knownSource = remotePortToSource.get(ev.remotePort);
				entry = ensureRow(ev.connectionId, knownSource);
				if (knownSource) {
					entry.clientLocalId = ev.remotePort;
					clientKeyToServerConnId.set(`${knownSource}:${ev.remotePort}`, ev.connectionId);
				} else if (ev.remotePort !== undefined) {
					pendingServerOpens.set(ev.remotePort, ev.connectionId);
				}
			}
			return entry || null;
		}
		const linkedConnId = linkClientConnection(ev.source, ev.connectionId);
		if (linkedConnId === null) return null;
		return rows.get(linkedConnId) || null;
	}

	function renderGauge(entry, isServer, ev) {
		const target = isServer
			? { state: entry.srvState, in: entry.srvIn, out: entry.srvOut, unacked: entry.srvUnacked, rtt: entry.srvRtt }
			: { state: entry.cliState, in: entry.cliIn, out: entry.cliOut, unacked: entry.cliUnacked, rtt: entry.cliRtt };
		target.state.textContent = STATE_LABELS[ev.state] ?? ev.state;
		target.state.className = `c-${isServer ? 'srv' : 'cli'}-state state-${ev.state}`;
		target.in.textContent = `${ev.inSeq} (${ev.inAckSeq})`;
		target.out.textContent = `${ev.outSeq} (${ev.outAckSeq})`;
		target.unacked.textContent = `${ev.outUnAckedBunches} / ${ev.reliableBufferCap}`;
		const pct = (ev.outUnAckedBunches / ev.reliableBufferCap) * 100;
		// RTT는 마이크로초로 오는 걸 밀리초(소수 1자리)로 표시. rttSampleCount가
		// 0이면 아직 이 연결에서 확인된 Ack가 없다는 뜻.
		if (ev.rttSampleCount > 0) {
			const usToMs = (us) => (us / 1000).toFixed(1);
			target.rtt.textContent = `${usToMs(ev.rttP50Us)}/${usToMs(ev.rttP95Us)}/${usToMs(ev.rttP99Us)}ms`;
			target.rtt.title = `min=${usToMs(ev.rttMinUs)}ms max=${usToMs(ev.rttMaxUs)}ms (${ev.rttSampleCount}개 샘플, 최근 10초)`;
		}
		return pct;
	}

	// 'connection'(닫힘 등, 열림은 resolveEntry가 이미 처리)과 'topology'
	// 재동기화를 처리. 행이 삭제됐으면 true(호출자는 이후 entry를 쓰면 안 됨).
	function handleConnectionLifecycle(entry, isServer, ev) {
		if (isServer && ev.event === 'closed') {
			removeRow(ev.connectionId);
			if (onLog) onLog('ev-handshake', `[connection] ${ev.source} closed (${ev.reason || ''})`);
			return true;
		}
		if (onLog) onLog('ev-handshake', `[connection] ${ev.source} ${ev.event} (${ev.reason || ''})`);
		return false;
	}

	function handleTopology(ev) {
		// source가 이미 연결된 행만 확인 가능 -- 아직 연결 안 된 행은 비교
		// 기준이 없어 그대로 둔다(연결되거나, 드물게 그냥 남는다).
		for (const [connId, entry] of [...rows]) {
			if (entry.source && !ev.sources.includes(entry.source)) removeRow(connId);
		}
	}

	return {
		rows,
		ensureRow, removeRow, linkClientConnection, resolveEntry,
		renderGauge, handleConnectionLifecycle, handleTopology,
		updateConnCount, updateHandshake,
	};
}

window.ConnectionTable = { create: createConnectionTable };
