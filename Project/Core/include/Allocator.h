#pragma once
#include "Memory.h"


/*
    MSDN: https://learn.microsoft.com/ko-kr/cpp/standard-library/allocators?view=msvc-170
*/
template <class T>
class Mallocator
{
public:
    typedef T value_type;
    Mallocator() noexcept {} //C++ 표준 라이브러리가 기본 생성자를 요구하진 않음

    template<class U>
    Mallocator(const Mallocator<U>&) noexcept {}

    template<class U>
    bool operator==(const Mallocator<U>&) const noexcept
    {
        return true;
    }

    template<class U>
    bool operator!=(const Mallocator<U>&) const noexcept
    {
        return false;
    }

    T* allocate(const size_t n) const
    {
        if (n == 0)
        {
            return nullptr;
        }
        if (n > static_cast<size_t>(-1) / sizeof(T))
        {
            throw std::bad_array_new_length();
        }

        void* const pv = LMemory::Allocate(n * sizeof(T));

        if (!pv)
        {
            throw std::bad_alloc();
        }

        return static_cast<T*>(pv);
    }

    void deallocate(T* const p, size_t) const noexcept
    {
        LMemory::Deallocate(p);
    }

};
