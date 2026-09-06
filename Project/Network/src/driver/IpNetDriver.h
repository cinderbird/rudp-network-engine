#pragma once
#include "pch.h"
#include "NetDriver.h"
#include "GameNetDriverResolution.h"
#include "IDAllocator.h"
#include "PacketSimulator.h"


class IpNetDriver;
class NetAddr;
class DriverSetting;
class NetworkSocket;

class RecvPacketReader;
class PendingRemoteConnection;
class NetConnection;

class OutSendPacketEvent;
class PacketEvent;

static inline uint32_t RandInRange(uint32_t MinNum, uint32_t MaxNum)
{
	if (MinNum > MaxNum)
	{
		std::swap(MinNum, MaxNum);
	}

	thread_local std::mt19937 Rng19937{ std::random_device{}() };

	std::uniform_int_distribution<uint32_t> dist(MinNum, MaxNum);
	return dist(Rng19937);
};


class IpNetDriver : public NetDriver, public std::enable_shared_from_this<IpNetDriver>
{
public:
	IpNetDriver();
	virtual ~IpNetDriver();

	virtual bool InitBase(std::shared_ptr<DriverSetting> Setting, std::string& Error) override;
	virtual bool InitListen(std::shared_ptr<DriverSetting> Setting, std::string& Error) override;
	virtual bool InitConnect(std::shared_ptr<DriverSetting> Setting, std::string& Error) override;

	virtual void PumpIO(uint32_t TimeoutMs) override;
	virtual void TickHousekeeping() override;
	virtual void WakeIO() override;
	virtual void IOCompletionEvent(PacketEvent* Handle, int NumOfBytes) override;
	virtual void RecvCompletionEvent(PacketEvent* Event, int NumOfBytes) override;
	virtual void SendCompletionEvent(PacketEvent* Event) override;

	virtual void SendToRemote(std::shared_ptr<const NetAddr> RemoteAddress, OutSendPacketEvent* Packet, bool Handshakepacket) override;
	virtual void SendToConnection(std::shared_ptr<NetConnection> RemoteConnection, OutSendPacketEvent* Packet, bool Handshakepacket) override;

	virtual void TickDispatch() override;
	virtual void TickFlush() override;

	virtual std::shared_ptr<NetworkSocket> GetSocket() override;
	virtual void InitConnectionlessHandler() override;
	virtual std::unique_ptr<WindowsSocket> CreateAndBindSocket(std::shared_ptr<NetAddr> BindAddr, std::string& Error);

	virtual void ProcessConnectionlessPacket(std::shared_ptr<RecvPacketReader> PacketReader) override;
	virtual void AddClient(uint64_t Key, std::shared_ptr<PendingRemoteConnection> PendingConnection) override;
	void AddClient_Internal(uint64_t Key, uint32_t NewConnectionId, std::shared_ptr<NetConnection> Connection);

	virtual bool SendRawNetMessage(BYTE* Buffer, uint32_t Size, uint8_t ChannelId, uint32_t PlayerId, ReleaseMessageBufferCallback Callback, void* Context) override;
	virtual std::shared_ptr<NetConnection> FindConnection(uint32_t PlayerId) override;

	// HashToConnectionMap/ConnectionMap에서도 지운다(base 클래스는
	// RemoteConnections만 앎).
	virtual void RemoveConnection(NetConnection* Conn) override;

protected:
	const HANDLE GetHandle() const;
	void InitPacketPipelineHandler();

	void DispatchReceivedDatagram(std::shared_ptr<RecvPacketReader> Reader);
	const std::shared_ptr<NetworkSocket> GetUdpSocket() const;
	void SetUdpSocket_Internal(const std::shared_ptr<NetworkSocket>& InSocket);
	void SetSocketAndLocalAddress(const std::shared_ptr<NetworkSocket>& SharedSocket);

	void InitTelemetry(std::shared_ptr<DriverSetting> Setting);

private:
	std::unique_ptr<GameNetDriverResolution> Resolver; //Thread-Safe use only init state
	std::shared_ptr<NetAddr> LocalAddr; //Thread-Safe use only init state

	HANDLE DriverHandle; //Thread-safe: https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-getqueuedcompletionstatus

	std::shared_ptr<NetworkSocket> UdpSocketPrivate;


	std::unordered_map<uint64_t, std::shared_ptr<NetConnection>> HashToConnectionMap;
	std::unordered_map<uint32_t, std::shared_ptr<NetConnection>> ConnectionMap;

	IDAllocator PlayerUniqueIdAllocator;

	Network::Simulation::PacketSimulator Simulator;

	Network::Simulation::PacketSimulator OutboundSimulator;

	bool bWSAStarted = false;
};


struct RecvEventPooleturner
{
	IpNetDriver* Driver = nullptr;

	void operator()(InRecvPacketEvent* event) const;
};

