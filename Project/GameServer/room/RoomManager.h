#pragma once
#include "pch.h"
#include <shared_mutex>

class Player;
class Room;




class RoomManager
{
public:
	RoomManager();

	void AddPlayer(std::shared_ptr<Player> newPlayer);
	void BroadCast(int roomId, MessageCaputre message);
	std::vector<std::shared_ptr<Room>> GetRoomAll() const;

	const std::vector<int> GetRoomsId() const;

	bool VerifiedUser(const std::string& Id, const std::string& Password, uint32_t ConnectionId);

	void RemovePlayer(uint32_t ConnectionId);

protected:
	std::shared_ptr<Room> AddNewRoom();

	std::shared_ptr<Room> FindRoomWithConnection(uint32_t ConnectionId) const;

private:
	mutable std::shared_mutex RoomsMutex;

	std::vector<std::shared_ptr<Room>> Rooms;
	std::unordered_map<int, std::shared_ptr<Room>> RoomMap;
	Game::IDAllocator RoomIdAllocator;

	std::unordered_map<std::string, std::string> CredentialStore;

	Game::IDAllocator PlayerIdAllocator;
};
