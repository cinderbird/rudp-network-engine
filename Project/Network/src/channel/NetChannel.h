#pragma once
#include "pch.h"
#include "NetMessageReader.h"
#include "NetBunchBuilder.h"

struct NetMessage;

class ChannelBunchBuilder;
class InRecvPacketEvent;

struct OutReadyBunch;
class InBunchReader;

class NetConnection;
class AOutBunch;
class NetProtocolMessage;
class BunchHeader;
class RecvMessageReader;

enum class EChannelCloseReason : uint8_t;

class NetChannel
{
public:
	enum { RELIABLE_BUFFER = 512 }; // 2의 거듭제곱, 1 이상.

	NetChannel();
	NetChannel(NetChannel* NewChannel);

	virtual ~NetChannel() {}

	virtual void Tick();
	virtual void Init(std::shared_ptr<NetConnection> InConnection, int8_t InChIndex, uint32_t InReliable, uint32_t OutReliable) = 0;
	virtual const uint16_t GetChannelID() const = 0;

	//송신
	void RegisterMessage(void* BufferPtr, uint32_t Size, ReleaseMessageBufferCallback Callback, void* Context) const;
	void RegisterMessage(NetMessage& Message) const;

	bool AddUnAckedOutBunch(uint32_t OutPacketId, const std::shared_ptr<OutReadyBunch>& Bunch);
	void AddUnreadInCopyBunch(uint32_t ChSequence, std::shared_ptr<InBunchReader> CopyBunch);

	void Acked(int32_t AckPacketId);
	void Nacked(int32_t NackPacketId);

	void ForceRetransmitAllUnAcked();

	void DispatchRawBunch(std::span<const uint8_t>& InView, std::shared_ptr<InRecvPacketEvent> Event, uint32_t MessageCount);

	bool DequeueReadyBunch(std::shared_ptr<OutReadyBunch>& out);

	void PumpBunch(size_t batchLimit = 256, bool flushTail = true); // 틱(혹은 작업 스레드)에서 조립
	bool HasReadyBunch() const;

	//수신
	virtual void ReceivedMessage(RecvMessageReader& Message) = 0;
	virtual void DispatchPendingMessage() = 0;


	void TickFlush();
	void UpdateChannelSeq(uint32_t InReliable_, uint32_t OutReliable_);

private:
	bool DispatchRawBunch_Interanl(BufferReader& bunchReader, RecvBunchOwnerRef Onwer, uint32_t MessageCount);
	bool DequeueReadyBunch_Interanl(std::shared_ptr<OutReadyBunch>& out) const; 	// 준비된 번치 제공 (커넥션에서 호출)


public:
	std::weak_ptr<NetConnection>	Connection;	// Owner connection.

	bool		bControl;
	bool		bOpen;
	bool		bClose;
	bool		bReliable;

	int8_t		ChIndex;  //4비트(index와 Channel은 Mapping 따라서 16종류의 Channel 존재 가능)
	uint32_t	InReliable{ 0 };
	uint32_t	OutReliable{ 0 };
	std::string	ChName;

	std::unique_ptr<ChannelBunchBuilder> BunchBuilder;

	std::deque<std::shared_ptr<OutReadyBunch>> OutBunchQueue;

	std::map<uint32_t, std::vector<std::shared_ptr<OutReadyBunch>>> OutUnAckedBunches;//Key: OutPacketId
	std::map<uint32_t, std::shared_ptr<InBunchReader>> InUnreadBunchMap;
};
