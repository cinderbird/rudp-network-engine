#pragma once
#include "pch.h"

#include "NetConnection.h"
#include "NetConnectionResolution.h"
#include "NetBunch.h"

class NetDriver;
class NetworkSocket;
class GameNetConnectionAddressResolution;

class GameNetConnection : public NetConnection, public std::enable_shared_from_this<GameNetConnection>
{
	friend class GameNetDriverResolution;

public:
	GameNetConnection();
	virtual ~GameNetConnection();

	virtual void InitLocalConnection(std::shared_ptr<NetDriver> InSupervisor, std::shared_ptr<NetworkSocket> InSocket, EConnectionState InState, int32_t InMaxPacket = 0, int32_t InPacketOverhead = 0, uint32_t PlayerId = 0) override;
	virtual void InitRemoteConnection(std::shared_ptr<NetDriver> InDriver, std::shared_ptr<NetworkSocket> InSocket, const NetAddr& InRemoteAddr, EConnectionState InState, int32_t InMaxPacket = 0, int32_t InPacketOverhead = 0, uint32_t PlayerId = 0) override;
	virtual void InitBase(std::shared_ptr<NetDriver> InDriver, std::shared_ptr<NetworkSocket> InSocket, EConnectionState InState, int32_t InMaxPacket = 0, int32_t InPacketOverhead = 0) override;

	virtual void ReceivedRawPacket(std::shared_ptr<RecvPacketReader> Reader) override;
	virtual void TickDispatch() override;
	virtual void TickFlush() override;

	virtual std::shared_ptr<NetChannel> CreateChannelByName(const std::string& ChName, int32_t ChIndex = -1/*INDEX_NONE*/) override;

	void SetUdpSocket_Local(const std::shared_ptr<NetworkSocket>& InSocket);

	std::shared_ptr<NetworkSocket> GetUdpSocket() const;

	virtual bool SendNetMessage(NetMessage& Message, uint32_t ChannelId) override;

private:
	std::unique_ptr<GameNetConnectionAddressResolution> Resolver;

	std::shared_ptr<NetworkSocket> UdpSocketPrivate;
};


