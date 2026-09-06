#include "LaDelta.h"
#include "ActorProtocolHandler.h"
#include "ControlProtocolHandler.h"
#include "ProtocolHandlerManager.h"
#include "ConfigSetting.h"
#include "DriverSetting.h"
#include "INetworkService.h"
#include "IpNetDriver.h"
#include "NetConnection.h"
#include "PacketPipeline.h"

static ActorHandlerFactoryFn GActorProtocolFactory = nullptr;
static ControlHandlerFactoryFn GControlProtocolFactory = nullptr;
static NetworkEventNotifyFactoryFn GNetworkEventNotifyFactory = nullptr;
static NetworkServiceFactoryFn GNetworkServiceFactory = nullptr;

bool LaDelta::RegisterActorHandlerFactory(ActorHandlerFactoryFn fn)
{
	if (GActorProtocolFactory)
		return false;
	return GActorProtocolFactory = fn;
}

bool LaDelta::RegisterControlHandlerFactory(ControlHandlerFactoryFn fn)
{
	if (GControlProtocolFactory)
		return false;
	return GControlProtocolFactory = fn;
}


bool LaDelta::RegisterNetworkInterface(NetworkEventNotifyFactoryFn fn)
{
	if (GNetworkEventNotifyFactory)
		return false;
	return GNetworkEventNotifyFactory = fn;
}

bool LaDelta::RegisterNetworkNotifyInterface(NetworkServiceFactoryFn fn)
{
	if (GNetworkServiceFactory)
		return false;
	return GNetworkServiceFactory = fn;
}

ActorProtocolHandler* LaDelta::CreateActorHandler()
{
	if (GActorProtocolFactory)
	{
		return GActorProtocolFactory();
	}

	return nullptr;
}

ControlProtocolHandler* LaDelta::CreateControlHandler()
{
	if (GControlProtocolFactory)
	{
		return GControlProtocolFactory();
	}

	return nullptr;
}

void LaDelta::InitProtocolHandler()
{
	HandlerManager = std::make_shared<ProtocolHandlerManager>();

	if (!GActorProtocolHandler)
	{
		GActorProtocolHandler = CreateActorHandler();
		if (GActorProtocolHandler)
		{
			std::shared_ptr<ActorProtocolHandler> Actorhandler(GActorProtocolHandler);
			Actorhandler->InitHandler();
			HandlerManager->AddActorHandler(Actorhandler);
		}
	}
	else
	{
		std::shared_ptr<ActorProtocolHandler> Actorhandler(GActorProtocolHandler, [](ActorProtocolHandler*) {});
		HandlerManager->AddActorHandler(Actorhandler);
	}

	if (!GControlProtocolHandler)
	{
		GControlProtocolHandler = CreateControlHandler();
		if (GControlProtocolHandler)
		{
			std::shared_ptr<ControlProtocolHandler> Controlhandler(GControlProtocolHandler);
			Controlhandler->InitHandler();
			HandlerManager->AddControlHandler(Controlhandler);
		}
	}
	else
	{
		std::shared_ptr<ControlProtocolHandler> Controlhandler(GControlProtocolHandler, [](ControlProtocolHandler*) {});
		HandlerManager->AddControlHandler(Controlhandler);
	}
}

void LaDelta::InitInterface()
{
	if (GNetworkEventNotifyFactory)
	{
		INetworkEventNotify* Notify = GNetworkEventNotifyFactory();
		if (Notify)
		{
			NetworkEventNotify = std::shared_ptr<INetworkEventNotify>(Notify);
		}
	}

	if (GNetworkServiceFactory)
	{
		INetworkService* Service = GNetworkServiceFactory();
		if (Service)
		{
			NetworkService = std::shared_ptr<INetworkService>(Service);
		}
	}
}

void LaDelta::InitNetworkSetting(std::filesystem::path ConfigOverride)
{
	NetworkSetting NetSetting{};
	std::filesystem::path Filepath = GetConfigJsonPath();

	if (!std::filesystem::exists(Filepath))
	{
		//오류: 설정 파일을 못 찾음
		return;
	}

	ReadNetworkConfig(NetSetting, Filepath.string());
	Setting = std::make_shared<DriverSetting>(NetSetting);
}

void LaDelta::InitDriver()
{
	std::string Error;

	if (Setting->IsClient())
	{
		Driver = std::make_shared<IpNetDriver>();
		Driver->InitConnect(Setting, Error);
	}
	else if(Setting->IsServer())
	{
		switch (Setting->GetServerType())
		{
			case EServerType::LoginServer:
			{
				Driver = std::make_shared<IpNetDriver>();
				Driver->InitListen(Setting, Error);
				break;
			}
			default:
			{
				//error
				break;
			}
		}
	}
}

