#include "Player.h"

Player::Player(int connectionId)
	: connectionId(connectionId)
{
}

const int Player::GetPlayerId() const
{
	return playerId;
}
