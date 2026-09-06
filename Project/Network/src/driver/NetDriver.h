#pragma once
#include "pch.h"
#include "WindowsPlatformTime.h"
#include "ChannelDefinition.h"

class NetAddr;
class PendingRemoteConnection;
class RecvPacketReader;
class DriverSetting;
class NetConnection;
class NetworkSocket;

class UdpConnectionProcessor;
class NetChannel;

struct ChannelDefinition;
class PacketPipeline;
class NetworkNotify;

class PacketEvent;
class OutSendPacketEvent;
class ProtocolHandlerManager;

class InRecvPacketEvent;

class NetDriver
{
public:
	//https://puzpuzpuz.dev/seqlock-based-atomic-memory-snapshots?utm_source=chatgpt.com
	struct TimeSnapshot
	{
		uint32_t lastDeltaMs;
		uint64_t elapsedUs;
		Network::Clock::TimePoint lastTick;
	};

public: 
	NetDriver();
	virtual ~NetDriver();

	virtual bool InitBase(std::shared_ptr<DriverSetting> Setting, std::string& Error);
	virtual bool InitListen(std::shared_ptr<DriverSetting> Setting, std::string& Error);
	virtual bool InitConnect(std::shared_ptr<DriverSetting> Setting, std::string& Error);

	virtual void PumpIO(uint32_t TimeoutMs);
	virtual void TickHousekeeping();

	virtual void WakeIO();
	virtual void IOCompletionEvent(PacketEvent* Handle, int NumOfBytes) = 0;
	virtual void RecvCompletionEvent(PacketEvent* Event, int NumOfBytes) = 0;
	virtual void SendCompletionEvent(PacketEvent* Event) = 0;

	virtual void SendToRemote(std::shared_ptr<const NetAddr> RemoteAddress, OutSendPacketEvent* Packet, bool Handshakepacket) = 0;
	virtual void SendToConnection(std::shared_ptr<NetConnection> RemoteConnection, OutSendPacketEvent* Packet, bool Handshakepacket) = 0;

	virtual void TickDispatch();
	virtual void TickFlush() = 0;

	virtual void AddClient(uint64_t Key, std::shared_ptr<PendingRemoteConnection> PendingConnection) = 0;
	virtual void ProcessConnectionlessPacket(std::shared_ptr<RecvPacketReader> PacketReader) = 0;
	
	void PreRecvEvent();
	void PostRecvEvent(InRecvPacketEvent* Event);

	virtual std::shared_ptr<NetworkSocket> GetSocket() = 0;
	virtual void InitConnectionlessHandler();

	virtual void AddClientConnection(std::shared_ptr<NetConnection> NewConnection);

	virtual void RemoveConnection(NetConnection* Conn);

	void CheckConnectionTimeouts();


	virtual NetChannel* InternalCreateChannelByName(const std::string& ChName);
	void CreateInitialChannels();
	void CreateInitialChannels(std::shared_ptr<NetConnection> Connection) const;

	std::shared_ptr<NetChannel> GetOrCreateChannelByName(const std::string& ChName);
	FORCEINLINE bool IsKnownChannelName(const std::string& ChName) const;


	virtual bool SendRawNetMessage(BYTE* Buffer, uint32_t Size, uint8_t ChannelId, uint32_t PlayerId, ReleaseMessageBufferCallback Callback, void* Context) = 0;
	virtual std::shared_ptr<NetConnection> FindConnection(uint32_t PlayerId) = 0;

	TimeSnapshot GetTimeSnapshot() const noexcept;
	uint32_t GetLastDeltaTimeMs() const noexcept;
	uint64_t GetElapsedTimeUs()  const noexcept;
	double   GetElapsedTimeSec() const noexcept;

	[[nodiscard]] uint64_t MsSince(Network::Clock::TimePoint tp) const noexcept; //기준 시각으로부터 지난 시간(ms/us)

protected:
	std::shared_ptr<UdpConnectionProcessor>		UdpProcessor; //READ-ONLY

	mutable std::shared_mutex ConnectionsMutex;
	std::vector<std::shared_ptr<NetConnection>> RemoteConnections;

	std::atomic<uint64_t> StatDatagramsReceived{ 0 };
	std::atomic<uint64_t> StatDatagramsSent{ 0 };
	std::atomic<uint64_t> StatBytesReceived{ 0 };
	std::atomic<uint64_t> StatBytesSent{ 0 };
	std::atomic<uint64_t> StatIoCompletions{ 0 };
	std::atomic<uint64_t> StatIoTimeouts{ 0 };

	void EmitStatsSnapshot();

private:
	Network::Clock::TimePoint LastStatsEmitTime{};
	uint64_t LastStatDatagramsReceived{ 0 };
	uint64_t LastStatDatagramsSent{ 0 };
	uint64_t LastStatBytesReceived{ 0 };
	uint64_t LastStatBytesSent{ 0 };
	uint64_t LastStatIoCompletions{ 0 };
	uint64_t LastStatIoTimeouts{ 0 };
	std::vector<uint64_t> LastWorkerJobsRun;
	std::vector<uint64_t> LastWorkerCpuTimeUs;

private:
	std::vector<ChannelDefinition>	ChannelDefinitions;		//READ-ONLY
	std::map<std::string, ChannelDefinition>	ChannelDefinitionMap;	//READ-ONLY
	std::shared_ptr<DriverSetting>				Setting = nullptr;		//READ-ONLY
	std::shared_ptr<PacketPipeline>				ConnectionlessHandler;	//READ-ONLY
	std::shared_ptr<NetConnection>				ServerConnection;		//READ-ONLY
	std::shared_ptr<ProtocolHandlerManager>		HandlerManager;

	std::atomic<uint32_t>     LastDeltaTimeMs{ 0 };
	std::atomic<uint64_t>     ElapsedTimeUsCount{ 0 }; // microseconds (정수 카운트)
	Network::Clock::TimePoint LastTickTime{};
	Network::Clock::TimePoint LastCacheCleanupTime{};

	uint32_t CacheCleanupIndex{ 0 };

	mutable std::mutex DriverMutex{};

public:
	//Begin NetDriver Interface
	const std::shared_ptr<DriverSetting> GetSetting() const;
	const std::shared_ptr<NetConnection> GetServerConnection() const;
	const std::shared_ptr<UdpConnectionProcessor> GetUdpProcessor() const;
	const std::shared_ptr<PacketPipeline> GetConnectionlessHandler() const;
	const std::shared_ptr<ProtocolHandlerManager> GetHandlerManager() const;

	const std::vector<ChannelDefinition>& GetChannelDefinitions() const;
	const std::map<std::string, ChannelDefinition>& GetChannelDefinitionMap() const;
	std::vector<std::shared_ptr<NetConnection>> GetClientConnections() const;
	//~~ NetDriver Interface End
	
	//Begin Setting ~~
	const bool IsServer() const;
	const bool IsClient() const;
	const bool IsNoTimeout() const;
	const bool IsConnectionlessOnly() const;
	const uint16_t GetKeepAliveTime() const; // M10: fixed casing (was GetKeepAlivetime), only caller added this milestone
	const uint16_t GetMaxTickRate() const;
	const uint16_t GetInitialConnectTimeout() const;
	const uint16_t GetConnectionTimeout() const;
	const uint32_t GetMaxChannelsSize() const;
	//~~Setting End
};
