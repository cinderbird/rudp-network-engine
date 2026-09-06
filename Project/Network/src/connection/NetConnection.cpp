#include "NetConnection.h"
#include "BufferReader.h"
#include "NetBunchReader.h"
#include "PacketEvent.h"
#include "NetDriver.h"
#include "NetChannel.h"
#include "PacketPipeline.h"
#include <NetPacketBuilder.h>
#include "NetPacketReader.h"
#include "TelemetrySink.h"

class NetConnectionHelper
{
public:
	static int32_t BestSignedDifference(int32_t Value, int32_t Reference, int32_t Max)
	{
		return ((Value - Reference + Max / 2) & (Max - 1)) - Max / 2;
	}

	static int32_t MakeRelative(int32_t Value, int32_t Reference, int32_t Max)
	{
		return Reference + BestSignedDifference(Value, Reference, Max);
	}
};

NetConnection::NetConnection()
	: Driver(nullptr)
	, MaxPacket(0)
	, RemoteAddr(nullptr)
	, MaxPacketHandlerBits(0)
	, State(USOCK_Invalid)
	, Handler()
	, InPacketId(-1)
	, OutPacketId(0)
	, DefaultMaxChannelSize(32767)
	, InitOutReliable(0)
	, InitInReliable(0)
	, LastNotifiedSendPacketId(-1)
	, HasDirtyAcks(0u)
	, ConnectionId(0)
{
}

NetConnection::~NetConnection()
{
}

void NetConnection::WriteHeaderCallback(BitWriter& Buffer) noexcept
{
	//1. MagicHeader, Sessionid, ClinetId, HandShake 여부 부착
	GetHandler()->Outgoing(Buffer);

	//2. PacketNotify 부착
	bool bIsHeaderUpdate = false;
	bool bWroteHeader = PacketNotify.WriteHeader(Buffer, bIsHeaderUpdate);

}

void NetConnection::AssembleOutgoingPackets() noexcept
{
	if (!IsRequireAssembleBunch())
	{
		if (Driver != nullptr)
		{
			const auto Now = Network::Clock::Now();
			const auto ElapsedSec = std::chrono::duration<float>(Now - LastSendTime).count();
			if (ElapsedSec >= static_cast<float>(Driver->GetKeepAliveTime()))
			{
				NETWORK_LOG_TRACE("[M10][KeepAlive] sending header-only packet -- ElapsedSec={}, KeepAliveTime={}", ElapsedSec, Driver->GetKeepAliveTime());

				auto KeepAlivePacket = ConnectionPacketBuilder::BuildStart();
				KeepAlivePacket->OutPacketId = OutPacketId;
				ConnectionChannelRecord::PushPacketId(Record, OutPacketId);
				OutPacketId++;
				KeepAlivePacket->PacketSealed();
				OutPacketQueue.push_back(KeepAlivePacket);
			}
		}
		return;
	}

	auto createNewPacket = [&]() -> std::shared_ptr<OutReadyPacket>
		{
			auto newPacket = ConnectionPacketBuilder::BuildStart();
			newPacket->OutPacketId = OutPacketId;
			ConnectionChannelRecord::PushPacketId(Record, OutPacketId);
			return newPacket;
		};

	//step2. 패킷 준비
	std::shared_ptr<OutReadyPacket> Packet = nullptr;

	size_t chCount = Channels.size();
	size_t rr = 0;

	std::vector<std::shared_ptr<OutReadyPacket>> readyPackets;
	readyPackets.reserve(64);

	std::shared_ptr<OutReadyBunch> Bunch;
	bool progress = true;
	while (progress)
	{
		progress = false;
		for (size_t i = 0; i < chCount; ++i)
		{
			auto& Channel = Channels[(rr + i) % chCount];

			if (!Channel)
			{
				continue;
			}

			while (Channel->DequeueReadyBunch(Bunch))
			{
				progress = true;

				const uint32_t RequireSize = Bunch->TotalSize;

				if (ChannelBunchBuilder::IsHeaderOnlyBunch(Bunch) || RequireSize > ConnectionPacketBuilder::PacketMaxSize)
				{
					ChannelBunchBuilder::DiscardOutReadyBunch(std::move(Bunch));
					continue;
				}

				if (Packet == nullptr)
				{
					Packet = createNewPacket();
				}

				if (Packet->TotalSize + RequireSize > ConnectionPacketBuilder::PacketMaxSize)
				{
					//패킷을 전부 완성한 시점
					OutPacketId++;
					Packet->PacketSealed();
					readyPackets.push_back(Packet);

					Packet = createNewPacket();
				}

				//정확히 여기서 Bunch가 Packet에 추가되는 시점
				ConnectionChannelRecord::PushChannelRecord(Record, Packet->OutPacketId, Channel->ChIndex);

				if (!Channel->AddUnAckedOutBunch(Packet->OutPacketId, Bunch))
				{
					return;
				}
				(void)ConnectionPacketBuilder::Append(Packet, Bunch);
			}
		}

		rr = (rr + 1) % std::max<size_t>(1, chCount);
	}

	// 4) 마지막 패킷 확정
	if (Packet && Packet->Bunchs.size() != 0)
	{
		OutPacketId++;
		Packet->PacketSealed();
		readyPackets.push_back(Packet);
	}

	//5) 패킷 저장
	for (auto& Packet : readyPackets)
	{
		OutPacketQueue.push_back(Packet);
	}
}

