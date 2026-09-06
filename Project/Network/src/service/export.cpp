#include "export.h"

#include "ActorProtocolHandler.h"
#include "ControlProtocolHandler.h"
#include "LaDelta.h"
#include "INetworkService.h"
#include "INetworkEventNotify.h"

LADELTA_NETWORK_HANDLE LaDeltaNetworkKitInstanceCreate()
{
	LaDelta* NetworkHandle = new LaDelta();
	return NetworkHandle;
}

unsigned int NetworkKitInitialize(LADELTA_NETWORK_HANDLE Handle)
{
	auto* NetworkHandle = reinterpret_cast<LaDelta*>(Handle);
	return NetworkHandle->InitAPI();
}

unsigned int NetworkKitInitializeWithConfig(LADELTA_NETWORK_HANDLE Handle, const wchar_t* ConfigPath)
{
	auto* NetworkHandle = reinterpret_cast<LaDelta*>(Handle);
	std::filesystem::path Override = ConfigPath ? std::filesystem::path{ ConfigPath } : std::filesystem::path{};
	return NetworkHandle->InitAPI(Override);
}

unsigned int NetworkKitShutdown(LADELTA_NETWORK_HANDLE Handle)
{
	auto* NetworkHandle = reinterpret_cast<LaDelta*>(Handle);
	return NetworkHandle->ShutdownAPI();
}

bool RegisterActorHandlerFactory(LADELTA_NETWORK_HANDLE Handle, ActorHandlerFactoryFn fn)
{
	auto* NetworkHandle = reinterpret_cast<LaDelta*>(Handle);
	return NetworkHandle->RegisterActorHandlerFactory(fn);
}

bool RegisterControlHandlerFactory(LADELTA_NETWORK_HANDLE Handle, ControlHandlerFactoryFn fn)
{
	auto* NetworkHandle = reinterpret_cast<LaDelta*>(Handle);
	return NetworkHandle->RegisterControlHandlerFactory(fn);
}

bool RegisterNetworkInterface(LADELTA_NETWORK_HANDLE Handle, NetworkEventNotifyFactoryFn fn)
{
	auto* NetworkHandle = reinterpret_cast<LaDelta*>(Handle);
	return NetworkHandle->RegisterNetworkInterface(fn);
}

bool RegisterNetworkNotifyInterface(LADELTA_NETWORK_HANDLE Handle, NetworkServiceFactoryFn fn)
{
	auto* NetworkHandle = reinterpret_cast<LaDelta*>(Handle);
	return NetworkHandle->RegisterNetworkNotifyInterface(fn);
}

unsigned int LaDeltaStartOnlineGame(LADELTA_NETWORK_HANDLE Handle, AccoutInfo Info, SendInitialJoinCallback Callback, void* Context)
{
	auto* NetworkHandle = reinterpret_cast<LaDelta*>(Handle);
	return NetworkHandle->StartOnlineGame(Info, Callback, Context);
}

unsigned int LaDeltaEndOnlineGame(LADELTA_NETWORK_HANDLE Handle, AccoutInfo Info)
{
	auto* NetworkHandle = reinterpret_cast<LaDelta*>(Handle);
	return NetworkHandle->EndOnlineGame(Info);
}

bool NetworkKitEnqueueBuffer(LADELTA_NETWORK_HANDLE Handle, BYTE* Buffer, uint32_t Size, uint8_t ChannelId, uint32_t PlayerId, ReleaseMessageBufferCallback Callback, void* Context)
{
	auto* NetworkHandle = reinterpret_cast<LaDelta*>(Handle);
	return NetworkHandle->EnqueueMessageBuffer(Buffer, Size, ChannelId, PlayerId, Callback, Context);
}

void NetworkKitTickOnce(LADELTA_NETWORK_HANDLE Handle)
{
	auto* NetworkHandle = reinterpret_cast<LaDelta*>(Handle);
	NetworkHandle->TickOnce();
}

void NetworkKitTickSweep(LADELTA_NETWORK_HANDLE Handle)
{
	auto* NetworkHandle = reinterpret_cast<LaDelta*>(Handle);
	NetworkHandle->TickSweep();
}

unsigned int NetworkKitMsUntilNextTick(LADELTA_NETWORK_HANDLE Handle)
{
	auto* NetworkHandle = reinterpret_cast<LaDelta*>(Handle);
	return NetworkHandle->MsUntilNextTick();
}

unsigned long long NetworkKitUsUntilNextTick(LADELTA_NETWORK_HANDLE Handle)
{
	auto* NetworkHandle = reinterpret_cast<LaDelta*>(Handle);
	return NetworkHandle->UsUntilNextTick();
}

void NetworkKitPumpIO(LADELTA_NETWORK_HANDLE Handle, unsigned int TimeoutMs)
{
	auto* NetworkHandle = reinterpret_cast<LaDelta*>(Handle);
	NetworkHandle->PumpIO(TimeoutMs);
}

void NetworkKitWakeIO(LADELTA_NETWORK_HANDLE Handle)
{
	auto* NetworkHandle = reinterpret_cast<LaDelta*>(Handle);
	NetworkHandle->WakeIO();
}

unsigned int NetworkKitGetMaxTickRate(LADELTA_NETWORK_HANDLE Handle)
{
	auto* NetworkHandle = reinterpret_cast<LaDelta*>(Handle);
	return NetworkHandle->GetMaxTickRate();
}

unsigned int NetworkKitGetSyntheticTrafficHz(LADELTA_NETWORK_HANDLE Handle)
{
	auto* NetworkHandle = reinterpret_cast<LaDelta*>(Handle);
	return NetworkHandle->GetSyntheticTrafficHz();
}

uint32_t NetworkKitGetClientConnectionId(LADELTA_NETWORK_HANDLE Handle)
{
	auto* NetworkHandle = reinterpret_cast<LaDelta*>(Handle);
	return NetworkHandle->GetClientConnectionId();
}

