[문서 안내](../README.md) · [문제 해결 과정](../problem-solving.md) · [설계 결정](../decisions/README.md) · **레퍼런스** · [저장소 홈](../../README.md)

# 시퀀스 번호 시스템

`src/sequence/` 디렉토리는 RUDP 신뢰성 계층의 수학적 기반인 순환 시퀀스 번호 산술과 수신 이력 비트셋을 구현합니다.

---

## 목차

1. [TSequenceNumber — wrap-around 산술](#tsequencenumber--wrap-around-산술)
2. [TSequenceHistory — 비트셋 구조](#tsequencehistory--비트셋-구조)
3. [NetPacketNotify — Ack/Nack 흐름](#netpacketnotify--acknack-흐름)

---

## TSequenceNumber — wrap-around 산술

```cpp
template <size_t NumBits, typename SequenceType>
class TSequenceNumber;

// 실제 사용 타입 (NetPacketNotify 내부)
using SequenceNumberT = TSequenceNumber<14, uint16_t>;
```

### 상수 정의

```
NumBits         = 14
SeqNumberCount  = 2^14 = 16384   (시퀀스 공간 크기)
SeqNumberHalf   = 2^13 =  8192   (순환 비교 기준값)
SeqNumberMax    = 16383
SeqNumberMask   = 0x3FFF         (하위 14비트 마스크)
```

### 생성자의 범위 제한

```cpp
TSequenceNumber(SequenceType ValueIn) : Value(ValueIn & SeqNumberMask) {}
```

임의의 `uint16_t` 값을 받아 `& SeqNumberMask` 연산으로 0 ~ 16383 범위로 강제합니다.

### 순환 대소 비교 (`operator>`)

```cpp
bool operator>(const TSequenceNumber& Other) const {
    return (Value != Other.Value) &&
           (((Value - Other.Value) & SeqNumberMask) < SeqNumberHalf);
}
```

이산수학의 **Cyclic Order** 를 그대로 적용한 식입니다.

```
A > B  ⟺  A ≠ B  ∧  (A − B) mod 2^14 < 2^13
```

예시:
- `A = 100, B = 50`  → `(100 - 50) & 0x3FFF = 50 < 8192` → A > B ✓
- `A = 50, B = 100`  → `(50 - 100) & 0x3FFF = 16334 ≥ 8192` → A > B ✗
- `A = 1, B = 16383` → `(1 - 16383) & 0x3FFF = 2 < 8192` → A > B ✓ (올바른 wrap-around 처리)

### 부호 있는 차이 계산 (`Diff`)

패킷 간 거리를 **음수/양수 모두** 표현해야 할 때 사용합니다.

#### 문제

C++ 산술에서 `uint16_t` 뺄셈 결과는 자동 형 승격으로 `int` (32비트)가 됩니다.

```
C++ : (A - B) mod 2^32   (int 승격 후)
UDP : (A - B) mod 2^14   (원하는 것)
```

단순히 `int(A - B)` 를 반환하면 14비트 범위 밖의 값이 나옵니다.

#### 해결 — 비트 시프트 기반 부호 확장

```cpp
constexpr size_t ShiftValue = sizeof(DifferenceT) * 8 - NumBits;
// = 32 - 14 = 18

return (DifferenceT)((ValueA - ValueB) << ShiftValue) >> ShiftValue;
```

단계별 설명:

```
1. (ValueA - ValueB)
   uint16_t 뺄셈 후 int 승격 → 32비트 값. 유효 데이터는 하위 14비트.

2. << ShiftValue  (왼쪽으로 18비트 이동)
   유효한 14비트를 MSB(31번 비트) 기준으로 정렬.
   부호 비트(31번)가 원래 14비트의 MSB(13번) 값을 갖게 됨.

3. (DifferenceT) 캐스트 → int32_t 로 해석
   이제 31번 비트가 실제 부호 비트.

4. >> ShiftValue  (오른쪽으로 18비트 산술 이동)
   부호 확장(Arithmetic Shift): 상위 18비트는 부호 비트로 채워짐.
   하위 14비트에 원래 값이 복원됨.
```

수식으로 정리:

```
Diff(A, B) = (int32_t)(((A − B) & 0x3FFF) << 18) >> 18
           = signExtend14( (A − B) mod 2^14 )
```

여기서 `signExtend14(x)`:
- `x < 2^13` (= 8192) 이면 양수 그대로
- `x ≥ 2^13` 이면 `x - 2^14` (음수로 해석)

예시:

| A | B | (A-B) & 0x3FFF | Diff 결과 |
|---|---|----------------|-----------|
| 100 | 50 | 50 | +50 |
| 50 | 100 | 16334 | −50 |
| 1 | 16383 | 2 | +2 |
| 16383 | 1 | 16382 | −2 |

---

## TSequenceHistory — 비트셋 구조

```cpp
template <size_t HistorySize>
class TSequenceHistory;

// 실제 사용 타입 (NetPacketNotify 내부)
using SequenceHistoryT = TSequenceHistory<256>;
```

### 메모리 레이아웃

```
WordT  = uint32_t  (32비트)
WordCount = 256 / 32 = 8 개의 워드
Storage[8]  →  총 256비트

Storage[0] : 가장 최근에 받은 32개 패킷의 도착 여부
Storage[1] : 그 다음 32개
...
Storage[7] : 가장 오래된 32개
```

비트 인덱스 0이 **가장 최근** 패킷, 인덱스 255가 **가장 오래된** 패킷입니다.

### `AddDeliveryStatus` — 새 패킷 기록

```cpp
void AddDeliveryStatus(bool Delivered) {
    WordT Carry = Delivered ? 1u : 0u;
    for (size_t i = 0; i < WordCount; ++i) {
        WordT OldCarry = Carry;
        Carry = (Storage[i] & HighBitMask) >> 31;   // MSB 올림수
        Storage[i] = (Storage[i] << 1u) | OldCarry; // 1비트 왼쪽 시프트 + 새 값 삽입
    }
}
```

모든 워드를 왼쪽으로 1비트씩 밀고, 각 워드의 MSB를 다음 워드의 LSB로 올림수 처리합니다. 가장 오래된 비트(Storage[7]의 MSB)는 자동으로 버려집니다.

```
시프트 전: [b255 … b33 b32 | b31 … b1 b0]
                  Storage[1]      Storage[0]

신규 패킷 Delivered=true 추가 후:
           [b254 … b32 b31 | b30 … b0 1]
```

### `IsDelivered` — 이력 조회

```cpp
bool IsDelivered(size_t Index) const {
    size_t WordIndex = Index / 32;
    WordT  WordMask  = 1u << (Index & 31);
    return (Storage[WordIndex] & WordMask) != 0u;
}
```

`Index = 0` → 가장 최근 패킷 도착 여부. `Index = 255` → 256개 전 패킷 도착 여부.

### `Write` / `Read` — BitWriter/BitReader 직렬화

```cpp
void Write(BitWriter& Writer, size_t NumWords) const;
void Read(BitReader& Reader, size_t NumWords);
```

패킷 헤더에 포함할 워드 수를 `HistoryWordCount` 로 결정하고, 그만큼만 직렬화합니다. 최대 8워드(256비트)이지만 RTT와 손실률에 따라 더 적게 보낼 수도 있습니다.

---

## NetPacketNotify — Ack/Nack 흐름

### 전체 상태 다이어그램

```
송신측                                      수신측
──────────────────────────────────────────────────────

OutSeq++ → WriteHeader()                ReadHeader()
  패킷 헤더:                              → NotificationHeader 파싱
    [OutSeq | InAckSeq | History]          Seq      = 상대방이 보낸 OutSeq
                         ──────────────▶  AckedSeq = 상대방이 Ack한 나의 시퀀스

                                         Update(header, functor)
                                           InSeqDelta = Diff(Seq, InSeq)
                                           if InSeqDelta > 0:
                                             ProcessReceivedAcks()  ← Ack/Nack 판정
                                             InternalUpdate()       ← InSeq 갱신
```

### `CommitAndIncrementOutSeq`

```cpp
SequenceNumberT CommitAndIncrementOutSeq();
```

패킷을 실제로 전송하기 직전에 호출합니다.

1. 현재 `{OutSeq, InAckSeq}` 쌍을 `AckRecord` 덱의 뒤에 추가
2. `OutSeq` 를 1 증가
3. 이전 `OutSeq` 반환 (패킷 헤더의 시퀀스 번호로 사용)

`AckRecord` 는 나중에 상대방이 어떤 `AckedSeq` 를 보내왔을 때, "그 시점에 내가 어느 상대방 시퀀스까지 Ack를 보냈는가"를 역추적하는 데 사용됩니다.

### `AckSeq` / `NakSeq`

```cpp
void AckSeq(SequenceNumberT Seq);  // 수신 성공 기록
void NakSeq(SequenceNumberT Seq);  // 수신 실패 기록
```

내부적으로 `InSeqHistory.AddDeliveryStatus(true/false)` 를 호출하여 비트셋을 갱신합니다.

### `ProcessReceivedAcks` 상세

```
AckCount = Diff(NotificationData.AckedSeq, OutAckSeq)

경우 1: AckCount > 256  (대규모 패킷 손실)
    AckCount > HistoryBits 동안:
        Functor(CurrentAck, false)  ← 전부 Nack 처리
        ++CurrentAck

경우 2: 정상
    AckCount > 0 동안:
        bool delivered = History.IsDelivered(AckCount - 1)
        Functor(CurrentAck, delivered)  ← Ack 또는 Nack
        ++CurrentAck

최종: OutAckSeq = NotificationData.AckedSeq
```

`Functor` 는 `NetConnection` 에서 람다로 전달되며, 내부에서 `NetChannel::Acked(PacketId)` 또는 `NetChannel::Nacked(PacketId)` 를 호출합니다.

- **`Acked`**: `OutUnAckedBunches` 에서 해당 패킷 ID를 가진 Bunch를 제거합니다.
- **`Nacked`**: 해당 Bunch를 다시 `OutBunchQueue` 에 삽입하여 재전송을 예약합니다.

### 히스토리 플러시 메커니즘

256비트 히스토리가 꽉 차기 전에 상대방이 우리의 `InAckSeq` 를 아직 확인하지 못한 경우(`IsWaitingForSequenceHistoryFlush`), 새 패킷 헤더에 더 많은 히스토리 워드를 포함시켜 손실 없이 동기화를 완료합니다.

```cpp
bool IsWaitingForSequenceHistoryFlush() const {
    return WaitingForFlushSeqAck > OutAckSeq;
}
```

이 조건이 참인 동안 `WriteHeader` 는 `WrittenHistoryWordCount` 를 최대값으로 설정하여 전체 256비트 히스토리를 헤더에 포함시킵니다.

---

← [아키텍처](architecture.md) · 다음 → [WebViz 실행](../web-visualization.md)
