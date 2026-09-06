#include "ControlChannel.h"
#include "ControlProtocolHandler.h"
#include "NetMessageReader.h"
#include "PacketEnum.h"
#include "NetBunchBuilder.h"
#include "NetConnection.h"

ControlChannel::ControlChannel()
	: NetChannel()
{
	this->ChName = "Control";
	this->ChIndex = 0;
}

ControlChannel::ControlChannel(NetChannel* NewChannel)
	: NetChannel(NewChannel)
{
	this->ChName = "Control";
	this->ChIndex = 0;
}

ControlChannel::~ControlChannel()
{
}

void ControlChannel::ReceivedMessage(RecvMessageReader& Message)
{
	PendingDispatchControlMessageQueue.push(Message);
}

void ControlChannel::DispatchPendingMessage()
{
	const auto PlayerId = Connection.lock()->GetPlayerConnectionID();

	while (!PendingDispatchControlMessageQueue.empty())
	{
		auto QMessage = PendingDispatchControlMessageQueue.front();
		GControlProtocolHandler->ControlProtocol(PlayerId, QMessage.View.data(), QMessage.MessageId, (uint16_t)QMessage.View.size_bytes());
		PendingDispatchControlMessageQueue.pop();
	}
}

const uint16_t ControlChannel::GetChannelID() const
{
	return static_cast<uint16_t>(EChannelType::Control);
}

void ControlChannel::Init(std::shared_ptr<NetConnection> InConnection, int8_t InChIndex, uint32_t InReliable_, uint32_t OutReliable_)
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

void ControlChannel::Tick()
{
	//1. DispatchMessageQueue를 돌며 메세지를 읽고 처리
	DispatchPendingMessage();

	//2. IOCPDispatch
	NetChannel::Tick();
}
