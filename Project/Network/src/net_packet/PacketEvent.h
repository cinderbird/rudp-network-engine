#pragma once
#include "pch.h"
#include <memory>
#include <vector>
#include <array>
#include "PacketEnum.h"
#include "PacketEvent.h"

enum class EOverlappedType : uint8_t
{
	None = 0,
	Send = 1,
	Recv = 2
};

class PacketEvent : public OVERLAPPED
{
public:
	PacketEvent(EOverlappedType Type);

	virtual ~PacketEvent() = default;

	void Reset(EOverlappedType EventType);

public:
	EOverlappedType Type;
};

struct OutReadyPacket;


/*
	https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsasendto
*/
class OutSendPacketEvent : public PacketEvent, public std::enable_shared_from_this<OutSendPacketEvent>
{
	friend class LMemory;
	friend class NetConnection;
	friend class PendingRemoteConnection;
public:
	static OutSendPacketEvent* Create(std::shared_ptr<OutReadyPacket> readyPacket);

	virtual ~OutSendPacketEvent();

	/*
		Getter
	*/
	std::vector<WSABUF>& GetwsaBufs();
	uint32_t GetSize() const;

	void EndSendEvent();

private:
	OutSendPacketEvent(std::shared_ptr<OutReadyPacket> readyPacket);

	void Clear();
	void CopyBufView();

private:
	std::vector<WSABUF> wsaBufs;
	uint32_t TotalByteSize = 0;

	std::shared_ptr<OutReadyPacket> Packet;
};

class InRecvPacketEvent : public PacketEvent
{
	friend class RecvPacketReader;
	friend class RecvPacketPool;

public:
	InRecvPacketEvent();

	virtual ~InRecvPacketEvent() = default;

	void Clear();

	DWORD flags = 0;
	WSABUF wsabuf{};
	std::array<uint8_t, MAX_PACKET_SIZE> Data{};
	sockaddr_storage remoteAddr{};
	int AddrLen = sizeof(sockaddr_storage);
};

