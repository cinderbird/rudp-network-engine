#pragma once
#include "pch.h"

template <size_t HistorySize>
class TSequenceHistory
{
public:
	typedef uint32_t WordT;

	static constexpr size_t BitsPerWord = sizeof(WordT) * 8;
	static constexpr size_t WordCount = HistorySize / BitsPerWord;
	static constexpr size_t MaxSizeInBits = WordCount * BitsPerWord;
	static constexpr size_t Size = HistorySize;

	static_assert(HistorySize > 0, "HistorySize must be > 0");
	static_assert(HistorySize % BitsPerWord == 0, "InMaxHistorySize must be a modulo of the wordsize");

public:
	TSequenceHistory();

	/** 리셋 */
	void Reset();

	/** 전달 상태 저장, 가장 오래된 것부터 버려짐 */
	void AddDeliveryStatus(bool Delivered);

	/** 특정 인덱스의 상태 조회, 인덱스 0이 가장 최근 저장된 상태 */
	bool IsDelivered(size_t Index) const;

	bool operator==(const TSequenceHistory& Other) const { return ::memcmp(Storage, Other.Storage, WordCount * sizeof(WordT)) == 0; }

	bool operator!=(const TSequenceHistory& Other) const { return ::memcmp(Storage, Other.Storage, WordCount * sizeof(WordT)) != 0; }

	/** history를 BitStream에 쓰기 */
	void Write(BitWriter& Writer, size_t NumWords) const
	{
		NumWords = std::min<size_t>(NumWords, WordCount);
		for (size_t CurrentWordIt = 0; CurrentWordIt < NumWords; ++CurrentWordIt)
		{
			WordT temp = Storage[CurrentWordIt];
			Writer << temp;
		}
	}

	/** BitStream에서 history 읽기 */
	void Read(BitReader& Reader, size_t NumWords)
	{
		NumWords = std::min<size_t>(NumWords, WordCount);
		for (size_t CurrentWordIt = 0; CurrentWordIt < NumWords; ++CurrentWordIt)
		{
			Reader << Storage[CurrentWordIt];
		}
	}

private:
	WordT Storage[WordCount];
};

template<size_t HistorySize>
constexpr size_t TSequenceHistory<HistorySize>::BitsPerWord;

template<size_t HistorySize>
constexpr size_t TSequenceHistory<HistorySize>::WordCount;

template<size_t HistorySize>
constexpr size_t TSequenceHistory<HistorySize>::MaxSizeInBits;

template<size_t HistorySize>
constexpr size_t TSequenceHistory<HistorySize>::Size;

template <size_t HistorySize>
TSequenceHistory<HistorySize>::TSequenceHistory()
{
	Reset();
}

template <size_t HistorySize>
void TSequenceHistory<HistorySize>::Reset()
{
	::memset(&Storage[0], 0, WordCount * sizeof(WordT));
}

template <size_t HistorySize>
void TSequenceHistory<HistorySize>::AddDeliveryStatus(bool Delivered)
{
	WordT Carry = Delivered ? 1u : 0u;
	const WordT ValueMask = 1u << (BitsPerWord - 1);
	
	for (size_t CurrentWordIt = 0; CurrentWordIt < WordCount; ++CurrentWordIt)
	{
		const WordT OldValue = Carry;

		// 각 word의 최상위 비트를 다음 word로 이월
		Carry = (Storage[CurrentWordIt] & ValueMask) >> (BitsPerWord - 1);
		Storage[CurrentWordIt] = (Storage[CurrentWordIt] << 1u) | OldValue;
	}

}

template <size_t HistorySize>
bool TSequenceHistory<HistorySize>::IsDelivered(size_t Index) const
{
	const size_t WordIndex = Index / BitsPerWord;
	const WordT WordMask = (WordT(1) << (Index & (BitsPerWord - 1)));

	return (Storage[WordIndex] & WordMask) != 0u;
}
