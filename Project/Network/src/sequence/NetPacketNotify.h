#pragma once
#include "pch.h"
#include "SequenceNumber.h"
#include "SequenceHistory.h"

struct FPackedHeader;

class NetPacketNotify
{
public:
	enum { SequenceNumberBits = 14 };
	enum { MaxSequenceHistoryLength = 256 };

	typedef TSequenceNumber<SequenceNumberBits, uint16_t> SequenceNumberT;
	typedef TSequenceHistory<MaxSequenceHistoryLength> SequenceHistoryT;

	struct NotificationHeader
	{
		SequenceHistoryT History;
		size_t HistoryWordCount;
		SequenceNumberT Seq;      //상대방이 보낸 패킷의 Seq
		SequenceNumberT AckedSeq; //(==InAckSeq: 상대방이 마지막으로 Ack를 보낸 나의 패킷 시퀀스 번호)
	};

	NetPacketNotify();

	void Init(SequenceNumberT InitialInSeq, SequenceNumberT InitialOutSeq);

	void AckSeq(SequenceNumberT Seq) { AckSeq(Seq, true); }

	void NakSeq(SequenceNumberT Seq) { AckSeq(Seq, false); }

	SequenceNumberT CommitAndIncrementOutSeq();

	bool WriteHeader(BitWriter& Writer, bool bRefresh = false);

	bool ReadHeader(NotificationHeader& Data, BitReader& Reader) const;

	SequenceNumberT::DifferenceT GetSequenceDelta(const NotificationHeader& NotificationData);

	template<class Functor>
	SequenceNumberT::DifferenceT Update(const NotificationHeader& NotificationData, Functor&& InFunc);

	const SequenceHistoryT& GetInSeqHistory() const { return InSeqHistory; }

	SequenceNumberT GetInSeq() const { return InSeq; }

	SequenceNumberT GetInAckSeq() const { return InAckSeq; }

	SequenceNumberT GetOutSeq() const { return OutSeq; }

	SequenceNumberT GetOutAckSeq() const { return OutAckSeq; }

	NetPacketNotify::SequenceNumberT::DifferenceT GetCurrentSequenceHistoryLength() const;

	bool IsWaitingForSequenceHistoryFlush() const { return WaitingForFlushSeqAck > OutAckSeq; }

private: 
	using AckRecordT = std::deque<std::pair<SequenceNumberT, SequenceNumberT>>; //{OutSeq, InAckSeq}

	AckRecordT AckRecord;				
	size_t WrittenHistoryWordCount;		
	SequenceNumberT WrittenInAckSeq;	

	SequenceHistoryT InSeqHistory;		// 수신 패킷 이력을 나타내는 비트필드를 담은 BitBuffer
	SequenceNumberT InSeq;				// 내가 마지막으로 받은 상대방의 패킷 시퀀스
	SequenceNumberT InAckSeq;			// 내가 마지막으로 Ack를 보낸 상대방의 패킷 시퀀스 번호(나는 확인을 했지만 내가 "확인: Ack" 를 했다는 정보를 상대방도 인지를 하였는지는 알 수 없는 상태)
	SequenceNumberT InAckSeqAck;		// 내가 마지막으로 Ack를 보냈고 상대방도 인지한 상대방의 패킷 시퀀스 번호
	SequenceNumberT WaitingForFlushSeqAck;

	// 송신 시퀀스 데이터 추적
	SequenceNumberT OutSeq;				//내가 마지막으로 보낸 패킷의 Seq
	SequenceNumberT OutAckSeq;			//내가 송신한 패킷 중에서, 상대방이 Ack(확인응답)를 보낸 것을 나도 정상적으로 받은, 마지막 패킷의 시퀀스 번호

private:

	SequenceNumberT UpdateInAckSeqAck(SequenceNumberT::DifferenceT AckCount, SequenceNumberT AckedSeq);
	SequenceNumberT::DifferenceT InternalUpdate(const NotificationHeader& NotificationData, SequenceNumberT::DifferenceT InSeqDelta);

	bool GetHasUnacknowledgedAcks() const;

	bool WillSequenceFitInSequenceHistory(SequenceNumberT Seq) const;

	void SetWaitForSequenceHistoryFlush();

	template<class Functor>
	inline void ProcessReceivedAcks(const NotificationHeader& NotificationData, Functor&& InFunc);
	void AckSeq(SequenceNumberT AckedSeq, bool IsAck);
};

template<class Functor>
NetPacketNotify::SequenceNumberT::DifferenceT NetPacketNotify::Update(const NotificationHeader& NotificationData, Functor&& InFunc)
{
	const SequenceNumberT::DifferenceT InSeqDelta = GetSequenceDelta(NotificationData);
	
	if (InSeqDelta > 0)
	{
		ProcessReceivedAcks(NotificationData, InFunc);

		return InternalUpdate(NotificationData, InSeqDelta);
	}
	return 0;
}

template<class Functor>
void NetPacketNotify::ProcessReceivedAcks(const NotificationHeader& NotificationData, Functor&& InFunc)
{
	if (NotificationData.AckedSeq > OutAckSeq)
	{
		//1. 패킷 AckSeq 값 diff 계산: 나와 상대방의 패킷차이 계산
		SequenceNumberT::DifferenceT AckCount = SequenceNumberT::Diff(NotificationData.AckedSeq, OutAckSeq);

		//2. 나의 AckRecord를 Update(pop)
		const SequenceNumberT NewInAckSeqAck = UpdateInAckSeqAck(AckCount, NotificationData.AckedSeq);
		if (NewInAckSeqAck > InAckSeqAck)
		{
			InAckSeqAck = NewInAckSeqAck;
		}

		SequenceNumberT CurrentAck(OutAckSeq);
		++CurrentAck;

		const SequenceNumberT::DifferenceT HistoryBits = NotificationData.HistoryWordCount * SequenceHistoryT::BitsPerWord;

		if (AckCount > (SequenceNumberT::DifferenceT)(SequenceHistoryT::Size))
		{
			//대규모 패킷 로스: AckCount > 256
		
			NETWORK_LOG_WARN("AckCount: {} > 256", AckCount);
		}

		//대규모 패킷 로스 발생 -> 동기화 기능 작동
		//1. AckCount == 256이 될 때까지 내가 보낸 모든 패킷(OldestSeq)을 Nak 처리
		while (AckCount > HistoryBits)
		{
			--AckCount;
			InFunc(CurrentAck, false);
			++CurrentAck;
		}

		//2. History를 가장 오래된 기록부터 순차적으로 내가 보낸 패킷(OldestSeq)에 대한 Ack/Nak 처리
		while (AckCount > 0)
		{
			--AckCount;
			InFunc(CurrentAck, NotificationData.History.IsDelivered(AckCount));
			++CurrentAck;
		}

		//3. resend 및 AckRecord Update 이후 OutAckSeq를 Update
		OutAckSeq = NotificationData.AckedSeq;

		//4. 대규모 패킷 로스에 대한 Flush가 성공적으로 진행되어 Lock을 풀어도 되는 경우 풀어준다
		if (OutAckSeq > WaitingForFlushSeqAck)
		{
			WaitingForFlushSeqAck = OutAckSeq;
		}
	}
}

