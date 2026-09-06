#include "NetChannel.h"
#include "NetBunchReader.h"
#include "INetworkService.h"
#include "NetConnection.h"


NetChannel::NetChannel()
	: ChIndex(-1)
	, ChName()
	, bControl(false)
	, bOpen(false)
	, bClose(true)
	, bReliable(false)
	, InReliable(0)
	, OutReliable(0)
{
}

NetChannel::NetChannel(NetChannel* NewChannel)
	: ChIndex(NewChannel->ChIndex)
	, ChName(NewChannel->ChName)
	, bControl(NewChannel->bControl)
	, bOpen(NewChannel->bOpen)
	, bClose(NewChannel->bClose)
	, bReliable(NewChannel->bReliable)
	, InReliable(NewChannel->InReliable)
	, OutReliable(NewChannel->OutReliable)
{
}

void NetChannel::Tick()
{
	//번치 조립 및 Queue 대기
	TickFlush();
}

void NetChannel::RegisterMessage(void* BufferPtr, uint32_t Size, ReleaseMessageBufferCallback Callback, void* Context) const
{
	BunchBuilder->Submit(NetMessage{ BufferPtr, Size, Callback, Context });
}

void NetChannel::RegisterMessage(NetMessage& Message) const
{
	BunchBuilder->Submit(Message);
}

bool NetChannel::AddUnAckedOutBunch(uint32_t OutPacketId, const std::shared_ptr<OutReadyBunch>& Bunch)
{
	// RELIABLE_BUFFER 상한
	if (OutUnAckedBunches.size() >= RELIABLE_BUFFER)
	{
		if (auto Conn = Connection.lock())
		{
			Conn->Close("ReliableBufferOverflow");
		}
		return false;
	}

	OutUnAckedBunches[OutPacketId].push_back(Bunch);
	return true;
}

void NetChannel::AddUnreadInCopyBunch(uint32_t ChSequence, std::shared_ptr<InBunchReader> CopyBunch)
{
	//중복된 Bunch가 아니면
	if (InUnreadBunchMap.count(ChSequence) == 0)
	{
		//추가
		InUnreadBunchMap[ChSequence] = CopyBunch;
	}
}

void NetChannel::Acked(int32_t AckPacketId)
{
	OutUnAckedBunches.erase(AckPacketId);
}

void NetChannel::Nacked(int32_t NackPacketId)
{
	NETWORK_LOG_DEBUG("Nacked Bunch PacketId: {} ", NackPacketId);

	auto& ResendBunchs = OutUnAckedBunches[NackPacketId];

	//1 -> 2 -> 3 -> 4
	//queue에 1번이 가장 마지막으로 push_front을 해야 한다.
	for (auto it = ResendBunchs.rbegin(); it != ResendBunchs.rend(); ++it)
	{
		auto &Bunch = *it;
		OutBunchQueue.push_front(Bunch);
	}

	OutUnAckedBunches.erase(NackPacketId);

	NETWORK_LOG_TRACE("[NackRetransmit] ChIndex={} OutBunchQueue.size={} OutUnAckedBunches.size={}",
		ChIndex, OutBunchQueue.size(), OutUnAckedBunches.size());
}

void NetChannel::ForceRetransmitAllUnAcked()
{
	if (OutUnAckedBunches.empty())
	{
		return;
	}

	size_t RequeuedCount = 0;

	for (auto PacketIt = OutUnAckedBunches.rbegin(); PacketIt != OutUnAckedBunches.rend(); ++PacketIt)
	{
		auto& Bunches = PacketIt->second;
		for (auto BunchIt = Bunches.rbegin(); BunchIt != Bunches.rend(); ++BunchIt)
		{
			OutBunchQueue.push_front(*BunchIt);
			++RequeuedCount;
		}
	}

	OutUnAckedBunches.clear();

	NETWORK_LOG_WARN("[ForceRetransmit] ChIndex={} requeued {} bunch(es) after sequence history overflow", ChIndex, RequeuedCount);
}

