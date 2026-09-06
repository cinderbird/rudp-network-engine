#include "pch.h"
#include "BitWriter.h"
#include "MathUtility.h"

extern const uint8_t GShift[8];
extern const uint8_t GMask[8];

void appBitsCpy(uint8_t* Dest, int32_t DestBit, uint8_t* Src, int32_t SrcBit, int32_t BitCount);

BitWriter::BitWriter() : Pos(0), Max(0), bAllowResize(false), bAllowOverflow(false)
{
	this->SetIsSaving(true);
	this->SetIsPersistent(true);
	ArIsNetArchive = true;
}

BitWriter::BitWriter(int64_t InMaxBits, bool InAllowResize) //MTU = 1024, fixed
	: Pos(0)
	, Max(InMaxBits)
	, bAllowOverflow(false)
{
	Buffer.clear();
	Buffer.resize((InMaxBits + 7) >> 3);

	bAllowResize = InAllowResize;
	this->SetIsSaving(true);
	this->SetIsPersistent(true);

	ArIsNetArchive = true;
}

void BitWriter::SetAllowResize(bool NewResize)
{
}

void BitWriter::Reset()
{
	BitController::Reset();

	Pos = 0;

	std::memset(Buffer.data(), 0, Buffer.size() * sizeof(uint8_t));
	this->SetIsSaving(true);
	this->SetIsPersistent(true);

	ArIsNetArchive = true;
}

void BitWriter::WriteAlign()
{

}

void BitWriter::CountMemory(BitController& Con) const
{

}

void BitWriter::ProtobufSerialize(uint16_t NewPos)
{
	//Pkt.ByteSizeLong()
	if (Buffer.size() == NewPos)
	{
		Pos = NewPos;
	}
}

//
void BitWriter::SerializeBits(void* Src, int64_t LengthBits)
{
	//MTU = 1024
	//PacketBuffer size는 1024 바이트 FIX
	if (AllowAppend(LengthBits))
	{
		if (LengthBits == 1)
		{
			//1비트 직렬화일 때
			//해당 버퍼의 1비트만 직렬화를 하겠다는 의미 -> 해당 비트가 0이면 굳이 계산할 필요X
			//LSB(최하위 비트)를 추출
			if (((uint8_t*)Src)[0] & 0x01)
			{
				//Max >> 3 : 버퍼의 바이트 위치
				//Max & 7 : 필요한 GShift의 Index를 가져오는데 & 7(0111)은 최대값이 7로 고정(하위 3비트를 추출)
				Buffer[Pos >> 3] |= GShift[Pos & 7];
			}

			Pos++;
		}
		else
		{
			appBitsCpy(Buffer.data(), (int32_t)Pos, (uint8_t*)Src, 0, (int32_t)LengthBits);
			Pos += LengthBits;
		}
	}
}

void BitWriter::SerializeBitsWithOffset(void* Src, int32_t SourceBit, int64_t LengthBits)
{
	if (AllowAppend(LengthBits))
	{
		appBitsCpy(Buffer.data(), (int32_t)Pos, (uint8_t*)Src, SourceBit, (int32_t)LengthBits);
		Pos += LengthBits;
	}
}

void BitWriter::Serialize(void* Src, int64_t LengthBytes)
{
	int64_t LengthBits = LengthBytes * 8;
	if (AllowAppend(LengthBits))
	{
		appBitsCpy(Buffer.data(), (int32_t)Pos, (uint8_t*)Src, 0, (int32_t)LengthBits);
		Pos += LengthBits;
	}
}

void BitWriter::SerializeString(std::string* Src, int64_t LengthBytes)
{
	Src->resize(LengthBytes, 0);
	int64_t LengthBits = LengthBytes * 8;
	if (AllowAppend(LengthBits))
	{
		appBitsCpy(Buffer.data(), (int32_t)Pos, (uint8_t*)Src, 0, (int32_t)LengthBits);
		Pos += LengthBits;
	}
}

void BitWriter::WriteBit(uint8_t In)
{
	if (AllowAppend(1))
	{
		if (In)
			Buffer[Pos >> 3] |= GShift[Pos & 7];
		Pos++;
	}
}

bool BitWriter::AllowAppend(int64_t LengthBits)
{
	//지금은 MTU사이즈에 고정에서 사용
	if (Pos + LengthBits > Max)
	{
		if (bAllowResize)
		{
			//우리는 MTU 1024에 Fix 해 두었다. size 변경은 허용되지 않는다
			return false;
		}
		//크기를 넘어서면 false
		return false;
	}

	return true;
}

