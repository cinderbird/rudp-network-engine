#pragma once
#include "pch.h"
//#include "SocketMinimal.h"
#include "WindowsInternetAddress.h"
#include "BitReader.h"
#include <memory>

class NetAddr;
class InRecvPacketEvent;

class RecvPacketReader;
struct PacketTraits;

struct PacketTraits
{
	bool bConnectionlessPacket{ false };
	bool bFromRecentlyDisconnected{ false };
};

class RecvPacketReader
{
public:
	RecvPacketReader(std::shared_ptr<InRecvPacketEvent> RecvEvent, int NumOfBytes);

	void Init(uint8_t* HeaderPtr, int64_t HeaderBitSize, uint8_t* PayloadPtr, uint32_t PayloadSize, sockaddr_storage& AddrStorage);

	PacketTraits& GetTraits();
	void SetConnectionlessPacket(bool bConnectionless);
	void SetFromRecentlyDisconnected(bool bRecentlyDisconnected);
	bool IsConnectionlessPacket() const;
	bool IsRecentlyDisconnected() const;

	BitReader& GetHeader();
	uint32_t GetHeaderSize();

	uint8_t* GetPayloadPtr();
	uint32_t GetPayloadSize();

	WindowsAddr& GetAddress();
	uint64_t GetHashKey() const;

public:
	std::shared_ptr<InRecvPacketEvent> OwnerEvent;

private:
	//메타데이터
	PacketTraits Traits{};

	uint64_t HashKey{ 0 };
	WindowsAddr Address{};

	BitReader Header;
	uint8_t* RawBufferPtr{ nullptr };
	uint32_t RawBufferByteSize{ 0 };
};


