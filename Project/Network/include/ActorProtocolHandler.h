#pragma once

#include <IProtocolHandler.h>
#include <ActorProtocol.pb.h>
#include <INetworkEventNotify.h>

extern class ActorProtocolHandler* GActorProtocolHandler;

class NetDriver;

enum class EActorProtocol : uint16_t
{
	INVALID,
	S_Spawn,
	S_Move,
	C_Move,
	C_Spawn
};

class ActorProtocolHandler : public IProtocolHandler
{
	using ActorProtocolFunc = std::function<void(unsigned __int32 ConnectionId, const uint8_t* Buffer, uint16_t MessageSize)>;

public:
	virtual void InitHandler() override;

	virtual void ActorProtocol(unsigned __int32 ConnectionId, const uint8_t* Buffer, uint16_t MessageType, uint16_t MessageSize) = 0;
	virtual void Init() = 0;

	virtual bool Protocol_INVALID(unsigned __int32 ConnectionId, const uint8_t* Buffer, uint16_t MessageSize) { return false; };

	virtual bool Protocol_S_Spawn(unsigned __int32 ConnectionId, GameProtocol::S_Spawn& Message) { return false; };
	virtual bool Protocol_S_Move(unsigned __int32 ConnectionId, GameProtocol::S_Move& Message) { return false; };

	virtual bool Protocol_C_Move(unsigned __int32 ConnectionId, GameProtocol::C_Move& Message) { return false; };
	virtual bool Protocol_C_Spawn(unsigned __int32 ConnectionId, GameProtocol::C_Spawn& Message) { return false; };

	template<typename T, typename HandlerFunc>
	ActorProtocolFunc MakeProtocolHandler(HandlerFunc&& handler) //패킷이 NetChannel -> Client/Server로 넘어가는 과정에서 사용
	{
		return [handler = std::forward<HandlerFunc>(handler)](unsigned __int32 ConnectionId, const uint8_t* Buffer, uint16_t MessageSize)
			{
				T Message;
				if (Message.ParseFromArray(Buffer, MessageSize))
				{
					handler(ConnectionId, Message);
				}
			};
	}


protected:
	ActorProtocolFunc ProtocolFuncMap[UINT16_MAX];
};