bool NetConnection::IsRequireAssembleBunch() noexcept
{
	for (const auto& Channel : GetChannels())
	{
		if (Channel && Channel->bOpen && Channel->HasReadyBunch())
		{
			return true;
		}
	}

	return false;
}

bool NetConnection::TryDequeuePacket(std::shared_ptr<OutReadyPacket>& OutPacket)
{
	if (OutPacketQueue.empty())
	{
		return false;
	}
	else
	{
		OutPacket = OutPacketQueue.front();
		OutPacketQueue.pop_front();
	}

	return !!OutPacket;
}

void NetConnection::UpdatePacketHeader(std::shared_ptr<OutReadyPacket>& Packet)
{
	//step1. Callback 람다 준비
	auto PacketOutgoing = [this](BitWriter& Buffer)
		{
			WriteHeaderCallback(Buffer);
		};

	ConnectionPacketBuilder::BuildEnd(Packet, PacketOutgoing);

	ConnectionRttTracker::RecordSend(RttTracker, Packet->OutPacketId, Packet->SendTimeUs);

	PacketNotify.CommitAndIncrementOutSeq();

	NET_TELEMETRY_EMIT({
		{"type", "packet_send"}, {"connectionId", UniqueConnectionId},
		{"packetId", Packet->OutPacketId}, {"bytes", Packet->TotalSize}
	});
}

