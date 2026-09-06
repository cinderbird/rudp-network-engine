#include "RemoteControlProtocolHandler.h"
#include "service/NetworkService.h"
#include "room/RoomManager.h"
#include <spdlog/spdlog.h>

ControlProtocolServerHandler::ControlProtocolServerHandler()
{

}

void ControlProtocolServerHandler::Init()
{
	for (int i = 0; i < UINT16_MAX; ++i)
	{
		ProtocolFuncMap[i] = [this](unsigned __int32 ConnectionId, const unsigned __int8* Buffer, unsigned __int16 MessageSize) {Protocol_INVALID(ConnectionId, Buffer, MessageSize); };
	}

	REGISTER_PROTOCOL_HANDLER(EControlProtocol::NMT_Hello, GameProtocol::NMT_Hello, Protocol_NMT_Hello);
	REGISTER_PROTOCOL_HANDLER(EControlProtocol::NMT_Login, GameProtocol::NMT_Login, Protocol_NMT_Login);
	REGISTER_PROTOCOL_HANDLER(EControlProtocol::NMT_Join, GameProtocol::NMT_Join, Protocol_NMT_Join);
	REGISTER_PROTOCOL_HANDLER(EControlProtocol::NMT_NetSpeed, GameProtocol::NMT_NetSpeed, Protocol_NMT_NetSpeed);
	REGISTER_PROTOCOL_HANDLER(EControlProtocol::NMT_Failure, GameProtocol::NMT_Failure, Protocol_NMT_Failure);
}

void ControlProtocolServerHandler::ControlProtocol(unsigned __int32 ConnectionId, const unsigned __int8* Buffer, unsigned __int16 MessageType, unsigned __int16 MessageSize)
{
	ProtocolFuncMap[MessageType](ConnectionId, Buffer, MessageSize);
}

bool ControlProtocolServerHandler::Protocol_INVALID(unsigned __int32 ConnectionId, const unsigned __int8* Buffer, unsigned __int16 MessageSize)
{
	return false;
}

bool ControlProtocolServerHandler::Protocol_NMT_Hello(unsigned __int32 ConnectionId, GameProtocol::NMT_Hello& Message)
{
	GameProtocol::NMT_Challenge newMessage;
	std::string option{ "hello" };
	std::string url{ "try it" };

	newMessage.set_option(option);
	newMessage.set_url(url);

	return GNetworkService->SendPacket(newMessage, ConnectionId);
}

bool ControlProtocolServerHandler::Protocol_NMT_Login(unsigned __int32 ConnectionId, GameProtocol::NMT_Login& Message)
{
	const std::string Id = Message.id();
	const std::string Password = Message.password();

	const bool bVerified = GNetworkService->GetRoomManger()->VerifiedUser(Id, Password, ConnectionId);
	spdlog::info("[Login] ConnectionId={} Id={} result={}", ConnectionId, Id, bVerified ? "VERIFIED" : "REJECTED");

	if (bVerified)
	{
		GameProtocol::NMT_Welcome newMessage;
		std::string gameName{ "road runner" };
		std::string worldName{ "L_Default" };

		newMessage.set_gamename(gameName);
		newMessage.set_worldname(worldName);

		return GNetworkService->SendPacket(newMessage, ConnectionId);
	}
	else
	{
		GameProtocol::NMT_Failure newMessage;
		newMessage.set_errortype(0); // login failure
		newMessage.set_errornum(1);  // invalid id/password

		return GNetworkService->SendPacket(newMessage, ConnectionId);
	}
}

bool ControlProtocolServerHandler::Protocol_NMT_Join(unsigned __int32 ConnectionId, GameProtocol::NMT_Join& Message)
{
	std::cout << "NMT_End" << std::endl;
	return true;
}

bool ControlProtocolServerHandler::Protocol_NMT_NetSpeed(unsigned __int32 ConnectionId, GameProtocol::NMT_NetSpeed& Message)
{
	RUdpStatus* StatusPtr;
	{
		std::lock_guard Lock(ConnectionRUDPMapMutex);
		StatusPtr = &ConnectionRUDPMap[ConnectionId];
	}
	auto& Status = *StatusPtr;

	std::cout << "Message: " << Message.lastackedpackettime() << std::endl;

	Status.totalPacket.fetch_add(1, std::memory_order_release);
	if (Status.expectedReliable.load() == -1)
	{
		Status.expectedReliable.store(Message.lastackedpackettime());
		Status.expectedReliable.fetch_add(1, std::memory_order_release);
		Status.passCount.fetch_add(1, std::memory_order_release);
	}
	else
	{
		if (Status.expectedReliable.load() == Message.lastackedpackettime())
		{
			Status.passCount.fetch_add(1, std::memory_order_release);
			Status.expectedReliable.fetch_add(1, std::memory_order_release);
		}
		else
		{
			std::string debugMessage = std::format("Expected: {}, Recv: {}", Status.expectedReliable.load(), Message.lastackedpackettime());
			Status.failCount.fetch_add(1, std::memory_order_release);
			Status.expectedReliable.store(Message.lastackedpackettime());
		}
	}

	return true;
}

bool ControlProtocolServerHandler::Protocol_NMT_Failure(unsigned __int32 ConnectionId, GameProtocol::NMT_Failure& Message)
{
	return true;
}

void ControlProtocolServerHandler::AddPlayerConnection(uint32_t ConnectionId)
{
	{
		std::lock_guard Lock(ConnectionRUDPMapMutex);
		ConnectionRUDPMap[ConnectionId];
	}
	GNetworkService->AddPlayer(ConnectionId);
}

void ControlProtocolServerHandler::RemovePlayerConnection(uint32_t ConnectionId)
{
	{
		std::lock_guard Lock(ConnectionRUDPMapMutex);
		ConnectionRUDPMap.erase(ConnectionId);
	}
	GNetworkService->RemovePlayer(ConnectionId);
	spdlog::info("[Disconnect] ConnectionId={} removed from RoomManager/ConnectionRUDPMap", ConnectionId);
}