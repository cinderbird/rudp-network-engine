#include "ActorChannel.h"
#include "ActorProtocolHandler.h"
#include "NetMessageReader.h"
#include "PacketEnum.h"
#include "NetBunchBuilder.h"
#include "NetConnection.h"

ActorChannel::ActorChannel() : NetChannel()
{
	this->ChName = "Actor";
	this->ChIndex = -1;
}

ActorChannel::ActorChannel(NetChannel* NewChannel) : NetChannel(NewChannel)
{
	this->ChName = "Actor";
	this->ChIndex = 1;
}

ActorChannel::~ActorChannel()
{
}

void ActorChannel::Init(std::shared_ptr<NetConnection> InConnection, int8_t InChIndex, uint32_t InReliable_, uint32_t OutReliable_)
{
	Connection = InConnection;
	ChIndex = InChIndex;
	bControl = true;
	bOpen = true;
	bClose = false;
	bReliable = true;
	InReliable = InReliable_;
	OutReliable = OutReliable_;

	ChannelBunchBuilder::BunchPolicy pl;
	pl.MaxPacketBytes = 1200;
	pl.PacketHeaderBytes = 16;
	pl.BunchHeaderBytes = 8;
	pl.MaxCoalesceUs = 2000;

	BunchBuilder = std::make_unique<ChannelBunchBuilder>(ChIndex, bControl, bOpen, bClose, bReliable, pl, this);
}

void ActorChannel::Tick()
{
	//1. 큐에 쌓인 메시지 처리
	DispatchPendingMessage();

	//2. IOCPDispatch
	NetChannel::Tick();
}

void ActorChannel::ReceivedMessage(RecvMessageReader& Message)
{
	PendingDispatchActorMessageQueue.push(Message);
}

void ActorChannel::DispatchPendingMessage()
{
	const auto PlayerId = Connection.lock()->GetPlayerConnectionID();

	while (!PendingDispatchActorMessageQueue.empty())
	{
		auto QMessage = PendingDispatchActorMessageQueue.front();
		GActorProtocolHandler->ActorProtocol(PlayerId, QMessage.View.data(), QMessage.MessageId, (uint16_t)QMessage.View.size_bytes());
		PendingDispatchActorMessageQueue.pop();
	}
}

const uint16_t ActorChannel::GetChannelID() const
{
	return static_cast<uint16_t>(EChannelType::Actor);
}
