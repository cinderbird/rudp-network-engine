[문서 안내](../README.md) · [문제 해결 과정](../problem-solving.md) · **설계 결정** · [레퍼런스](../reference/engine-design.md) · [저장소 홈](../../README.md)

# 결정-0002: JobSystem 워커의 대기 방식으로 CV-wait을 채택한다

| | |
|---|---|
| **근거** | 측정을 통한 성능 향상 확인 |
| **관련** | [결정-0001](0001-connection-worker-pinning.md), [결정-0004](0004-tick-io-separation.md), [문제 해결 과정 사례 2](../problem-solving.md) |

## 배경

JobSystem 워커가 할 일이 없을 때 무엇을 하는가에 두 가지 선택지가 있었다.

- **busy-spin** — 절대 잠들지 않는다. 반응은 빠르지만 유휴 상태에서도 코어를 전부 태운다.
- **CV-wait** — 큐가 비면 `condition_variable`에 실제로 블록한다. CPU는 아끼지만 기상
  지연이 생긴다.

측정 결과:  CPU는 busy-spin이 197.9%, CV-wait이 2.3%로 **86배 차이**.

## 결정

**CV-wait을 기본값으로 채택.**

## 결과

**장점**
- 신뢰성을 유지한 채 CPU 86배 절감.

**단점**
- 복잡한 내부 구현.

---

← [결정-0001 커넥션 핀 고정](0001-connection-worker-pinning.md) · 다음 → [결정-0003 텔레메트리 opt-in](0003-telemetry-opt-in.md)
