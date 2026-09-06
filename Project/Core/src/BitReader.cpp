#include "pch.h"
#include "BitReader.h"

extern const uint8_t GShift[8] = { 0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80 };
extern const uint8_t GMask[8] = { 0x00,0x01,0x03,0x07,0x0f,0x1f,0x3f,0x7f };

void appBitsCpy(uint8_t* Dest, int32_t DestBit, uint8_t* Src, int32_t SrcBit, int32_t BitCount)
{
	if (BitCount == 0) return;

	if (BitCount <= 8)
	{
		uint32_t DestIndex = DestBit / 8; //Write byte index
		uint32_t SrcIndex = SrcBit / 8; //Read byte index
		uint32_t LastDest = (DestBit + BitCount - 1) / 8; //End of write-memory byte index
		uint32_t LastSrc = (SrcBit + BitCount - 1) / 8; //End of Read-memory byte index
		uint32_t ShiftDest = DestBit & 7; //In Write-memory byte, bit index
		uint32_t ShiftSrc = SrcBit & 7;  //In Read-memory byte, bit index
		uint32_t FirstMask = 0xFF << ShiftDest;  //첫번째 Byte의 copy를 위한 mask(주의 clear - mask 둘다 한다)
		uint32_t LastMask = 0xFE << ((DestBit + BitCount - 1) & 7); // Pre-shifted left by 1. //두번째 byte의 Copy를 위한 mask(주의 clear - mask 둘다 한다)
		uint32_t Accu; //Read-memory 임시 저장소(Accumulator)

		if (SrcIndex == LastSrc) //Read byte index == End of Read-memory byte index -> 즉 동일한 바이트를 읽을 때 ==> 한 바이트안에서만 읽을 때
		{
			Accu = (Src[SrcIndex] >> ShiftSrc);
		}
		else
		{
			Accu = ((Src[SrcIndex] >> ShiftSrc) | (Src[LastSrc] << (8 - ShiftSrc)));
		}
			
		if (DestIndex == LastDest) //Write byte index == End of write-memory byte index, 즉 동일한 바이트에 Write를 할 때
		{
			uint32_t MultiMask = FirstMask & ~LastMask;
			Dest[DestIndex] = ((Dest[DestIndex] & ~MultiMask) | ((Accu << ShiftDest) & MultiMask));
		}
		else
		{
			Dest[DestIndex] = (uint8_t)((Dest[DestIndex] & ~FirstMask) | ((Accu << ShiftDest) & FirstMask));
			Dest[LastDest] = (uint8_t)((Dest[LastDest] & LastMask) | ((Accu >> (8 - ShiftDest)) & ~LastMask));
		}

		return;
	}

	// 메인 복사기, 바이트 단위 시프트 사용. 최소 크기는 9비트라 최소 2회 읽고 2회 쓴다.
	//주의! Inclusive vs Exclusive를 생각하며 변수의 의미를 파악해야 함

	uint32_t DestIndex = DestBit / 8; //Write-memory 바이트 인덱스
	uint32_t SrcIndex  = SrcBit / 8;  //Read-memory 바이트 인덱스

	uint32_t FirstSrcMask = 0xFF << (DestBit & 7);              //첫번째 Byte의 copy를 위한 Mask
	uint32_t LastSrcMask  = 0xFF << ((DestBit + BitCount) & 7); //마지막 Byte의 Copy를 위한 Mask  => 바이트 경계 문제 해결을 위해 존재

	//주의! Exclusive 연산 -> 2바이트 복사와 다른 스타일의 코드
	uint32_t LastDest = (DestBit + BitCount) / 8; //Write-memory 바이트 인덱스(=크기)
	uint32_t LastSrc  = (SrcBit + BitCount)  / 8; //Read-memory  바이트 인덱스(=크기)

	int32_t   ShiftCount = (DestBit & 7) - (SrcBit & 7); //Write-Read memory 간 offset 정렬
	//추가 설명: Read의 N번째 비트” 
	// → Write의 M번째 비트, N번째 비트에서 M번째 비트로 복사의 "시작"을 하기에 이 둘간의 offset을 정렬해야 한다

	int32_t   DestLoop = LastDest - DestIndex; //Write 해야 하는 총 Byte 수 = 끝난 뒤(다음 비트) 바이트 인덱스 - 시작하는 바이트 인덱스
	int32_t   SrcLoop = LastSrc - SrcIndex;    //Read  해야 하는 총 Byte 수 = 끝난 뒤(다음 비트) 바이트 인덱스 - 시작하는 바이트 인덱스

	uint32_t FullLoop; //Inner loop 반복 횟수 결정
	uint32_t BitAccu;  //Accumulator(임시 저장소)

	// 도입부는 정렬 상태에 따라 소스 바이트를 1개 또는 2개 읽어야 한다.
	//양수 == (DestBit & 7) >= (SrcBit & 7) == 바이트 안에서 Write-memory의 bit index가 Read-memory보다 더 상위 비트
	if (ShiftCount >= 0)
	{
		FullLoop = std::max<int32_t>(DestLoop, SrcLoop); 
		//max를 하는 이유: 충분히 많은 loop를 통해 읽기-쓰기를 모두 처리하기 위함,
		// 루프 안에서 메모리 범위를 벗어나지 않도록 제어함으로 정의되지 않은 행동을 하지 않는다.
		//작은 값으로 하면 더 읽어올 데이터가 있어도 읽지 못하거나, 더 써야하는 데이터가 있어도 쓰지 못한다

		BitAccu = Src[SrcIndex] << ShiftCount; 
		//1단계
		// Read-Write offset 맞추기 ( << ShiftCount)
		//2단계
		// 정렬된 Read-Memory의 첫번 째 Byte를 BitAccu에 임시 저장

		ShiftCount += 8; //내부 루프를 위한 준비.
	}
	//음수 == (DestBit & 7) < (SrcBit & 7) 바이트 안에서 Read-memory의 bit index가 Write-memory보다 더 상위 비트
	else
	{	
		ShiftCount += 8;
		FullLoop = std::max<int32_t>(DestLoop, SrcLoop - 1); //BitAccu는 하나가 아닌 두개의 바이트를 읽는다 => 그래서 SrcLoop - 1를 해준다
		BitAccu = Src[SrcIndex] << ShiftCount; //첫번 째 바이트 읽기
		SrcIndex++; //올려주고
		ShiftCount += 8; // 내부 루프를 위한 준비. 올려주고
		BitAccu = (((uint32_t)Src[SrcIndex] << ShiftCount) + (BitAccu)) >> 8; //또 읽어준다(여기서 쓰는 >> 8 은 고정된 시프트)
	}

	Dest[DestIndex] = (uint8_t)((BitAccu & FirstSrcMask) | (Dest[DestIndex] & ~FirstSrcMask));
	SrcIndex++;
	DestIndex++;
	
	// 빠른 내부 루프.
	for (; FullLoop > 1; FullLoop--)
	{   // ShiftCount는 8~15 범위 - 모든 읽기가 유효함.
		BitAccu = (((uint32_t)Src[SrcIndex] << ShiftCount) + (BitAccu)) >> 8; // 새 값을 채워넣고 옛 값은 버림.
		SrcIndex++;
		Dest[DestIndex] = (uint8_t)BitAccu;  // 하위 8비트 복사.
		DestIndex++;
	}

	if (LastSrcMask != 0xFF)
	{
		if ((uint32_t)(SrcBit + BitCount - 1) / 8 == SrcIndex) // 마지막 유효 바이트
		{
			BitAccu = (((uint32_t)Src[SrcIndex] << ShiftCount) + (BitAccu)) >> 8;
		}
		else
		{
			BitAccu = BitAccu >> 8;
		}

		Dest[DestIndex] = (uint8_t)((Dest[DestIndex] & LastSrcMask) | (BitAccu & ~LastSrcMask));
	}
}

