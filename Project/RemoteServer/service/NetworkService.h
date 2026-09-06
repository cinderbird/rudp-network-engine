#pragma once
#include "pch.h"
#include "INetworkService.h"
#include "ProtocolTrait.h"




class RoomManager;

class NetworkService : public INetworkService
{
	//TEST
	int ConnectionReliable = 0;

public:
	virtual ~NetworkService() = default;

	bool Start();
	void End();

	NetworkService() = default;

	virtual bool Initialize() override;
	virtual void Shutdown() override;
	virtual void Tick() override;
	// 스레딩 계약은 INetworkService.h 참고.
	virtual void TickSweep() override;
	virtual unsigned int MsUntilNextTick() const override;
	virtual unsigned long long UsUntilNextTick() const override; // M23
	virtual void PumpIO(unsigned int TimeoutMs) override;
	virtual void WakeIO() override;
	virtual unsigned int GetMaxTickRateHz() const override;
	virtual void Clear() override;
	virtual unsigned int StartOnlineGame(const AccoutInfo& Info) override;
	virtual unsigned int EndOnlineGame(const AccoutInfo& Info) override;
	static void ReleaseMessageBuffer(void* Buffer, uint32_t Size, void* Context);

public:
	template<typename T>
	bool SendPacket(T& Pkt, uint32_t ConnectionId);

	void SetServerConnectionId(int Id);

	uint32_t GetClientConnectionId() const { return ClientConnectionId; }

private:
	void RemoteNetworkStatusCheck();
	Network::Clock::TimePoint LastStatusCheckTime{};

	void PumpSyntheticTraffic();

private:
	void* NETWORK_HANDLE{ nullptr };
	AccoutInfo Info{};
	int ServerConnectionId{ 0 }; //debugging - client는 network 내부에서 서버id를 0으로 고정
	uint32_t ClientConnectionId{ 0 }; 

private:
	static unsigned int SendInitialJoin(uint32_t ConnectionId, void* Context);
};

namespace RemoteServerRegistry
{
	void Register(uint32_t ConnectionId, NetworkService* Service);
	void Unregister(uint32_t ConnectionId);
	NetworkService* Find(uint32_t ConnectionId);
}

template<typename T>
inline bool NetworkService::SendPacket(T& Pkt, uint32_t ConnectionId)
{
	const uint16_t PayloadSize = static_cast<uint16_t>(Pkt.ByteSizeLong());
	const uint16_t TotalSize = static_cast<uint16_t>(sizeof(MessageHeader) + PayloadSize);

	BYTE* Buffer = static_cast<BYTE*>(::malloc(TotalSize));
	if (!Buffer) return false;
	std::memset(Buffer, 0, TotalSize);

	MessageHeader* Header = reinterpret_cast<MessageHeader*>(Buffer);
	Header->Size = TotalSize;
	Header->MessageId = ProtocolTraits<T>::Id;

	const uint32_t ChannelId = ProtocolTraits<T>::Category;

	const bool ok = Pkt.SerializeToArray(Buffer + sizeof(MessageHeader), static_cast<int>(PayloadSize));
	if (!ok)
	{
		::free(Buffer);
		return false;
	}

	return NetworkKitEnqueueBuffer(NETWORK_HANDLE, Buffer, TotalSize, ChannelId, ConnectionId, &ReleaseMessageBuffer, this);
}

extern std::shared_ptr<NetworkService> GNetworkService;