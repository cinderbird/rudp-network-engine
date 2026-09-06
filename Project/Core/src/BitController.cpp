#include "pch.h"
#include "BitController.h"
#include "ByteSwap.h"

namespace ArchiveUtil
{
    template<typename T>
    BitController& SerializeByteOrderSwapped(BitController& Ar, T& Value)
    {
        static_assert(!std::is_signed_v<T>, "To reduce the number of template instances, cast 'Value' to a uint16_t&, uint32_t& or uint64_t& prior to the call.");

        if (Ar.IsLoading())
        {
            Ar.Serialize(&Value, sizeof(T));
            Value = ByteSwap(Value);
        }
        else // Saving
        {
            T SwappedValue = ByteSwap(Value);
            Ar.Serialize(&SwappedValue, sizeof(T));
        }
        return Ar;
    }
}

BitController::BitController() 
    : ArForceByteSwapping(0)
    , ArIsError(0)
    , ArIsLoading(0)
    , ArIsNetArchive(0)
    , ArIsPersistent(0)
    , ArIsSaving(0)
    , ArMaxSerializeSize(0)
    , NextProxy(nullptr)
{
}


void BitController::Reset()
{
    ArIsSaving = false;
    ArIsPersistent = false;
    ArIsNetArchive = false;
    ArIsLoading = false;
}

void BitController::SetIsSaving(bool bInIsSaving)
{
    ArIsSaving = bInIsSaving;
}

void BitController::SetIsPersistent(bool bInIsPersistent)
{
    ArIsPersistent = bInIsPersistent;
}

BitController& BitController::GetInnermostState()
{
    return *this;
}

void BitController::SetIsLoading(bool bInIsLoading)
{
    ArIsLoading = bInIsLoading;
}

void BitController::CountBytes(size_t InNum, size_t InMax)
{
}

void BitController::SerializeBool(bool& D)
{
    uint32_t OldUBoolValue;

    {
        OldUBoolValue = D ? 1 : 0;
        this->Serialize(&OldUBoolValue, sizeof(OldUBoolValue));
    }
    if (OldUBoolValue > 1)
    {
        this->SetError();
    }
    D = !!OldUBoolValue;
}

BitController& BitController::ByteOrderSerialize(void* V, int32_t Length)
{
    if (!IsByteSwapping())
    {
        Serialize(V, Length);
        return *this;
    }
    return SerializeByteOrderSwapped(V, Length);
}

bool BitController::IsLoading() const
{
    return ArIsLoading;
}

bool BitController::IsError() const
{
    return ArIsError;
}

void BitController::SetError()
{
    ForEachState([](BitController& State) { State.ArIsError = true; });
}

void BitController::ClearError()
{
    ForEachState([](BitController& State) { State.ArIsError = false; });
}

void BitController::Serialize(void* V, int64_t Length)
{
    memset(V, 0, Length);
}

void BitController::SerializeBits(void* Src, int64_t LengthBits)
{
}

void BitController::SerializeBitsWithOffset(void* Src, int32_t SourceBit, int64_t LengthBits)
{
}

void BitController::SerializeInt(uint32_t& Value, uint32_t Max)
{
}

void BitController::SerializeIntPacked(uint32_t& Value)
{
}

BitController& BitController::SerializeByteOrderSwapped(void* V, int32_t Length)
{
    if (IsLoading())
    {
        Serialize(V, Length); // 읽기
        ByteSwap(V, Length);  // 바이트 순서 반전
    }
    else // Writing
    {
        ByteSwap(V, Length);  // V를 바이트 순서 반전
        Serialize(V, Length); // 쓰기
        ByteSwap(V, Length);  // 원래 순서로 복원
    }

    return *this;
}

BitController& BitController::SerializeByteOrderSwapped(uint16_t& Value)
{
    return ArchiveUtil::SerializeByteOrderSwapped(*this, Value);
}

BitController& BitController::SerializeByteOrderSwapped(uint32_t& Value)
{
    return ArchiveUtil::SerializeByteOrderSwapped(*this, Value);
}

BitController& BitController::SerializeByteOrderSwapped(uint64_t& Value)
{
    return ArchiveUtil::SerializeByteOrderSwapped(*this, Value);
}

BitController& BitController::SerializeByteOrderSwapped(unsigned long& Value)
{
    return SerializeByteOrderSwapped(reinterpret_cast<uint64_t&>(Value));
}

bool BitController::IsByteSwapping()
{
    bool SwapBytes = ArForceByteSwapping;

    return SwapBytes;
}

BitController& operator<<(BitController& Ar, wchar_t& Value)
{
    Ar.ByteOrderSerialize(&Value, sizeof(Value));
    return Ar;
}

BitController& operator<<(BitController& Ar, uint8_t& Value)
{
    Ar.Serialize(&Value, 1);
    return Ar;
}

BitController& operator<<(BitController& Ar, int8_t& Value)
{
    Ar.Serialize(&Value, 1);
    return Ar;
}

BitController& operator<<(BitController& Ar, uint16_t& Value)
{
    Ar.ByteOrderSerialize(Value);
    return Ar;
}

BitController& operator<<(BitController& Ar, int16_t& Value)
{
    Ar.ByteOrderSerialize(reinterpret_cast<uint16_t&>(Value));
    return Ar;
}

BitController& operator<<(BitController& Ar, uint32_t& Value)
{
    Ar.ByteOrderSerialize(Value);
    return Ar;
}

BitController& operator<<(BitController& Ar, int32_t& Value)
{
    Ar.ByteOrderSerialize(reinterpret_cast<uint32_t&>(Value));
    return Ar;
}

BitController& operator<<(BitController& Ar, long& Value)
{
    Ar.ByteOrderSerialize(reinterpret_cast<unsigned long&>(Value));
    return Ar;
}

BitController& operator<<(BitController& Ar, float& Value)
{
    static_assert(sizeof(float) == sizeof(uint32_t), "Expected float to be 4 bytes to swap as uint32_t");
    Ar.ByteOrderSerialize(reinterpret_cast<uint32_t&>(Value));
    return Ar;
}

BitController& operator<<(BitController& Ar, double& Value)
{
    static_assert(sizeof(double) == sizeof(uint64_t), "Expected double to be 8 bytes to swap as uint64_t");
    Ar.ByteOrderSerialize(reinterpret_cast<uint64_t&>(Value));
    return Ar;
}

BitController& operator<<(BitController& Ar, uint64_t& Value)
{
    Ar.ByteOrderSerialize(Value);
    return Ar;
}

BitController& operator<<(BitController& Ar, int64_t& Value)
{
    Ar.ByteOrderSerialize(reinterpret_cast<uint64_t&>(Value));
    return Ar;
}

BitController& operator<<(BitController& Ar, bool& D)
{
    Ar.SerializeBool(D);
    return Ar;
}

BitController& operator<<(BitController& Ar, std::string& Str)
{

    uint32_t Length = static_cast<uint32_t>(Str.size());
    Ar << Length;  // 길이 먼저 직렬화

    if (Ar.IsLoading())
    {
        Str.resize(Length); // 읽을 때 미리 공간 확보
    }

    if (Length > 0)
    {
        Ar.Serialize((void*)Str.data(), Length); // 문자열 직렬화
    }
    return Ar;
}

