#pragma once
#include "pch.h"

#include <export_struct.h>

class ActorProtocolHandler;
class ControlProtocolHandler;
class ProtocolHandlerManager;
class INetworkEventNotify;
class INetworkService;

struct NetworkSetting;
class DriverSetting;
class NetDriver;

using ActorHandlerFactoryFn = ActorProtocolHandler* (*)();
using ControlHandlerFactoryFn = ControlProtocolHandler* (*)();
using NetworkEventNotifyFactoryFn = INetworkEventNotify* (*)();
using NetworkServiceFactoryFn = INetworkService* (*)();

using SendInitialJoinCallback = unsigned int (*)(uint32_t ConnectionId, void* Context);

using ReleaseMessageBufferCallback = void (*)(void* Buffer, uint32_t Size, void* Context);

class LaDelta
{
	friend class NetworkManager;

public:
	LaDelta() = default;
	~LaDelta() = default;

	bool RegisterActorHandlerFactory(ActorHandlerFactoryFn fn);
	bool RegisterControlHandlerFactory(ControlHandlerFactoryFn fn);
	bool RegisterNetworkInterface(NetworkEventNotifyFactoryFn fn);
	bool RegisterNetworkNotifyInterface(NetworkServiceFactoryFn fn);

	unsigned int InitAPI(std::filesystem::path ConfigOverride = std::filesystem::path{});
	unsigned int ShutdownAPI();

	unsigned int StartOnlineGame(AccoutInfo Info, SendInitialJoinCallback Callback, void* Context);
	unsigned int EndOnlineGame(AccoutInfo Info);

	void TickOnce(); //외부에서 Thread를 생성하여 사용

	void TickSweep();
	unsigned int MsUntilNextTick() const;
	unsigned long long UsUntilNextTick() const;
	void PumpIO(unsigned int TimeoutMs);
	void WakeIO();

	unsigned int GetMaxTickRate() const; // M5: config.json's MaxTickRate, 0 = unthrottled
	unsigned int GetSyntheticTrafficHz() const; // config.json의 SyntheticTrafficHz

	uint32_t GetClientConnectionId() const;

	bool EnqueueMessageBuffer(BYTE* Buffer, uint32_t Size, uint8_t ChannelId, uint32_t ConnectionId, ReleaseMessageBufferCallback Callback, void* Context);

protected:
	ActorProtocolHandler* CreateActorHandler();
	ControlProtocolHandler* CreateControlHandler();

	void InitProtocolHandler();
	void InitInterface();

	void InitNetworkSetting(std::filesystem::path ConfigOverride);
	void InitDriver();

private:
	unsigned int CheckDefaultInitialize();

	// MaxTickRate에서 유도한 스윕 주기. TickOnce() 참고.
	std::chrono::steady_clock::duration GetTickInterval() const;

private:
	std::shared_ptr<ProtocolHandlerManager> HandlerManager;
	std::shared_ptr<DriverSetting> Setting;
	std::shared_ptr<NetDriver> Driver;

	// 다음 예정된 커넥션 스윕. 틱 스레드만 건드림.
	std::chrono::steady_clock::time_point NextTickTime{ std::chrono::steady_clock::now() };

	std::shared_ptr<INetworkEventNotify> NetworkEventNotify;
	std::shared_ptr<INetworkService> NetworkService;
};

