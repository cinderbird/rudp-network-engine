#pragma once
#include "Object.h"

class Actor : public Object
{
public:
	virtual ~Actor() = default;


private:
	bool bSpawned{ false };
};