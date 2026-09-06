#include "NetworkService.h"
#include "export.h"
#include <filesystem>
#include <mutex>
#include <unordered_map>
#include "RemoteActorProtocolHandler.h"
#include "RemoteControlProtocolHandler.h"

std::shared_ptr<NetworkService> GNetworkService = nullptr;

// 선언부 주석은 NetworkService.h 참고.
namespace RemoteServerRegistry
{
	namespace
	{
		std::mutex RegistryMutex;
		std::unordered_map<uint32_t, NetworkService*> Registry;
	}

	void Register(uint32_t ConnectionId, NetworkService* Service)
	{
		std::lock_guard Lock(RegistryMutex);
		Registry[ConnectionId] = Service;
	}

	void Unregister(uint32_t ConnectionId)
	{
		std::lock_guard Lock(RegistryMutex);
		Registry.erase(ConnectionId);
	}

	NetworkService* Find(uint32_t ConnectionId)
	{
		std::lock_guard Lock(RegistryMutex);
		auto It = Registry.find(ConnectionId);
		return It != Registry.end() ? It->second : nullptr;
	}
}

static ActorProtocolHandler* CreateActorHandler()
{
	return new ActorProtocolClientHandler();
}

static ControlProtocolHandler* CreateControlHandler()
{
	return new ControlProtocolClientHandler();
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
	NETWORK_HANDLE = LaDeltaNetworkKitInstanceCreate();

	RegisterActorHandlerFactory(NETWORK_HANDLE, &CreateActorHandler);
	RegisterControlHandlerFactory(NETWORK_HANDLE, &CreateControlHandler);

	if (NetworkKitInitializeWithConfig(NETWORK_HANDLE, nullptr))
	{
		return false;
	}

	ClientConnectionId = NetworkKitGetClientConnectionId(NETWORK_HANDLE);
	RemoteServerRegistry::Register(ClientConnectionId, this);

	return true;
}

void NetworkService::Shutdown()
{
	RemoteServerRegistry::Unregister(ClientConnectionId);
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
		RemoteNetworkStatusCheck();
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
		return LaDeltaStartOnlineGame(NETWORK_HANDLE, Info, &SendInitialJoin, this); //client의 경우 함수 더 호출
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

void NetworkService::SetServerConnectionId(int Id)
{
	ServerConnectionId = Id;
}

void NetworkService::RemoteNetworkStatusCheck()
{
	GameProtocol::NMT_NetSpeed message;
	message.set_lastackedpackettime(ConnectionReliable);

	ConnectionReliable++;

	SendPacket(message, 0);
}

unsigned int NetworkService::SendInitialJoin(uint32_t ConnectionId, void* Context)
{
	GameProtocol::NMT_Hello Message;

	std::string token = "Hello World";
	Message.set_networkid(1557);
	Message.set_token(token);

	return static_cast<NetworkService*>(Context)->SendPacket(Message, ConnectionId);
}
