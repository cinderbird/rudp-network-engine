#pragma once
#include "pch.h"
#include "ControlProtocolHandler.h"
#include <mutex>


class ControlProtocolServerHandler : public ControlProtocolHandler
{
	struct RUdpStatus
	{
		std::atomic<int> expectedReliable = -1;
		std::atomic<int> totalPacket = 0;
		std::atomic<int> passCount = 0;
		std::atomic<int> failCount = 0;

		RUdpStatus()
			: expectedReliable(-1)
			, totalPacket(0)
			, passCount(0)
			, failCount(0)
		{
		}
	};

	// ConnectionRUDPMap의 구조를 보호(Docs/PARALLELISM.md)
	std::mutex ConnectionRUDPMapMutex;
	std::map<unsigned __int32, RUdpStatus> ConnectionRUDPMap;

public:
	ControlProtocolServerHandler();

	virtual void Init() override;
	virtual void ControlProtocol(unsigned __int32 ConnectionId, const unsigned __int8* Buffer, unsigned __int16 MessageType, unsigned __int16 MessageSize) override;

	virtual bool Protocol_INVALID(unsigned __int32 ConnectionId, const unsigned __int8* Buffer, unsigned __int16 MessageSize)override;
	virtual bool Protocol_NMT_Hello(unsigned __int32 ConnectionId, GameProtocol::NMT_Hello& Message) override;
	virtual bool Protocol_NMT_Login(unsigned __int32 ConnectionId, GameProtocol::NMT_Login& Message) override;
	virtual bool Protocol_NMT_Join(unsigned __int32 ConnectionId, GameProtocol::NMT_Join& Message) override;
	virtual bool Protocol_NMT_NetSpeed(unsigned __int32 ConnectionId, GameProtocol::NMT_NetSpeed& Message) override;
	virtual bool Protocol_NMT_Failure(unsigned __int32 ConnectionId, GameProtocol::NMT_Failure& Message) override;

	//------------Notify-------------//
	virtual void AddPlayerConnection(uint32_t ConnectionId) override;
	virtual void RemovePlayerConnection(uint32_t ConnectionId) override; // M14
};