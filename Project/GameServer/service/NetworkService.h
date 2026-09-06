#pragma once
#include "pch.h"
#include "INetworkService.h"
#include "ProtocolTrait.h"
#include <mutex>

#include "room/RoomManager.h"


class RoomManager;

class NetworkService : public INetworkService
{
public:
	//TEST
	std::unordered_map<uint32_t, int> ConnectionReliableMap;

	std::mutex ConnectionReliableMapMutex;


public:
	virtual ~NetworkService() = default;

	bool Start();
	void End();

	NetworkService() = default;

	virtual bool Initialize() override;
	virtual void Shutdown() override;
	virtual void Tick() override;
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
	bool SendCapturedMessage(uint32_t ConnectionId, MessageCaputre message);
	

	template<typename T>
	bool SendPacket(T& Pkt, uint32_t ConnectionId);

	template<typename T>
	bool BroadCast(T& Pkt, uint32_t RoomId);

	void AddPlayer(uint32_t connectionId);
	void RemovePlayer(uint32_t connectionId); // M14
	std::shared_ptr<RoomManager> GetRoomManger();

protected:
	void NetworkStatusCheck();
	Network::Clock::TimePoint LastStatusCheckTime{};


	void PumpSyntheticTraffic();

private:
	void* NETWORK_HANDLE{ nullptr };
	AccoutInfo Info{};

	std::shared_ptr<RoomManager> roomManager;
};

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

template<typename T>
inline bool NetworkService::BroadCast(T& Pkt, uint32_t RoomId)
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

	MessageCaputre messageWrapper{ Buffer, TotalSize, ChannelId };
	roomManager->BroadCast(RoomId, messageWrapper);

	return true;
}

extern std::shared_ptr<NetworkService> GNetworkService;