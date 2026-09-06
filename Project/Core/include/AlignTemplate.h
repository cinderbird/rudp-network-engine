#pragma once
#include <type_traits>

template <typename T>
FORCEINLINE constexpr T Align(T Val, uint64_t Alignment)
{
    static_assert(std::is_integral<T>::value || std::is_pointer<T>::value, "Align<T>: T must be an integer or pointer type.");
    return (T)(((uint64_t)Val + Alignment - 1) & ~(Alignment - 1));
}

template <typename T>
FORCEINLINE constexpr T AlignDown(T Val, uint64_t Alignment)
{
    static_assert(std::is_integral<T>::value || std::is_pointer<T>::value, "AlignDown<T>: T must be an integer or pointer type.");
    return (T)(((uint64_t)Val) & ~(Alignment - 1));
}
