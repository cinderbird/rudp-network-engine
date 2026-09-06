#include "PacketEvent.h"
#include "NetPacket.h"

PacketEvent::PacketEvent(EOverlappedType Type)
{
	Reset(Type);
}

void PacketEvent::Reset(EOverlappedType EventType)
{
	OVERLAPPED::hEvent = WSA_INVALID_EVENT;
	OVERLAPPED::Internal = 0;
	OVERLAPPED::InternalHigh = 0;
	OVERLAPPED::Offset = 0;
	OVERLAPPED::OffsetHigh = 0;

	Type = EventType;
}

void OutSendPacketEvent::Clear()
{
	PacketEvent::Reset(EOverlappedType::Send);

	wsaBufs.clear();
	TotalByteSize = 0;
	Packet = nullptr;
}

void OutSendPacketEvent::CopyBufView()
{
	{
		TotalByteSize = 0;

		for (const auto& Data : Packet->BufferViews)
		{
			WSABUF Buf{};
			Buf.buf = (CHAR*)Data.BufferPtr;
			Buf.len = Data.Size;
			TotalByteSize += Data.Size;

			wsaBufs.push_back(Buf);
		}
	}
}

OutSendPacketEvent* OutSendPacketEvent::Create(std::shared_ptr<OutReadyPacket> readyPacket)
{
	return CORE::New<OutSendPacketEvent>(readyPacket);
}

OutSendPacketEvent::~OutSendPacketEvent()
{
}

std::vector<WSABUF>& OutSendPacketEvent::GetwsaBufs()
{
	return wsaBufs;
}

uint32_t OutSendPacketEvent::GetSize() const
{
	return TotalByteSize;
}

void OutSendPacketEvent::EndSendEvent()
{
	auto RefHolder = shared_from_this(); //Add Ref Count

	if (Packet != nullptr)
	{
		Clear();
	}
}

OutSendPacketEvent::OutSendPacketEvent(std::shared_ptr<OutReadyPacket> readyPacket)
	: PacketEvent(EOverlappedType::Send)
	, Packet(readyPacket)
{
	CopyBufView();
}

InRecvPacketEvent::InRecvPacketEvent()
	: PacketEvent(EOverlappedType::Recv)
{
}

void InRecvPacketEvent::Clear()
{
	//1. Overlapped Event
	PacketEvent::Reset(EOverlappedType::Recv);

	// 3. WSABUF
	wsabuf.buf = reinterpret_cast<CHAR*>(Data.data());
	wsabuf.len = static_cast<ULONG>(Data.size());

	// 4
	ZeroMemory(&remoteAddr, sizeof(sockaddr_storage));

	// 5. 
	AddrLen = sizeof(sockaddr_storage);
}

