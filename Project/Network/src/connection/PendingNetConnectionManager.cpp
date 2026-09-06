#include "PendingNetConnectionManager.h"
#include "PendingNetConnection.h"

PendingConnectionManager::PendingConnectionManager()
{
}

void PendingConnectionManager::TimeKillEvent(uint8_t TimeId, IDAllocator& IDManager, double Now, double InactivityTimeoutSec)
{
	std::vector<int> tempIdVec;
	{
		//쓰기 락
		std::unique_lock Lock(sharedMutexs[TimeId]);

		auto& CachedMap = ClientCache[TimeId];

		for (auto it = CachedMap.begin(); it != CachedMap.end(); )
		{
			const double Idle = Now - it->second->GetLastActivityTime();
			if (Idle >= InactivityTimeoutSec)
			{
				NETWORK_LOG_WARN("PendingConnectionManager::TimeKillEvent - reaping inactive pending connection, TimeId: {}, ClientId: {}, IdleSec: {:.2f}", TimeId, it->second->GetClientId(), Idle);
				tempIdVec.push_back(it->second->GetClientId());
				it = CachedMap.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	for (const auto it : tempIdVec)
	{
		IDManager.Free(it);
	}
}

bool PendingConnectionManager::FindConnection(uint64_t ClienAddrtHash, std::shared_ptr<PendingRemoteConnection>& RequestClient) const
{
	for (int TimeId = 0; TimeId < 60; ++TimeId)
	{
		//읽기 락
		std::shared_lock Lock(sharedMutexs[TimeId]);

		auto& CachedMap = ClientCache[TimeId];
		auto CachedClientPair = CachedMap.find(ClienAddrtHash);
		if (CachedClientPair != CachedMap.end())
		{
			RequestClient = CachedClientPair->second;
			return true;
		}
	}

	return false;
}

void PendingConnectionManager::AddConnection(uint8_t TimeId, uint64_t ClienAddrtHash, std::shared_ptr<PendingRemoteConnection> RequestClient)
{
	//쓰기 락
	std::unique_lock Lock(sharedMutexs[TimeId]);
	ClientCache[TimeId].insert({ ClienAddrtHash, RequestClient });
}

const PendingConnectionManager::ClientHashMap PendingConnectionManager::GetHashMapbyTimeId(uint8_t TimeId)
{
	//읽기 락
	std::shared_lock Lock(sharedMutexs[TimeId]);
	return ClientCache[TimeId];
}

std::shared_ptr<PendingRemoteConnection> PendingConnectionManager::CreatePendingClient(double Time, uint32_t SessionId, uint32_t ClientID, std::shared_ptr<NetAddr> Address, uint8_t TimeId)
{
	auto PendingClient = std::make_shared<PendingRemoteConnection>(Time, SessionId, ClientID, Address, TimeId);
	return PendingClient;
}

void PendingConnectionManager::AckConnectionKill(uint8_t TimeId, uint64_t ClienAddrtHash, IDAllocator& IDManager)
{
	int CachedId = -1;

	{
		//쓰기 락
		std::unique_lock Lock(sharedMutexs[TimeId]);
		auto& CachedMap = ClientCache[TimeId];
		auto CacahedClientPair = CachedMap.find(ClienAddrtHash);
		if (CacahedClientPair != CachedMap.end())
		{
			CachedId = CacahedClientPair->second->GetClientId();
			CachedMap.erase(ClienAddrtHash);
		}
		else
		{
			NETWORK_LOG_WARN("PendingConnectionManager::{} - Fail to find PendingConnection - TimeId: {}, ClienAddrtHash: {}", __FUNCDNAME__, TimeId, ClienAddrtHash);
		}
	}

	if (CachedId != -1)
	{
		IDManager.Free(CachedId);
	}
}