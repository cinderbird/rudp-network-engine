#pragma once
#include "pch.h"
#include "NetChannel.h"

class NetConnection;
class AOutBunch;
class RecvMessageReader;

class ControlChannel : public NetChannel
{
public:
	ControlChannel();
	ControlChannel(NetChannel* NewChannel);

	virtual ~ControlChannel();

	virtual void Init(std::shared_ptr<NetConnection> InConnection, int8_t InChIndex, uint32_t InReliable, uint32_t OutReliable) override;
	virtual void Tick() override;

	//수신
	virtual void ReceivedMessage(RecvMessageReader& Message) override;
	virtual void DispatchPendingMessage() override;
	virtual const uint16_t GetChannelID() const override;

private:
	std::queue<RecvMessageReader> PendingDispatchControlMessageQueue;
};
