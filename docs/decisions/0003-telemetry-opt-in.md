[문서 안내](../README.md) · [문제 해결 과정](../problem-solving.md) · **설계 결정** · [레퍼런스](../reference/engine-design.md) · [저장소 홈](../../README.md)

# 결정-0003: 서버 상태 전송은 기본 비활성 opt-in + 매크로 게이트

| | |
|---|---|
| **근거** | 테스트에서 성능 저하 검출 (핸드셰이크 성공률 50%) |
| **관련** | [결정-0001](0001-connection-worker-pinning.md), [문제 해결 과정 사례 3](../problem-solving.md) |

## 배경

엔진의 신뢰성 메커니즘(Ack/Nack/재전송)을 브라우저에서 실시간으로 보여주려면 엔진 곳곳에
상태 측정 지점이 필요하다. 

## 결정
1. **기본 비활성** — `TelemetryPort = 0`이면 소켓을 열지 않는다.
2. **활성 검사를 호출부에** — `Emit()` 내부가 아니라 매크로로 호출부를 감싼다.
   ```cpp
   #define NET_TELEMETRY_EMIT(...) \
       do { if (TelemetrySink::Get().IsEnabled()) { TelemetrySink::Get().Emit(__VA_ARGS__); } } while (0)
   ```
3. **핫 패스에서 소켓 write 금지** — 직렬화한 문자열을 lock-free 큐에 push, 전담 스레드가 처리.
4. **패킷당 이벤트를 만들지 않는다** — 주기 지표는 초당 1회 게이지에 필드로 얹는다.
5. **지표는 그 커넥션의 소유 스레드에서만 수집한다.**

## 결과
이번 결정에 따른 장단점

**장점**
- 기본 상태에서 기존 성능 결론이 그대로 유효.
- 테스트를 수행 할 때 

**단점**
- 호출부가 함수가 아니라 매크로. 디버깅이 불편하다.
- 계측 지점을 추가할 때마다 **스레드 소유권을 매번 따져야 한다.**

---

← [결정-0002 CV-wait 채택](0002-cv-wait-scheduler.md) · 다음 → [결정-0004 틱-IO 분리](0004-tick-io-separation.md)