BitReader::BitReader(const uint8_t* Src, int64_t CountBits)
	: Max(CountBits)
	, Pos(0)
{

	Buffer.resize((CountBits + 7) >> 3);


	this->SetIsLoading(true);
	this->SetIsPersistent(true);

	ArIsNetArchive = true;

	if (Src != nullptr)
	{
		memcpy(Buffer.data(), Src, (CountBits + 7) >> 3);
		if (Max & 7)
		{
			Buffer[Max >> 3] &= GMask[Max & 7];
		}
	}
}


void BitReader::SetData(uint8_t* Src, int64_t CountBits)
{
	Max = CountBits;
	Pos = 0;
	ClearError();

	Buffer.resize((Max + 7) >> 3);


	if (Src != nullptr)
	{
		memcpy(Buffer.data(), Src, (Max + 7) >> 3);

		if (Max & 7)
		{
			Buffer[Max >> 3] &= GMask[Max & 7];
		}
	}
}

void BitReader::SetData(BitReader* Src, int64_t CountBits)
{
	Max = CountBits;
	Pos = 0;
	ClearError();

	Buffer.resize((CountBits + 7) >> 3);
	Src->SerializeBits(Buffer.data(), CountBits);
}

void BitReader::SetData(std::vector<uint8_t>&& Src, int64_t CountBits)
{
	Max = CountBits;
	Pos = 0;
	ClearError();

	Buffer = std::move(Src);

	if (Max & 7)
	{
		Buffer[Max >> 3] &= GMask[Max & 7];
	}
}

