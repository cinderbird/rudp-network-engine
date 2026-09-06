#include "RoomManager.h"
#include "room/Room.h"

RoomManager::RoomManager()
{
	{
		std::unique_lock Lock(RoomsMutex);
		AddNewRoom();
	}

	CredentialStore.emplace("test_id_1", "test_pw_1");
}

void RoomManager::AddPlayer(std::shared_ptr<Player> newPlayer)
{
	std::unique_lock Lock(RoomsMutex);

	for (const auto& it : Rooms)
	{
		if (it->IsAddPlayer() && it->AddPlayer(newPlayer))
		{
			return;
		}
	}

	auto room = AddNewRoom();
	if (room->AddPlayer(newPlayer))
	{
		return;
	}

	//경고: 치명적 오류
}

void RoomManager::BroadCast(int roomId, MessageCaputre message)
{
	std::shared_ptr<Room> Target;
	{
		std::shared_lock Lock(RoomsMutex);
		const auto& it = RoomMap.find(roomId);
		if (it != RoomMap.end())
		{
			Target = it->second;
		}
	}

	if (Target)
	{
		Target->BroadCast(message);
	}
}

std::vector<std::shared_ptr<Room>> RoomManager::GetRoomAll() const
{
	std::shared_lock Lock(RoomsMutex);
	return Rooms;
}

const std::vector<int> RoomManager::GetRoomsId() const
{
	std::shared_lock Lock(RoomsMutex);
	std::vector<int> Ids;
	for (const auto& it : Rooms)
	{
		Ids.push_back(it->GetRoomId());
	}
	return Ids;
}

bool RoomManager::VerifiedUser(const std::string& Id, const std::string& Password, uint32_t ConnectionId)
{
	const auto CredIt = CredentialStore.find(Id);
	if (CredIt == CredentialStore.end() || CredIt->second != Password)
	{
		return false;
	}

	std::shared_ptr<Room> TargetRoom;
	{
		std::shared_lock Lock(RoomsMutex);
		TargetRoom = FindRoomWithConnection(ConnectionId);
	}

	if (!TargetRoom)
	{
		return false;
	}

	std::shared_ptr<Player> TargetPlayer = TargetRoom->GetPlayer(ConnectionId);
	if (!TargetPlayer)
	{
		return false;
	}

	const uint32_t NewPlayerId = PlayerIdAllocator.Allocate();
	TargetPlayer->VerifiedUser(static_cast<int>(NewPlayerId));

	return true;
}

void RoomManager::RemovePlayer(uint32_t ConnectionId)
{
	std::shared_ptr<Room> TargetRoom;
	{
		std::shared_lock Lock(RoomsMutex);
		TargetRoom = FindRoomWithConnection(ConnectionId);
	}

	if (TargetRoom)
	{
		TargetRoom->RemovePlayer(ConnectionId);
	}
}

std::shared_ptr<Room> RoomManager::AddNewRoom()
{
	const auto roomId = RoomIdAllocator.Allocate();
	std::shared_ptr<Room> newRoom = std::make_shared<Room>(roomId);
	Rooms.push_back(newRoom);
	RoomMap.insert({ roomId, newRoom });

	return newRoom;
}

std::shared_ptr<Room> RoomManager::FindRoomWithConnection(uint32_t ConnectionId) const
{
	for (const auto& RoomPtr : Rooms)
	{
		if (RoomPtr && RoomPtr->GetPlayer(ConnectionId))
		{
			return RoomPtr;
		}
	}

	return nullptr;
}
