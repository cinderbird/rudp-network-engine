#pragma once
#include "pch.h"

template <size_t NumBits, typename SequenceType>
class TSequenceNumber
{
	static_assert(std::is_signed<SequenceType>::value == false, "The base type for sequence numbers must be unsigned");

public:
	using SequenceT = SequenceType;
	using DifferenceT = int32_t; //시퀀스 간 차의(음수, 양수) 표현을 위해 사용, 충분히 넓은 범위

	// 상수
	enum { SeqNumberBits = NumBits };
	enum { SeqNumberCount = SequenceT(1) << NumBits };
	enum { SeqNumberHalf = SequenceT(1) << (NumBits - 1) };
	enum { SeqNumberMax = SeqNumberCount - 1u };
	enum { SeqNumberMask = SeqNumberMax };

	TSequenceNumber() : Value(0u) {}

	/** Constructor with given value */              //-> 비트 & 연산 == 값의 범위를 0 ~ SeqNumberMax 로 제한
	TSequenceNumber(SequenceT ValueIn) : Value(ValueIn & SeqNumberMask) {}

	/** 현재 값 가져오기 */
	SequenceT Get() const { return Value; }

	/** 시퀀스 번호 간 차이(A - B), (A - B) < SeqNumberHalf일 때만 유효 */
	static DifferenceT Diff(TSequenceNumber A, TSequenceNumber B);


	bool operator>(const TSequenceNumber& Other) const { return (Value != Other.Value) && (((Value - Other.Value) & SeqNumberMask) < SeqNumberHalf); } //이산수학의 Cyclic Order
	bool operator>=(const TSequenceNumber& Other) const { return ((Value - Other.Value) & SeqNumberMask) < SeqNumberHalf; }
	TSequenceNumber& operator++() { Increment(1u); return *this; }
	TSequenceNumber operator++(int) { TSequenceNumber Tmp(*this); Increment(1u); return Tmp; }

private:
	void Increment(SequenceT InValue) { *this = TSequenceNumber(Value + InValue); } //컴퍼일러 inline 최적화를 해주기에 새 객체를 만들어서 주는 것이  오버헤드에 큰 영향이 없다

	SequenceT Value;

	friend bool operator<(const TSequenceNumber& Lhs, const TSequenceNumber& Rhs)
	{
		return !(Lhs >= Rhs);
	}
	friend bool operator<=(const TSequenceNumber& Lhs, const TSequenceNumber& Rhs)
	{
		return !(Lhs > Rhs);
	}

	/** 동등 비교, 시퀀스 번호는 wrap-around하므로 0 == 0 + SequenceNumberCount임에 유의 */
	friend bool operator==(const TSequenceNumber& Lhs, const TSequenceNumber& Rhs)
	{
		return Lhs.Get() == Rhs.Get();
	}
	friend bool operator!=(const TSequenceNumber& Lhs, const TSequenceNumber& Rhs)
	{
		return Lhs.Get() != Rhs.Get();
	}
	friend const TSequenceNumber operator+(const TSequenceNumber& Lhs, const TSequenceNumber& Rhs)
	{
		return TSequenceNumber(Lhs.Get() + Rhs.Get());
	}
	friend const TSequenceNumber operator-(const TSequenceNumber& Lhs, const TSequenceNumber& Rhs)
	{
		return TSequenceNumber(Lhs.Get() - Rhs.Get());
	}
	friend const TSequenceNumber operator+(const TSequenceNumber& Lhs, SequenceType Rhs)
	{
		return TSequenceNumber(Lhs.Get() + Rhs);
	}
	friend const TSequenceNumber operator-(const TSequenceNumber& Lhs, SequenceType Rhs)
	{
		return TSequenceNumber(Lhs.Get() - Rhs);
	}
};



/*
GetNumBits = 14
ShiftValue = 32 - 14 = 18
우리는 14비트에 해당되는 영역의 wrap-around 거리계산(== N: 14인 Module 연산)을 하고 싶은데
C++의 자동승격 및 type에 의하여 (ValueA - ValueB) 에 대해 더 큰값으로 wrap-around 
거리계산(C++ 에서 2^16 Module) 연산을 수행하기에 우리가 원하는 값을 정확하게 뽑아내기 위해
14비트만 남기고 나머지는 left right 쉬프트 연산을 통해 제거하는 것

C++: (A - B) Mod 2^type
UDP: (A - b) Mod 2^14 (GetNumBits)

일반적인 type 승격 고려한 type의 사이즈 int32_t
C++: (A - B) = (A - B) Mod 2^16

mask = 2^14 - 1 = 00[11 1111 1111 1111] -> 하위 14비트 추출
(A - B) & mask < 2^{N-1} : 양수
(A - B) & mask >= 2^{N-1}: 음수

(ValueA - ValueB) << ShiftValue -> 상위 14비트로 밀어버리고
(DifferenceT) 적용하여 부호 있는 값으로 변환 후
>> ShiftValue 적용하여 자연스럽게 부호 확장 및 우리가 원하는 14비트를 하위 14비트로 설정
이를 통해 signed value -> 부호는 자연스럽게 확장되어 - 값으로 해석되고 우리가 원하는 14비트는 하위 14비트에 안착
*/
template <size_t NumBits, typename SequenceType>
typename TSequenceNumber<NumBits, SequenceType>::DifferenceT TSequenceNumber<NumBits, SequenceType>::Diff(TSequenceNumber A, TSequenceNumber B)
{
	constexpr size_t ShiftValue = sizeof(DifferenceT) * 8 - NumBits;

	const SequenceT ValueA = A.Value;
	const SequenceT ValueB = B.Value;

	return (DifferenceT)((ValueA - ValueB) << ShiftValue) >> ShiftValue;
};

