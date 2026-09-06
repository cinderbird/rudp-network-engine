#pragma once
#include "GameNetDriverResolution.h"
#include "WindowsInternetAddress.h"
#include "WindowsSocket.h"
#include "GameNetConnection.h"

GameNetDriverResolution::GameNetDriverResolution()
{

}

bool GameNetDriverResolution::InitBindSockets(CreateAndBindSocketFunc LazySocketFunc, std::string& Error, const std::string &Host, uint32_t Port, bool bServerDriver)
{
	std::shared_ptr<WindowsAddr> BindAddress = std::make_shared<WindowsAddr>();

	if (bServerDriver)
	{
		BindAddress->SetAnyIPv4Address();
		BindAddress->SetAddrPort(Port);
	}
	else
	{
		BindAddress->SetAnyIPv4Address();
		BindAddress->SetAddrPort(0);
	}

	std::shared_ptr<WindowsSocket> NewSocket(std::move(LazySocketFunc(BindAddress, Error)));

	if (!NewSocket)
		return false;

	BoundSockets[1] = NewSocket;
	
	return true;
}

void GameNetDriverResolution::InitConnect(std::shared_ptr<NetConnection> ServerConnection, const NetworkSocket* ActiveSocket)
{
	GameNetConnection* IPConnection = static_cast<GameNetConnection*>(ServerConnection.get());
	

	if (IPConnection->Resolver->IsAddressResolutionEnabled())
	{
		IPConnection->Resolver->BindSockets = std::move(BoundSockets);
	}
}

GameNetConnectionAddressResolution * GameNetDriverResolution::GetConnectionResolver(GameNetConnection* Connection)
{
	return Connection != nullptr ? Connection->Resolver.get() : nullptr;
}


