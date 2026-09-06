#include "IpNetDriver.h"
#include "SocketBuilder.h"
#include "ControlProtocolHandler.h"
#include "GameNetConnection.h"
#include "PacketEvent.h"
#include "DriverSetting.h"
#include "PacketPipeline.h"
#include "NetPacketReader.h"
#include "PendingNetConnection.h"
#include "UdpPacketProcessor.h"
#include "JobSystem.h"
#include "TelemetrySink.h"

namespace
{
	void AssignConnectionThreadIndex(const std::shared_ptr<NetConnection>& Connection)
	{
		static std::atomic<uint32_t> NextThreadIndex{ 0 };
		const uint32_t ThreadCount = Network::Thread::Job::JobSystem::GetThreadCount();
		const uint32_t Assigned = (ThreadCount == 0) ? 0 : (NextThreadIndex.fetch_add(1, std::memory_order_relaxed) % ThreadCount);
		Connection->SetOwnerThreadId(Assigned);
	}
}

class NetworkConnectionHelper
{
private:
	friend class IpNetDriver;

	static void SetUdpSocket_Local(GameNetConnection* Connection, const std::shared_ptr<NetworkSocket>& InSocket)
	{
		Connection->SetUdpSocket_Local(InSocket);
	}
};

void IpNetDriver::SendToRemote(std::shared_ptr<const NetAddr> RemoteAddress, OutSendPacketEvent* Packet, bool Handshakepacket)
{
	if (OutboundSimulator.ShouldDropOutbound())
	{
		CORE::Delete(Packet);
		return;
	}

	sockaddr_storage ss{};
	RemoteAddress->GetIp(ss);
	int addrLen = (ss.ss_family == AF_INET6) ? sizeof(sockaddr_in6) : sizeof(sockaddr_in);

	auto& Buffer = Packet->GetwsaBufs();
	DWORD flags = 0, sent = 0;

	{
		uint64_t QueuedBytes = 0;
		for (const auto& Buf : Buffer)
		{
			QueuedBytes += Buf.len;
		}
		StatDatagramsSent.fetch_add(1, std::memory_order_relaxed);
		StatBytesSent.fetch_add(QueuedBytes, std::memory_order_relaxed);
	}

	int Code = ::WSASendTo(GetSocket()->GetPlatformSocket(), Buffer.data(), static_cast<DWORD>(Buffer.size()), &sent, flags, reinterpret_cast<sockaddr*>(&ss), addrLen, Packet, nullptr);
	if (Code == SOCKET_ERROR)
	{
		const int errCode = ::WSAGetLastError();
		if (errCode == WSA_IO_PENDING)
		{
			return;
		}
		else
		{
			CORE::Delete(Packet);
			return;
		}
	}
}

void IpNetDriver::SendToConnection(std::shared_ptr<NetConnection> RemoteConnection, OutSendPacketEvent* Packet, bool Handshakepacket)
{
	//step2. 송신
	SendToRemote(RemoteConnection->GetRemoteAddress(), Packet, Handshakepacket);
}

void IpNetDriver::SendCompletionEvent(PacketEvent* Event)
{
	CORE::Delete(Event);
}

void IpNetDriver::TickFlush()
{
	for (const auto& Conn : GetClientConnections())
	{
		Conn->TickFlush();
	}
}

IpNetDriver::IpNetDriver()
	: NetDriver()
	, Resolver(std::make_unique<GameNetDriverResolution>())
	, DriverHandle(::CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0))
	, PlayerUniqueIdAllocator(IDAllocator())
{
}

IpNetDriver::~IpNetDriver()
{
	if (bWSAStarted)
	{
		WSACleanup();
		bWSAStarted = false;
	}
}

void IpNetDriver::ProcessConnectionlessPacket(std::shared_ptr<RecvPacketReader> PacketReader)
{
	const auto& ConnectionlessHandler = GetConnectionlessHandler();

	if (ConnectionlessHandler && GetUdpProcessor())
	{
		ConnectionlessHandler->IncomingConnectionless(PacketReader);
	}
}

