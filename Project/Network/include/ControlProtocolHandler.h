#pragma once
#include <functional>
#include <IProtocolHandler.h>
#include <ControlProtocol.pb.h>
#include <INetworkEventNotify.h>

extern class ControlProtocolHandler* GControlProtocolHandler;

class NetDriver;

enum class EControlProtocol : uint16_t
{
	INVALID = 0,
	NMT_Hello = 1,
	NMT_Challenge = 2,
	NMT_Login = 3,
	NMT_Welcome = 4,
	NMT_Join = 5,
	NMT_NetSpeed = 6,
	NMT_Failure = 7
};

class ControlProtocolHandler : public IProtocolHandler, public INetworkEventNotify
{
	using ControlProtocolFunc = std::function<void(unsigned __int32 ConnectionId, const uint8_t* Buffer, uint16_t MessageSize)>;

public:
	virtual void InitHandler() override;

	virtual void ControlProtocol(unsigned __int32 ConnectionId, const uint8_t* Buffer, uint16_t MessageType, uint16_t MessageSize) = 0;
	virtual void Init() = 0;

	virtual bool Protocol_INVALID(unsigned __int32 ConnectionId, const uint8_t* Buffer, uint16_t MessageSize);
	virtual bool Protocol_NMT_Hello(unsigned __int32 ConnectionId, GameProtocol::NMT_Hello& Message);
	virtual bool Protocol_NMT_Challenge(unsigned __int32 ConnectionId, GameProtocol::NMT_Challenge& Message);
	virtual bool Protocol_NMT_Login(unsigned __int32 ConnectionId, GameProtocol::NMT_Login& Message);
	virtual bool Protocol_NMT_Welcome(unsigned __int32 ConnectionId, GameProtocol::NMT_Welcome& Message);
	virtual bool Protocol_NMT_Join(unsigned __int32 ConnectionId, GameProtocol::NMT_Join& Message);
	virtual bool Protocol_NMT_NetSpeed(unsigned __int32 ConnectionId, GameProtocol::NMT_NetSpeed& Message);
	virtual bool Protocol_NMT_Failure(unsigned __int32 ConnectionId, GameProtocol::NMT_Failure& Message);

	template<typename T, typename HandlerFunc>
	ControlProtocolFunc MakeProtocolHandler(HandlerFunc&& handler)
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
	
	//INetworkEvent 인터페이스
	virtual void AddPlayerConnection(uint32_t ConnectionId) = 0;

	virtual void RemovePlayerConnection(uint32_t ConnectionId) = 0;

protected:
	ControlProtocolFunc ProtocolFuncMap[UINT16_MAX];
};
