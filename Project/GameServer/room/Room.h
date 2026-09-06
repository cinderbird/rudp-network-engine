#pragma once
#include "pch.h"
#include <shared_mutex>

class Player;

class Room
{
public:
	Room(int roomId);

	bool IsAddPlayer() const;


	bool AddPlayer(std::shared_ptr<Player> newPlayer);
	bool RemovePlayer(uint32_t playerId);

	uint32_t GetPlayerCount() const;
	std::shared_ptr<Player> GetPlayer(uint32_t PlayerId);

	const int GetRoomId() const;
	
	void BroadCast(MessageCaputre message);



private:
	uint32_t maxPlayer{};

	mutable std::shared_mutex PlayersMutex;

	std::vector<std::shared_ptr<Player>> Players;
	std::unordered_map<uint32_t, std::shared_ptr<Player>> playerMap;
	int roomId{ 0 };
};