int64_t BitWriter::GetNumBytes(void) const
{
	return (Pos + 7) >> 3;
}

const std::vector<uint8_t>* BitWriter::GetBuffer(void)
{
	return &Buffer;
}

uint8_t* BitWriter::GetData(void)
{
	return Buffer.data();
}

const uint8_t* BitWriter::GetData(void) const
{
	return Buffer.data();
}

int64_t BitWriter::GetNumBits(void) const
{
	return Pos;
}

int64_t BitWriter::GetMaxBits(void) const
{
	return Max;
}

void BitWriter::SetOverflowed(int32_t LengthBits)
{
}

void BitWriter::SetAllowOverflow(bool bInAllow)
{
}

void BitWriter::SerializeInt(uint32_t& Value, uint32_t Max)
{
	if (!(Max >= 2))
	{
		return;
	}

	const int32_t LengthBits = CeilLogTwo(Max);
	uint32_t WriteValue = Value;

	if (WriteValue >= Max)
	{
		WriteValue = Max - 1;
	}

	if (AllowAppend(LengthBits))
	{
		uint32_t NewValue = 0;
		int64_t LocalNum = Pos;

		for (uint32_t Mask = 1; (NewValue + Mask) < Max && Mask; Mask *= 2, LocalNum++)
		{
			if (WriteValue & Mask)
			{
				Buffer[LocalNum >> 3] += GShift[LocalNum & 7];
				NewValue += Mask;
			}
		}

		Pos = LocalNum;
	}
	else
	{
		SetOverflowed(LengthBits);
	}
}

void BitWriter::SerializeIntPacked(uint32_t& InValue)
{
	uint32_t Value = InValue;
	uint32_t BytesAsWords[5];
	uint32_t ByteCount = 0;
	for (unsigned It = 0; (It == 0) | (Value != 0); ++It, Value = Value >> 7U)
	{
		const uint32_t NextByteIndicator = (Value & ~0x7FU) != 0;
		const uint32_t ByteAsWord = ((Value & 0x7FU) << 1U) | NextByteIndicator;
		BytesAsWords[ByteCount++] = ByteAsWord;
	}

	const int64_t LengthBits = ByteCount * 8;
	if (!AllowAppend(LengthBits))
	{
		SetOverflowed((int32_t)LengthBits);
		return;
	}

	const uint32_t BitCountUsedInByte = Pos & 7;
	const uint32_t BitCountLeftInByte = 8 - (Pos & 7);
	const uint8_t DestMaskByte0 = uint8_t((1U << BitCountUsedInByte) - 1U);
	const uint8_t DestMaskByte1 = 0xFFU ^ DestMaskByte0;
	const bool bStraddlesTwoBytes = (BitCountUsedInByte != 0);
	uint8_t* Dest = Buffer.data() + (Pos >> 3U);

	Pos += LengthBits;


	for (uint32_t ByteIt = 0; ByteIt != ByteCount; ++ByteIt)
	{
		const uint32_t ByteAsWord = BytesAsWords[ByteIt];

		*Dest = (*Dest & DestMaskByte0) | uint8_t(ByteAsWord << BitCountUsedInByte);

		++Dest;
		if (bStraddlesTwoBytes)
		{
			*Dest = (*Dest & DestMaskByte1) | uint8_t(ByteAsWord >> BitCountLeftInByte);

		}
	}

}

void BitWriter::WriteIntWrapped(uint32_t Value, uint32_t ValueMax)
{
	const int32_t LengthBits = CeilLogTwo(ValueMax);

	if (AllowAppend(LengthBits))
	{
		uint32_t NewValue = 0;

		for (uint32_t Mask = 1; NewValue + Mask < ValueMax && Mask; Mask *= 2, Pos++)
		{
			if (Value & Mask)
			{
				Buffer[Pos >> 3] += GShift[Pos & 7];
				NewValue += Mask;
			}
		}
	}
	else
	{
		SetOverflowed(LengthBits);
	}
}

BitWriter& operator<<(BitWriter& Writer, const std::string& Str)
{
	uint32_t Length = static_cast<uint32_t>(Str.size());
	Writer.SerializeIntPacked(Length);

	if (Length > 0)
	{
		Writer.SerializeBits((void*)Str.data(), Length * 8);
	}

	return Writer;
}
