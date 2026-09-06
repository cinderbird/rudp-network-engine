#pragma once
#include "pch.h"

class NetAddr;
class NetworkSocket;

class GameNetConnectionAddressResolution
{
	friend class GameNetConnection;
	friend class GameNetDriverResolution;
	friend class IpNetDriver;

private:
	bool InitLocalConnection(std::string remoteAddr);
	std::shared_ptr<NetAddr> GetRemoteAddr() const;
	bool IsAddressResolutionEnabled() const;
	ECheckAddressResolutionResult CheckAddressResolution();

	std::shared_ptr<NetworkSocket> GetResolutionUdpSocket();

	bool IsAddressResolutionComplete() const;
	void NotifyAddressResolutionConnected();
	void CleanupResolutionSockets();
	void DisableAddressResolution();

private:
	std::shared_ptr<NetAddr> RemoteAddr = nullptr;
	EAddressResolutionState ResolutionState = EAddressResolutionState::None;

	std::array<std::shared_ptr<NetworkSocket>, 2> BindSockets;

	std::shared_ptr<NetworkSocket> ResolutionUdpSocket = nullptr;

	std::vector <std::shared_ptr<NetAddr>> ResolverResults;

	int32_t CurrentAddressIndex = 0;
};