bool IpNetDriver::InitBase(std::shared_ptr<DriverSetting> Setting, std::string& Error)
{
	if (!NetDriver::InitBase(Setting, Error))
	{
		return false;
	}

	if (!bWSAStarted)
	{
		WSADATA wsaData;
		int32_t wsaErr = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (wsaErr != 0)
		{
			Error = "WSAStartup failed";
			return false;
		}
		bWSAStarted = true;
	}

	const int32_t DesiredRecvSize = Setting->DesiredRecvSize;
	const int32_t DesiredSendSize = Setting->DesiredSendSize;
	const EInitBindSocketsFlags InitBindFlags = Setting->IsClient() ? EInitBindSocketsFlags::Client : EInitBindSocketsFlags::Server;

	bool bSharedSocket = true;
	CreateAndBindSocketFunc LazyUdpSocketFunc = [this](std::shared_ptr<NetAddr> BindAddr, std::string& Error) -> std::unique_ptr<WindowsSocket>
		{
			return this->CreateAndBindSocket(BindAddr, Error);
		};

	bool bInitBindSocketsSuccess = Resolver->InitBindSockets(LazyUdpSocketFunc, Error, Setting->RemoteAddr, Setting->RemotePort, Setting->IsServer());

	if (!bInitBindSocketsSuccess)
	{
		return false;
	}

	for (const std::shared_ptr<NetworkSocket>(Socket) : Resolver->BoundSockets)
	{
		if (Socket != nullptr)
		{
			SetSocketAndLocalAddress(Socket);
		}
	}

	return true;
}

bool IpNetDriver::InitListen(std::shared_ptr<DriverSetting> Setting, std::string& Error)
{
	if (!InitBase(Setting, Error))
		return false;

	Simulator.Reconfigure(Network::Simulation::PacketSimulationSettings{
		Setting->GetPacketDropPermille(),
		Setting->GetPacketDuplicatePermille(),
		Setting->GetPacketReorderWindow()
	});
	// 송신 측 대응 -- 드롭만, OutboundSimulator의 선언부 주석은 IpNetDriver.h
	// 참고.
	OutboundSimulator.Reconfigure(Network::Simulation::PacketSimulationSettings{
		Setting->GetPacketDropPermilleOutbound(),
		0,
		0
	});
	Simulator.SetTelemetryLabel("inbound");
	OutboundSimulator.SetTelemetryLabel("outbound");
	InitTelemetry(Setting);

	InitPacketPipelineHandler();

	PreRecvEvent();

	ServerLog::Init(Setting);
	return true;
}

bool IpNetDriver::InitConnect(std::shared_ptr<DriverSetting> Setting, std::string& Error)
{
	if (!InitBase(Setting, Error))
		return false;

	Simulator.Reconfigure(Network::Simulation::PacketSimulationSettings{
		Setting->GetPacketDropPermille(),
		Setting->GetPacketDuplicatePermille(),
		Setting->GetPacketReorderWindow()
	});
	// 송신 측 대응 -- 드롭만, OutboundSimulator의 선언부 주석은 IpNetDriver.h
	// 참고.
	OutboundSimulator.Reconfigure(Network::Simulation::PacketSimulationSettings{
		Setting->GetPacketDropPermilleOutbound(),
		0,
		0
	});
	Simulator.SetTelemetryLabel("inbound");
	OutboundSimulator.SetTelemetryLabel("outbound");
	InitTelemetry(Setting);

	InitConnectionlessHandler();

	uint32_t LocalClientConnectionId = 0;
	{
		sockaddr_in LocalAddr{};
		int LocalAddrLen = sizeof(LocalAddr);
		if (::getsockname(UdpSocketPrivate->GetPlatformSocket(), reinterpret_cast<sockaddr*>(&LocalAddr), &LocalAddrLen) == 0)
		{
			LocalClientConnectionId = ntohs(LocalAddr.sin_port);
		}
	}

	GetServerConnection()->InitLocalConnection(shared_from_this(), UdpSocketPrivate, USOCK_Pending, 0, 0, LocalClientConnectionId);

	Resolver->InitConnect(GetServerConnection(), GetSocket().get());

	CreateInitialChannels();

	AssignConnectionThreadIndex(GetServerConnection());

	PreRecvEvent();

	ServerLog::Init(Setting);
	return true;
}

