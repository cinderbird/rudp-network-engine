[문서 안내](../README.md) · [문제 해결 과정](../problem-solving.md) · **설계 결정** · [레퍼런스](../reference/engine-design.md) · [저장소 홈](../../README.md)

# 설계 결정 기록 (ADR)

## 목록

| # | 결정 | 
|---|---|
| [0001](0001-connection-worker-pinning.md) | 커넥션을 JobSystem 워커에 핀 고정한다 | 
| [0002](0002-cv-wait-scheduler.md) | 워커 대기 방식으로 CV-wait을 채택한다 | 
| [0003](0003-telemetry-opt-in.md) | 서버 상태 전송은 opt-in + 매크로 게이트 | 
| [0004](0004-tick-io-separation.md) | 소켓 완료 꺼내기(GetQueuedCompletionStatus)와 커넥션 관리를 분리한다 | 
| [0005](0005-multi-connection-load-generator.md) | 부하 생성기를 프로세스당 N 커넥션으로 | 

← [문제 해결 과정](../problem-solving.md) · 다음 → [결정-0001 커넥션 핀 고정](0001-connection-worker-pinning.md)
