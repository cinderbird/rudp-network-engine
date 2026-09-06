[문서 안내](../README.md) · [문제 해결 과정](../problem-solving.md) · [설계 결정](../decisions/README.md) · **레퍼런스** · [저장소 홈](../../README.md)

# RUDP 네트워크 엔진 — 설계 및 핵심 로직 전체 문서

---

## 목차

1. [전체 개요](#1-전체-개요)
2. [커넥션 계층](#2-커넥션-계층-projectnetworksrcconnection)
3. [채널 계층](#3-채널-계층-projectnetworksrcchannel)
4. [시퀀스 번호 / 신뢰성](#4-시퀀스-번호--신뢰성-요약)
5. [스레딩 — JobSystem과 세 종류의 엔진 스레드](#5-스레딩--projectnetworksrcthread)
6. [드라이버 계층](#6-드라이버-계층-projectnetworksrcdriver)
7. [패킷 손실 시뮬레이터](#7-패킷-손실-시뮬레이터-projectnetworksrcsimulation)
8. [설정 / 로깅](#8-설정--로깅-projectnetworksrcutils)
9. [프로세서 / 파이프라인](#9-프로세서--파이프라인-projectnetworksrcprocessor)
10. [기타 Network 하위 디렉토리](#10-기타-network-하위-디렉토리)
11. [게임 레이어](#11-게임-레이어-projectgameserver)
12. [Core 라이브러리](#12-core-라이브러리-projectcore)
13. [프로토콜 와이어 포맷](#13-프로토콜-와이어-포맷-protos)
14. [참고 문서 지도](#14-참고-문서-지도)

---

## 1. 전체 개요

| 항목 | 내용 |
|---|---|
| 언어 | C++20 |
| 플랫폼 | Windows 10 x64 (IOCP 전용) |
| 모델 | UE5 `IpNetDriver → PacketHandler → NetConnection → NetChannel` 계층을 독립 C++ 서버 엔진으로 재구현 |
| 신뢰성 방식 | UE5와 동일하게 **RTO/타이머 기반 재전송이 아닌** Nack-driven 재전송 + Keepalive + 연결 레벨 타임아웃 + `RELIABLE_BUFFER` 상한의 조합 |

프로세스 3종: `GameServer.exe`(서버, `ServerType=LoginServer`만 실제 처리), `RemoteServer.exe`(테스트/부하용 헤드리스 클라이언트 — 실제 게임 클라이언트가 아니라 스트레스 테스트 도구), 공유 `Network.lib`(엔진 본체).

**세 개의 독립된 스레드 역할**
- **TickLoop**(1개): 커넥션 스윕(`TickHousekeeping`/`TickDispatch`/`TickFlush`)을 일정한 주기로 돌린다 — 실제 채널/Bunch 작업은 하지 않고 Job만 큐에 넣는다.
- **IOCP 풀**(`NET_IOCP_THREAD_COUNT`개): `GetQueuedCompletionStatusEx`로 소켓 완료를 드레인하며, 신규 패킷을 해당 커넥션의 Job으로 큐잉한다.
- **JobSystem 워커 풀**(서버 8개/클라이언트 1개): 커넥션별로 핀 고정된 작업(패킷 파싱, 채널 디스패치, 패킷 조립)을 수행한다.

자세한 관계는 [5.6절](#56-세-종류의-엔진-스레드--tickloop-iocp-jobsystem-워커) 참고.

---

## 2. 커넥션 계층 (`Project/Network/src/connection/`)

파일: `NetConnection.h/.cpp`, `GameNetConnection.h/.cpp`, `PendingNetConnection.h/.cpp`, `PendingNetConnectionManager.h/.cpp`, `ChannelRecord.h/.cpp`.

### 2.1 상태 머신

`EConnectionState`: `USOCK_Invalid=0` / `USOCK_Closed=1` / `USOCK_Pending=2` / `USOCK_Open=3`. `NetConnection::State`는 `std::atomic<EConnectionState>`.

생성 흐름: 생성자 → `InitBase()`(state 설정, `LastReceiveTime`/`LastSendTime`을 현재 시각으로 시드) → 로컬(클라이언트) 커넥션은 `InitLocalConnection()`이 `GameNetConnectionAddressResolution`으로 원격 주소를 해석, 원격(서버 accept) 커넥션은 `InitRemoteConnection()`이 피어 주소를 복제하고 `UniqueConnectionId = PlayerId`로 설정 → `InitSequence()`(`InPacketId==-1` 가드로 한 번만 실행, `InPacketId`/`OutPacketId`/`OutAckPacketId`/`LastNotifiedSendPacketId` 시드, `InitInReliable`/`InitOutReliable`을 `& (MAX_CHSEQUENCE-1)`(1024)로 도출, `PacketNotify.Init()`, 모든 채널에 `UpdateChannelSeq()` 전파).

### 2.2 `AssembleOutgoingPackets()` — 패킷 조립 알고리즘

`NetConnection::AssembleOutgoingPackets()`는 이 엔진에서 가장 정교한 단일 함수다.

1. `IsRequireAssembleBunch()`가 거짓이면(모든 채널이 보낼 게 없음) **의도적으로 `Driver->IsNoTimeout()`으로 게이트하지 않는다** — keepalive는 타임아웃 설정과 무관하게 항상 나가야 한다.
2. 그렇지 않으면 `Channels`를 라운드로빈(`rr` 커서)으로 순회하며 각 채널의 `DequeueReadyBunch()`를 드레인한다. 헤더만 있거나 `PacketMaxSize`(1024바이트)를 초과하는 Bunch는 `ChannelBunchBuilder::DiscardOutReadyBunch()`로 버려진다(메시지 해제 콜백은 여전히 호출됨). 그렇지 않으면 현재 `Packet`(지연 생성, `createNewPacket()`)에 추가하되, 추가 시 `PacketMaxSize`를 넘으면 현재 패킷을 봉인해 `readyPackets`에 넣고 **항상 새 패킷을 시작**한다(재사용하지 않음) — 오버플로를 유발한 Bunch는 항상 새 패킷에 정상적으로 담긴다.
3. 패킷에 담기는 모든 Bunch는 먼저 `ConnectionChannelRecord::PushChannelRecord(Record, Packet->OutPacketId, Channel->ChIndex)`를 거친다 — Ack/Nack 처리가 나중에 소비할 패킷-채널 매핑을 여기서 기록한다([2.5절](#25-channelrecord--패킷-레벨과-채널-레벨-신뢰성) 참고).
4. `Channel->AddUnAckedOutBunch(Packet->OutPacketId, Bunch)`가 `RELIABLE_BUFFER` 상한을 강제한다([3.4절](#34-reliable_buffer)) — `false`를 반환하면 커넥션이 방금 닫힌 것이므로 함수는 즉시 리턴한다.
5. 함수 끝에 아직 봉인 안 된 마지막 패킷이 있으면 봉인해 push한다.

### 2.3 Keepalive / 타임아웃 메커니즘

- `LastReceiveTime`(`std::atomic<Network::Clock::TimePoint>`)은 `ReceivedPacket()` 맨 앞에서 무조건 찍힌다 — 파싱에 실패한 패킷이라도 "여기까지 도달했다는 것 자체가 피어가 살아있다는 증거"이기 때문.
- `LastSendTime`(원자적이지 않은 평범한 필드)은 항상 같은 핀 고정된 Job에서만 읽고 쓴다(`AssembleOutgoingPackets`가 읽고, `GameNetConnection::TickFlush`의 람다가 `SendToRemote` 직후 씀) — 그래서 원자화가 필요 없다.
- `NetConnection::GetTimeoutValue()`: `IsNoTimeout()`이면 `FLT_MAX`, 아니면 `USOCK_Pending`일 때 `InitialConnectTimeout`, 아니면 `ConnectionTimeout`.
- `NetDriver::CheckConnectionTimeouts()`: `IsNoTimeout()`이면 즉시 리턴; 아니면 락 아래서 커넥션 목록 스냅샷을 뜬 뒤(락 밖에서) 각 커넥션의 `now - LastReceiveTime > GetTimeoutValue()`를 확인해 `Close("Timeout")` 호출. `NetDriver::TickHousekeeping()`의 초당 1회 블록에서 호출된다([5.6절](#56-세-종류의-엔진-스레드--tickloop-iocp-jobsystem-워커) 참고).

### 2.4 `Close()` / 연결 제거 흐름

`NetConnection::Close(const char* Reason)`: 멱등적(`USOCK_Closed`면 즉시 리턴), state를 `USOCK_Closed`로, 로그, `Driver->RemoveConnection(this)`. 호출 지점은 정확히 둘: `NetDriver::CheckConnectionTimeouts()`(타임아웃)와 `NetChannel::AddUnAckedOutBunch()`(RELIABLE_BUFFER 오버플로).

`IpNetDriver::RemoveConnection()`은 베이스 버전을 호출하지 않고 독자적으로 구현한다 — `RemoteConnections`/`HashToConnectionMap`/`ConnectionMap` 세 컨테이너를 **하나의** `ConnectionsMutex` unique_lock 아래서 함께 지운다(`AddClient_Internal`이 셋을 항상 같이 채우는 것과 대칭). 락 해제 **후** `GControlProtocolHandler->RemovePlayerConnection(RemovedConnectionId)`를 호출 — `AddClient()`의 `AddPlayerConnection()` 호출과 대칭이며, 네트워크 레이어 락이 `RoomManager`의 락과 절대 중첩되지 않게 한다.

### 2.5 `ChannelRecord` — 패킷 레벨과 채널 레벨 신뢰성

`ConnectionChannelRecord`(`ChannelRecord.h`)는 `std::deque<ChannelRecordEntry>`(각 엔트리는 `Value:31 + IsSequence:1` 비트필드)다. "시퀀스" 엔트리는 새 `OutPacketId`의 레코드 시작을 표시하고, 그 뒤에 오는 비-시퀀스 엔트리들이 그 패킷에 실린 서로 다른 `ChannelIndex`를 나열한다. `PushPacketId()`는 `PacketId`가 바뀔 때만 새 시퀀스 마커를 넣어 중복을 막는다.

`ConsumeChannelRecordsForPacket<Functor>()`(템플릿, inline)는 순수 `PacketId`를 채널별 `Acked()`/`Nacked()` 호출로 변환하는 다리다: 맨 앞 시퀀스 엔트리를 팝하며 예상 `PacketId`와 일치하는지 확인, 이어지는 비-시퀀스 엔트리들을 팝하며(중복 제거) 채널마다 한 번씩 `Func(PacketId, ChannelIndex)`를 호출한다. 불일치가 발견되면 assert로 프로세스를 죽이는 대신 `false`를 반환해 호출자가 그 커넥션 하나만 닫도록 한다.

`NetConnection::RecivedAck`/`RecivedNack`이 이 함수를 호출하는 쪽이다 — 각각 `Channels[ChannelIndex]->Acked(AckedId)`/`Nacked(NackedId)`를 부르는 람다를 감싼다.

### 2.6 Pending Connection — 핸드셰이크 중간 상태

**쿠키 생성/검증**: `UdpConnectionProcessor::GenerateCookie()`가 `HMAC-SHA1(key=HandshakeSecret[SecretId], data=ToString(LastUpdateTime) + ClientAddress->ToString(true))`를 CryptoPP로 계산한다. 서버 흐름(`IncomingConnectionless`): `InitialPacket`을 받으면 `PendingRemoteConnection`을 할당/재사용하고 `SendConnectChallenge()`; `Response`를 받으면 `CookieDelta = Now - CookieTime`이 `[0, MAX_COOKIE_LIFETIME)`(40초, `NetMacro.h`) 안인지 확인하고, 쿠키를 재계산해 `memcmp`로 비교, 일치하면 쿠키의 첫 2바이트에서 `ServerSequence`/`ClientSequence`를 뽑아 `Driver->AddClient()`를 호출하고 `SendChallengeAck()`.

**60버킷 타임휠**: `PendingConnectionManager::ClientHashMap ClientCache[60]` + `std::shared_mutex sharedMutexs[60]`. 새 pending 커넥션은 `ClientTimeId`(서버: `IncomingConnectionless`에 전달된 값, `UdpConnectionProcessor::ClientTimeId` 원자 카운터가 초당 0→59 증가) 버킷에 파일링된다. `NetDriver::TickHousekeeping()`의 초당 1회 블록이 `CacheCleanupIndex`(0..59)를 진행시키며 `ConnectionsUpdate(CacheCleanupIndex)`를 호출 — 초당 버킷 하나씩, 전체 사이클 60초.

**활동 기반 만료**: 각 엔트리의 `Idle = Now - GetLastActivityTime()`을 계산해 `PendingConnectionInactivityTimeoutSec`(기본 20초, `MAX_COOKIE_LIFETIME`=40초보다 충분히 짧게 설정) 이상 유휴했을 때만 회수한다. 원래는 "버킷 차례가 오면 그 안의 모든 항목을 무조건 파괴"하는 방식이었는데, 느리지만 여전히 진행 중인 핸드셰이크까지 함께 파괴되는 문제가 있어 지금 방식으로 바뀌었다. `PendingRemoteConnection::Touch(Now)`가 `IncomingConnectionless`(찾았든 새로 만들었든 양쪽 분기 모두)와 `Incoming`(`AddClient` 이후의 재전송 경로)에서 호출된다. 매칭되는 `PendingRemoteConnection`을 못 찾으면 `NETWORK_LOG_WARN`을 남긴다.

**클라이언트 재전송**: `ResendHandshake(TimeId)`는 클라이언트 전용 — 항상 버킷 0의 마지막 전송 패킷을 재전송한다(클라이언트는 pending 커넥션이 하나뿐, 항상 버킷 0). 60버킷 커서와 무관하게 `ConnectionsUpdate()`가 호출될 때마다(초당 1회) 실행된다.

---

## 3. 채널 계층 (`Project/Network/src/channel/`)

파일: `NetChannel.h/.cpp`(베이스), `ControlChannel.h/.cpp`, `ActorChannel.h/.cpp`. 관련: `Project/Network/src/net_packet/NetBunch*.h/.cpp`.

### 3.1 Bunch 구조체

- `NetMessage`: 제로카피 래퍼 — `void* BufferPtr, uint32_t Size, ReleaseMessageBufferCallback, void* Context`. `NetChannel::RegisterMessage()`가 받는 단위.
- `OutReadyBunch`: 봉인되어 전송 준비된 Bunch — `HeaderBytes/HeaderSize`, `Messages`(vector, 복사 아닌 이동), `PayloadBytes`, `TotalSize`, `PacketId`, `ChIndex`.
- `BunchHeaderFields`: `bControl/bOpen/bClose/bReliable` 플래그, `ChIndex`(uint8), `ChSequence`(봉인 시점의 채널 `OutReliable` 카운터), `MessageCount`(≤255), `PayloadSize`(uint16).

### 3.2 `ChannelBunchBuilder` — 메시지 코얼레싱

`ChannelBunchBuilder`가 채널별 메시지 코얼레싱을 담당한다. `BunchPolicy`: `MaxPacketBytes=1200`(양쪽 채널 모두 `Init()`에서 이 값으로 오버라이드, 구조체 기본값 1024가 아님), `PacketHeaderBytes=16`, `BunchHeaderBytes=8`, `MaxMsgsPerBunch=255`, `MaxCoalesceUs=2000`(2ms 최대 보류 후 강제 flush).

`Submit()`은 뮤텍스+CV 기반 `ThreadsafeQueue<NetMessage>`에 push. `Pump(batchLimit, flushTail)`(`NetChannel::TickFlush()`에서 `PumpBunch(256, true)`로 호출)는 배치 드레인하며 삽입 실패 시(초과) 현재 Bunch를 봉인+push하고 새 Bunch로 재시도한다. **매 Tick마다 최대 하나의 부분 Bunch 꼬리만 남는다** — Tick 간에 아무것도 보류되지 않는다(`flushTail=true`이므로 배치 끝에 항상 flush). `SealAndPush()`가 `HeaderField.ChSequence = ++Owner->OutReliable`를 할당하는 지점 — 채널 시퀀스 번호가 실제로 발급되는 곳이다.

### 3.3 `OutBunchQueue` / `OutUnAckedBunches` / `InUnreadBunchMap`

셋 다 `NetChannel` 자체에 산다(빌더가 아니라):
- `std::deque<shared_ptr<OutReadyBunch>> OutBunchQueue` — 봉인됐지만 아직 패킷에 안 들어간 Bunch. `AssembleOutgoingPackets()`가 `DequeueReadyBunch()`로 드레인.
- `std::map<uint32_t, vector<shared_ptr<OutReadyBunch>>> OutUnAckedBunches` — `OutPacketId`로 키잉(한 패킷에 여러 출처의 Bunch가 실릴 수 있어 vector). `AddUnAckedOutBunch()`로만 채워지고, `Acked()`(erase) 또는 `Nacked()`/`ForceRetransmitAllUnAcked()`(다시 `OutBunchQueue`로)로 줄어든다.
- `std::map<uint32_t, shared_ptr<InBunchReader>> InUnreadBunchMap` — `ChSequence`로 키잉, 순서가 어긋나 도착한 reliable Bunch를 자기 차례가 올 때까지 보관.

### 3.4 `RELIABLE_BUFFER`

`NetChannel::RELIABLE_BUFFER = 512`(UE5의 실제 배치를 따라 채널 레벨에 둠). `AddUnAckedOutBunch()`에서만 강제: 추가 **전** `OutUnAckedBunches.size() >= RELIABLE_BUFFER`면 `Connection.lock()->Close("ReliableBufferOverflow")`를 부르고 `false` 반환. 이게 `OutUnAckedBunches`의 유일한 증가 지점이라 여기 하나만 강제하면 충분하다.

### 3.5 Pump/Dispatch 흐름과 Ack/Nack 연결

- **송신**: `NetChannel::Tick()` → `TickFlush()` → `PumpBunch(256,true)` → `BunchBuilder->TryDequeueReady()`를 `OutBunchQueue`로 드레인.
- **수신**: `NetConnection::DispatchPacket()`이 패킷 페이로드에서 각 Bunch 헤더를 파싱, 채널을 찾거나(`CreateChannelByName`) 만든 뒤 순서대로면 `DispatchBunch()` → `Channel->DispatchRawBunch()`, reliable이고 순서가 밖이면 `AddUnreadInCopyBunch()`로 보관 후 다음 Bunch로 계속.
- `NetChannel::DispatchRawBunch()`: `DispatchRawBunch_Interanl()`로 디스패치 성공 시 `InReliable` 증가, 이어 `InUnreadBunchMap`을 `begin()->first == InReliable+1`인 동안 반복 드레인 — 이게 재조립/순서-복구 루프다.

Ack/Nack 연결(`NetConnection::RecivedAck`/`RecivedNack` → `Channel->Acked()`/`Nacked()`):
- **`Acked()`**: `OutUnAckedBunches.erase(AckPacketId)`.
- **`Nacked()`**: 그 패킷 슬롯에 있던 모든 Bunch를 원래 순서를 보존하며(역순 반복 후 `push_front`) `OutBunchQueue` **앞쪽**에 다시 삽입, 슬롯 erase.
- **`ForceRetransmitAllUnAcked()`**: `NetPacketNotify::IsWaitingForSequenceHistoryFlush()`(256슬롯 히스토리 윈도우 오버플로)가 발동했을 때 `OutUnAckedBunches` 전체를 순서 보존하며 강제로 `OutBunchQueue`에 되돌린다 — 256슬롯 밖으로 밀려난 PacketId들이 "영원히 대기"하는 걸 막는다.

---

## 4. 시퀀스 번호 / 신뢰성 요약

전체 상세는 [`sequence.md`](sequence.md)에 있다. 핵심만 요약:

- `TSequenceNumber<14, uint16_t>`: 14비트 순환 시퀀스(0~16383). `operator>`가 이산수학의 Cyclic Order(`(A-B) mod 2^14 < 2^13`)를 구현. `Diff(A,B)`는 비트 시프트 기반 부호 확장(`(int32_t)((A-B) << 18) >> 18`)으로 wrap-around를 넘나드는 부호 있는 거리를 계산.
- `TSequenceHistory<256>`: 256비트 수신 이력 비트셋(Selective Ack), 8개의 32비트 워드, `AddDeliveryStatus`가 매 패킷마다 전체를 1비트씩 시프트.
- `NetPacketNotify`: `InSeq`/`InAckSeq`/`InAckSeqAck`/`OutSeq`/`OutAckSeq`/`InSeqHistory`/`AckRecord`를 관리, `Update()`→`ProcessReceivedAcks()`(Functor로 Ack/Nack 판정)→`InternalUpdate()`(자기 수신 이력 갱신) 흐름으로 패킷 헤더 하나마다 상대의 Ack/Nack을 판독한다.

---

## 5. 스레딩 — `Project/Network/src/thread/`

파일: `JobSystem.h/.cpp`, `SListQueue.h`, `StrongType.h`, `LockQueue.h`(범용 `ThreadsafeQueue<T>`, JobSystem과 무관).

### 5.1 출처와 범위

`JobSystem`은 사용자의 다른 프로젝트(`TcpIocp/NaritaTR`)에서 거의 그대로 포팅한 코루틴 지원 스케줄러다. 헤더 주석이 명시하듯 **fire-and-forget `FuncTask` 패턴만 검증됐다** — 코루틴 기계(`CoroutineObject<T>`, `CoroutineTask<T>`, `AwaitableTuple`)는 컴파일을 위해 그대로 남아있지만 이 엔진 어디서도 실제 `co_await`을 호출하지 않는다(죽었지만 컴파일되는 코드).

### 5.2 Task / FuncTask / 코루틴 Job 모델

- `Task`(베이스): `_children_task_count`(원자), `is_function_task`(원자 bool), `_root_task`, `_thread_index`/`_thread_id`/`_thread_type`(모두 `StrongType<int64_t,...>` — [5.5절](#55-thread_index_type과-핀-고정)).
- `FuncTask : Task`: `std::function<void()>`를 감쌈. `DoTask()`는 그냥 호출.
- `CoroutineTaskBase<T>`/`CoroutineTask<T>`/`CoroutineTask<void>`: 표준 C++20 프로미스 타입 기계. `final_suspend()`가 완료 시 부모의 `_children_task_count`를 감소시키고 마지막 자식이면 부모를 재스케줄.

`AddAsyncJob()` 오버로드들이 모두 `Internal_AddJob<T>()`로 수렴한다: `job->_root_task` 설정, 필요하면 부모의 `_children_task_count` 증가, `next_thread_index` 결정(핀 고정된 `_thread_index`가 ≥0이면 그것, 아니면 라운드로빈), 함수/코루틴 × 로컬/글로벌 4개 큐 배열 중 하나에 push.

### 5.3 로컬 vs 글로벌 워커별 큐

워커마다 4개의 `SListQueue` — `global_func`, `global_coroutine`, `local_func`, `local_coroutine`. "글로벌" 큐는 핀 고정 없이 제출된 Job(라운드로빈 배정), "로컬" 큐는 `thread_index_type`을 명시해 제출된 Job이 받는다. `RunPendingTsak()`의 핫 루프는 항상 **로컬 함수 → 로컬 코루틴 → 글로벌 함수 → 글로벌 코루틴** 순서로 확인한다 — 이 순서가 핀 고정된 커넥션의 Job들이 우선순위를 가지고 서로 간에 엄격한 FIFO를 유지하게 보장한다.

`SListQueue<T>`는 Windows의 네이티브 `InterlockedSList`/`SLIST_HEADER` 위에 만든 진짜 lock-free MPMC 큐다(2-스택 설계 — `push_stack`/`pop_stack`) — `PushBack`은 완전 lock-free, `TryPopFront`는 "pop_stack이 비어 push_stack을 뒤집어야 하는" 드문 전환 시점에만 내부 `CRITICAL_SECTION`을 잡는다.

### 5.4 busy-spin vs CV-wait 스케줄링 경로

`RunPendingTsak()`이 4개 큐를 다 확인해도 작업이 없으면 `ShouldUseCVWait()`으로 분기한다:
- **true(CV-wait)**: `threads_cv.wait(lock, pred)`로 블록. `pred`는 `!is_system_work || pending_task_count.load() > 0`. CPU를 거의 쓰지 않지만 깨어나는 데 OS 스케줄러 지연이 낀다.
- **false(busy-spin)**: `YieldProcessor()`를 도는 순수 스핀 루프. 깨어나는 지연은 사실상 0이지만 코어를 계속 점유한다.

`ShouldUseCVWait()`은 환경변수 `NET_FORCE_BUSYSPIN`을 한 번 읽어 캐싱한다 — **CV-wait이 기본값**이고, `NET_FORCE_BUSYSPIN`을 설정하면 재빌드 없이 busy-spin으로 되돌아간다(안전망).

이 기본값을 정하기까지 몇 차례 재평가가 있었다: 이 스트레스 하네스처럼 OS 프로세스 수십 개가 동시에 경합하는 환경에서는 CV-wait의 깨어남 지연이 핸드셰이크 재시도 창을 넘겨버려 완주율이 떨어지는 현상이 반복 관측됐고, 그중 두 가지는 실제 동시성 버그였다(활동 기반 pending-connection 만료 부재, `Internal_AddJob`의 lost-wakeup 레이스)로 밝혀져 고쳤다. 두 버그를 고친 뒤에도 부하가 높을 때의 완주율 저하가 완전히는 닫히지 않았는데, 이는 이 스트레스 하네스 특유의 프로세스 과다구독(클라이언트 하나당 busy-spin OS 프로세스 하나) 때문으로 추정되며, 실제 배포 환경에서는 나타나지 않을 아티팩트일 가능성이 크다는 판단 아래 CV-wait을 최종 기본값으로 채택했다.

### 5.5 `thread_index_type`과 핀 고정

`thread_index_type`(및 `thread_count_type`/`thread_id_type`/`thread_type_type`)은 모두 `Network::HAL::StrongType<int64_t, counter<>, integral_constant<int64_t,-1>>`의 인스턴스다 — 상태를 갖는 메타프로그래밍 `counter<>` 트릭으로 각 인스턴스화마다 구분되는 컴파일타임 태그를 부여하는 제로 오버헤드 타입 래퍼, 기본값 `-1`("미할당"/"핀 고정 안 됨").

`NetConnection::OwnerThreadId`(기본 `-1`)는 `AssignConnectionThreadIndex()`(익명 네임스페이스 자유함수, `IpNetDriver.cpp`)가 커넥션당 정확히 한 번 배정한다 — 정적 원자 카운터로 `JobSystem::GetThreadCount()`를 모듈로 라운드로빈. `IpNetDriver::AddClient_Internal()`(서버 accept 커넥션, `ConnectionsMutex` 해제 후)과 `IpNetDriver::InitConnect()`(클라이언트의 `ServerConnection`, `CreateInitialChannels()` 이후)에서 호출된다 — **반드시** 커넥션의 `Channels`가 완전히 채워진 뒤에 실행해야 한다(그렇지 않으면 아직 채널 목록이 안 끝난 커넥션을 Job이 건드릴 수 있음).

`GameNetConnection`의 세 Job 제출 지점(`ReceivedRawPacket`/`TickDispatch`/`TickFlush`) 모두 `GetOwnerThreadId()`를 넘겨 `AddAsyncJob()`의 명시적 `thread_index_type` 오버로드를 호출한다 — 이게 커넥션의 모든 Job을 그 하나의 배정된 워커의 **로컬** 큐로 핀 고정시킨다.

**왜 존재하는가**: "한 커넥션의 Job은 항상 하나의 스레드에서, push된 순서대로 실행된다"는 불변식을 지키기 위해서다 — `NetChannel` 상태(`OutBunchQueue`/`OutUnAckedBunches`/`InUnreadBunchMap`)에는 **별도의 락이 전혀 없다** — 핀 고정이 유일한 안전 장치이지, 락이 아니다. `SListQueue`의 로컬 큐 FIFO 순서 + `RunPendingTsak`이 로컬 큐를 글로벌 큐보다 먼저 비우는 것이 순서 보장의 핵심이다. JobSystem이 워커 여러 개로 돌게 된 뒤 대규모 실측 로그로 정확성을 검증했다.

### 5.6 세 종류의 엔진 스레드 — TickLoop vs IOCP 드레인 vs JobSystem 워커

**TickLoop** (`ThreadManager::Run()`이 루프 없이 `emplace_back(&ThreadManager::TickLoop, ...)`을 정확히 한 번만 호출: `LaDelta::TickSweep()`을 반복 호출 — `Driver->TickHousekeeping()`(핸드셰이크 캐시 정리, 커넥션 타임아웃 스캔), `Driver->TickDispatch()`, `Driver->TickFlush()` 순으로, 각 커넥션마다 Job 하나씩만 큐잉한다(실제 채널/Bunch 작업은 여기서 안 함). 다음 사이클까지는 타이머로 대기한다(`WaitOnHighResTimer`)

**IOCP** (`NET_IOCP_THREAD_COUNT`, 현재 컴파일타임 기본값 **2**; `NET_IO_THREADS` 환경변수로 런타임 오버라이드 가능): `ThreadManager::Run()`이 이 개수만큼 `IOLoop()` 스레드를 스폰한다. 각 `IOLoop()`은 `GetQueuedCompletionStatusEx`로 소켓 완료를 최대 64개씩 배치 처리하고, 완료된 수신 데이터그램을 `PacketSimulator`를 거쳐 처리 — 기존 커넥션이면 `Connection->ReceivedRawPacket(Reader)` 호출(Job 하나 큐잉), 신규면 핸드셰이크 상태머신을 직접 진행한다. 완료 큐가 비면 최대 50ms까지 블로킹 대기하다가 다시 확인 — TickLoop과 달리 **의도적으로 제한이 없다**. 2개 이상을 돌려도 안전하다

**JobSystem 워커 풀**: `NetDriver::InitBase()`에서 `JobSystem::Init(ResolveWorkerCount(GetSetting()->IsClient() ? 1 : 8))` — **서버 프로세스 8개, 클라이언트 프로세스 1개**(`NET_JOBSYSTEM_WORKERS` 환경변수로 오버라이드 가능). 서버 8은 워커 스케일링 실측(1/2/4/8 중 8이 가장 높은 jobs/s)에 근거하고, 클라이언트 1, 커넥션별 작업(`ReceivedRawPacket`/`TickDispatch`/`TickFlush`의 람다 본문)이 여기서 실행된다, [5.5절](#55-thread_index_type과-핀-고정)의 핀 고정 규칙에 따라.

**관계**: TickLoop과 IOCP 드레인 풀 둘 다 "언제·무슨 일이 생겼는지 감지해서 Job으로 포장해 JobSystem에 넘기는" 디스패처일 뿐, 실제 파싱·상태갱신·송신 같은 무거운 작업은 전부 JobSystem 워커에서 돈다 — 그래서 워커의 `jobs/sec`가 이 엔진의 실질적인 처리량 지표로 쓰인다.

`NetworkService::NetworkStatusCheck()`(모든 커넥션에 `NMT_NetSpeed`를 브로드캐스트)의 발화 간격은 `SyntheticTrafficHz` 설정이 게이트하며, `NET_IOCP_THREAD_COUNT`와는 별개이고 무관하다.

---

## 6. 드라이버 계층 (`Project/Network/src/driver/`)

파일: `NetDriver.h/.cpp`(베이스), `IpNetDriver.h/.cpp`(구체 UDP/IOCP 드라이버).

### 6.1 IOCP 설정

`IpNetDriver` 생성자가 즉시 완료 포트를 만든다: `CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0)`. 소켓은 나중에 `SetUdpSocket_Internal()`에서 연결된다: `CreateIoCompletionPort((HANDLE)socket, DriverHandle, 0, 0)`.

### 6.2 `PreRecvEvent` / `RecvCompletionEvent`

`NetDriver::PreRecvEvent()`가 초기에 **100개**의 오버랩드 `WSARecvFrom()`을 미리 posting한다(고정 크기 pre-post 풀, 각각 `new InRecvPacketEvent()`). `PostRecvEvent(event)`가 완료 소비 후 하나씩 재-post — `RecvEventPooleturner::operator()`가 `shared_ptr<InRecvPacketEvent>`의 deleter로 배선되어, 마지막 참조가 사라지는 정확히 그 시점에 재-post된다.

`IpNetDriver::RecvCompletionEvent()`: 원시 이벤트를 `shared_ptr`로 감싸고, `RecvPacketReader`를 만들고, `Simulator.Process(...)`(recv-side 시뮬레이션, [7장](#7-패킷-손실-시뮬레이터-projectnetworksrcsimulation))를 거쳐 시뮬레이터가 내놓는 각 데이터그램(0, 1, 또는 2개)마다 `DispatchReceivedDatagram()`을 호출.

### 6.3 `DispatchReceivedDatagram` — 연결 없는/있는 라우팅

- **클라이언트 모드**: `MyServerConnection`의 주소와 일치하면 곧바로 `MyServerConnection->ReceivedRawPacket(Reader)`(클라이언트는 항상 피어가 하나뿐).
- **서버 모드**: `HashToConnectionMap`(IPv4:port 합성 해시로 키잉)을 `shared_lock` 아래서 조회. 찾으면 `Connection->ReceivedRawPacket(Reader)`. 못 찾으면 `ProcessConnectionlessPacket(Reader)` → `IncomingConnectionless()`(핸드셰이크 진입점).

### 6.4 초당 1회 하우스키핑과 IOCP 드레인 — 서로 다른 스레드

`NetDriver::TickHousekeeping()`(베이스)이 매 호출마다 델타타임을 계산하고(`DriverMutex` 아래), **초당 1회** 블록에서 `CacheCleanupIndex`를 진행시키며 `UdpProcessor->ConnectionsUpdate(CacheCleanupIndex)`(60버킷 스윕, [2.6절](#26-pending-connection--핸드셰이크-중간-상태) 참고)와 `CheckConnectionTimeouts()`([2.3절](#23-keepalive--타임아웃-메커니즘) 참고)를 호출한다. `IpNetDriver::TickHousekeeping()`은 이걸 먼저 호출한 뒤 `Simulator.FlushStale()`을 처리한다. **이 전부가 TickLoop 스레드**([5.6절](#56-세-종류의-엔진-스레드--tickloop-iocp-jobsystem-워커))에서 돈다.

소켓 완료 드레인은 완전히 별개의 메서드/스레드다: `IpNetDriver::PumpIO(TimeoutMs)`가 `GetQueuedCompletionStatusEx`(배치, 최대 64개씩)로 완료를 뽑는다. `ThreadManager::IOLoop()`이 이 함수를 `TimeoutMs=50`으로 반복 호출한다 — TickHousekeeping과 달리 이쪽엔 초당 1회 게이트가 없다(패킷 도착 자체가 자연스러운 상한).

### 6.5 `AddClient` / `AddClient_Internal` / `RemoveConnection`

`AddClient(Key, PendingConnection)`: 새 `GameNetConnection` 생성 → `GControlProtocolHandler->AddPlayerConnection(NewConnectionId)`(게임 레이어 훅, 커넥션이 완전히 배선되기도 전에 먼저 알림) → `InitRemoteConnection()` → `InitSequence()` → `CreateInitialChannels()` → `AddClient_Internal()`.

`AddClient_Internal()`: `ConnectionsMutex` unique_lock 아래서 `HashToConnectionMap`/`ConnectionMap`/`RemoteConnections` 세 곳에 함께 삽입. 락 해제 **후** `AssignConnectionThreadIndex(Connection)`([5.5절](#55-thread_index_type과-핀-고정)).

`RemoveConnection()`은 [2.4절](#24-close--연결-제거-흐름) 참고.

### 6.6 `ConnectionsMutex` 락 규율

`NetDriver::ConnectionsMutex`는 `std::shared_mutex`, `protected`로 선언되어 `IpNetDriver`가 자기 자신의 두 맵(`HashToConnectionMap`/`ConnectionMap`)에도 같은 락을 재사용한다 — "항상 함께 변경된다"는 불변식을 지키기 위해 별도 락을 추가하지 않았다. 읽기(`GetClientConnections()` 등)는 락 아래서 **복사본**을 반환(참조 절대 노출 안 함), 쓰기는 unique_lock, 오래 걸리는 작업(디스패치 루프, `Close()`)은 항상 락을 놓은 뒤 스냅샷 위에서 수행한다 — 이 패턴이 커넥션/드라이버 계층 전체에서 일관되게 반복된다.

---

## 7. 패킷 손실 시뮬레이터 (`Project/Network/src/simulation/`)

`Network::Simulation::PacketSimulator` — recv-side 네트워크 상태 시뮬레이터(드롭/중복/제한된 재정렬), `IpNetDriver::RecvCompletionEvent()`에서 어떤 디스패치 로직보다도 먼저 적용된다.

- **설정**(`PacketSimulationSettings`): `DropPermille`(0-1000), `DuplicatePermille`(0-1000), `ReorderWindowSize`(0이면 비활성; 이 개수만큼 버퍼링 후 가득 차면 무작위로 하나 방출), `ReorderMaxHoldMs`(기본 50 — 윈도우가 이 시간 동안 안 차면 강제 flush).
- **`Process()`**: `Mutex` 아래서 드롭 롤 먼저(드롭되면 빈 벡터 반환); 아니면 중복 롤로 1개 또는 2개짜리 벡터 구성; 재정렬 비활성이면 그대로 통과; 활성이면 윈도우에 push하고, 윈도우가 다 찼을 때만 무작위 인덱스 하나를 골라 방출.
- **`FlushStale()`**: `IpNetDriver::TickHousekeeping()`마다 1회 호출(TickLoop 스레드) — 마지막 활동 이후 `ReorderMaxHoldMs`가 지났으면 윈도우 전체를 한꺼번에 방출.
- **`ShouldDropOutbound()`**: 송신측 대응 — 순수 예/아니오 롤. 중복/재정렬은 송신측에 없다(`OutSendPacketEvent`가 `RecvPacketReader`와 소유권/생명주기가 달라, 복제하려면 진짜 클론된 IOCP 완료가 필요하고, 재정렬은 별도의 시간 기반 보류-방출 큐가 필요해 — 검증 목적에 비해 훨씬 위험한 변경으로 판단, 드롭만 구현).

**분리**: `IpNetDriver`는 독립된 두 인스턴스를 갖는다 — `Simulator`(recv-side, 전체 드롭/중복/재정렬)와 `OutboundSimulator`(send-side, 드롭만). 둘 다 `InitListen()`/`InitConnect()`에서 각자 `Reconfigure()`된다.

**스레드 안전성**: `PacketSimulator`는 내부적으로 `std::mutex` 보호된다 — 소켓 완료를 뽑는 스레드가 하나라는 보장이 없기 때문이다. 정확히 `IpNetDriver::PumpIO()`를 부르는 `NET_IOCP_THREAD_COUNT`개의 IOLoop 스레드가 동시 호출자다. `OutboundSimulator`는 추가로 각 커넥션의 핀 고정된 JobSystem 워커에서도 `SendToRemote()`를 통해 호출되므로 진짜 멀티스레드 접근이다 — 이 클래스가 단일 호출자 가정에 기대지 않고 스스로 락을 거는 이유다.

---

## 8. 설정 / 로깅 (`Project/Network/src/utils/`)

### 8.1 설정 JSON 스키마 (`ServerConfig`)

`"Network"` 최상위 키 아래 nlohmann::json으로 로드. 전체 필드:

| 필드 | 타입 | 용도 |
|---|---|---|
| `RemoteIp`/`RemotePort` | string/int32 | 피어 주소(클라이언트: 접속할 서버) |
| `DriverMode` | `EMode`(`None`/`Client`/`Server`) | 드라이버 종류 |
| `ClientType` | `EClientType`(`None`/`TestClient`/`UEClient`) | Client 모드에서만 의미 |
| `ServerType` | `EServerType`(`None`/`LoginServer`/`GatewayServer`/`GameServer`) | Server 모드에서만 의미 — **`LaDelta::InitDriver()`는 실제로 `LoginServer`만 처리한다** |
| `bNoTimeouts` | bool | true면 `GetTimeoutValue()`가 `FLT_MAX` 반환(수신 타임아웃 종료 비활성화) |
| `bConnectionlessOnly` | bool | (읽히지만 이번 조사에서 이걸로 분기하는 동작을 찾지 못함) |
| `KeepAliveTime` | uint16 | 이 시간 동안 송신이 없으면 헤더만 있는 keepalive 발송(기본 20) |
| `MaxTickRate` | uint16 | 틱/초 상한. TickLoop의 스윕 주기를 조절한다 |
| `InitialConnectTimeout`/`ConnectionTimeout` | uint16 | 각각 pending/established 상태의 수신 타임아웃(기본 60) |
| `MaxChannelsSize` | uint32 | 커넥션당 `Channels` 벡터 크기(기본 16) |
| `DesiredRecvSize`/`DesiredSendSize` | uint32 | 소켓 버퍼 힌트 — **실제로는 `DriverSetting` 생성자가 모드별로 하드코딩한 값(서버 32768, 클라 8192)이 이 JSON 값을 덮어쓴다**, 사실상 미사용 |
| `SessionID` | uint32 | 서버 전용, 핸드셰이크 헤더에서 echo/검증 |
| `ActiveSecret` | uint8 | 서버 전용, `GenerateCookie`에 쓸 `HandshakeSecret` 인덱스 |
| `MagicHeaderSizeBits`/`MagicHeader`/`MagicHeaderOffset` | uint32 | 연결없는 패킷의 매직넘버 프레이밍 |
| `PacketDropPermille`/`PacketDuplicatePermille`/`PacketReorderWindow` | uint32 | recv-side 시뮬레이션, 기본 0 |
| `PacketDropPermilleOutbound` | uint32 | send-side 드롭 전용, 기본 0 |
| `PendingConnectionInactivityTimeoutSec` | double | pending 커넥션 회수 임계값, 기본 20.0 |
| `TelemetryPort` | uint16 | `TelemetrySink`([10장](#10-기타-network-하위-디렉토리)) 리스닝 포트, 기본 0(비활성) |

설정 파일 경로 해석(`GetConfigJsonPath`): 명시적 override → `LADELTA_CONFIG_PATH` 환경변수 → `<exe의 조부모>/Project/<exe이름>/config/DefaultNetworkEngine.json` → `<exe 디렉토리>/Config/DefaultNetworkEngine.json` 순으로 폴백.

`DriverSetting`은 생성 후 불변인 런타임 해석 뷰다 — 눈에 띄는 점: `DesiredRecvSize`/`DesiredSendSize`를 JSON이 아니라 모드별 하드코딩 상수에서 도출하고, `HandshakeSecret`을 조건 없이 0으로 채운다([2.6절](#26-pending-connection--핸드셰이크-중간-상태) 참고).

### 8.2 로깅 (`ServerLog::Init`, `AyncyLog.h`)

spdlog 비동기 로거 사용: `spdlog::init_thread_pool(10000, 1)`(10000개 큐, 백그라운드 스레드 1개). 로그 디렉토리 해석: `current_path().parent_path().parent_path() / "Logs"`(작업 디렉토리 기준 2단계 위), 없으면 생성; `Logs/<Server|Remote>/network_log_pid<PID>.txt`(프로세스별 파일 — 여러 프로세스가 같은 파일에 동시에 쓰며 서로 뒤섞이는 걸 방지). `spdlog::sinks::daily_file_sink_mt` 사용(23:59 기준 매일 로테이션). 로그 레벨은 설정 필드로 조절 가능(기본 `trace`).

---

## 9. 프로세서 / 파이프라인 (`Project/Network/src/processor/`)

핸드셰이크/쿠키 로직은 [2.6절](#26-pending-connection--핸드셰이크-중간-상태)에서 이미 다뤘고, 상태머신/패킷 파이프라인은 [`architecture.md`](architecture.md)가 다룬다. 그 문서가 다루지 않는 구조적인 부분만 보충:

- `PacketProcessor`(베이스): `IncomingConnectionless/Incoming/OutgoingConnectionless/Outgoing/Initialize/Tick/NotifyHandshakeBegin` 순수가상. `UdpConnectionProcessor`가 현재 **유일한** 구상 서브클래스다 — `EProcessorType::Tcp`가 열거형에 존재하지만 `PacketPipeline::AddProcessor()`는 `Udp`만 처리한다(TCP는 미구현/흔적기관).
- `PacketPipeline`: `ProcessorContainer`(현재 항상 `UdpConnectionProcessor` 하나) 소유, `Incoming`/`Outgoing`을 모든 프로세서에 팬아웃(오늘은 사실상 하나뿐이라 트리비얼하지만 여럿을 지원하도록 작성됨). `BeginHandShake()`가 완료 델리게이트를 저장하고 컨테이너의 **첫** 프로세서에 `NotifyHandshakeBegin()`을 부른다. `BeginNMTHello()`가 그 델리게이트를 한 번 발동(클라이언트가 최종 `Ack`를 받았을 때, `UdpConnectionProcessor::Incoming()`에서 호출 — 게임 레이어의 `NMT_Hello` 발송을 트리거).

---

## 10. 기타 Network 하위 디렉토리

**`net_packet/`**: 패킷/Bunch 데이터 모델. `NetPacket.h`의 `OutReadyPacket`(아웃바운드 패킷 구조체 — `BufferViews`, `Header`, `Bunchs`, `HeaderSize`/`TotalSize`/`OutPacketId` 등), `PacketHeader`(BitWriter 기반 헤더 빌더). `NetPacketBuilder.h`의 `ConnectionPacketBuilder`(일반 커넥션 패킷, `PacketMaxSize=1024`, `PacketHeaderSize=10`바이트)와 `ProcessorPacketBuilder`(핸드셰이크/연결없는 패킷, `ConnectionlessPacketHeaderSize=100`바이트) — 둘 다 `HeaderWriter` 콜백을 받는 템플릿. `NetPacketReader.h`의 `RecvPacketReader`(원시 IOCP 수신 버퍼를 `BitReader` 헤더 뷰 + 페이로드 포인터/크기 + `WindowsAddr` + `HashKey`로 파싱). `PacketEvent.h`의 `PacketEvent : OVERLAPPED`(IOCP 오버랩드 I/O 베이스), `OutSendPacketEvent`(`WSABUF`로 감싸며 `enable_shared_from_this`로 참조 카운트, `EndSendEvent()`로 재사용 가능), `InRecvPacketEvent`(고정 `std::array<uint8_t, MAX_PACKET_SIZE>` 스크래치 버퍼).

**`net_socket/`**: 얇은 Windows 소켓/주소 추상화. `NetworkSocket`(베이스)/`WindowsSocket`(구체 — bind/listen/reuse-addr/non-block/TCP-nodelay). `NetAddr`(추상 인터페이스)/`WindowsAddr`(구체, `sockaddr_storage` 기반, IPv4/IPv6 모두 지원). `SocketBuilder.h`: 템플릿 팩토리로 non-blocking/reuse-addr/overlapped UDP 또는 TCP 소켓을 빌드.

**`protocol_handler/`**: 엔진 쪽 플러그형 게임-프로토콜-핸들러 메커니즘. `GActorProtocolHandler`/`GControlProtocolHandler` 프로세스 전역을 정의. `ProtocolHandlerManager`가 `NetDriver`가 소유하는 작은 컨테이너.

**`service/`**: 공개 C API 표면(`export.h`)과 그 구현. `LaDelta.h/.cpp`가 최상위 파사드 클래스([5.6절](#56-세-종류의-엔진-스레드--tickloop-iocp-jobsystem-워커) 참고) — 설정→드라이버→핸들러→인터페이스를 배선하고, `extern "C"` 함수들(`export.cpp`)에 `InitAPI/TickOnce/EnqueueMessageBuffer/StartOnlineGame`을 노출한다. `Project/Network/src/service/NetworkService.cpp`는 사실상 빈 번역 단위(`#include "INetworkService.h"`뿐) — 실제 `NetworkService` 구현은 실행 파일마다(`Project/GameServer/service/`, `Project/RemoteServer/service/`) 따로 있다.

**`telemetry/`**(`../web-visualization.md`): `TelemetrySink`(싱글턴, `Get()`) — 이 엔진 안에서 유일하게 "엔진 자체를 보여주기 위해" 존재하는 컴포넌트다. `TelemetryPort`(위 8.1절 표)가 0이 아니면 127.0.0.1 전용 TCP 서버를 열어, 같은 소켓으로 텔레메트리(핸드셰이크/패킷 송신/Ack·Nack/드롭·중복·재정렬/게이지/연결 생명주기 — 각각 기존 로직 바로 옆에 한 줄씩 추가된 계측 지점에서 발행)를 내보내고 제어 명령(`set_drop`, `IpNetDriver`가 `Simulator`/`OutboundSimulator`를 실시간으로 재설정하는 데 씀)을 받는다. 핫 패스가 절대 소켓 I/O에 블록되지 않도록 `Emit()`은 JobSystem과 같은 `SListQueue`에 미리 직렬화한 문자열만 push하고, 전담 스레드가 드레인한다. 늦게 접속한 구독자를 위한 최근 이벤트 300개 링 버퍼도 갖고 있다 — `Project/Network/src/net_socket/`의 UDP/IOCP 지향 소켓 추상화와는 의도적으로 완전히 분리된, 블로킹 TCP 기반의 독립 구현이다. `Programs/WebViz/`의 Node.js 브릿지와 웹 프론트엔드가 이 소켓의 유일한 소비자다.

---

## 11. 게임 레이어 (`Project/GameServer/`)

### 11.1 RoomManager / Room / Player

`RoomManager`: `Rooms`(vector) + `RoomMap`(id→Room), `Game::IDAllocator RoomIdAllocator`, 하드코딩된 `CredentialStore`(`{"test_id_1":"test_pw_1"}`, `RemoteServer`의 하드코딩된 테스트 로그인과 일치 — 실제 인증/DB 계층의 대체물이 아니라는 주석 명시), `PlayerIdAllocator`(`Free()` 경로가 절대 호출되지 않음 — 플레이어 제거 후 ID 재사용 흐름이 없음). `RoomsMutex`(shared_mutex)로 보호 — `AddPlayer()`/`AddNewRoom()`은 unique_lock, `BroadCast()`/`GetRoomAll()`/`VerifiedUser()`/`RemovePlayer()`는 shared_lock 후 찾은 `Room`의 자체 `PlayersMutex`로 위임. `AddPlayer()`는 기존 방을 스캔해 자리가 있는 방을 찾고(first-fit), 없으면 `AddNewRoom()`. `VerifiedUser(Id, Password, ConnectionId)`는 자격 증명 확인 → 이 커넥션을 이미 가진 방을 찾아(`FindRoomWithConnection`, O(rooms) 선형 스캔 — 이 프로젝트 규모에서는 충분) → 실제 `playerId` 할당 → `Player::VerifiedUser(playerId)` 호출.

`Room`: `maxPlayer=1024`(하드코딩), `Players`(vector, 브로드캐스트 순회 순서) + `playerMap`(ConnectionId→Player, 조회용), 자체 `PlayersMutex`(`RoomManager`의 락과 의도적으로 분리 — "한 방의 브로드캐스트가 다른 방의 입장을 막지 않도록"). `AddPlayer()`가 자기 락 아래서 용량을 재확인(check-then-act 경합 방어). `RemovePlayer(playerId)`는 `playerMap`과 `Players` **둘 다** 지운다 — 전자만 지우면 후자에 유령이 남아 `BroadCast()`가 계속 전송을 시도한다.

`Player : Character : Actor : Object`: `EPlayerLoginState{NotAuthenticated, Verified}`, `playerId`(DB/로그인 키), `ConnectionId`(런타임 네트워크 키). 상속 체인의 나머지(`Character`/`Actor`/`Object`)는 각각 1-2개의 트리비얼한 필드만 추가하는 얇은 스캐폴딩이다.

### 11.2 NetworkService (GameServer)

`NetworkService::Tick()`: 설정된 간격으로 `NetworkStatusCheck()`를 게이트한 뒤 무조건 `NetworkKitTickOnce(NETWORK_HANDLE)`(TickHousekeeping/TickDispatch/TickFlush를 한 번에 묶어 부르는 편의 API) 호출. **다만 `ThreadManager`(실제 GameServer/RemoteServer 프로덕션 경로)는 이 `Tick()`을 부르지 않는다** — `TickLoop`이 `TickSweep()`을, `IOLoop`이 별도로 `PumpIO()`를 부른다([5.6절](#56-세-종류의-엔진-스레드--tickloop-iocp-jobsystem-워커)). `Tick()`/`NetworkKitTickOnce()`는 소스에 여전히 존재하고 컴파일되지만("`ThreadManager`를 직접 구동하는 대신 쓰로틀 없이 `Service->Tick()`을 busy-loop하고 싶은 호스트를 위한 것"이라는 주석이 있음), 현재 어떤 실제 호출 경로도 이걸 쓰지 않는다(전수 grep 확인) — 레거시 호환 API다. `AddPlayer`/`RemovePlayer` 둘 다 `ConnectionReliableMapMutex`를 잡고 `ConnectionReliableMap`을 변경한 뒤 `roomManager->AddPlayer`/`RemovePlayer`로 위임 — `RemovePlayer`는 `NetConnection::Close()`가 핀 고정된 JobSystem 워커에서도 실행 가능해진 뒤로 생긴 크로스 스레드 접근 표면이라는 주석이 달려 있다. `NetworkStatusCheck()`가 `ConnectionReliableMap`의 모든 엔트리에 `NMT_NetSpeed`(단조 증가하는 카운터를 담음, 실제 ack-time이 아님)를 보낸다 — 이게 신뢰성 자가 테스트 메커니즘이고, 수신 측은 `ControlProtocolServerHandler::Protocol_NMT_NetSpeed`가 엄격한 순서 도착을 검증한다.

### 11.3 ControlProtocolHandler / ControlProtocolServerHandler

베이스 `ControlProtocolHandler`: `GControlProtocolHandler` 전역, `EControlProtocol`(`INVALID/NMT_Hello/Challenge/Login/Welcome/Join/NetSpeed/Failure`), 디스패치 테이블, `AddPlayerConnection`/`RemovePlayerConnection`(둘 다 죽어있는 `INetworkEventNotify` 인터페이스를 거치지 않고 전역을 통해 직접 호출한다는 게 명시적으로 주석 처리되어 있다).

`ControlProtocolServerHandler`: `Init()`이 `NMT_Hello/Login/Join/NetSpeed/Failure`를 등록. `Protocol_NMT_Login`이 실제 자격 증명 확인 — `RoomManager::VerifiedUser` 호출, 로그, `NMT_Welcome` 또는 `NMT_Failure` 발송. `Protocol_NMT_NetSpeed`가 커넥션당 `RUdpStatus{expectedReliable, totalPacket, passCount, failCount}`(모두 원자)를 `ConnectionRUDPMap`에서 관리, `ConnectionRUDPMapMutex`는 맵 구조 변경(`operator[]`)에만 걸리고 필드 자체 갱신은 락 없이(엔트리가 절대 삭제되지 않으므로 안전) — 이게 `NetworkService::NetworkStatusCheck()`의 신뢰성 순서 테스트를 수신 측에서 검증하는 대응 코드다.

`ActorProtocolServerHandler`는 `S_Spawn`/`S_Move` 핸들러를 등록하지만 `Protocol_C_Move`/`Protocol_C_Spawn` 둘 다 그냥 `return true` — 액터 게임플레이 로직은 완전히 스텁이다. 전송 레벨(채널, 메시지 프레이밍, protobuf 스키마)까지는 배선되어 있지만 실제 게임플레이 로직은 없는 스캐폴딩.

### 11.4 `GControlProtocolHandler` 전역과 훅 포인트

`LaDelta::InitProtocolHandler()`에서 한 번 설정되며(`CreateControlHandler()` → `new ControlProtocolServerHandler()`), 네트워크 레이어와 게임 레이어를 잇는 **유일한** 훅 지점이다: (a) 메시지 디스패치 — `ControlChannel::DispatchPendingMessage()`가 직접 `GControlProtocolHandler->ControlProtocol(PlayerId, ...)`를 호출; (b) 연결 생명주기 알림 — `IpNetDriver::AddClient()`가 `AddPlayerConnection(NewConnectionId)`(커넥션 객체가 완전히 구성되기 전!), `IpNetDriver::RemoveConnection()`이 `RemovePlayerConnection(RemovedConnectionId)`(커넥션이 드라이버 맵에서 완전히 지워진 후). `GActorProtocolHandler`도 Actor 채널에 대해 같은 패턴으로 존재한다.

### 11.5 RemoteServer — 테스트/부하용 클라이언트

`Project/RemoteServer/`는 `Project/GameServer/`와 구조적으로 대칭이지만 클라이언트로 설정된다. `ControlProtocolClientHandler`가 클라이언트 측 프로토콜 반응을 구현: `Protocol_NMT_Challenge`가 하드코딩된 `id="test_id_1"/password="test_pw_1"`으로 자동 응답, `Protocol_NMT_Welcome`이 성공 로그 후 `NMT_Join` 발송, `Protocol_NMT_Failure`가 거부를 로그, `Protocol_NMT_NetSpeed`가 서버 측과 동일한 `RUdpStatus` 순서 검증 로직을 돈다. `AddPlayerConnection`은 `RoomManager`가 없으므로 `GNetworkService->SetServerConnectionId(ConnectionId)`만 호출한다. **`RemoteServer`는 헤드리스 부하/소크 테스트 클라이언트이지, 실제 게임 클라이언트가 아니다.**

**프로세스 하나가 커넥션 여러 개를 호스팅한다.** `RemoteServer.exe <N>`(위치 인자)으로 실행하면, N개의 독립된 `NetworkService`+`ThreadManager` 쌍(각자 자기만의 `IpNetDriver`/소켓/커넥션 하나)을 한 프로세스 안에 만든다 — "커넥션 수 = OS 프로세스 수"였던 이전 구조의 비용(수백~수천 커넥션을 내려면 그만큼 프로세스를 스폰해야 했던)을 없앤다. 클라이언트 쪽 `UniqueConnectionId`는 이제 그 커넥션 소켓 자신의 로컬 ephemeral 포트 번호를 쓴다(`IpNetDriver.cpp`, `::getsockname()`) — 한 프로세스 안에서 소켓마다 당연히 겹치지 않으므로, 여러 커넥션을 구분할 근거가 확실하다. `Programs/WebViz/`의 "+100개 한번에"/"+1000개 한번에" 버튼과 `Programs/StressTest/Run-*.ps1`의 `-ConnectionsPerProcess`가 이 기능을 쓴다.

---

## 12. Core 라이브러리 (`Project/Core/`)

별도 VS 프로젝트(`Core.vcxproj` → `Core.lib`), `include/`(공개 헤더) + `src/`(구현).

### 12.1 BitReader / BitWriter

둘 다 `BitController`(UE `FArchive` 스타일 추상 비트 직렬화 베이스)를 상속 — `ArIsSaving`/`ArIsLoading`/`ArIsError` 비트필드, 모든 기본 수치 타입에 대한 friend `operator<<` 오버로드, 엔디안 스와핑 지원.

- `BitWriter`: `std::vector<uint8_t> Buffer` + `Pos`/`Max` 비트 커서. `Serialize()`/`SerializeBits()`/`SerializeBitsWithOffset()`(원시 비트 범위 쓰기), `SerializeInt()`/`SerializeIntPacked()`(제한/가변 길이 정수), `WriteBit()`. 패킷/Bunch/핸드셰이크 헤더 구성 전반에 쓰인다.
- `BitReader`: 대칭 리더 — `ReadInt()`/`ReadBit()`, `GetBitsLeft()`/`AtEnd()`/`SetAtEnd()`(핸드셰이크 패킷을 다 읽은 뒤 `UdpConnectionProcessor::Incoming()`이 더 이상의 파싱을 막는 데 사용).

와이어 상의 모든 것(패킷 헤더, Bunch 헤더, `NetPacketNotify`의 Ack/Nack 헤더, 핸드셰이크 패킷)의 기반 이진 직렬화 프리미티브다 — protobuf 메시지 자체는 별도로(`SerializeToArray`) 직렬화되어 `BitWriter`/`BitReader`가 프레이밍하는 Bunch 안에 불투명 페이로드 바이트로 실린다.

### 12.2 커스텀 할당자 — 존재하지만 실제 할당 경로에 배선되어 있지 않음

`MallocBinned.h/.cpp`: UE4/5 스타일의 밑바닥부터 만든 바인드 풀 할당자 — 41개 크기 클래스 풀(16바이트~32768바이트), 각 풀 64KB 슬랩, Windows `SLIST_ENTRY`/`SLIST_HEADER`로 프리 블록 추적. `StompAllocator.h/.cpp`: use-after-free/오버런 탐지용 가드 페이지 할당자.

이 두 클래스는 자기 자신의 헤더/소스 파일 밖 어디서도 참조되지 않는다(전수 grep 확인). 실제로 엔진 전체가 쓰는 할당 경로 — `CORE::New<T>()`/`CORE::Delete<T>()`/`CORE::TMakeShared<T>()` — 는 `LMemory::Allocate`/`Deallocate`(순수 `std::malloc`/`std::free` 래퍼)를 거친다. 즉 **커스텀 할당자는 컴파일은 되지만 죽은 코드다** — 모든 `OutReadyPacket`/`OutReadyBunch`/`InBunchReader`/커넥션/채널 등의 실제 할당은 CRT 힙으로 직행한다.

### 12.3 NetAddr / WindowsAddr

`NetAddr`(추상, `Project/Network/src/net_socket/InternetAddress.h` — **`Project/Core/`가 아니라 Network 레이어에 산다**)와 `WindowsAddr`(유일한 구체 구현, `sockaddr_storage` 직접 래핑, IPv4/IPv6 모두 지원).

### 12.4 Clock 유틸리티

`Network::Clock`(`Project/Core/include/WindowsPlatformTime.h`) — `std::chrono::steady_clock`을 감싸는 작은 네임스페이스: `Now()`, `NowUs()`, `Since<Dur>(from,to)`. 커넥션/드라이버/채널/시뮬레이션 계층 전체가 일관되게 쓰는 유일한 시간 소스다 — 파일명("WindowsPlatformTime.h")과 달리 실제로는 그냥 `std::chrono::steady_clock`(Windows에서는 내부적으로 QPC 기반)일 뿐, 직접적인 플랫폼별 타이밍 API 사용은 없다.

---

## 13. 프로토콜 와이어 포맷 (`Protos/`)

세 개의 proto3 파일, 패키지 `GameProtocol`(`Common/protoc-21.12-win64/`로 컴파일).

**`Struct.proto`**: `Vector3D{x,y,z}`, `Vector4D{x,y,z,r}`(미사용), `Location{Coordinate: Vector3D}`, `Rotation{Coordinate: Vector3D}`(쿼터니언/오일러 이름이 아니라 `Vector3D`를 그대로 감싼 형태 — 아마 placeholder), `Character{CharacterClass: int32}`.

**`ControlProtocol.proto`**(`Control` 채널, `EControlProtocol`과 1:1 대응):
- `NMT_Hello{Token, NetworkId}` — 3-way 핸드셰이크 직후 클라이언트→서버 인사.
- `NMT_Challenge{Option, URL}` — 서버→클라이언트, 로그인 요청.
- `NMT_Login{Id, Password}` — 클라이언트→서버 자격 증명.
- `NMT_Welcome{GameName, WorldName, PlayerCharacter}` — 서버→클라이언트, 로그인 성공.
- `NMT_Join{Response}` — 클라이언트→서버, welcome 후 확인(서버 핸들러는 현재 로그만 남기고 아무것도 안 함).
- `NMT_NetSpeed{LastAckedPacketTime}` — 양방향 reliable 채널 keepalive/자가 테스트. 이름과 달리 실제 RTT/ack-시간이 아니라 단순 증가 카운터로 쓰이며, 양쪽의 `RUdpStatus`가 엄격한 순서 도착을 실측 검증한다.
- `NMT_Failure{ErrorType, ErrorNum}` — 서버→클라이언트, 로그인 거부(현재 `ErrorType=0`/`ErrorNum=1`만 생성됨).

**`ActorProtocol.proto`**(`Actor` 채널, `EActorProtocol`): `S_Spawn`/`C_Spawn`(동일한 필드 형태 — `PawnClass`, `NewLocation`, `NewRotation` — 서버 권위 vs 클라이언트 요청을 메시지 타입으로만 구분), `S_Move`/`C_Move`(동일 패턴). 앞서 언급했듯 인바운드 핸들러만 등록되어 있고 실제 로직은 스텁이다.

---

## 14. 참고 문서 지도

- [`architecture.md`](architecture.md) — 핸드셰이크 상태머신, 패킷 처리 파이프라인.
- [`sequence.md`](sequence.md) — `TSequenceNumber`/`TSequenceHistory`/`NetPacketNotify`의 수학적 기반.
- [`../web-visualization.md`](../web-visualization.md) — 신뢰성 메커니즘을 실시간으로 보여주는 웹 시각화 도구(`Programs/WebViz/`)의 구조와 사용법.

---

← [설계 결정](../decisions/README.md) · 다음 → [아키텍처](architecture.md)