void IpNetDriver::InitTelemetry(std::shared_ptr<DriverSetting> Setting)
{
	if (Setting->GetTelemetryPort() == 0)
	{
		return; // 비활성(기본값) -- TelemetrySink::Start()도 포트 0이면
				// 아무것도 안 하지만, 핸들러 등록 자체를 생략
	}

	std::weak_ptr<IpNetDriver> WeakSelf = shared_from_this();
	auto Current = std::make_shared<Network::Simulation::PacketSimulationSettings>(
		Network::Simulation::PacketSimulationSettings{
			0, Setting->GetPacketDuplicatePermille(), Setting->GetPacketReorderWindow()
		});

	Network::Telemetry::TelemetrySink::Get().SetControlHandler(
		[WeakSelf, Current](const nlohmann::json& Cmd)
		{
			auto Self = WeakSelf.lock();
			if (!Self)
			{
				return;
			}

			if (Cmd.value("cmd", std::string()) == "set_drop")
			{
				const uint32_t InboundPermille = Cmd.value("inbound", 0u);
				const uint32_t OutboundPermille = Cmd.value("outbound", 0u);
				Current->DuplicatePermille = Cmd.value("duplicate", Current->DuplicatePermille);
				Current->ReorderWindowSize = Cmd.value("reorderWindow", Current->ReorderWindowSize);

				Self->Simulator.Reconfigure(Network::Simulation::PacketSimulationSettings{
					InboundPermille, Current->DuplicatePermille, Current->ReorderWindowSize
				});

				Self->OutboundSimulator.Reconfigure(Network::Simulation::PacketSimulationSettings{
					OutboundPermille, 0, 0
				});

				NETWORK_LOG_INFO("[Telemetry] set_drop -- inbound={} outbound={} duplicate={} reorderWindow={}",
					InboundPermille, OutboundPermille, Current->DuplicatePermille, Current->ReorderWindowSize);
			}
		});

	Network::Telemetry::TelemetrySink::Get().Start(Setting->GetTelemetryPort());
}

void IpNetDriver::InitConnectionlessHandler()
{
	std::shared_ptr<PacketProcessor> NewProcessor = GetConnectionlessHandler()->AddProcessor(EProcessorType::Udp, true);

	if (NewProcessor)
	{
		UdpProcessor = std::static_pointer_cast<UdpConnectionProcessor>(NewProcessor);
		if (UdpProcessor)
		{
			UdpProcessor->SetSupervisor(shared_from_this());
		}
	}
}

/*
	reinterpret_cast
	https://learn.microsoft.com/en-us/cpp/cpp/reinterpret-cast-operator?view=msvc-170

	static_cast
	https://learn.microsoft.com/en-us/cpp/cpp/static-cast-operator?view=msvc-170

*/
void IpNetDriver::TickHousekeeping()
{
	NetDriver::TickHousekeeping();


	for (auto& Stale : Simulator.FlushStale())
	{
		DispatchReceivedDatagram(std::move(Stale));
	}
}

void IpNetDriver::WakeIO()
{
	::PostQueuedCompletionStatus(DriverHandle, 0, 0, nullptr);
}

void IpNetDriver::PumpIO(uint32_t TimeoutMs)
{
	constexpr ULONG MaxEntries = 64;
	OVERLAPPED_ENTRY Entries[MaxEntries]{};
	ULONG RemovedCount = 0;

	// fAlertable = FALSE: 이 스레드는 처리할 APC가 없다.
	if (::GetQueuedCompletionStatusEx(DriverHandle, Entries, MaxEntries, &RemovedCount, TimeoutMs, FALSE))
	{
		StatIoCompletions.fetch_add(RemovedCount, std::memory_order_relaxed);

		for (ULONG Index = 0; Index < RemovedCount; ++Index)
		{
			if (PacketEvent* IOHandle = static_cast<PacketEvent*>(Entries[Index].lpOverlapped))
			{
				IOCompletionEvent(IOHandle, static_cast<int>(Entries[Index].dwNumberOfBytesTransferred));
			}
		}
	}
	else
	{
		int32_t Code = ::GetLastError();
		switch (Code)
		{
		case WAIT_TIMEOUT:
		{
			StatIoTimeouts.fetch_add(1, std::memory_order_relaxed);
			return;
		}

		default:
		{
			NETWORK_LOG_WARN("GetQueuedCompletionStatusEx Error: {}", Code);
			return;
		}
		}
	}
}

void IpNetDriver::IOCompletionEvent(PacketEvent* Handle, int NumOfBytes)
{
	switch (Handle->Type)
	{
	case EOverlappedType::Send:
	{
		SendCompletionEvent(Handle);
		break;
	}
	case EOverlappedType::Recv:
	{
		RecvCompletionEvent(Handle, NumOfBytes);
		break;
	}
	default:
	{
		break;
	}
	}
}

