#include "Room.h"
#include "actor/Player.h"
#include "service/NetworkService.h"

Room::Room(int roomId)
	: maxPlayer(1024)
	, roomId(roomId)
{
}

bool Room::IsAddPlayer() const
{
	std::shared_lock Lock(PlayersMutex);
	return (uint32_t)playerMap.size() < maxPlayer;
}

bool Room::AddPlayer(std::shared_ptr<Player> newPlayer)
{
	std::unique_lock Lock(PlayersMutex);

	// 삽입과 같은 락 아래서 용량을 여기서도 다시 체크 -- RoomManager::
	// AddPlayer()가 IsAddPlayer()를 별도의, 더 앞선 락 호출로 부르는데,
	// 이는 워커 여러 개가 동시에 이 Room에 닿을 수 있게 되면
	// check-then-act 레이스가 된다(RoomManager::AddPlayer가 단일 바깥쪽
	// 틱 스레드에서만 도는 지금은 실제 트래픽으로 도달 불가능 --
	// Docs/M12_REAL_PARALLELISM.md 참고 -- 그래도 이 Room 레벨 재검사가
	// 누가 몇 개 스레드에서 부르든 AddPlayer() 자체를 안전하게 만든다).
	if ((uint32_t)playerMap.size() >= maxPlayer)
	{
		return false;
	}

	const auto ConnectionId = newPlayer->GetConnectionId();
	if (playerMap.count(ConnectionId) == 0)
	{
		playerMap[ConnectionId] = newPlayer;
		Players.push_back(newPlayer);
		return true;
	}

	return false;
}

bool Room::RemovePlayer(uint32_t playerId)
{
	std::unique_lock Lock(PlayersMutex);
	if (playerMap.count(playerId) != 0)
	{
		playerMap.erase(playerId);

		// 예전엔 playerMap에서만 지워서 나간 플레이어가 Players에 영원히
		// 남았다 -- BroadCast()는 playerMap이 아니라 Players를 순회하므로,
		// 연결 끊긴 플레이어가 계속 (시도라도) 브로드캐스트를 받았다.
		// playerMap은 ConnectionId로 키가 잡혀 있으니(여기 매개변수 이름과
		// 무관하게, AddPlayer 참고) 그 값으로 매칭한다.
		std::erase_if(Players, [playerId](const std::shared_ptr<Player>& P)
			{
				return P && static_cast<uint32_t>(P->GetConnectionId()) == playerId;
			});

		return true;
	}

	//오류
	return false;
}

uint32_t Room::GetPlayerCount() const
{
	std::shared_lock Lock(PlayersMutex);
	return (uint32_t)playerMap.size();
}

std::shared_ptr<Player> Room::GetPlayer(uint32_t PlayerId)
{
	std::shared_lock Lock(PlayersMutex);
	auto it = playerMap.find(PlayerId);
	if (it != playerMap.end())
	{
		return it->second;
	}

	return nullptr;
}

const int Room::GetRoomId() const
{
	return roomId;
}

void Room::BroadCast(MessageCaputre message)
{
	std::vector<std::shared_ptr<Player>> PlayersSnapshot;
	{
		std::shared_lock Lock(PlayersMutex);
		PlayersSnapshot = Players;
	}

	for (const auto& it : PlayersSnapshot)
	{
		GNetworkService->SendCapturedMessage(it->GetConnectionId(), message);
	}
}