void NetConnection::ReceivedPacket(std::shared_ptr<RecvPacketReader> Reader)
{

	LastReceiveTime.store(Network::Clock::Now(), std::memory_order_release);

	auto& Buffer = Reader->GetHeader();
	//0 Mark
	{
		//1. NotifyHeader 읽기
		NetPacketNotify::NotificationHeader Header;
		if (!PacketNotify.ReadHeader(Header, Buffer))
		{
			return;
		}

		NETWORK_LOG_DEBUG("Local : Notification - InSeq: {}. InAckSeq: {}. OutSeq: {}, OutAckSeq: {}", PacketNotify.GetInSeq().Get(), PacketNotify.GetInAckSeq().Get(), PacketNotify.GetOutSeq().Get(), PacketNotify.GetOutAckSeq().Get());
		NETWORK_LOG_DEBUG("Remote: Notification - Seq: {}, AckedSeq: {}", Header.Seq.Get(), Header.AckedSeq.Get());

		//2. 패킷 유효성 검사 후 Delta값 반환
		const int32_t PacketSequenceDelta = PacketNotify.GetSequenceDelta(Header);
		if (PacketSequenceDelta > 0)
		{
			const int32_t MissingPacketCount = PacketSequenceDelta - 1;
			auto HandlePacketNotification = [&Header, this](NetPacketNotify::SequenceNumberT AckedSequence, bool bDelivered)
				{
					++LastNotifiedSendPacketId;

					// 정합성 체크
					if (NetPacketNotify::SequenceNumberT(LastNotifiedSendPacketId) != AckedSequence)
					{
						const bool bRecordEmpty = Record.EntryRecord.empty();
						const uint32_t frontIsSequence = bRecordEmpty ? 0 : Record.EntryRecord.front().IsSequence;
						const uint32_t frontValue = bRecordEmpty ? 0 : Record.EntryRecord.front().Value;
						NETWORK_LOG_WARN(
							"[ISSUE-1] HandlePacketNotification mismatch -- LastNotifiedSendPacketId(after++)={}, AckedSequence={}, bDelivered={}, PacketNotify.OutAckSeq={}, Record.empty={}, Record.front.IsSequence={}, Record.front.Value={}",
							LastNotifiedSendPacketId, AckedSequence.Get(), bDelivered, PacketNotify.GetOutAckSeq().Get(), bRecordEmpty, frontIsSequence, frontValue);
						return;
					}

					if (bDelivered)
					{
						RecivedAck(LastNotifiedSendPacketId);
					}
					else
					{
						RecivedNack(LastNotifiedSendPacketId);
					};
				};

			//3. Update 수행: 1. 내가 보낸 패킷 처리, 2. 내가 받은 패킷 처리
			const int32_t UpdatedPacketSequenceDelta = PacketNotify.Update(Header, HandlePacketNotification);

			//4. 대규모 패킷 로스 발생 핸들링
			if (PacketNotify.IsWaitingForSequenceHistoryFlush())
			{
				NETWORK_LOG_WARN("WARRNING!! Packet Loss is too heavy must re sink immediately!");

				++HasDirtyAcks;

				for (const auto& Channel : GetChannels())
				{
					if (Channel)
					{
						Channel->ForceRetransmitAllUnAcked();
					}
				}

				InPacketId += UpdatedPacketSequenceDelta;
				return;
			}
		}

		InPacketId += PacketSequenceDelta;

		//5. PendingDispatchPacketsQueue에 저장
		PendingDispatchPacketReaders.push(Reader);
	}
}

void NetConnection::RecivedAck(int32_t AckMarkPacketId)
{
	//Channel에게 알려줘야 한다.
	OutAckPacketId = AckMarkPacketId;

	uint32_t RttUs = 0;
	ConnectionRttTracker::ConsumeAcked(RttTracker, AckMarkPacketId, Network::Clock::NowUs(), RttUs);

	auto Acked = [this](int32_t AckedId, uint32_t ChannelIndex)
		{
			auto& Channel = Channels[ChannelIndex];

			Channel->Acked(AckedId);

			NET_TELEMETRY_EMIT({
				{"type", "ack"}, {"connectionId", UniqueConnectionId},
				{"packetId", AckedId}, {"channel", ChannelIndex}
			});
		};

	if (!ConnectionChannelRecord::ConsumeChannelRecordsForPacket(Record, AckMarkPacketId, Acked))
	{
		NETWORK_LOG_ERROR("[ISSUE-1] ConnectionChannelRecord desync on Ack -- UniqueConnectionId={} AckMarkPacketId={} Record.size={}",
			UniqueConnectionId, AckMarkPacketId, Record.EntryRecord.size());
		Close("ChannelRecordDesync");
	}
}