void IpNetDriver::RecvCompletionEvent(PacketEvent* Event, int NumOfBytes)
{
	InRecvPacketEvent* RecvPacket = static_cast<InRecvPacketEvent*>(Event);

	StatDatagramsReceived.fetch_add(1, std::memory_order_relaxed);
	StatBytesReceived.fetch_add(static_cast<uint64_t>(NumOfBytes < 0 ? 0 : NumOfBytes), std::memory_order_relaxed);

	std::shared_ptr<InRecvPacketEvent> eventOwner(RecvPacket, RecvEventPooleturner{ this });

	//step1. Reader 준비
	std::shared_ptr<RecvPacketReader> Reader = CORE::TMakeShared<RecvPacketReader>(eventOwner, NumOfBytes);

	// 수신 측 드롭/중복/재정렬 시뮬레이션. 비활성화(전부 0인 설정)면 그냥
	// 통과({Reader}를 즉시 반환).
	for (auto& ToDispatch : Simulator.Process(std::move(Reader)))
	{
		DispatchReceivedDatagram(std::move(ToDispatch));
	}
}

void IpNetDriver::DispatchReceivedDatagram(std::shared_ptr<RecvPacketReader> Reader)
{
	//step2. IO 정보를 통해 Connection 여부 확인
	const auto& MyServerConnection = GetServerConnection();
	if (IsClient() && MyServerConnection && MyServerConnection->GetRemoteAddress()->CompareEndpoints(Reader->GetAddress()))
	{
		MyServerConnection->ReceivedRawPacket(Reader);
	}
	else if (IsServer())
	{
		NetConnection* Connection = nullptr;
		{
			std::shared_ptr<NetConnection> Result;
			{
				std::shared_lock Lock(ConnectionsMutex);
				auto CachedConnectionPair = HashToConnectionMap.find(Reader->GetHashKey());
				if (CachedConnectionPair != HashToConnectionMap.end())
				{
					Result = CachedConnectionPair->second;
				}
			}

			if (Result != nullptr)
			{
				NetConnection* ConnVal = Result.get();

				if (ConnVal != nullptr)
				{
					Connection = ConnVal;
				}
				else
				{
					Reader->SetFromRecentlyDisconnected(true);
				}
			}
		}

		bool bIgnorePacket = false;

		//step3. Connecitonless Client: 3-way handshake 수행
		if (Connection == nullptr)
		{
			ProcessConnectionlessPacket(Reader);
		}

		//step4. Connection Client: Message Processing 수행
		else if (Connection != nullptr && !bIgnorePacket)
		{
			Connection->ReceivedRawPacket(Reader);
		}
	}
}

void IpNetDriver::TickDispatch()
{
	// 락 아래서 스냅샷 뜨고, 락 없이 디스패치 -- 커넥션마다 TickDispatch()는
	// Job을 enqueue만 하므로, 이렇게 하면 락을 최소한만 들고 있는다.
	for (const auto& Conn : GetClientConnections())
	{
		Conn->TickDispatch();
	}
}

void IpNetDriver::SetSocketAndLocalAddress(const std::shared_ptr<NetworkSocket>& SharedSocket)
{
	SetUdpSocket_Internal(SharedSocket);

	if (UdpSocketPrivate.get() != nullptr)
	{
		LocalAddr = std::make_shared<WindowsAddr>();
		UdpSocketPrivate->GetAddress(*LocalAddr);
	}
}

bool IpNetDriver::SendRawNetMessage(BYTE* Buffer, uint32_t Size, uint8_t ChannelId, uint32_t PlayerId, ReleaseMessageBufferCallback Callback, void* Context)
{
	//step1. Protobuf의 Buffer 래핑
	NetMessage Message = { Buffer, Size, Callback, Context };

	//step2. PlayerID를 Key값으로 Conneciton을 찾는다.
	std::shared_ptr<NetConnection> Conn;
	if (IsServer())
	{
		Conn = FindConnection(PlayerId);
	}
	else
	{
		Conn = GetServerConnection();
	}

	if (Conn == nullptr)
	{
		return false;
	}
	//step3. Message 전송
	return Conn->SendNetMessage(Message, ChannelId);
}

std::shared_ptr<NetConnection> IpNetDriver::FindConnection(uint32_t PlayerId)
{
	std::shared_lock Lock(ConnectionsMutex);
	auto It = ConnectionMap.find(PlayerId);
	if (It != ConnectionMap.end())
	{
		return It->second;
	}
	return nullptr;
}

const std::shared_ptr<ProtocolHandlerManager> NetDriver::GetHandlerManager() const
{
	return HandlerManager;
}

std::shared_ptr<NetworkSocket> IpNetDriver::GetSocket()
{
	GameNetConnection* IpServerConnection = static_cast<GameNetConnection*>(GetServerConnection().get());

	if (IpServerConnection != nullptr && GameNetDriverResolution::GetConnectionResolver(IpServerConnection)->IsAddressResolutionEnabled())
	{
		return IpServerConnection->GetUdpSocket();
	}

	return UdpSocketPrivate;
}

