#include "NetPacketNotify.h"

struct FPackedHeader;

struct FPackedHeader
{
	using SequenceNumberT = NetPacketNotify::SequenceNumberT;

	static_assert(NetPacketNotify::SequenceNumberBits <= 14, "SequenceNumbers must be smaller than 14 bits to fit history word count");

	enum { HistoryWordCountBits = 4 };
	enum { SeqMask = (1 << NetPacketNotify::SequenceNumberBits) - 1 };
	enum { HistoryWordCountMask = (1 << HistoryWordCountBits) - 1 };
	enum { AckSeqShift = HistoryWordCountBits };
	enum { SeqShift = AckSeqShift + NetPacketNotify::SequenceNumberBits };

	static uint32_t Pack(SequenceNumberT Seq, SequenceNumberT AckedSeq, size_t HistoryWordCount)
	{
		//코드 정리
		uint32_t Packed = 0u;

		Packed |= Seq.Get() << SeqShift;
		Packed |= AckedSeq.Get() << AckSeqShift;
		Packed |= HistoryWordCount & HistoryWordCountMask;

		return Packed;
	}

	static SequenceNumberT GetSeq(uint32_t Packed) { return SequenceNumberT(Packed >> SeqShift & SeqMask); }
	static SequenceNumberT GetAckedSeq(uint32_t Packed) { return SequenceNumberT(Packed >> AckSeqShift & SeqMask); }
	static size_t GetHistoryWordCount(uint32_t Packed) { return (Packed & HistoryWordCountMask); }
};

NetPacketNotify::NetPacketNotify()
	: WrittenHistoryWordCount(0)
{

}

void NetPacketNotify::Init(SequenceNumberT InitialInSeq, SequenceNumberT InitialOutSeq)
{
	InSeqHistory.Reset();									//{Storage=0x00000a87382bbd1c {0, 0, 0, 0, 0, 0, 0, 0} }	TSequenceHistory<256>
	InSeq = InitialInSeq;									//16383
	InAckSeq = InitialInSeq;								//16383
	InAckSeqAck = InitialInSeq;								//16383
	OutSeq = InitialOutSeq;									//0
	OutAckSeq = SequenceNumberT(InitialOutSeq.Get() - 1);	//16383
	WaitingForFlushSeqAck = OutAckSeq;						//16383

	//AckRecord 초기화
	AckRecord.clear();
}

NetPacketNotify::SequenceNumberT NetPacketNotify::CommitAndIncrementOutSeq()
{
	//WrittenHistoryWordCount == 0: 헤더 작성 이전 -> 패킷 조립 이전 -> AckRecord를 하면 안된다
	if (WrittenHistoryWordCount == 0)
	{
		//std::cout << "WARNNING: NetPacketNotify::CommitAndIncrementOutSeq Before Write Header" << std::endl;
		return OutSeq;
	}

	//{가장 최신 패킷 Seq, 상대방 패킷에 대한 나의 마지막 Ack}
	AckRecord.push_back({ OutSeq, WrittenInAckSeq });
	
	//헤더 작성 완료 -> 크기 초기화
	WrittenHistoryWordCount = 0u;

	//패킷 Seq 증가
	return ++OutSeq;
}

bool NetPacketNotify::WriteHeader(BitWriter& Writer, bool bRefresh)
{
	//WriteHeader 함수는 미리 설정된 OutSeq, InAckSeq를 설정해주는 함수일 뿐

	size_t CurrentHistoryWordCount = std::clamp<size_t>((GetCurrentSequenceHistoryLength() + SequenceHistoryT::BitsPerWord - 1u) / SequenceHistoryT::BitsPerWord, 1u, SequenceHistoryT::WordCount);

	// history에 더 많은 공간이 필요 없을 때만 refresh할 수 있다
	if (bRefresh && (CurrentHistoryWordCount > WrittenHistoryWordCount))
	{
		return false;
	}

	// ack 데이터를 몇 word 써야 하나? refresh라면 원래 헤더와 같은 크기로 써야 한다
	WrittenHistoryWordCount = bRefresh ? WrittenHistoryWordCount : CurrentHistoryWordCount;

	// 지금 시점에 우리가 확인한 마지막 InAck
	WrittenInAckSeq = InAckSeq;

	SequenceNumberT::SequenceT Seq = OutSeq.Get();
	SequenceNumberT::SequenceT AckedSeq = InAckSeq.Get();

	// 데이터를 uint에 패킹
	uint32_t PackedHeader = FPackedHeader::Pack(Seq, AckedSeq, WrittenHistoryWordCount - 1);

	// 패킹된 헤더 쓰기
	Writer << PackedHeader;

	// ack history 쓰기
	InSeqHistory.Write(Writer, WrittenHistoryWordCount);

	return true;
}

bool NetPacketNotify::ReadHeader(NotificationHeader& Data, BitReader& Reader) const
{
	uint32_t PackedHeader = 0;
	Reader << PackedHeader;

	// 언패킹
	Data.Seq = FPackedHeader::GetSeq(PackedHeader);
	Data.AckedSeq = FPackedHeader::GetAckedSeq(PackedHeader);
	Data.HistoryWordCount = FPackedHeader::GetHistoryWordCount(PackedHeader) + 1;

	// ack history 읽기
	Data.History.Read(Reader, Data.HistoryWordCount);

	return Reader.IsError() == false;
}