void NetConnection::RecivedNack(int32_t NakMarkPacketId)
{
	ConnectionRttTracker::ConsumeNacked(RttTracker, NakMarkPacketId);

	auto Nacked = [this](int32_t NackedId, uint32_t ChannelIndex)
		{
			auto& Channel = Channels[ChannelIndex];

			Channel->Nacked(NackedId);

			NET_TELEMETRY_EMIT({
				{"type", "nack"}, {"connectionId", UniqueConnectionId},
				{"packetId", NackedId}, {"channel", ChannelIndex}
			});
		};

	// 위 RecivedAck의 대응 주석 참고.
	if (!ConnectionChannelRecord::ConsumeChannelRecordsForPacket(Record, NakMarkPacketId, Nacked))
	{
		NETWORK_LOG_ERROR("[ISSUE-1] ConnectionChannelRecord desync on Nack -- UniqueConnectionId={} NakMarkPacketId={} Record.size={}",
			UniqueConnectionId, NakMarkPacketId, Record.EntryRecord.size());
		Close("ChannelRecordDesync");
	}
}

void NetConnection::DispatchPendingPacket()
{
	while (!PendingDispatchPacketReaders.empty())
	{
		auto QReader = PendingDispatchPacketReaders.front();

		//이후 Dispatch
		{
			bool bSkipAck = false;
			bool bHasBunchErrors = false;

			DispatchPacket(QReader, InPacketId, bSkipAck, bHasBunchErrors);

			if (bSkipAck)
			{
				PacketNotify.NakSeq(InPacketId);
			}
			else
			{
				PacketNotify.AckSeq(InPacketId);
			}
		}


		PendingDispatchPacketReaders.pop();
	}
}

void NetConnection::InitLocalConnection(std::shared_ptr<NetDriver> InSupervisor, std::shared_ptr<NetworkSocket> InSocket, EConnectionState InState, int32_t InMaxPacket, int32_t InPacketOverhead, uint32_t PlayerId)
{
}

void NetConnection::InitRemoteConnection(std::shared_ptr<NetDriver> InDriver, std::shared_ptr<NetworkSocket> InSocket, const NetAddr& InRemoteAddr, EConnectionState InState, int32_t InMaxPacket, int32_t InPacketOverhead, uint32_t PlayerId)
{
}

void NetConnection::InitBase(std::shared_ptr<NetDriver> InDriver, std::shared_ptr<NetworkSocket> InSocket, EConnectionState InState, int32_t InMaxPacket, int32_t InPacketOverhead)
{
	Driver = InDriver;
	InitChannelData();

	SetConnectionState(InState);

	Handler.reset();
	Handler = Driver->GetConnectionlessHandler();

	const auto Now = Network::Clock::Now();
	LastReceiveTime.store(Now, std::memory_order_release);
	LastSendTime = Now;
}



float NetConnection::GetTimeoutValue()
{
	const auto& Driver = GetDriver();

	if (Driver != nullptr)
	{
		if (Driver->IsNoTimeout())
		{
			return std::numeric_limits<float>::max();
		}

		if (GetConnectionState() == EConnectionState::USOCK_Pending)
		{
			return static_cast<float>(Driver->GetInitialConnectTimeout());
		}

		return static_cast<float>(Driver->GetConnectionTimeout());
	}

	return 0;
}