void BitReader::SerializeBits(void* Dest, int64_t LengthBits)
{
	if (IsError() || Pos + LengthBits > Max)
	{
		if (!IsError())
		{
			SetOverflowed(LengthBits);
		}
		if (LengthBits == 1)
		{
			((uint8_t*)Dest)[0] = 0;
		}
		else if (LengthBits != 0)
		{
			memset(Dest, 0, (size_t)((LengthBits + 7) >> 3));
		}
		return;
	}

	if (LengthBits == 1)
	{
		((uint8_t*)Dest)[0] = 0;
		if (Buffer[(int32_t)(Pos >> 3)] & Shift(Pos & 7))
			((uint8_t*)Dest)[0] |= 0x01;
		Pos++;
	}
	else if (LengthBits != 0)
	{
		((uint8_t*)Dest)[((LengthBits + 7) >> 3) - 1] = 0;
		appBitsCpy((uint8_t*)Dest, 0, Buffer.data(), (int32_t)Pos, (int32_t)LengthBits);
		Pos += LengthBits;
	}
}

void BitReader::SerializeBitsWithOffset(void* Dest, int32_t DestBit, int64_t LengthBits)
{
	if (IsError() || Pos + LengthBits > Max)
	{
		if (!IsError())
		{
			SetOverflowed(LengthBits);
		}
		return;
	}

	if (LengthBits != 0)
	{
		appBitsCpy((uint8_t*)Dest, DestBit, Buffer.data(), (int32_t)Pos, (int32_t)LengthBits);
		Pos += LengthBits;
	}
}

void BitReader::SerializeInt(uint32_t& OutValue, uint32_t ValueMax)
{
	if (!IsError())
	{
		uint32_t Value = 0;
		int64_t LocalPos = Pos;
		const int64_t LocalNum = Max;

		for (uint32_t Mask = 1; (Value + Mask) < ValueMax && Mask; Mask *= 2, LocalPos++)
		{
			if (LocalPos >= LocalNum)
			{
				SetOverflowed(LocalPos - Pos);
				break;
			}

			if (Buffer[(int32_t)(LocalPos >> 3)] & Shift(LocalPos & 7))
			{
				Value |= Mask;
			}
		}

		Pos = LocalPos;
		OutValue = Value;
	}
}

