/*
	C style interface for Network library
*/
#pragma once
#include <WinSock2.h>
#include <Windows.h>
#include <export_struct.h>
#include <cstdint>

/*
	Network Engine Require public interface factory class
*/
class ActorProtocolHandler;
class ControlProtocolHandler;
class INetworkEventNotify;
class INetworkService;

/*
	NetworkKit instance handle created by calling NetworkKitInstanceCreate()
*/
using LADELTA_NETWORK_HANDLE = void*;

/*
	Factory Handler used by RegisterActorHandlerFactory(ActorHandlerFactoryFn fn)
*/
using ActorHandlerFactoryFn = ActorProtocolHandler* (*)();

/*
	Factory Handler used by RegisterControlHandlerFactory(ControlHandlerFactoryFn fn) 
*/
using ControlHandlerFactoryFn = ControlProtocolHandler* (*)();

/*
	Factory Handler used by RegisterNetworkNotifyInterface(NetworkEventNotifyFactoryFn fn)
*/
using NetworkEventNotifyFactoryFn = INetworkEventNotify* (*)();

/*
	Factory Handler used by RegisterNetworkInterface(NetworkSErviceFactoryFn fn)
*/
using NetworkServiceFactoryFn = INetworkService* (*)();


/*
	After 3-way handshake begin NMT callback
*/
using SendInitialJoinCallback = unsigned int (*)(uint32_t ConnectionId, void* Context);

/*
	After Register NetMessage, if Recv Sucesses Callback function will release Buffer
*/
using ReleaseMessageBufferCallback = void (*)(void* Buffer, uint32_t Size, void* Context);


//초기화, 등록, 생성
extern "C"
{
	/*
		Create LaDelta Network instance and return Handle
	*/
	LADELTA_NETWORK_HANDLE LaDeltaNetworkKitInstanceCreate();

	/*
		Notion: You Must call RegisterHandlerFactory before call NetworkKitInitialize

		Explicitly initialize the Network
		return 0: success; other number is fail about Initialize
	*/
	unsigned int NetworkKitInitialize(LADELTA_NETWORK_HANDLE Handle);

	/*
		
	*/
	unsigned int NetworkKitInitializeWithConfig(LADELTA_NETWORK_HANDLE Handle, const wchar_t* ConfigPath);


	/*
		Explicitly shutdown the Network
	*/
	unsigned int NetworkKitShutdown(LADELTA_NETWORK_HANDLE Handle);
	
	/*
		Explicity Register Handler Factory class and require LADELTA_NETWORK_HANDLE
		if already registing the factory ActorProtocolHandler. return false.
	*/
	bool RegisterActorHandlerFactory(LADELTA_NETWORK_HANDLE Handle, ActorHandlerFactoryFn fn);
	
	/*
		Explicity Register Handler Factory class and require LADELTA_NETWORK_HANDLE
		if already registing the factory ControlProtocolHandler. return false.
	*/
	bool RegisterControlHandlerFactory(LADELTA_NETWORK_HANDLE Handle, ControlHandlerFactoryFn fn);

	/*
		Register Network Service interface, if already registing the interface, return false.
	*/
	bool RegisterNetworkInterface(LADELTA_NETWORK_HANDLE Handle, NetworkEventNotifyFactoryFn fn);

	/*
		Register Network Event Notify interface, if already registing the interface, return false.
	*/
	bool RegisterNetworkNotifyInterface(LADELTA_NETWORK_HANDLE Handle, NetworkServiceFactoryFn fn);
}

//게임 시작, 종료
extern "C"
{
	/*
		Start Online Game, try to connect game server
	*/
	unsigned int LaDeltaStartOnlineGame(LADELTA_NETWORK_HANDLE Handle, AccoutInfo Info, SendInitialJoinCallback Callback, void* Context);

	/*
		End Online Game, try to disconnect game server
	*/
	unsigned int LaDeltaEndOnlineGame(LADELTA_NETWORK_HANDLE Handle, AccoutInfo Info);
}

//패킷 전송
extern "C"
{
	/*
		Enqueue NetMessage Buffer to Pool and Register Packet
	*/
	bool NetworkKitEnqueueBuffer(LADELTA_NETWORK_HANDLE Handle, unsigned char* Buffer, uint32_t Size, uint8_t ChannelId, uint32_t PlayerId, ReleaseMessageBufferCallback Callback, void* Context);
}

//Tikcing
extern "C"
{
	void NetworkKitTickOnce(LADELTA_NETWORK_HANDLE Handle);

	void NetworkKitTickSweep(LADELTA_NETWORK_HANDLE Handle);
	unsigned int NetworkKitMsUntilNextTick(LADELTA_NETWORK_HANDLE Handle);
	/*
		위 함수의 마이크로초 정밀도 버전 -- 서브밀리초 해상도로 대기 가능한
		host용(ThreadManager::TickLoop 참고).
	*/
	unsigned long long NetworkKitUsUntilNextTick(LADELTA_NETWORK_HANDLE Handle);
	void NetworkKitPumpIO(LADELTA_NETWORK_HANDLE Handle, unsigned int TimeoutMs);
	void NetworkKitWakeIO(LADELTA_NETWORK_HANDLE Handle);


	unsigned int NetworkKitGetMaxTickRate(LADELTA_NETWORK_HANDLE Handle);

	unsigned int NetworkKitGetSyntheticTrafficHz(LADELTA_NETWORK_HANDLE Handle);

	uint32_t NetworkKitGetClientConnectionId(LADELTA_NETWORK_HANDLE Handle);
}