void NetConnection::EmitTelemetryGaugeOwningThread()
{

	if (!Network::Telemetry::TelemetrySink::Get().IsEnabled())
	{
		return;
	}

	using namespace std::chrono_literals;
	const auto Now = Network::Clock::Now();
	if (Now - LastTelemetryGaugeTime < 1s)
	{
		return;
	}
	LastTelemetryGaugeTime = Now;

	size_t MaxOutUnAcked = 0;
	for (const auto& Channel : Channels)
	{
		if (Channel)
		{
			MaxOutUnAcked = std::max(MaxOutUnAcked, Channel->OutUnAckedBunches.size());
		}
	}

	const ConnectionRttTracker::Stats RttStats = ConnectionRttTracker::ComputeStats(RttTracker, Network::Clock::NowUs());

	NET_TELEMETRY_EMIT({
		{"type", "gauge"},
		{"connectionId", UniqueConnectionId},
		{"outUnAckedBunches", MaxOutUnAcked},
		{"reliableBufferCap", static_cast<uint32_t>(NetChannel::RELIABLE_BUFFER)},
		{"state", static_cast<int>(GetConnectionState())},
		{"inSeq", PacketNotify.GetInSeq().Get()},
		{"outSeq", PacketNotify.GetOutSeq().Get()},
		{"inAckSeq", PacketNotify.GetInAckSeq().Get()},
		{"outAckSeq", PacketNotify.GetOutAckSeq().Get()},
		{"rttP50Us", RttStats.P50Us},
		{"rttP95Us", RttStats.P95Us},
		{"rttP99Us", RttStats.P99Us},
		{"rttMinUs", RttStats.MinUs},
		{"rttMaxUs", RttStats.MaxUs},
		{"rttSampleCount", RttStats.SampleCount}
	});
}

void NetConnection::Close(const char* Reason)
{
	if (State.exchange(EConnectionState::USOCK_Closed, std::memory_order_acq_rel) == EConnectionState::USOCK_Closed)
	{
		return; // 이미 다른 누군가가 이 커넥션을 닫았다
	}

	NETWORK_LOG_WARN("NetConnection::Close -- UniqueConnectionId={} Reason={}", UniqueConnectionId, Reason);
	NET_TELEMETRY_EMIT({
		{"type", "connection"}, {"event", "closed"}, {"connectionId", UniqueConnectionId}, {"reason", Reason}
	});

	// (상태는 위 exchange에서 이미 세팅됨)
	if (Driver != nullptr)
	{
		Driver->RemoveConnection(this);
	}
}

int32_t NetConnection::GetFreeChannelIndex(const std::string& ChName) const
{
	const auto& ChannelDefMap = Driver->GetChannelDefinitionMap();
	const auto& Channel = ChannelDefMap.find(ChName);

	if (Channel == ChannelDefMap.end())
		return -1;

	return Channel->second.StaticChannelIndex;
}

const EConnectionState NetConnection::GetConnectionState() const
{
	return State.load(std::memory_order_acquire);
}

void NetConnection::SetConnectionState(EConnectionState ConnectionState)
{
	State.store(ConnectionState, std::memory_order_release);
}

bool NetConnection::InitSequence(int32_t IncomingSequence, int32_t OutgoingSequence)
{
	//AssertCheck(InPacketId == -1 || Driver->ServerConnection != nullptr);
	if (InPacketId == -1)
	{
		InPacketId = IncomingSequence - 1;
		OutPacketId = OutgoingSequence;
		OutAckPacketId = OutgoingSequence - 1;
		LastNotifiedSendPacketId = OutAckPacketId; //LastNotifiedSendPacketId는 OutPacketId에 따라 증가하기에 -1을 하여 순서를 맞춘다.

		// 리라이어블 패킷 시퀀스 초기화(공격 방지에 더 유용/효과적)
		InitInReliable = IncomingSequence & (MAX_CHSEQUENCE - 1); //서버의 InReliable == 클라의 OutReliable
		InitOutReliable = OutgoingSequence & (MAX_CHSEQUENCE - 1);//서버의 OutReliable == 클라의 InReliable

		//초기화 되는 이시점에 모든 신뢰성 장치를 초기화 해야 한다.
		PendingDispatchPacketReaders = {}; //clear
		Record.EntryRecord.clear();
		PacketNotify.Init(InPacketId, OutPacketId);

		//InitInReliable, InitOutReliable은 오직 2가지 용도, InReliable, OutReliable 초기화 및 Channel 종료 및 연결 종료 파악
		//가지고 있는 모든 Channel에게 Seq Update
		for (const auto& Channel : GetChannels())
		{
			if (Channel)
			{
				Channel->UpdateChannelSeq(InitInReliable, InitOutReliable);
			}
		}
		return true;
	}
	return false;
}

