#pragma once
#include "pch.h"
#include "NetConnectionResolution.h"

class WindowsSocket;
class NetAddr;
class NetConnection;
class NetworkSocket;
class GameNetConnection;


using CreateAndBindSocketFunc = std::function<std::unique_ptr<WindowsSocket>(std::shared_ptr<NetAddr> BindAddr, std::string& Error)>;

class GameNetDriverResolution
{
	friend class IpNetDriver;

public:
	GameNetDriverResolution();

private:
	bool InitBindSockets(CreateAndBindSocketFunc LazySocketFunc, std::string& Error, const std::string & Host, uint32_t Port, bool bServerDriver);

	void InitConnect(std::shared_ptr<NetConnection> ServerConnection, const NetworkSocket* ActiveSocket);

	static GameNetConnectionAddressResolution* GetConnectionResolver(GameNetConnection* Connection);


private:
	std::array<std::shared_ptr<NetworkSocket>, 2> BoundSockets;
};
