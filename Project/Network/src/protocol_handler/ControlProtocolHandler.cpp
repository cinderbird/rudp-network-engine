#include "ControlProtocolHandler.h"
#include "AyncyLog.h"

ControlProtocolHandler* GControlProtocolHandler = nullptr;

void ControlProtocolHandler::InitHandler()
{
	Init();
}

bool ControlProtocolHandler::Protocol_INVALID(unsigned __int32 ConnectionId, const uint8_t* Buffer, uint16_t MessageSize)
{
	return true;
}

bool ControlProtocolHandler::Protocol_NMT_Hello(unsigned __int32 ConnectionId, GameProtocol::NMT_Hello& Message)
{
	return true;
}

bool ControlProtocolHandler::Protocol_NMT_Challenge(unsigned __int32 ConnectionId, GameProtocol::NMT_Challenge& Message)
{
	return true;
}

bool ControlProtocolHandler::Protocol_NMT_Login(unsigned __int32 ConnectionId, GameProtocol::NMT_Login& Message)
{
	return true;
}

bool ControlProtocolHandler::Protocol_NMT_Welcome(unsigned __int32 ConnectionId, GameProtocol::NMT_Welcome& Message)
{
	return true;
}

bool ControlProtocolHandler::Protocol_NMT_Join(unsigned __int32 ConnectionId, GameProtocol::NMT_Join& Message)
{
	return true;
}

bool ControlProtocolHandler::Protocol_NMT_NetSpeed(unsigned __int32 ConnectionId, GameProtocol::NMT_NetSpeed& Message)
{
	return true;
}

bool ControlProtocolHandler::Protocol_NMT_Failure(unsigned __int32 ConnectionId, GameProtocol::NMT_Failure& Message)
{
	return true;
}