void IpNetDriver::AddClient(uint64_t Key, std::shared_ptr<PendingRemoteConnection> PendingConnection)
{
	std::shared_ptr<NetConnection> ReturnVal = std::make_shared<GameNetConnection>();
	auto NewConnectionId = PlayerUniqueIdAllocator.Allocate();

	GControlProtocolHandler->AddPlayerConnection(NewConnectionId);

	ReturnVal->InitRemoteConnection(shared_from_this(), GetUdpSocket(), *PendingConnection->GetAddress(), USOCK_Open, 0, 0, NewConnectionId);

	if (GetUdpProcessor() != nullptr)
	{
		int32_t ServerSequence = PendingConnection->GetServerSeq();
		int32_t ClientSequence = PendingConnection->GetClientSeq();

		ReturnVal->InitSequence(ClientSequence, ServerSequence);
	}

	CreateInitialChannels(ReturnVal);

	AddClient_Internal(Key, NewConnectionId, ReturnVal);

	NET_TELEMETRY_EMIT({
		{"type", "connection"}, {"event", "opened"}, {"connectionId", NewConnectionId},
		{"remotePort", PendingConnection->GetAddress()->GetPort()}
	});
}

void IpNetDriver::AddClient_Internal(uint64_t Key, uint32_t NewConnectionId, std::shared_ptr<NetConnection> Connection)
{
	{
		std::unique_lock Lock(ConnectionsMutex);

		HashToConnectionMap.insert({ Key , Connection });
		ConnectionMap.insert({ NewConnectionId, Connection }); //ConectionId : NetConnection
		RemoteConnections.push_back(Connection);
	}

	AssignConnectionThreadIndex(Connection);
}

void IpNetDriver::RemoveConnection(NetConnection* Conn)
{
	const uint32_t RemovedConnectionId = Conn->GetUniqueConnectionId();

	{

		std::unique_lock Lock(ConnectionsMutex);

		std::erase_if(HashToConnectionMap, [Conn](const auto& Entry)
			{
				return Entry.second.get() == Conn;
			});

		ConnectionMap.erase(Conn->GetUniqueConnectionId());
		std::erase_if(RemoteConnections, [Conn](const std::shared_ptr<NetConnection>& Existing)
			{
				return Existing.get() == Conn;
			});
	}

	if (GControlProtocolHandler)
	{
		GControlProtocolHandler->RemovePlayerConnection(RemovedConnectionId);
	}
}

std::unique_ptr<WindowsSocket> IpNetDriver::CreateAndBindSocket(std::shared_ptr<NetAddr> BindAddr, std::string& Error)
{
	if (GetSetting()->IsServer())
	{
		return SocketBuilder::BuildTemplate<ESocketBuildTemplate::ListenSocket>(ENetworkProtocolPlags::UDP, EInternetProtocolPlags::IPv4, BindAddr.get());
	}
	else
	{
		return SocketBuilder::BuildTemplate<ESocketBuildTemplate::ClientSocket>(ENetworkProtocolPlags::UDP, EInternetProtocolPlags::IPv4, BindAddr.get());
	}
}

const std::shared_ptr<NetworkSocket> IpNetDriver::GetUdpSocket() const
{
	return UdpSocketPrivate;
}

void IpNetDriver::SetUdpSocket_Internal(const std::shared_ptr<NetworkSocket>& InSocket)
{
	if (InSocket != UdpSocketPrivate)
	{
		std::shared_ptr<NetworkSocket> SavedSocket = std::move(UdpSocketPrivate);
		UdpSocketPrivate = InSocket;

		if (DriverHandle != nullptr && UdpSocketPrivate)
		{
			::CreateIoCompletionPort(reinterpret_cast<HANDLE>(UdpSocketPrivate->GetPlatformSocket()), DriverHandle, 0, 0);
		}

		if (NetConnection* IpServerConnection = GetServerConnection().get())
		{
			NetworkConnectionHelper::SetUdpSocket_Local(static_cast<GameNetConnection*>(IpServerConnection), InSocket);
		}
	}
}

const HANDLE IpNetDriver::GetHandle() const
{
	return DriverHandle;
}

void IpNetDriver::InitPacketPipelineHandler()
{
	if (UdpSocketPrivate.get())
		InitConnectionlessHandler();
}

void RecvEventPooleturner::operator()(InRecvPacketEvent* event) const
{
	if (Driver && event)
	{
		Driver->PostRecvEvent(event);
	}
}