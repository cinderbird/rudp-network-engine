#pragma once
#include "pch.h"
#include "NetPacketNotify.h"
#include "NetPacket.h"
#include "ChannelRecord.h"
#include "RttTracker.h"
#include "PacketEnum.h"
#include "JobSystem.h"

class NetDriver;
class NetworkSocket;
class NetAddr;
class RecvPacketReader;
class NetChannel;
class PacketPipeline;
class UdpConnectionProcessor;
class InRecvPacketEvent;

struct OutReadyPacket;
struct PacketSimulationSettings;

enum class EChannelCloseReason : uint8_t;

enum { MAX_PACKETID = NetPacketNotify::SequenceNumberT::SeqNumberCount };  // 2의 거듭제곱, 1 이상
enum { MAX_CHSEQUENCE = 1024 }; // 2의 거듭제곱, RELIABLE_BUFFER보다 큼
enum { MAX_BUNCH_HEADER_BITS = 256 };
enum { MAX_PACKET_RELIABLE_SEQUENCE_HEADER_BITS = 32 + NetPacketNotify::SequenceHistoryT::MaxSizeInBits };
enum { MAX_PACKET_HEADER_BITS = MAX_PACKET_RELIABLE_SEQUENCE_HEADER_BITS };
enum { MAX_PACKET_TRAILER_BITS = 1 };


class NetConnection
{
public:
	NetConnection();
	virtual ~NetConnection();

	virtual void InitLocalConnection(std::shared_ptr<NetDriver> InSupervisor, std::shared_ptr<NetworkSocket> InSocket, EConnectionState InState, int32_t InMaxPacket = 0, int32_t InPacketOverhead = 0, uint32_t PlayerId = 0);
	virtual void InitRemoteConnection(std::shared_ptr<NetDriver> InDriver, std::shared_ptr<NetworkSocket> InSocket, const NetAddr& InRemoteAddr, EConnectionState InState, int32_t InMaxPacket = 0, int32_t InPacketOverhead = 0, uint32_t PlayerId = 0);
	virtual void InitBase(std::shared_ptr<NetDriver> InDriver, std::shared_ptr<NetworkSocket> InSocket, EConnectionState InState, int32_t InMaxPacket = 0, int32_t InPacketOverhead = 0);

	virtual bool InitSequence(int32_t IncomingSequence, int32_t OutgoingSequence);
	virtual std::shared_ptr<NetChannel> CreateChannelByName(const std::string& ChName, int32_t ChIndex = -1/*INDEX_NONE*/) = 0;
	void InitChannelData();

	//송신 - 패킷 생성
	void WriteHeaderCallback(BitWriter& Buffer) noexcept;
	void AssembleOutgoingPackets() noexcept;
	bool IsRequireAssembleBunch() noexcept;
	bool TryDequeuePacket(std::shared_ptr<OutReadyPacket>& OutPacket);

	//송신 - 메시지 등록
	virtual bool SendNetMessage(NetMessage& Message, uint32_t ChannelId) = 0;

	//재전송 - 헤더 갱신
	virtual void UpdatePacketHeader(std::shared_ptr<OutReadyPacket>& Packet);

	//수신 - 패킷 Ack/Nack
	virtual void ReceivedRawPacket(std::shared_ptr<RecvPacketReader> Reader) = 0;
	void ReceivedPacket(std::shared_ptr<RecvPacketReader> Reader);
	void RecivedAck(int32_t AckMarkPacketId);
	void RecivedNack(int32_t NakMarkPacketId);

	//수신 - 데이터 읽기
	void DispatchPendingPacket();
	virtual void DispatchPacket(std::shared_ptr<RecvPacketReader> Reader, int32_t PacketId, bool& bOutSkipAck, bool& bOutHasBunchErrors);
	virtual void DispatchBunch(std::span<const uint8_t>& BunchView, std::shared_ptr<InRecvPacketEvent> Event, std::shared_ptr<NetChannel> Channel, const uint8_t MessageCount);

	//IOCPDispatch
	virtual void TickDispatch() = 0;
	virtual void TickFlush() = 0;

public:
	virtual float GetTimeoutValue();

	void Close(const char* Reason);

	int32_t GetFreeChannelIndex(const std::string& ChName) const;
	const EConnectionState GetConnectionState() const;
	void SetConnectionState(EConnectionState ConnectionState);
	int32_t GetMaxSingleBunchSizeBits() const;
	std::shared_ptr<NetChannel> GetOpenChannelByType(EProtocolType Type);
	const uint32_t GetPlayerConnectionID() const;
	void SetOwnerThreadId(uint32_t id);
	uint32_t GetOwnerThreadId() const;


	const std::deque<std::shared_ptr<OutReadyPacket>>& GetOutPacketQueue() const; 
	const std::shared_ptr<NetDriver>& GetDriver() const; 
	const std::shared_ptr<NetAddr>& GetRemoteAddress() const; 
	const std::shared_ptr<PacketPipeline>& GetHandler() const; 
	const std::vector<std::shared_ptr<NetChannel>>& GetChannels() const;

	void EmitTelemetryGaugeOwningThread();

	const int32_t GetMaxPacket() const;
	const int32_t GetMaxPacketHandlerBits() const;
	const int32_t GetDefaultMaxChannelSize() const;

	const uint32_t GetConnectionId() const;
	const uint32_t GetHasDirtyAcks() const;

	const int32_t GetInPacketId() const;
	const int32_t GetOutPacketId() const;
	const int32_t GetOutAckPacketId() const;
	const int32_t GetLastNotifiedSendPacketId() const;

	const int32_t GetInitInReliable() const;
	const int32_t GetInitOutReliable() const;
	const uint32_t GetUniqueConnectionId() const;

	std::atomic<Network::Clock::TimePoint> LastReceiveTime{};
	Network::Clock::TimePoint LastSendTime{};

	Network::Clock::TimePoint LastTelemetryGaugeTime{};

protected:
	std::shared_ptr<NetDriver> Driver;

	std::shared_ptr<NetAddr> RemoteAddr;

	uint32_t UniqueConnectionId{ 0 };

	int32_t	InitInReliable;//초기화 및 연결 종료 확인
	int32_t	InitOutReliable;//초기화 및 연결 종료 확인

	std::vector<std::shared_ptr<NetChannel>> Channels;

private:
	std::deque<std::shared_ptr<OutReadyPacket>> OutPacketQueue;


	std::shared_ptr<PacketPipeline> Handler;


	NetPacketNotify PacketNotify;

	std::queue<std::shared_ptr<RecvPacketReader>> PendingDispatchPacketReaders;

	ConnectionChannelRecord Record;

	ConnectionRttTracker RttTracker;


	std::atomic<EConnectionState> State{ EConnectionState::USOCK_Invalid };

	int32_t	 MaxPacket = 0;
	int32_t	 MaxPacketHandlerBits = 0;
	int32_t	 DefaultMaxChannelSize = 4;

	uint32_t ConnectionId = 0;
	uint32_t HasDirtyAcks = 0;

	int32_t	InPacketId;  //Recv Packet Id == 내가 받은 패킷의 순서
	int32_t	OutPacketId; //Send Packet Id == 내가 보낸 패킷의 순서
	int32_t OutAckPacketId;				
	int32_t	LastNotifiedSendPacketId;	 //마지막으로 Ack, Nack를 확인한 내가 보낸 Packet Id -> Channel에 저장한 Bunch를 제거하기 위해

	std::condition_variable JobCond;

	std::deque<std::shared_ptr<OutReadyPacket>> DelayedPacketQueue;
	uint32_t OwnerThreadId = -1;

};