void BitReader::SerializeIntPacked(uint32_t& OutValue)
{
	if (IsError())
	{
		OutValue = 0;
		return;
	}

	const uint8_t* Src = Buffer.data() + (Pos >> 3U);
	const uint32_t BitCountUsedInByte = Pos & 7;
	const uint32_t BitCountLeftInByte = 8 - (Pos & 7);
	const uint8_t SrcMaskByte0 = uint8_t((1U << BitCountLeftInByte) - 1U);
	const uint8_t SrcMaskByte1 = uint8_t((1U << BitCountUsedInByte) - 1U);
	const uint32_t NextSrcIndex = (BitCountUsedInByte != 0);

	uint32_t Value = 0;
	for (unsigned It = 0, ShiftCount = 0; It < 5; ++It, ShiftCount += 7)
	{
		if (Pos + 8 > Max)
		{
			SetOverflowed(8);
			break;
		}

		Pos += 8;

		const uint8_t Byte = ((Src[0] >> BitCountUsedInByte) & SrcMaskByte0) | ((Src[NextSrcIndex] & SrcMaskByte1) << (BitCountLeftInByte & 7));
		const uint8_t NextByteIndicator = Byte & 1;
		const uint32_t ByteAsWord = Byte >> 1U;
		Value = (ByteAsWord << ShiftCount) | Value;
		++Src;

		if (!NextByteIndicator)
		{
			break;
		}
	}

	OutValue = Value;
}

void BitReader::SerializeWithPos(void* Dest, int64_t InPos, int64_t LengthBits)
{
	if (LengthBits == 1)
	{
		((uint8_t*)Dest)[0] = 0;
		if (Buffer[(int32_t)(InPos >> 3)] & Shift(InPos & 7))
			((uint8_t*)Dest)[0] |= 0x01;
	}
	else if (LengthBits != 0)
	{
		((uint8_t*)Dest)[((LengthBits + 7) >> 3) - 1] = 0;
		appBitsCpy((uint8_t*)Dest, 0, Buffer.data(), (int32_t)InPos, (int32_t)LengthBits);
	}
}

uint32_t BitReader::ReadInt(uint32_t Max)
{
	uint32_t Value = 0;

	SerializeInt(Value, Max);

	return Value;
}

void BitReader::SetOverflowed(int64_t LengthBits)
{
	SetError();
}

void BitReader::SetAtEnd()
{
	Pos = Max;
}

uint8_t BitReader::ReadBit()
{
	uint8_t Bit = 0;
	if (!IsError())
	{
		int64_t LocalPos = Pos;
		const int64_t LocalNum = Max;
		if (LocalPos >= LocalNum)
		{
			//경고: 치명적 오류!
		}
		else
		{
			Bit = !!(Buffer[(int32_t)(LocalPos >> 3)] & Shift(LocalPos & 7));
			Pos++;
		}
	}
	return Bit;
}

void BitReader::Serialize(void* Dest, int64_t LengthBytes)
{
	SerializeBits(Dest, LengthBytes * 8);
}

uint8_t* BitReader::GetData()
{
	return Buffer.data();
}

const uint8_t* BitReader::GetData() const
{
	return Buffer.data();
}

const std::vector<uint8_t>& BitReader::GetBuffer() const
{
	return Buffer;
}

uint8_t* BitReader::GetDataPosChecked()
{
	return &Buffer[(int32_t)(Pos >> 3)];
}

int64_t BitReader::GetBitsLeft() const
{
	return (Max - Pos);
}

int64_t BitReader::GetBytesLeft() const
{
	return ((Max - Pos) + 7) >> 3;
}

int64_t BitReader::GetNumBytes() const
{
	return (Max + 7) >> 3;
}

int64_t BitReader::GetNumBits() const
{
	return Max;
}

bool BitReader::AtEnd()
{
	return IsError() || Pos >= Max;
}

int64_t BitReader::GetPosBits() const
{
	return Pos;
}

void BitReader::CountMemory(BitController& Ar) const
{
}

void BitReader::ResetData(BitReader& Src, int64_t CountBits, int64_t CountBitsWithSlack)
{
	Max = CountBits;
	Pos = 0;
	ClearError();

	Buffer.clear();
	Buffer.resize((CountBitsWithSlack + 7) >> 3);
	Src.SerializeBits(Buffer.data(), CountBits);
}

uint8_t BitReader::Shift(uint8_t Cnt)
{
	return (uint8_t)(1 << Cnt);
}