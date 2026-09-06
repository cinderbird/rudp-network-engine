#include "RemoteActorProtocolHandler.h"


ActorProtocolClientHandler::ActorProtocolClientHandler()
{

}

void ActorProtocolClientHandler::Init()
{
	for (int i = 0; i < UINT16_MAX; ++i)
	{
		ProtocolFuncMap[i] = [this](unsigned __int32 ConnectionId, const unsigned __int8* Buffer, unsigned __int16 MessageSize) {Protocol_INVALID(ConnectionId, Buffer, MessageSize); };
	}

	//수신
	REGISTER_PROTOCOL_HANDLER(EActorProtocol::S_Spawn, GameProtocol::S_Spawn, Protocol_S_Spawn);
	REGISTER_PROTOCOL_HANDLER(EActorProtocol::S_Move, GameProtocol::S_Move, Protocol_S_Move);
}

void ActorProtocolClientHandler::ActorProtocol(unsigned __int32 ConnectionId, const unsigned __int8* Buffer, unsigned __int16 MessageType, unsigned __int16 MessageSize)
{
	//std::cout << "Client Recv Actor Protocol" << std::endl;

	ProtocolFuncMap[MessageType](ConnectionId, Buffer, MessageSize);
}

bool ActorProtocolClientHandler::Protocol_INVALID(unsigned __int32 ConnectionId, const unsigned __int8* Buffer, unsigned __int16 MessageSize)
{
	return false;
}

bool ActorProtocolClientHandler::Protocol_C_Move(unsigned __int32 ConnectionId, GameProtocol::C_Move& Message)
{
	return true;
}

bool ActorProtocolClientHandler::Protocol_C_Spawn(unsigned __int32 ConnectionId, GameProtocol::C_Spawn& Message)
{
	return true;
}


