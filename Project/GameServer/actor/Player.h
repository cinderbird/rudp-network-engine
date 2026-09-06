#pragma once
#include "pch.h"
#include "Character.h"


class Player : public Character
{
	enum class EPlayerLoginState
	{
		NotAuthenticated,
		Verified
	};

public:
	Player(uint32_t ConnectionId); 
	virtual ~Player() = default;


	void VerifiedUser(int playerId); 
	const int GetPlayerId() const; 
	const int GetConnectionId() const; //ConnectionID: Runtime network Connection key
	bool IsVerified() const { return AuthState == EPlayerLoginState::Verified; } 


private:
	EPlayerLoginState AuthState = EPlayerLoginState::NotAuthenticated;
	int playerId{ 0 };
	uint32_t ConnectionId{ 0 };
};