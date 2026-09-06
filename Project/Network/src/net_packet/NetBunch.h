#pragma once
#include "pch.h"

struct NetMessage;
struct OutReadyBunch;
struct BunchHeaderFields;
class BunchHeader;

struct NetMessage
{
	void* BufferPtr;
	uint32_t Size; //BufferByteSize
	ReleaseMessageBufferCallback Callback;
	void* Context;

	NetMessage(void* Ptr, uint32_t Size, ReleaseMessageBufferCallback Callback, void* Context);
	NetMessage();
};

//준비가 완료된 Send Bunch -> 읽을 때 혼동을 방지하기 위해 해당 이름을 부여
struct OutReadyBunch
{
	std::unique_ptr<uint8_t[]> HeaderBytes;
	uint32_t HeaderSize{};

	std::vector<NetMessage> Messages; // zero-copy
	uint32_t PayloadBytes{};
	uint32_t TotalSize{};
	int32_t PacketId{ 0 };
	uint8_t  ChIndex{};
};

struct BunchHeaderFields
{
	bool     bControl{};
	bool     bOpen{};
	bool     bClose{};
	bool     bReliable{};
	uint8_t  ChIndex{};      // 0..15 권장
	uint32_t ChSequence{};   //= NetConnection.OutReliable[ChIndex]
	uint8_t  MessageCount{}; // ≤ 255
	uint16_t  PayloadSize{};
};

class BunchHeader
{
public:
	explicit BunchHeader(size_t reserveBits = 128);
	void Encode(const BunchHeaderFields& Field);

	const uint8_t* Data() const;
	uint32_t Size() const;

private:
	BitWriter Buffer;
};



