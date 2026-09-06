#include "NetPacketReader.h"
#include "PacketEvent.h"

class PacketReaderHelper
{
public:
	static uint64_t MakeIPv4PortKeyFromStorage(const sockaddr_storage& SockStorage)
	{
		if (SockStorage.ss_family != AF_INET)
			return 0;

		const sockaddr_in* Addr = reinterpret_cast<const sockaddr_in*>(&SockStorage);

		uint32_t Ip = ntohl(Addr->sin_addr.s_addr);
		uint16_t Port = ntohs(Addr->sin_port);

		uint64_t Key = (static_cast<uint64_t>(Ip) << 16) | Port;
		return Key;
	}
};

WindowsAddr& RecvPacketReader::GetAddress()
{
	return Address;
}

uint64_t RecvPacketReader::GetHashKey() const
{
	return HashKey;
}

PacketTraits& RecvPacketReader::GetTraits()
{
	return Traits;
}

void RecvPacketReader::SetConnectionlessPacket(bool bConnectionless)
{
	Traits.bConnectionlessPacket = bConnectionless;
}

void RecvPacketReader::SetFromRecentlyDisconnected(bool bRecentlyDisconnected)
{
	Traits.bFromRecentlyDisconnected = bRecentlyDisconnected;
}

bool RecvPacketReader::IsConnectionlessPacket() const
{
	return Traits.bConnectionlessPacket;
}

bool RecvPacketReader::IsRecentlyDisconnected() const
{
	return Traits.bFromRecentlyDisconnected;
}

RecvPacketReader::RecvPacketReader(std::shared_ptr<InRecvPacketEvent> RecvEvent, int NumOfBytes)
	: OwnerEvent(RecvEvent)
{
	//1. 첫번째 바이트를 읽고 Header의 ByteSize 추출
	const uint8_t HeaderByteSize = (OwnerEvent->Data[0]);
	const uint8_t HeaderDataByteSzie = HeaderByteSize - 1;
	const uint32_t BufferByteSize = NumOfBytes - HeaderByteSize;
	const int64_t HeaderBitSize = HeaderDataByteSzie * 8;

	uint8_t* PacketPtr = OwnerEvent->Data.data() + 1;
	uint8_t* BufferPtr = OwnerEvent->Data.data() + (HeaderByteSize);

	Init(PacketPtr, HeaderBitSize, BufferPtr, BufferByteSize, OwnerEvent->remoteAddr);
};

void RecvPacketReader::Init(uint8_t* HeaderPtr, int64_t HeaderBitSize, uint8_t* PayloadPtr, uint32_t PayloadSize, sockaddr_storage& AddrStorage)
{
	//step1. 주소 및 Hash 생성
	auto Addr = WindowsAddr(EInternetProtocolPlags::IPv4);
	Addr.Set(AddrStorage, sizeof(AddrStorage));

	Address = Addr;
	HashKey = PacketReaderHelper::MakeIPv4PortKeyFromStorage(AddrStorage);

	//step2. Header BitReader 설정
	Header = BitReader(HeaderPtr, HeaderBitSize);

	//step3. Payload Ptr 설정
	RawBufferPtr = PayloadPtr;
	RawBufferByteSize = PayloadSize;
};

BitReader& RecvPacketReader::GetHeader()
{
	return Header;
}

uint32_t RecvPacketReader::GetHeaderSize()
{
	return (uint32_t)Header.GetNumBytes();
}

uint8_t* RecvPacketReader::GetPayloadPtr()
{
	return RawBufferPtr;
}

uint32_t RecvPacketReader::GetPayloadSize()
{
	return RawBufferByteSize;
}