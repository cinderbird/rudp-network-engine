#pragma once

class INetworkEventNotify
{
public:
	virtual void AddPlayerConnection(uint32_t ConnectionId) = 0;
};