#include "GameNetConnection.h"
#include "NetDriver.h"
#include "DriverSetting.h"
#include "WindowsInternetAddress.h"
#include "NetChannel.h"
#include "PacketPipeline.h"
#include "NetPacketReader.h"
#include "JobSystem.h"
#include "PacketEvent.h"


GameNetConnection::GameNetConnection()
	: Resolver(std::make_unique<GameNetConnectionAddressResolution>())
{

}

GameNetConnection::~GameNetConnection()
{
}

void GameNetConnection::InitLocalConnection(std::shared_ptr<NetDriver> InSupervisor, std::shared_ptr<NetworkSocket> InSocket, EConnectionState InState, int32_t InMaxPacket, int32_t InPacketOverhead, uint32_t PlayerId)
{
	InitBase(InSupervisor, InSocket, InState, (InMaxPacket == 0 || InMaxPacket > MAX_PACKET_SIZE) ? MAX_PACKET_SIZE : InMaxPacket, InPacketOverhead == 0 ? UDP_HEADER_SIZE : InPacketOverhead);

	UniqueConnectionId = PlayerId;

	bool bResolverInit = Resolver->InitLocalConnection(InSupervisor->GetSetting()->GetRemoteAddr());

	if (!bResolverInit)
	{
		return;
	}

	RemoteAddr = std::make_shared<WindowsAddr>(EInternetProtocolPlags::IPv4);
	bool bIsValid = true;
	RemoteAddr->SetIp(Resolver->GetRemoteAddr()->ToString(true), bIsValid);
}

void GameNetConnection::InitRemoteConnection(std::shared_ptr<NetDriver> InDriver, std::shared_ptr<NetworkSocket> InSocket, const NetAddr& InRemoteAddr, EConnectionState InState, int32_t InMaxPacket, int32_t InPacketOverhead, uint32_t PlayerId)
{
	Resolver->DisableAddressResolution();

	InitBase(InDriver, InSocket, InState,
		(InMaxPacket == 0 || InMaxPacket > MAX_PACKET_SIZE) ? MAX_PACKET_SIZE : InMaxPacket,
		InPacketOverhead == 0 ? UDP_HEADER_SIZE : InPacketOverhead);

	RemoteAddr = InRemoteAddr.Clone();
	UniqueConnectionId = PlayerId;
}

void GameNetConnection::InitBase(std::shared_ptr<NetDriver> InDriver, std::shared_ptr<NetworkSocket> InSocket, EConnectionState InState, int32_t InMaxPacket, int32_t InPacketOverhead)
{
	NetConnection::InitBase(InDriver, InSocket, InState,
		(InMaxPacket == 0 || InMaxPacket > MAX_PACKET_SIZE) ? MAX_PACKET_SIZE : InMaxPacket, InPacketOverhead == 0 ? UDP_HEADER_SIZE : InPacketOverhead);


	if (Resolver != nullptr)
	{
		UdpSocketPrivate = InSocket;
	}
}

void GameNetConnection::ReceivedRawPacket(std::shared_ptr<RecvPacketReader> Reader)
{
	std::weak_ptr<GameNetConnection> selfWeak = shared_from_this();

	auto ConnTask = [selfWeak, Reader]()
		{
			if (auto selfRef = selfWeak.lock())
			{
				selfRef->GetHandler()->Incoming(Reader);

				if (Reader->IsConnectionlessPacket())
				{
					return;
				}

				selfRef->ReceivedPacket(Reader);
			}
		};

	Network::Thread::Job::JobSystem::AddAsyncJob(ConnTask, Network::Thread::Job::thread_index_type{ static_cast<int64_t>(GetOwnerThreadId()) });
}

void GameNetConnection::TickDispatch()
{
	std::weak_ptr<GameNetConnection> selfWeak = shared_from_this();

	auto ConnTask = [selfWeak]()
		{
			if (auto selfRef = selfWeak.lock())
			{
				selfRef->DispatchPendingPacket();

				for (const auto& Ch : selfRef->GetChannels())
				{
					if (Ch)
					{
						Ch->Tick();
					}
				}

				selfRef->AssembleOutgoingPackets();
			}
		};

	Network::Thread::Job::JobSystem::AddAsyncJob(ConnTask, Network::Thread::Job::thread_index_type{ static_cast<int64_t>(GetOwnerThreadId()) });
}

void GameNetConnection::TickFlush()
{
	std::weak_ptr<GameNetConnection> selfWeak = shared_from_this();

	auto ConnTask = [selfWeak]()
		{
			if (auto selfRef = selfWeak.lock())
			{
				std::shared_ptr<OutReadyPacket> ReadyPacket;
				while (selfRef->TryDequeuePacket(ReadyPacket))
				{
					//step1. 헤더 갱신
					selfRef->UpdatePacketHeader(ReadyPacket);

					//step2. SendPacket 생성
					auto SendPacket = OutSendPacketEvent::Create(ReadyPacket);

					//step3. Remote로 송신
					selfRef->GetDriver()->SendToRemote(selfRef->GetRemoteAddress(), SendPacket, false);

					// 실제 와이어 송신이 방금 일어남 -- keepalive 시계를
					// 리셋(AssembleOutgoingPackets()가 이걸 체크).
					selfRef->LastSendTime = Network::Clock::Now();
				}

				selfRef->EmitTelemetryGaugeOwningThread();
			}
		};

	// 고정 -- NetConnection.h의 GetOwnerThreadId() 노트 참고.
	Network::Thread::Job::JobSystem::AddAsyncJob(ConnTask, Network::Thread::Job::thread_index_type{ static_cast<int64_t>(GetOwnerThreadId()) });
}

std::shared_ptr<NetChannel> GameNetConnection::CreateChannelByName(const std::string& ChName, int32_t ChannelIndex)
{
	std::shared_ptr<NetChannel> Channel = GetDriver()->GetOrCreateChannelByName(ChName);

	if (Channel == nullptr)
	{
		return nullptr;
	}

	Channel->Init(shared_from_this(), ChannelIndex, InitInReliable, InitOutReliable);

	Channels[ChannelIndex] = Channel;

	return Channel;
}

void GameNetConnection::SetUdpSocket_Local(const std::shared_ptr<NetworkSocket>& InSocket)
{
	UdpSocketPrivate = InSocket;
}

std::shared_ptr<NetworkSocket> GameNetConnection::GetUdpSocket() const
{
	return UdpSocketPrivate;
}

bool GameNetConnection::SendNetMessage(NetMessage& Message, uint32_t ChannelId)
{
	for (const auto& Channel : GetChannels())
	{
		if (Channel && Channel->bOpen)
		{
			if (Channel->GetChannelID() == ChannelId)
			{
				//step2. 등록한다.
				Channel->RegisterMessage(Message);
				return true;
			}
		}
	}

	return false;
}

