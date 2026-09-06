#include "NetDriver.h"
#include "ActorChannel.h"
#include "ControlChannel.h"
#include "PacketEvent.h"
#include "WindowsSocket.h"
#include "DriverSetting.h"
#include "UdpPacketProcessor.h"
#include "NetConnection.h"
#include "PacketPipeline.h"
#include "GameNetConnection.h"
#include "JobSystem.h"
#include "NetChannel.h"
#include "TelemetrySink.h"
#include <cstdlib> 

NetDriver::NetDriver()
{
	const auto now = Network::Clock::Now();
	LastTickTime = now;
	LastCacheCleanupTime = now;
	LastDeltaTimeMs = 0;
	CacheCleanupIndex = 0;

	ChannelDefinition ControlChannelDef;
	ControlChannelDef.ChannelName = "Control";
	ControlChannelDef.StaticChannelIndex = 0;

	ChannelDefinition ActorChannelDef;
	ActorChannelDef.ChannelName = "Actor";
	ActorChannelDef.StaticChannelIndex = 1;

	ChannelDefinitions.push_back(ControlChannelDef);
	ChannelDefinitions.push_back(ActorChannelDef);

	ChannelDefinitionMap.insert({ ControlChannelDef.ChannelName, ControlChannelDef });
	ChannelDefinitionMap.insert({ ActorChannelDef.ChannelName, ActorChannelDef });

}

NetDriver::~NetDriver()
{
}

void NetDriver::PreRecvEvent()
{
	for (int i = 0; i < 100; ++i)
	{
		InRecvPacketEvent* event = new InRecvPacketEvent();

		event->Clear();

		int ret = ::WSARecvFrom(
			GetSocket()->GetPlatformSocket(),
			&event->wsabuf,
			1,
			NULL,
			&event->flags,
			reinterpret_cast<sockaddr*>(&event->remoteAddr),
			&event->AddrLen,
			event,
			nullptr);

		if (ret == SOCKET_ERROR)
		{
			int err = ::WSAGetLastError();
			if (err != WSA_IO_PENDING)
			{
#ifdef _DEBUG
				
#endif
			}
		}
	}
}

void NetDriver::PostRecvEvent(InRecvPacketEvent* event)
{
	event->Clear();

	int Ret = ::WSARecvFrom(
		GetSocket()->GetPlatformSocket(),
		&event->wsabuf,
		1,
		NULL,
		&event->flags,
		reinterpret_cast<sockaddr*>(&event->remoteAddr),
		&event->AddrLen,
		event,
		nullptr);

	if (Ret == SOCKET_ERROR)
	{
		int ErrorCode = ::WSAGetLastError();
		if (ErrorCode != WSA_IO_PENDING)
		{
			NETWORK_LOG_WARN("PostRecvEvent - WSARecvFrom Error: {}", ErrorCode);
		}
	}
}

namespace
{

	uint32_t ResolveWorkerCount(uint32_t DefaultCount)
	{
#pragma warning(suppress : 4996) // getenv is fine for this narrow, test-only read
		static const char* const Override = std::getenv("NET_JOBSYSTEM_WORKERS");
		if (!Override)
		{
			return DefaultCount;
		}

		const int Parsed = std::atoi(Override);

		if (Parsed < 1 || Parsed > 64)
		{
			return DefaultCount;
		}
		return static_cast<uint32_t>(Parsed);
	}

	uint32_t ResolveIoThreadCountForTelemetry(uint32_t DefaultCount)
	{
#pragma warning(suppress : 4996) // getenv is fine for this narrow, test-only read
		static const char* const Override = std::getenv("NET_IO_THREADS");
		if (!Override)
		{
			return DefaultCount;
		}

		const int Parsed = std::atoi(Override);
		if (Parsed < 1 || Parsed > 64)
		{
			return DefaultCount;
		}
		return static_cast<uint32_t>(Parsed);
	}
}

