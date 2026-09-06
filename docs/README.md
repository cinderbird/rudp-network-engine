**문서 안내** · [문제 해결 과정](problem-solving.md) · [설계 결정](decisions/README.md) · [레퍼런스](reference/engine-design.md) · [저장소 홈](../README.md)

# 문서 안내

```
docs/
├── README.md              이 문서 — 안내판
├── problem-solving.md     문제 해결 과정 (먼저 읽을 문서)
├── web-visualization.md   WebViz 실행 방법
├── decisions/             설계 결정 기록 5건 (ADR)
├── reference/             엔진 구조·알고리즘 상세 3건
└── assets/                스크린샷·데모 영상
```

## 처음 보신다면 — 읽는 순서

1. **[`problem-solving.md`](problem-solving.md)** — 개발 중 마주친 문제를 어떻게 분석하고
   해결했는지. **문제 해결에 대하여 정리되어 있습니다.**
2. **[`decisions/`](decisions/)** — 설계 결정 8건. 각 결정의 근거와 판단이 정리되어 있습니다.
3. **[`reference/engine-design.md`](reference/engine-design.md)** — 엔진의 구조가 정리되어 있습니다.

## 문서별 안내

### 이해하기

| 문서 | 내용 |
|---|---|
| [`problem-solving.md`](problem-solving.md) | 심층 사례 6편 + 짧은 사례 7건. 관측 → 가설 → 검증 설계 → 반증 → 수정 → 재검증 순서로 복원. |
| [`decisions/`](decisions/) | 설계 결정 기록. `Context / Decision / Consequences` |

### 찾아보기

| 문서 | 축척 | 내용 |
|---|---|---|
| [`reference/engine-design.md`](reference/engine-design.md) | 전체 | 계층별(커넥션 / 채널 / 스레딩 / 드라이버 / 게임 레이어 / 와이어 포맷) 구조와 핵심 로직 |
| [`reference/architecture.md`](reference/architecture.md) | 컴포넌트 | `UdpConnectionProcessor` — 상태머신, 수신/송신 경로, 4단계 핸드셰이크, Bunch 조립 |
| [`reference/sequence.md`](reference/sequence.md) | 알고리즘 | 14비트 wrap-around 산술, 256비트 히스토리 비트셋, Ack/Nack 처리 흐름 |


다음 → [문제 해결 과정](problem-solving.md)
