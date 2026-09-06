#pragma once

class IProtocolHandler
{
public:
	virtual void Init() = 0;
	virtual void InitHandler() = 0;
};

#define REGISTER_PROTOCOL_HANDLER(ID, TYPE, FUNC) \
    ProtocolFuncMap[static_cast<uint16_t>(ID)] = MakeProtocolHandler<TYPE>( \
        [this](unsigned __int32 ConnectionId, TYPE& Message) { FUNC(ConnectionId, Message); } \
    )