void LaDelta::TickOnce()
{
	TickSweep();

	const auto Remaining = NextTickTime - Network::Clock::Now();
	const uint32_t TimeoutMs = (Remaining <= std::chrono::steady_clock::duration::zero())
		? 0u
		: static_cast<uint32_t>(std::clamp<int64_t>(std::chrono::ceil<std::chrono::milliseconds>(Remaining).count(), 1, 100));

	Driver->PumpIO(TimeoutMs);
}

void LaDelta::TickSweep()
{
	const auto Now = Network::Clock::Now();
	if (Now < NextTickTime)
	{
		return;
	}

	Driver->TickHousekeeping();
	Driver->TickDispatch();
	Driver->TickFlush();

	const auto Interval = GetTickInterval();
	NextTickTime += Interval;
	if (NextTickTime < Now)
	{
		NextTickTime = Now + Interval;
	}
}

unsigned int LaDelta::MsUntilNextTick() const
{
	const auto Remaining = NextTickTime - Network::Clock::Now();
	if (Remaining <= std::chrono::steady_clock::duration::zero())
	{
		return 0u;
	}

	return static_cast<unsigned int>(std::clamp<int64_t>(
		std::chrono::ceil<std::chrono::milliseconds>(Remaining).count(), 1, 100));
}

unsigned long long LaDelta::UsUntilNextTick() const
{
	const auto Remaining = NextTickTime - Network::Clock::Now();
	if (Remaining <= std::chrono::steady_clock::duration::zero())
	{
		return 0ull;
	}
	return static_cast<unsigned long long>(std::clamp<int64_t>(
		std::chrono::ceil<std::chrono::microseconds>(Remaining).count(), 1, 100000));
}

void LaDelta::PumpIO(unsigned int TimeoutMs)
{
	Driver->PumpIO(TimeoutMs);
}

void LaDelta::WakeIO()
{
	Driver->WakeIO();
}

std::chrono::steady_clock::duration LaDelta::GetTickInterval() const
{

	const unsigned int Hz = GetMaxTickRate();
	if (Hz == 0)
	{
		return std::chrono::milliseconds(0);
	}
	return std::chrono::microseconds(1000000ull / Hz);
}

unsigned int LaDelta::GetMaxTickRate() const
{
	return Driver ? Driver->GetMaxTickRate() : 0u;
}

unsigned int LaDelta::GetSyntheticTrafficHz() const
{

	return (Driver && Driver->GetSetting()) ? Driver->GetSetting()->GetSyntheticTrafficHz() : 1u;
}

uint32_t LaDelta::GetClientConnectionId() const
{
	if (!Driver || !Driver->IsClient())
	{
		return 0u;
	}
	const auto ServerConn = Driver->GetServerConnection();
	return ServerConn ? ServerConn->GetUniqueConnectionId() : 0u;
}

bool LaDelta::EnqueueMessageBuffer(BYTE* Buffer, uint32_t Size, uint8_t ChannelId, uint32_t PlayerId, ReleaseMessageBufferCallback Callback, void* Context)
{
	return Driver->SendRawNetMessage(Buffer, Size, ChannelId, PlayerId, Callback, Context);
}

unsigned int LaDelta::CheckDefaultInitialize()
{
	if (Setting && Driver)
		return 0;
	else
	{
		unsigned int ret = 0;

		if (!Setting)
			ret |= 0x00000001;
		if (!Driver)
			ret |= 0x00000010;
		if (!NetworkEventNotify)
			ret |= 0x00010000;
		if (!NetworkService)
			ret |= 0x00100000;

		return ret;
	}
}

unsigned int LaDelta::InitAPI(std::filesystem::path ConfigOverride)
{
	//1. 실행 파일의 위치를 기반으로 config.json의 상대 위치를 찾고 세팅을 진행한다 
	InitNetworkSetting(ConfigOverride);

	//2. Setting을 기반으로 NetDriver 준비
	InitDriver();

	//3. 외부에서 전달받는 Handler 준비
	// -> Handler의 경우 Channel이 필요하기에 모든 세팅이 끝난 후 생성된 Channel을 연결한다
	InitProtocolHandler();

	//4. 외부에서 전달받는 Interface 준비
	InitInterface();

	return CheckDefaultInitialize();
}

unsigned int LaDelta::ShutdownAPI()
{
	//todo
	return 0;
}

unsigned int LaDelta::StartOnlineGame(AccoutInfo Info, SendInitialJoinCallback Callback, void* Context)
{
	if (Driver->IsClient())
	{
		std::shared_ptr<NetConnection> ServerConn = Driver->GetServerConnection();

		std::function<unsigned int(void)> HandShakeCompleDel = [ServerConn, Callback, Context]()
			{
				return Callback(ServerConn->GetPlayerConnectionID(), Context);
			};

		ServerConn->GetHandler()->BeginHandShake(HandShakeCompleDel);
	}

	return 1;
}

unsigned int LaDelta::EndOnlineGame(AccoutInfo Info)
{
	//todo
	return 0;
}


