#pragma once
#include <chrono>
#include <cstdint>

namespace Network
{
    namespace Clock
    {
        using Clock = std::chrono::steady_clock; //thread-safe
        using TimePoint = Clock::time_point;

        inline TimePoint Now() noexcept { return Clock::now(); } //thread safe

        inline uint64_t NowUs() noexcept
        {
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    Clock::now().time_since_epoch()
                ).count()
                );
        }

        template<class Dur>
        inline Dur Since(TimePoint from, TimePoint to) noexcept
        {
            return std::chrono::duration_cast<Dur>(to - from);
        }
    }
}