#include "NetBunch.h"

BunchHeader::BunchHeader(size_t reserveBits)
    : Buffer(reserveBits)
{
}

void BunchHeader::Encode(const BunchHeaderFields& Field)
{
    Buffer.Reset();
    uint8_t Size = 0; Buffer.Serialize(&Size, 1);
    uint8_t Flags = (Field.bControl ? 1 : 0)
        | (Field.bOpen ? 2 : 0)
        | (Field.bClose ? 4 : 0)
        | (Field.bReliable ? 8 : 0);

    Buffer.Serialize(&Flags, 1);

    uint8_t ChIndex = Field.ChIndex;

    Buffer.Serialize(&ChIndex, 1);

    //Buffer.Serialize(seq, 4);
    auto ChSeq = Field.ChSequence;
    Buffer << ChSeq;

    uint8_t MessageCount = Field.MessageCount;
    Buffer.Serialize(&MessageCount, 1);

    uint16_t PayloadSize = Field.PayloadSize;
    Buffer << PayloadSize;

    Buffer.GetData()[0] = uint8_t(Buffer.GetNumBytes());
}

const uint8_t* BunchHeader::Data() const
{
    return Buffer.GetData();
}

uint32_t BunchHeader::Size() const
{
    return (uint32_t)Buffer.GetNumBytes();
}

NetMessage::NetMessage(void* Ptr, uint32_t Size, ReleaseMessageBufferCallback Callback, void* Context)
    : BufferPtr(Ptr)
    , Size(Size)
    , Callback(Callback)
    , Context(Context)
{
}

NetMessage::NetMessage()
    : BufferPtr(nullptr)
    , Size(0)
    , Callback(nullptr)
    , Context(nullptr)
{
}