int32_t NetConnection::GetMaxSingleBunchSizeBits() const
{
	return (MaxPacket * 8) - MAX_BUNCH_HEADER_BITS - MAX_PACKET_TRAILER_BITS - MAX_PACKET_HEADER_BITS - MaxPacketHandlerBits;
}

std::shared_ptr<NetChannel> NetConnection::GetOpenChannelByType(EProtocolType Type)
{
	for (const auto& Channel : GetChannels())
	{
		if (Channel && Channel->bOpen && Channel->ChName == ToString(Type))
			return Channel;
	}
	return nullptr;
}

const uint32_t NetConnection::GetPlayerConnectionID() const
{
	return UniqueConnectionId;
}

void NetConnection::SetOwnerThreadId(uint32_t id)
{
	OwnerThreadId = id;
}

uint32_t NetConnection::GetOwnerThreadId() const
{
	return OwnerThreadId;
}

void NetConnection::DispatchPacket(std::shared_ptr<RecvPacketReader> Reader, int32_t PacketId, bool& bOutSkipAck, bool& bOutHasBunchErrors)
{
	const bool bIsServer = GetDriver()->IsServer();

	BufferReader PacketReader(Reader->GetPayloadPtr(), Reader->GetPayloadSize());

	while (PacketReader.CanRead<uint8_t>() && GetConnectionState() != USOCK_Closed)
	{
		//step1. uint8_t BunchHeaderSize를 추출 및 복사
		uint8_t BunchHeaderByteSize = 0;
		if (!PacketReader.Read(BunchHeaderByteSize))
		{
			bOutHasBunchErrors = true;
			break;
		}

		constexpr uint8_t MIN_BUNCH_HEADER_BYTES = 10;
		if (BunchHeaderByteSize < MIN_BUNCH_HEADER_BYTES)
		{
			bOutHasBunchErrors = true;
			break;
		}

		std::span<const uint8_t> bunchHeaderView = PacketReader.ReadView(BunchHeaderByteSize - 1);
		if (bunchHeaderView.empty())
		{
			bOutHasBunchErrors = true;
			break;
		}

		BitReader BunchHeader(bunchHeaderView.data(), bunchHeaderView.size_bytes() * 8);

		uint8_t ChannelFlags = 0;
		BunchHeader.Serialize(&ChannelFlags, sizeof(ChannelFlags));

		bool bControl = (ChannelFlags & (1 << 0)) != 0;
		bool bOpen = (ChannelFlags & (1 << 1)) != 0;
		bool bClose = (ChannelFlags & (1 << 2)) != 0;
		bool bReliable = (ChannelFlags & (1 << 3)) != 0;

		int8_t ChIndex = 0;
		BunchHeader.Serialize(&ChIndex, sizeof(ChIndex));

		if (ChIndex < 0 || static_cast<size_t>(ChIndex) >= Channels.size())
		{
			bOutHasBunchErrors = true;
			break;
		}

		std::string ChName;
		if (bReliable || bOpen)
		{
			switch (ChIndex)
			{
			case 0:
			{
				ChName = "Control";
				break;
			}
			case 1:
			{
				ChName = "Actor";
				break;
			}
			default:
			{
				ChName = "None";
				break;
			}
			}
		}
		std::shared_ptr<NetChannel> Channel = Channels[ChIndex];
		if (Channel == nullptr)
		{
			Channels[ChIndex] = CreateChannelByName(ChName, ChIndex);
			Channel = Channels[ChIndex];
		}

		if (Channel == nullptr)
		{
			bOutHasBunchErrors = true;
			break;
		}

		uint32_t ChSequence = 0;
		BunchHeader << ChSequence;
		const auto ReadInt = ChSequence;

		if (bReliable)
		{
			ChSequence = NetConnectionHelper::MakeRelative(ReadInt, Channel->InReliable, MAX_CHSEQUENCE);
		}

		uint8_t MessageCount = 0;
		BunchHeader << MessageCount;

		uint16_t BunchPayload = 0;
		BunchHeader << BunchPayload;

		std::span<const uint8_t> View = PacketReader.ReadView(BunchPayload);

		if (Channel != nullptr)
		{
			if (bReliable && ChSequence != Channel->InReliable + 1)
			{
				NETWORK_LOG_DEBUG("UnOrder Bunch - Bunch.Reliable: {}, Channel.Reliable: {}", ChSequence, Channel->InReliable);

				//step1. InBunchCopyReader 추가
				std::shared_ptr<InBunchReader> CopyBunch = CORE::TMakeShared<InBunchReader>(View, ChSequence, MessageCount);
				Channel->AddUnreadInCopyBunch(ChSequence, CopyBunch);
				continue;
			}
			else
			{
				switch (Channel->ChIndex)
				{
				case 0: // Control
				case 1: //Actor
				{
					DispatchBunch(View, Reader->OwnerEvent, Channel, MessageCount);
					break;
				}
				default: //None
					BunchHeader.SetError();
					break;
				}
			}
		}

		if (BunchHeader.IsError())
		{
			bOutHasBunchErrors = true;

			if (bIsServer)
			{
				return;
			}
		}
	}
}