NetPacketNotify::SequenceNumberT::DifferenceT NetPacketNotify::GetSequenceDelta(const NotificationHeader& NotificationData)
{
	if (NotificationData.Seq > InSeq && NotificationData.AckedSeq >= OutAckSeq && OutSeq > NotificationData.AckedSeq)
	{
		return SequenceNumberT::Diff(NotificationData.Seq, InSeq);
	}
	else
	{
		if (NotificationData.Seq <= InSeq)
		{
			NETWORK_LOG_DEBUG("Recv DelayedPacket, return 0");
		}

		if (NotificationData.AckedSeq < OutAckSeq)
		{
			NETWORK_LOG_DEBUG("Recv Dup Packet, return 0");
		}

		if (OutSeq <= NotificationData.AckedSeq)
		{
			NETWORK_LOG_WARN("Warnning someone try to modify packet");
		}

		return 0;
	}
}

NetPacketNotify::SequenceNumberT::DifferenceT NetPacketNotify::GetCurrentSequenceHistoryLength() const
{
	//코드 정리
	if (InAckSeq >= InAckSeqAck)
	{
		return std::min<size_t>(SequenceNumberT::Diff(InAckSeq, InAckSeqAck), (SequenceNumberT::DifferenceT)SequenceHistoryT::Size);
	}
	else
	{
		// 최악의 경우 전체 history를 보낸다
		return (SequenceNumberT::DifferenceT)SequenceHistoryT::Size;
	}
}

NetPacketNotify::SequenceNumberT NetPacketNotify::UpdateInAckSeqAck(SequenceNumberT::DifferenceT AckCount, SequenceNumberT AckedSeq)
{
	if ((size_t)AckCount <= AckRecord.size())
	{
		//2. AckRecord 제거
		if (AckCount > 1)
		{
			for (size_t i = 0; i < AckCount - 1; ++i)
			{
				AckRecord.pop_front(); 
			}
		}

		auto AckData = AckRecord.front(); 
		AckRecord.pop_front();                   

		//3. 최신 InAckSeq 반환
		if (AckData.first == AckedSeq)
		{
			return AckData.second;
		}
	}

	//WARRNING FATAL ERROR! 패킷 에러
#ifdef _DEBUG
	//std::cout << "WARRNING FATAL ERROR! 패킷 에러" << std::endl;
#endif
	return SequenceNumberT(AckedSeq.Get() - MaxSequenceHistoryLength);
}

NetPacketNotify::SequenceNumberT::DifferenceT NetPacketNotify::InternalUpdate(const NotificationHeader& NotificationData, SequenceNumberT::DifferenceT InSeqDelta)
{
	//내부 윈도우 = History = 256개의 패킷 = NotificationData.Seq - InAckSeqAck
	//										&& 256이상 차이가 난다
	if (!IsWaitingForSequenceHistoryFlush() && !WillSequenceFitInSequenceHistory(NotificationData.Seq))
	{
		if (GetHasUnacknowledgedAcks())
		{
			SetWaitForSequenceHistoryFlush();
		}
		else
		{
			//Window Buffer가 깔끔하게 비워진 상태
			// 이전 ack가 없다면 리셋해도 되고, 그러면 수신 쪽에서 nack을
			// 안전하게 합성해낼 수 있다
			const SequenceNumberT NewInAckSeqAck(NotificationData.Seq.Get() - 1);
			InAckSeqAck = NewInAckSeqAck;
		}
	}

	if (!IsWaitingForSequenceHistoryFlush())
	{
		// 들어온 시퀀스를 그냥 받아들인다, 정상적인 상황에선 NetConnection이 ack를 명시적으로 처리한다.
		InSeq = NotificationData.Seq;
		return InSeqDelta;
	}
	else
	{
		// history를 flush하기 전까지는 들어오는 패킷을 손실된 것으로 취급하되, ack 윈도우는 가능한 만큼 계속 진행시킨다.
		SequenceNumberT NewInSeqToAck(NotificationData.Seq);

		// 아직 flush를 기다리는 중이지만 history는 채울 수 있다
		if (!WillSequenceFitInSequenceHistory(NotificationData.Seq) && GetHasUnacknowledgedAcks())
		{
			// 시퀀스 history 끝까지, 손실로 표시할 수 있는 건 전부 표시 
			NewInSeqToAck = SequenceNumberT(InAckSeqAck.Get() + (MaxSequenceHistoryLength - GetCurrentSequenceHistoryLength()));
		}

		if (NewInSeqToAck >= InSeq)
		{
			const SequenceNumberT::DifferenceT AdjustedSequenceDelta = SequenceNumberT::Diff(NewInSeqToAck, InSeq);

			InSeq = NewInSeqToAck;

			// 여기서부터 Nack이 유발됨
			AckSeq(NewInSeqToAck, false);

			return AdjustedSequenceDelta;
		}
		else
		{
			return 0;
		}
	}
}

bool NetPacketNotify::GetHasUnacknowledgedAcks() const
{
	for (SequenceNumberT::DifferenceT It = 0, EndIt = GetCurrentSequenceHistoryLength(); It < EndIt; ++It)
	{
		//내부 버퍼에 Delivered가 true인 패킷이 존재한다
		if (InSeqHistory.IsDelivered(It))
		{
			return true;
		}
	}
	return false;
}

bool NetPacketNotify::WillSequenceFitInSequenceHistory(SequenceNumberT Seq) const
{
	if (Seq >= InAckSeqAck)
	{
		//256이상 차이가 발생
		return (size_t)SequenceNumberT::Diff(Seq, InAckSeqAck) <= SequenceHistoryT::Size;
	}

	return false;
}

void NetPacketNotify::SetWaitForSequenceHistoryFlush()
{
	WaitingForFlushSeqAck = OutSeq;
}

void NetPacketNotify::AckSeq(SequenceNumberT AckedSeq, bool IsAck)
{
	while (AckedSeq > InAckSeq)
	{
		++InAckSeq;

		const bool bReportAcked = InAckSeq == AckedSeq ? IsAck : false;

		InSeqHistory.AddDeliveryStatus(bReportAcked);
	}
}