bool NetDriver::InitBase(std::shared_ptr<DriverSetting> InSetting, std::string& Error)
{
	Setting = InSetting;


	Network::Thread::Job::JobSystem::Init(ResolveWorkerCount(GetSetting()->IsClient() ? 1 : 8));

	if (GetSetting()->IsClient())
	{
		ServerConnection = std::make_shared<GameNetConnection>();
		{
			std::unique_lock Lock(ConnectionsMutex);
			RemoteConnections.push_back(ServerConnection);
		}

	}

	ConnectionlessHandler = std::make_shared<PacketPipeline>((GetSetting()->IsServer()) ? EProcessorMode::Server : EProcessorMode::Client);
	
	return true;
}

bool NetDriver::InitListen(std::shared_ptr<DriverSetting> Setting, std::string& Error)
{
	return true;
}

bool NetDriver::InitConnect(std::shared_ptr<DriverSetting> Setting, std::string& Error)
{
	return false;
}

void NetDriver::InitConnectionlessHandler()
{

}

NetDriver::TimeSnapshot NetDriver::GetTimeSnapshot() const noexcept
{
	std::lock_guard<std::mutex> lock(DriverMutex);
	return TimeSnapshot
	{
		LastDeltaTimeMs.load(std::memory_order_relaxed),
		ElapsedTimeUsCount.load(std::memory_order_relaxed),
		LastTickTime
	};
}

uint32_t NetDriver::GetLastDeltaTimeMs() const noexcept
{
	return LastDeltaTimeMs.load(std::memory_order_relaxed);
}
uint64_t NetDriver::GetElapsedTimeUs() const noexcept
{
	return ElapsedTimeUsCount.load(std::memory_order_relaxed);
}
double NetDriver::GetElapsedTimeSec() const noexcept
{
	return ElapsedTimeUsCount.load(std::memory_order_relaxed) * 1e-6;
}

uint64_t NetDriver::MsSince(Network::Clock::TimePoint tp) const noexcept
{
	return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Network::Clock::Now() - tp).count());
}

void NetDriver::PumpIO(uint32_t /*TimeoutMs*/)
{
	// 베이스 드라이버는 자체 소켓이 없음 -- IpNetDriver가 오버라이드.
}

void NetDriver::WakeIO()
{
	// 베이스 드라이버는 완료 포트가 없음 -- IpNetDriver가 오버라이드.
}

void NetDriver::TickHousekeeping()
{
	const auto now = Network::Clock::Now();

	std::lock_guard<std::mutex> lock(DriverMutex);

	const auto delta = now - LastTickTime;
	const auto deltaUs = std::chrono::duration_cast<std::chrono::microseconds>(delta).count();
	const auto deltaMs = std::chrono::duration_cast<std::chrono::milliseconds>(delta).count();

	LastDeltaTimeMs.store(static_cast<uint32_t>(deltaMs), std::memory_order_relaxed);
	ElapsedTimeUsCount.fetch_add(static_cast<uint64_t>(deltaUs), std::memory_order_relaxed);

	LastTickTime = now;

	using namespace std::chrono_literals;
	if (now - LastCacheCleanupTime >= 1s)
	{
		LastCacheCleanupTime = now;
		CacheCleanupIndex = (CacheCleanupIndex + 1) % 60;
		if (UdpProcessor)
		{
			//
			UdpProcessor->ConnectionsUpdate(CacheCleanupIndex);

			spdlog::flush_on(spdlog::level::trace);
		}

		// 성립된 커넥션의 생존성 스캔, 위 기존 pending 커넥션 캐시 정리와
		// 함께 초당 1회.
		CheckConnectionTimeouts();

		EmitStatsSnapshot();
	}
}

