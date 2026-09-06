#include "NetworkService.h"
#include "export.h"
#include "room/RoomManager.h"
#include "RemoteActorProtocolHandler.h"
#include "RemoteControlProtocolHandler.h"

std::shared_ptr<NetworkService> GNetworkService = nullptr;

static ActorProtocolHandler* CreateActorHandler()
{
	return new ActorProtocolServerHandler();
}

static ControlProtocolHandler* CreateControlHandler()
{
	return new ControlProtocolServerHandler();
}

bool NetworkService::Start()
{
	return StartOnlineGame(Info);
}

void NetworkService::End()
{
	EndOnlineGame(Info);
}

bool NetworkService::Initialize()
{
	roomManager = std::make_shared<RoomManager>();

	NETWORK_HANDLE = LaDeltaNetworkKitInstanceCreate();

	RegisterActorHandlerFactory(NETWORK_HANDLE, &CreateActorHandler);
	RegisterControlHandlerFactory(NETWORK_HANDLE, &CreateControlHandler);

	if (NetworkKitInitializeWithConfig(NETWORK_HANDLE, nullptr))
	{
		return false;
	}
	
	return true;
}

void NetworkService::Shutdown()
{
	NetworkKitShutdown(NETWORK_HANDLE);
	Clear();
}

void NetworkService::PumpSyntheticTraffic()
{
	const unsigned int Hz = NetworkKitGetSyntheticTrafficHz(NETWORK_HANDLE);
	const auto Interval = std::chrono::microseconds(1000000ull / (Hz == 0 ? 1u : Hz));
	const auto Now = Network::Clock::Now();

	if (LastStatusCheckTime == Network::Clock::TimePoint{})
	{
		LastStatusCheckTime = Now;
		return;
	}

	constexpr int MaxPerCall = 64;
	int Sent = 0;
	while (Now - LastStatusCheckTime >= Interval && Sent < MaxPerCall)
	{
		NetworkStatusCheck();
		LastStatusCheckTime += Interval;
		++Sent;
	}

	if (Now - LastStatusCheckTime >= Interval)
	{
		LastStatusCheckTime = Now;
	}
}

void NetworkService::Tick()
{
	PumpSyntheticTraffic();
	NetworkKitTickOnce(NETWORK_HANDLE);
}

void NetworkService::TickSweep()
{
	PumpSyntheticTraffic();
	NetworkKitTickSweep(NETWORK_HANDLE);
}

unsigned int NetworkService::MsUntilNextTick() const
{
	return NetworkKitMsUntilNextTick(NETWORK_HANDLE);
}

unsigned long long NetworkService::UsUntilNextTick() const
{
	return NetworkKitUsUntilNextTick(NETWORK_HANDLE);
}

void NetworkService::PumpIO(unsigned int TimeoutMs)
{
	NetworkKitPumpIO(NETWORK_HANDLE, TimeoutMs);
}

void NetworkService::WakeIO()
{
	NetworkKitWakeIO(NETWORK_HANDLE);
}

unsigned int NetworkService::GetMaxTickRateHz() const
{
	return NetworkKitGetMaxTickRate(NETWORK_HANDLE);
}

unsigned int NetworkService::StartOnlineGame(const AccoutInfo& Info)
{
	if (Initialize())
	{
		return true; 
	}
	return false;
}

unsigned int NetworkService::EndOnlineGame(const AccoutInfo& Info)
{
	return LaDeltaEndOnlineGame(NETWORK_HANDLE, Info);
}

void NetworkService::Clear()
{
	//todo
}

void NetworkService::ReleaseMessageBuffer(void* Buffer, uint32_t Size, void* Context)
{
	::free(Buffer);
}

bool NetworkService::SendCapturedMessage(uint32_t ConnectionId, MessageCaputre message)
{
	return NetworkKitEnqueueBuffer(NETWORK_HANDLE, message.Buffer, message.TotalSize, message.ChannelId, ConnectionId, &ReleaseMessageBuffer, this);
}

void NetworkService::AddPlayer(uint32_t connectionId)
{
	{
		std::lock_guard Lock(ConnectionReliableMapMutex);
		ConnectionReliableMap.insert({ connectionId, 0 });
	}
	std::shared_ptr<Player> newPlayer = std::make_shared<Player>(connectionId);
	roomManager->AddPlayer(newPlayer);
}

void NetworkService::RemovePlayer(uint32_t connectionId)
{
	{
		std::lock_guard Lock(ConnectionReliableMapMutex);
		ConnectionReliableMap.erase(connectionId);
	}
	roomManager->RemovePlayer(connectionId);
}

std::shared_ptr<RoomManager> NetworkService::GetRoomManger()
{
	return roomManager;
}

void NetworkService::NetworkStatusCheck()
{
	std::lock_guard Lock(ConnectionReliableMapMutex);
	for (auto& it : ConnectionReliableMap)
	{
		GameProtocol::NMT_NetSpeed message;
		message.set_lastackedpackettime(it.second);

		it.second++;

		SendPacket(message, it.first);
	}

	//const auto& RoomsId = roomManager->GetRoomsId();
	//for (const auto& it : RoomsId)
	//{
	//	BroadCast(message, it);
	//}
}
