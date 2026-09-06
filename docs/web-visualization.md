[문서 안내](README.md) · [문제 해결 과정](problem-solving.md) · [설계 결정](decisions/README.md) · [레퍼런스](reference/engine-design.md) · [저장소 홈](../README.md)

# WebViz — RUDP 엔진 신뢰성(Ack/Nack/재전송) 실시간 시각화

`Programs/WebViz/`는 실제 `GameServer.exe`/`RemoteServer.exe`를 그대로 실행한 채, 패킷이 오가고 손실이 발생하고 Nack 기반 재전송이 복구하는 과정을 브라우저에서 실시간으로 보여주는 도구다. 사전 녹화된 리플레이나 브라우저 자체의 RUDP 재구현이 아니라, 지금 이 엔진이 실제로 하는 일을 그대로 중계한다.

---

## 목차

1. [아키텍처](#1-아키텍처)
2. [C++ 계측 계층](#2-c-계측-계층--projectnetworksrctelemetry)
3. [Node.js 브릿지](#3-nodejs-브릿지--programswebvizbridgejs)
4. [프론트엔드 화면 구성](#4-프론트엔드-화면-구성--programswebvizpublic)
5. [실행 방법](#5-실행-방법)

---

## 1. 아키텍처

```
[GameServer.exe / RemoteServer.exe]  (기존 엔진 + 최소 계측)
        │  TCP, 개행 구분 JSON (텔레메트리 out / 제어 in, 같은 소켓)
        ▼
[Node.js 브릿지 (Programs/WebViz/bridge.js)]
        │  WebSocket
        ▼
[브라우저 페이지 (Programs/WebViz/public/)]
```

엔진 쪽 변경은 완전히 opt-in인 로컬 전용 텔레메트리 계층(`TelemetrySink`, 기본 비활성) 하나뿐이다. Node 브릿지가 TCP↔WebSocket을 중계하고, 프론트엔드는 실시간 집계 차트와 커넥션별 실측 테이블을 그린다.

---

## 2. C++ 계측 계층 — `Project/Network/src/telemetry/`

`Network::Telemetry::TelemetrySink::Get()` — 프로세스 전역 싱글턴. 완전 opt-in: `DriverSetting::GetTelemetryPort() == 0`(기본값)이면 소켓을 아예 열지 않는다. 활성화되면 127.0.0.1 전용 TCP 서버로, 같은 연결에서 텔레메트리를 내보내고(out) 제어 명령을 받는다(in).

핫 패스 비용을 최소화하기 위해 `Emit()`은 relaxed atomic 체크(실행 여부, 구독자 존재 여부) 두 번만 하고, 비활성이거나 구독자가 없으면 즉시 리턴한다. 활성 상태에서도 실제 소켓 write는 하지 않는다 — 미리 직렬화한 문자열을 lock-free 큐에 push만 하고, 전담 백그라운드 스레드가 드레인하며 처리한다. 늦게 접속한 구독자를 위해 최근 이벤트 300개를 링 버퍼에 보관했다가, 새 구독자가 붙는 순간 통째로 재생해 곧바로 따라잡게 한다.

**계측 지점**(기존 로직 변경 없이, 이미 있는 로그/처리 지점 바로 옆에 한 줄씩 추가):

| 이벤트 타입 | 위치 |
|---|---|
| `handshake` | `UdpConnectionProcessor::SendInitialPacket/SendConnectChallenge/SendChallengeResponse/SendChallengeAck`, `IncomingConnectionless`의 Invalid Cookie 분기, `Incoming()`의 3-way 완료 지점 |
| `packet_send` | `NetConnection::UpdatePacketHeader` — `CommitAndIncrementOutSeq()` 직후 |
| `ack` / `nack` | `NetConnection::RecivedAck`/`RecivedNack`의 채널별 콜백 안 |
| `drop` / `duplicate` / `reorder` | `PacketSimulator::Process`/`ShouldDropOutbound` — 실제 주사위를 굴리는 그 지점(호출부에서 반환값만으로 추론하면 "드롭"과 "재정렬 윈도우에 보류 중"이 똑같이 빈 결과라 구분이 안 됨, 그래서 근원에서 직접 계측) |
| `gauge`(주기적, 초당 1회) | `OutUnAckedBunches`/`RELIABLE_BUFFER`(512)/연결 상태/`PacketNotify`의 시퀀스 값/RTT percentile |
| `stats`(주기적, 초당 1회) | 서버 전역 jobs/sec, 워커별 CPU 코어, IO completion/timeout, 데이터그램 송수신율, 스레드 수(TickLoop/IOCP/JobWorker) |
| `connection` | `IpNetDriver::AddClient`(opened, `remotePort` 포함), `NetConnection::Close`(closed, 실제 Reason 포함) |

제어 명령은 `set_drop`(`{inbound, outbound, duplicate, reorderWindow}`) 하나만 존재한다 — `IpNetDriver::InitTelemetry()`가 `Simulator`/`OutboundSimulator`를 직접 다시 설정한다. Outbound 방향은 드롭만 지원한다(엔진 자체가 송신 경로에 중복/재정렬 로직을 두지 않기 때문, [`reference/engine-design.md` 7장](reference/engine-design.md#7-패킷-손실-시뮬레이터-projectnetworksrcsimulation) 참고).

---

## 3. Node.js 브릿지 — `Programs/WebViz/bridge.js`

`GameServer.exe`를 시작 시 1회 실행(고정 텔레메트리 포트, `NET_IO_THREADS=2`/`NET_JOBSYSTEM_WORKERS=8`을 스폰 환경변수 기본값으로 채워 넣음 — 셸이 이미 값을 export해뒀으면 그게 우선). 각 프로세스의 텔레메트리 포트에 TCP 클라이언트로 접속해 라인을 읽고 `source`(`"server"`/`"client:N"`)를 태깅해 `ws` 패키지로 모든 브라우저 탭에 WebSocket 브로드캐스트. 브라우저發 `set_drop`/`connect_client`/`disconnect_client`도 같은 경로로 반대 방향 처리. 정적 파일 서빙은 Node 내장 `http`만 사용(별도 프레임워크 불필요). `npm start` 한 번으로 GameServer 기동 + 브릿지 + 정적 서버가 전부 뜬다. 새 npm 의존성은 `ws` 하나뿐이다.

**대량 연결**: "+100개 한번에"/"+1000개 한번에" 버튼은 `RemoteServer.exe <N>`(프로세스 하나가 커넥션 N개를 호스팅)을 스폰한다 — 커넥션 하나마다 별도 프로세스를 띄우면 Windows 데스크톱 힙 한계로 수백 개 근처에서 막히기 때문에, 프로세스당 커넥션 100개를 기본으로 묶는다("+1000"은 프로세스 1000개가 아니라 10개). 클라이언트 쪽 커넥션 식별자가 자기 소켓의 로컬 ephemeral 포트이고, 서버가 신규 커넥션의 `remotePort`를 그대로 텔레메트리에 실어 보내므로, 프론트엔드가 이 값으로 서버 쪽 connectionId와 클라이언트 쪽 이벤트를 정확히 이어붙인다.

---

## 4. 프론트엔드 화면 구성 — `Programs/WebViz/public/`

바닐라 HTML/CSS/JS, 빌드 스텝 없음.

- **실시간 라인 차트 6개**(canvas 직접 구현, 외부 라이브러리 없음, 롤링 윈도우로 매초 갱신): 처리량(송신 패킷/초), Ack/Nack(건/초), 드롭율(inbound/outbound, 건/초), 신뢰성 버퍼 사용률(모든 커넥션 중 `OutUnAckedBunches`/512 최댓값, %), 중복/재정렬(건/초), 서버 전역 처리량(jobs/sec).
- **서버 전역 실측치 패널**: 연결 수, 데이터그램 송수신율, IO 완료/타임아웃, TickLoop/IOCP/JobWorker 스레드 수, 워커별(worker id/OS 스레드 id/jobs·sec/CPU 코어) 표.
- **커넥션별 실측 테이블**: 행마다 핸드셰이크 상태 배지, 연결 상태(`EConnectionState`), 서버/클라이언트 양쪽 시점의 `InSeq(InAckSeq)`/`OutSeq(OutAckSeq)`, RTT p50/p95/p99(ms, min/max/샘플 수는 툴팁). 커넥션 수가 많아져도 화면 아래 `⛶ 크게 보기` 버튼으로 이 표만 전체 화면에 띄울 수 있다(별도 페이지가 아니라 같은 페이지 안의 CSS 토글이라, 이미 붙어있던 커넥션의 데이터도 그대로 이어서 보인다).
- **제어**: inbound/outbound 손실률, 중복률, 재정렬 윈도우 크기를 조절하는 슬라이더(%) — 움직이면 즉시 `set_drop`으로 엔진에 반영되고, 위 차트들이 실시간으로 반응한다.
- **연결 버튼**: "+ 클라이언트 연결"(1개), "+100개 한번에", "+1000개 한번에".
- **이벤트 로그**: 원시 텔레메트리 이벤트를 시간순으로 나열.
- 차트/표 제목 옆의 "?" 버튼을 누르면, 그 숫자가 실제로 어느 코드 경로에서 나오는지 설명하는 팝업이 뜬다.

---

## 5. 실행 방법

```bash
cd Programs/WebViz
npm install
npm start
```

브라우저에서 `http://localhost:8090` 열기. (`.claude/launch.json`에 `webviz-bridge` 설정이 등록되어 있어, Claude Code의 Browser 프리뷰로도 바로 열 수 있다.)

---

← [시퀀스 번호](reference/sequence.md) · [문서 안내](README.md)