void NetChannel::DispatchRawBunch(std::span<const uint8_t>& InView, std::shared_ptr<InRecvPacketEvent> Event, uint32_t MessageCount)
{
	auto connection = Connection.lock();

	NETWORK_LOG_DEBUG("Channel Status - InReliable: {}, OutReliable: {}", InReliable, OutReliable);

	//step1. 순서에 맞게 들어온 번치를 처리한다.
	BufferReader bunchReader(InView);
	if (DispatchRawBunch_Interanl(bunchReader, Event, MessageCount))
	{
		InReliable++;
	}

	//step2. 순서가 맞지 않아 저장된 번치 중 더 처리할 수 있는 번치를 찾아서 처리한다.
	while (bReliable && !InUnreadBunchMap.empty() && InUnreadBunchMap.begin()->first == InReliable + 1)
	{
		auto bufferedBunchOwner = InUnreadBunchMap.begin()->second; //1
		BufferReader reader = bufferedBunchOwner->GetReader();
		uint32_t messageCount = bufferedBunchOwner->GetMessageCount();

		if (DispatchRawBunch_Interanl(reader, bufferedBunchOwner, messageCount))
		{
			InReliable++; //1
			InUnreadBunchMap.erase(InUnreadBunchMap.begin()); //0 -> scope out 되면 소멸
		}
		else
		{
			//오염된 번치 - 제거
			InUnreadBunchMap.erase(InUnreadBunchMap.begin());
			return;
		}
	}
}

bool NetChannel::DequeueReadyBunch(std::shared_ptr<OutReadyBunch>& out)
{
	if (!OutBunchQueue.empty())
	{
		out = OutBunchQueue.front();
		OutBunchQueue.pop_front();
		return true;
	}
	return false;
}

bool NetChannel::DispatchRawBunch_Interanl(BufferReader& bunchReader, RecvBunchOwnerRef Onwer, uint32_t MessageCount)
{
	//temp
	auto Conn = Connection.lock();

	// 전부 다 읽을 수 있으면 true, 단 하나라도 실패하면 false: all or not
	for (uint32_t count = 0; count < MessageCount; ++count)
	{
		if (!bunchReader.CanRead<INetworkService::MessageHeader>())
		{
			const auto ConnectionId = Conn->GetPlayerConnectionID();
			NETWORK_LOG_WARN("PlayerId: {}, Bunch header read fail", ConnectionId);
			return false;
		}

		INetworkService::MessageHeader header;
		bunchReader.Read(header);
		
		uint16_t payloadSize = header.Size - sizeof(INetworkService::MessageHeader);
		if (!bunchReader.CanRead(payloadSize))
		{
			const auto ConnectionId = Conn->GetPlayerConnectionID();
			NETWORK_LOG_WARN("PlayerId: {}, Bunch payload read fail", ConnectionId);
			return false;
		}

		std::span<const uint8_t> messagePayloadView = bunchReader.ReadView(payloadSize);

		RecvMessageReader message = { messagePayloadView, header.MessageId, Onwer };
		ReceivedMessage(message);
	}

	return true;
}

bool NetChannel::DequeueReadyBunch_Interanl(std::shared_ptr<OutReadyBunch>& out) const
{
	return BunchBuilder->TryDequeueReady(out);
}

void NetChannel::UpdateChannelSeq(uint32_t InReliable_, uint32_t OutReliable_)
{
	InReliable = InReliable_;
	OutReliable = OutReliable_;

	OutBunchQueue.clear();
	OutUnAckedBunches. clear();//Key: OutPacketId
	InUnreadBunchMap.clear();
}

void NetChannel::PumpBunch(size_t batchLimit, bool flushTail)
{
	BunchBuilder->Pump(batchLimit, flushTail);
}

bool NetChannel::HasReadyBunch() const
{
	return !OutBunchQueue.empty();
}

void NetChannel::TickFlush()
{
	PumpBunch(256, true);

	std::shared_ptr<OutReadyBunch> Bunch;
	while (DequeueReadyBunch_Interanl(Bunch))
	{
		OutBunchQueue.push_back(Bunch);
	}
}