void NetDriver::EmitStatsSnapshot()
{

	if (!Network::Telemetry::TelemetrySink::Get().IsEnabled())
	{
		return;
	}

	const auto Now = Network::Clock::Now();


	if (LastStatsEmitTime == Network::Clock::TimePoint{})
	{
		LastStatsEmitTime = Now;
		LastStatDatagramsReceived = StatDatagramsReceived.load(std::memory_order_relaxed);
		LastStatDatagramsSent = StatDatagramsSent.load(std::memory_order_relaxed);
		LastStatBytesReceived = StatBytesReceived.load(std::memory_order_relaxed);
		LastStatBytesSent = StatBytesSent.load(std::memory_order_relaxed);
		LastStatIoCompletions = StatIoCompletions.load(std::memory_order_relaxed);
		LastStatIoTimeouts = StatIoTimeouts.load(std::memory_order_relaxed);
		return;
	}

	const double ElapsedSec = std::chrono::duration<double>(Now - LastStatsEmitTime).count();
	if (ElapsedSec <= 0.0)
	{
		return;
	}
	LastStatsEmitTime = Now;

	auto RateOf = [ElapsedSec](uint64_t Current, uint64_t& Previous) -> double
		{
			const uint64_t Delta = Current - Previous;
			Previous = Current;
			return static_cast<double>(Delta) / ElapsedSec;
		};

	const double RecvRate = RateOf(StatDatagramsReceived.load(std::memory_order_relaxed), LastStatDatagramsReceived);
	const double SentRate = RateOf(StatDatagramsSent.load(std::memory_order_relaxed), LastStatDatagramsSent);
	const double RecvByteRate = RateOf(StatBytesReceived.load(std::memory_order_relaxed), LastStatBytesReceived);
	const double SentByteRate = RateOf(StatBytesSent.load(std::memory_order_relaxed), LastStatBytesSent);
	const double CompletionRate = RateOf(StatIoCompletions.load(std::memory_order_relaxed), LastStatIoCompletions);
	const double TimeoutRate = RateOf(StatIoTimeouts.load(std::memory_order_relaxed), LastStatIoTimeouts);

	size_t ConnectionCount = 0;
	{
		std::shared_lock Lock(ConnectionsMutex);
		ConnectionCount = RemoteConnections.size();
	}

	const auto WorkerStats = Network::Thread::Job::JobSystem::SnapshotWorkerStats();
	LastWorkerJobsRun.resize(WorkerStats.size(), 0);
	LastWorkerCpuTimeUs.resize(WorkerStats.size(), 0);

	nlohmann::json Workers = nlohmann::json::array();
	double TotalJobRate = 0.0;
	for (size_t Index = 0; Index < WorkerStats.size(); ++Index)
	{
		const uint64_t JobsDelta = WorkerStats[Index].jobs_run - LastWorkerJobsRun[Index];
		const uint64_t CpuDelta = WorkerStats[Index].cpu_time_us - LastWorkerCpuTimeUs[Index];
		LastWorkerJobsRun[Index] = WorkerStats[Index].jobs_run;
		LastWorkerCpuTimeUs[Index] = WorkerStats[Index].cpu_time_us;

		const double JobRate = static_cast<double>(JobsDelta) / ElapsedSec;
		TotalJobRate += JobRate;

		Workers.push_back({
			{ "worker", static_cast<uint32_t>(Index) },
			{ "osThreadId", WorkerStats[Index].os_thread_id },
			{ "jobsPerSec", JobRate },
			{ "cpuCores", (static_cast<double>(CpuDelta) / 1000000.0) / ElapsedSec },
			{ "cpuTimeUs", WorkerStats[Index].cpu_time_us }
			});
	}

	NET_TELEMETRY_EMIT({
		{ "type", "stats" },
		{ "role", IsServer() ? "server" : "client" },
		{ "connections", static_cast<uint64_t>(ConnectionCount) },
		{ "tickThreadCount", 1u },
		{ "ioThreadCount", ResolveIoThreadCountForTelemetry(IsServer() ? 2u : 1u) },
		{ "datagramsRecvPerSec", RecvRate },
		{ "datagramsSentPerSec", SentRate },
		{ "bytesRecvPerSec", RecvByteRate },
		{ "bytesSentPerSec", SentByteRate },
		{ "ioCompletionsPerSec", CompletionRate },
		{ "ioTimeoutsPerSec", TimeoutRate },
		{ "jobsPerSec", TotalJobRate },
		{ "workers", Workers }
		});
}

void NetDriver::TickDispatch()
{

}

NetChannel* NetDriver::InternalCreateChannelByName(const std::string& ChName)
{
	NetChannel* NewChannel = nullptr;
	if (ChName == "Control")
	{
		NewChannel = new ControlChannel();
	}
	else if (ChName == "Actor")
	{
		NewChannel = new ActorChannel();
	}
	return NewChannel;
}

