#pragma once
#include "Actor.h"


class Character : public Actor
{
public:
	virtual ~Character() = default;


private:
	int characterId;
	bool bControlled{ false };
};