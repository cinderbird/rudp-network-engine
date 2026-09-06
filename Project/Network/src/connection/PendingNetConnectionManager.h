#pragma once
#include "pch.h"
#include "IDAllocator.h"

class NetAddr;
class PendingRemoteConnection;

class PendingConnectionManager
{
public:
	using ClientHashMap = std::unordered_map<uint64_t, std::shared_ptr<PendingRemoteConnection>>;

public:
	PendingConnectionManager();

	void TimeKillEvent(uint8_t TimeId, IDAllocator& IDManager, double Now, double InactivityTimeoutSec);
	bool FindConnection(uint64_t ClienAddrtHash, std::shared_ptr<PendingRemoteConnection>& RequestClient) const;
	void AddConnection(uint8_t TimeId, uint64_t ClienAddrtHash, std::shared_ptr<PendingRemoteConnection> RequestClient);
	const ClientHashMap GetHashMapbyTimeId(uint8_t TimeId);

	static std::shared_ptr<PendingRemoteConnection> CreatePendingClient(double Time, uint32_t SessionId, uint32_t ClientID, std::shared_ptr<NetAddr> Address, uint8_t TimeId);

private:
	/*
		주기적으로 연결을 정리하기 때문에 반드시 필요할때만 호출할 것
	*/
	void AckConnectionKill(uint8_t TimeId, uint64_t ClienAddrtHash, IDAllocator& IDManager);

	mutable std::shared_mutex sharedMutexs[60];
	
	ClientHashMap ClientCache[60]{};
};