void NetDriver::CreateInitialChannels()
{
	if (ServerConnection != nullptr)
	{
		for (const ChannelDefinition& ChannelDef : GetChannelDefinitions())
		{
			ServerConnection->CreateChannelByName(ChannelDef.ChannelName, ChannelDef.StaticChannelIndex);
		}
	}
}

void NetDriver::CreateInitialChannels(std::shared_ptr<NetConnection> Connection) const
{
	if (Connection != nullptr)
	{
		for (const ChannelDefinition& ChannelDef : GetChannelDefinitions())
		{
			Connection->CreateChannelByName(ChannelDef.ChannelName, ChannelDef.StaticChannelIndex);
		}
	}
}

std::shared_ptr<NetChannel> NetDriver::GetOrCreateChannelByName(const std::string& ChName)
{
	return std::shared_ptr<NetChannel>(InternalCreateChannelByName(ChName));
}

void NetDriver::AddClientConnection(std::shared_ptr<NetConnection> NewConnection)
{
	std::unique_lock Lock(ConnectionsMutex);
	RemoteConnections.push_back(NewConnection);
}

void NetDriver::RemoveConnection(NetConnection* Conn)
{
	std::unique_lock Lock(ConnectionsMutex);
	std::erase_if(RemoteConnections, [Conn](const std::shared_ptr<NetConnection>& Existing)
		{
			return Existing.get() == Conn;
		});
}

void NetDriver::CheckConnectionTimeouts()
{
	if (IsNoTimeout())
	{
		return;
	}

	const auto Now = Network::Clock::Now();
	for (const auto& Conn : GetClientConnections())
	{
		if (!Conn || Conn->GetConnectionState() == EConnectionState::USOCK_Closed)
		{
			continue;
		}

		const auto ElapsedSec = std::chrono::duration<float>(Now - Conn->LastReceiveTime.load(std::memory_order_acquire)).count();
		if (ElapsedSec > Conn->GetTimeoutValue())
		{
			Conn->Close("Timeout");
		}
	}
}

bool NetDriver::IsKnownChannelName(const std::string& ChName) const
{
	const auto& Channel = GetChannelDefinitionMap().find(ChName);
	if (Channel != GetChannelDefinitionMap().end())
		return true;
	else
		return false;
}

//READ-ONLY
const std::shared_ptr<DriverSetting> NetDriver::GetSetting() const { return Setting; } 
const std::shared_ptr<NetConnection> NetDriver::GetServerConnection() const { if (GetSetting()->IsClient()) return ServerConnection; else return nullptr; } 
const std::shared_ptr<UdpConnectionProcessor> NetDriver::GetUdpProcessor() const { return UdpProcessor; }
const std::shared_ptr<PacketPipeline> NetDriver::GetConnectionlessHandler() const { return ConnectionlessHandler; }
const std::vector<ChannelDefinition>& NetDriver::GetChannelDefinitions() const { return ChannelDefinitions; }
const std::map<std::string, ChannelDefinition>& NetDriver::GetChannelDefinitionMap() const { return ChannelDefinitionMap; }
std::vector<std::shared_ptr<NetConnection>> NetDriver::GetClientConnections() const { std::shared_lock Lock(ConnectionsMutex); return RemoteConnections; }
const bool NetDriver::IsServer() const { return GetSetting()->IsServer(); }
const bool NetDriver::IsClient() const { return GetSetting()->IsClient(); }
const bool NetDriver::IsNoTimeout() const { return GetSetting()->bNoTimeouts; }
const bool NetDriver::IsConnectionlessOnly() const { return GetSetting()->bConnectionlessOnly; }
const uint16_t NetDriver::GetKeepAliveTime() const { return GetSetting()->KeepAliveTime; }
const uint16_t NetDriver::GetMaxTickRate() const { return GetSetting()->MaxTickRate; }
const uint16_t NetDriver::GetInitialConnectTimeout() const { return GetSetting()->InitialConnectTimeout; }
const uint16_t NetDriver::GetConnectionTimeout() const { return GetSetting()->ConnectionTimeout; }
const uint32_t NetDriver::GetMaxChannelsSize() const { return GetSetting()->MaxChannelsSize; }