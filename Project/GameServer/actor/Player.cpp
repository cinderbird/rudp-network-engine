#include "Player.h"

Player::Player(uint32_t ConnectionId)
	: ConnectionId(ConnectionId)
{
}

void Player::VerifiedUser(int InplayerId)
{
	AuthState = EPlayerLoginState::Verified;
	playerId = InplayerId;
}

const int Player::GetPlayerId() const
{
	return playerId;
}

const int Player::GetConnectionId() const
{
	return ConnectionId;
}