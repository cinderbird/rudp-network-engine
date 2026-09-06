#pragma once
#include "pch.h"
#include "ActorProtocolHandler.h"


class ActorProtocolServerHandler : public ActorProtocolHandler
{
public:
	ActorProtocolServerHandler();

	virtual void Init() override;
	virtual void ActorProtocol(unsigned __int32 ConnectionId, const unsigned __int8* Buffer, unsigned __int16 MessageType, unsigned __int16 MessageSize) override;

	virtual bool Protocol_INVALID(unsigned __int32 ConnectionId, const unsigned __int8* Buffer, unsigned __int16 MessageSize) override;

	virtual bool Protocol_C_Move(unsigned __int32 ConnectionId, GameProtocol::C_Move& Message) override;
	virtual bool Protocol_C_Spawn(unsigned __int32 ConnectionId, GameProtocol::C_Spawn& Message) override;
};

