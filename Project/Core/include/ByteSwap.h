#pragma once


template<typename T>
T ByteSwap(T Value);

template<>
inline uint16_t ByteSwap<uint16_t>(uint16_t Value)
{
#if defined(_MSC_VER)
    return _byteswap_ushort(Value);
#elif defined(__clang__) || defined(__GNUC__)
    return __builtin_bswap16(Value);
#else
    return static_cast<uint16_t>(((Value >> 8) & 0xFF) | ((Value & 0xFF) << 8));
#endif
}

template<>
inline int16_t ByteSwap<int16_t>(int16_t Value)
{
    return static_cast<int16_t>(ByteSwap<uint16_t>(static_cast<uint16_t>(Value)));
}

// 32비트 정수 특수화
template<>
inline uint32_t ByteSwap<uint32_t>(uint32_t Value)
{
#if defined(_MSC_VER)
    return _byteswap_ulong(Value);
#elif defined(__clang__) || defined(__GNUC__)
    return __builtin_bswap32(Value);
#else
    return ((Value >> 24) & 0xFF) | (((Value >> 16) & 0xFF) << 8) |
        (((Value >> 8) & 0xFF) << 16) | ((Value & 0xFF) << 24);
#endif
}

template<>
inline int32_t ByteSwap<int32_t>(int32_t Value)
{
    return static_cast<int32_t>(ByteSwap<uint32_t>(static_cast<uint32_t>(Value)));
}

// 64비트 정수 특수화
template<>
inline uint64_t ByteSwap<uint64_t>(uint64_t Value)
{
#if defined(_MSC_VER)
    return _byteswap_uint64(Value);
#elif defined(__clang__) || defined(__GNUC__)
    return __builtin_bswap64(Value);
#else
    Value = ((Value << 8) & 0xFF00FF00FF00FF00ULL) | ((Value >> 8) & 0x00FF00FF00FF00FFULL);
    Value = ((Value << 16) & 0xFFFF0000FFFF0000ULL) | ((Value >> 16) & 0x0000FFFF0000FFFFULL);
    return (Value << 32) | (Value >> 32);
#endif
}

template<>
inline int64_t ByteSwap<int64_t>(int64_t Value)
{
    return static_cast<int64_t>(ByteSwap<uint64_t>(static_cast<uint64_t>(Value)));
}

template<>
inline float ByteSwap<float>(float Value)
{
    static_assert(sizeof(float) == sizeof(uint32_t), "Size mismatch");
    uint32_t Temp;
    std::memcpy(&Temp, &Value, sizeof(float));
    Temp = ByteSwap<uint32_t>(Temp);
    float Result;
    std::memcpy(&Result, &Temp, sizeof(float));
    return Result;
}

template<>
inline double ByteSwap<double>(double Value)
{
    static_assert(sizeof(double) == sizeof(uint64_t), "Size mismatch");
    uint64_t Temp;
    std::memcpy(&Temp, &Value, sizeof(double));
    Temp = ByteSwap<uint64_t>(Temp);
    double Result;
    std::memcpy(&Result, &Temp, sizeof(double));
    return Result;
}

template<>
inline char16_t ByteSwap<char16_t>(char16_t Value)
{
    return static_cast<char16_t>(ByteSwap<uint16_t>(static_cast<uint16_t>(Value)));
}

inline void ByteSwap(void* Data, int32_t Length)
{
    uint8_t* Bytes = static_cast<uint8_t*>(Data);
    for (int32_t i = 0; i < Length / 2; ++i)
    {
        std::swap(Bytes[i], Bytes[Length - i - 1]);
    }
}