void NetConnection::DispatchBunch(std::span<const uint8_t>& BunchView, std::shared_ptr<InRecvPacketEvent> Event, std::shared_ptr<NetChannel> Channel, const uint8_t MessageCount)
{
	Channel->DispatchRawBunch(BunchView, Event, MessageCount);
}

void NetConnection::InitChannelData()
{
	if (!(Channels.size() == 0))
	{
		return;
	}

	const auto ChannelSize = Driver->GetMaxChannelsSize();

	Channels.resize(ChannelSize);

	PacketNotify.Init(InPacketId, OutPacketId);
}

//------------------------------------------------------------------------------
//읽기 전용 Getter

const std::deque<std::shared_ptr<OutReadyPacket>>& NetConnection::GetOutPacketQueue() const { return OutPacketQueue; } //READ-ONLY
const std::shared_ptr<NetDriver>& NetConnection::GetDriver() const { return Driver; } //READ-ONLY
const std::shared_ptr<NetAddr>& NetConnection::GetRemoteAddress() const { return RemoteAddr; } //READ-ONLY
const std::shared_ptr<PacketPipeline>& NetConnection::GetHandler() const { return Handler; } //READ-ONLY
const std::vector<std::shared_ptr<NetChannel>>& NetConnection::GetChannels() const { return Channels; } //READ-ONLY
const int32_t NetConnection::GetMaxPacket() const { return MaxPacket; }//READ-ONLY
const int32_t NetConnection::GetMaxPacketHandlerBits() const { return MaxPacketHandlerBits; }//READ-ONLY
const int32_t NetConnection::GetDefaultMaxChannelSize() const { return DefaultMaxChannelSize; }//READ-ONLY
const uint32_t NetConnection::GetConnectionId() const { return ConnectionId; }//READ-ONLY
const uint32_t NetConnection::GetHasDirtyAcks() const { return HasDirtyAcks; }//READ-ONLY
const int32_t NetConnection::GetInPacketId() const { return InPacketId; }//READ-ONLY
const int32_t NetConnection::GetOutPacketId() const { return OutPacketId; }//READ-ONLY
const int32_t NetConnection::GetOutAckPacketId() const { return OutAckPacketId; }//READ-ONLY
const int32_t NetConnection::GetLastNotifiedSendPacketId() const { return LastNotifiedSendPacketId; }//READ-ONLY
const int32_t NetConnection::GetInitInReliable() const { return InitInReliable; }//READ-ONLY
const int32_t NetConnection::GetInitOutReliable() const { return InitOutReliable; }//READ-ONLY
const uint32_t NetConnection::GetUniqueConnectionId() const { return UniqueConnectionId; }//READ-ONLY
