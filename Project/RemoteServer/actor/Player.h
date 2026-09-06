#pragma once
#include "Character.h"
#include <cstdint>

class Player : public Character
{
public:
	Player(int connectionId);
	virtual ~Player() = default;

	const int GetPlayerId() const;
	const int GetConnectionId() const;

private:
	int playerId{ 0 };
	uint32_t connectionId{ 0 };
};