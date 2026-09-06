#include "RemoteControlProtocolHandler.h"
#include "service/NetworkService.h"
#include <spdlog/spdlog.h> 

ControlProtocolClientHandler::ControlProtocolClientHandler()
{

}

void ControlProtocolClientHandler::Init()
{
	for (int i = 0; i < UINT16_MAX; ++i)
	{
		ProtocolFuncMap[i] = [this](unsigned __int32 ConnectionId, const unsigned __int8* Buffer, unsigned __int16 MessageSize) {Protocol_INVALID(ConnectionId, Buffer, MessageSize); };
	}

	REGISTER_PROTOCOL_HANDLER(EControlProtocol::NMT_Challenge, GameProtocol::NMT_Challenge, Protocol_NMT_Challenge);
	REGISTER_PROTOCOL_HANDLER(EControlProtocol::NMT_Welcome, GameProtocol::NMT_Welcome, Protocol_NMT_Welcome);
	REGISTER_PROTOCOL_HANDLER(EControlProtocol::NMT_NetSpeed, GameProtocol::NMT_NetSpeed, Protocol_NMT_NetSpeed);
	REGISTER_PROTOCOL_HANDLER(EControlProtocol::NMT_Failure, GameProtocol::NMT_Failure, Protocol_NMT_Failure);
}

void ControlProtocolClientHandler::ControlProtocol(unsigned __int32 ConnectionId, const unsigned __int8* Buffer, unsigned __int16 MessageType, unsigned __int16 MessageSize)
{
	ProtocolFuncMap[MessageType](ConnectionId, Buffer, MessageSize);
}

bool ControlProtocolClientHandler::Protocol_INVALID(unsigned __int32 ConnectionId, const unsigned __int8* Buffer, unsigned __int16 MessageSize)
{
	//std::cout << "Client Recv Protocol_INVALID" << std::endl;

	return false;
}

bool ControlProtocolClientHandler::Protocol_NMT_Challenge(unsigned __int32 ConnectionId, GameProtocol::NMT_Challenge& Message)
{
	std::string option = Message.option();
	std::string url = Message.url();

	//todo road game config

	GameProtocol::NMT_Login newMessage;
	std::string id = "test_id_1";
	std::string password = "test_pw_1";
	newMessage.set_id(id);
	newMessage.set_password(password);

	NetworkService* Owner = RemoteServerRegistry::Find(ConnectionId);
	return Owner ? Owner->SendPacket(newMessage, ConnectionId) : false;
}

bool ControlProtocolClientHandler::Protocol_NMT_Welcome(unsigned __int32 ConnectionId, GameProtocol::NMT_Welcome& Message)
{
	spdlog::info("[Login] NMT_Welcome received -- GameName={} WorldName={}", Message.gamename(), Message.worldname());

	GameProtocol::NMT_Join newMessage;
	newMessage.set_response(1557);

	NetworkService* Owner = RemoteServerRegistry::Find(ConnectionId);
	return Owner ? Owner->SendPacket(newMessage, ConnectionId) : false;
}

bool ControlProtocolClientHandler::Protocol_NMT_NetSpeed(unsigned __int32 ConnectionId, GameProtocol::NMT_NetSpeed& Message)
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
			const int Expected = Status.expectedReliable.load();
			const int Received = Message.lastackedpackettime();
			Status.failCount.fetch_add(1, std::memory_order_release);
			Status.expectedReliable.store(Received);

			spdlog::warn("[RUdpStatus] ConnectionId={} ORDER MISMATCH Expected={} Received={} totalPacket={} passCount={} failCount={}",
				ConnectionId, Expected, Received,
				Status.totalPacket.load(), Status.passCount.load(), Status.failCount.load());
		}
	}

	spdlog::trace("[RUdpStatus] ConnectionId={} totalPacket={} passCount={} failCount={}",
		ConnectionId, Status.totalPacket.load(), Status.passCount.load(), Status.failCount.load());

	return true;
}

bool ControlProtocolClientHandler::Protocol_NMT_Failure(unsigned __int32 ConnectionId, GameProtocol::NMT_Failure& Message)
{
	spdlog::warn("[Login] NMT_Failure received -- ErrorType={} ErrorNum={}", Message.errortype(), Message.errornum());

	return true;
}

//Remote는 Server의 handshake가 성공함
void ControlProtocolClientHandler::AddPlayerConnection(uint32_t ConnectionId)
{
	{
		std::lock_guard Lock(ConnectionRUDPMapMutex);
		ConnectionRUDPMap[ConnectionId];
	}
	GNetworkService->SetServerConnectionId(ConnectionId);
}

void ControlProtocolClientHandler::RemovePlayerConnection(uint32_t ConnectionId)
{
	std::lock_guard Lock(ConnectionRUDPMapMutex);
	ConnectionRUDPMap.erase(ConnectionId);
	spdlog::info("[Disconnect] ConnectionId={} removed from ConnectionRUDPMap", ConnectionId);
}