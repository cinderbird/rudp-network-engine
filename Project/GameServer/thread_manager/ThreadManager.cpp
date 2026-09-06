#include "ThreadManager.h"
#include "service/NetworkService.h"
#include <cstdlib>

namespace
{

    void WaitOnHighResTimer(unsigned long long WaitUs)
    {
        static thread_local HANDLE Timer = []() -> HANDLE
            {
                HANDLE H = ::CreateWaitableTimerExW(nullptr, nullptr,
                    CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
                if (!H)
                {
                    H = ::CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
                }
                return H;
            }();

        if (!Timer)
        {
            std::this_thread::sleep_for(std::chrono::microseconds(WaitUs));
            return;
        }

        LARGE_INTEGER Due{};
        Due.QuadPart = -static_cast<LONGLONG>(WaitUs * 10ull);
        if (!::SetWaitableTimer(Timer, &Due, 0, nullptr, nullptr, FALSE))
        {
            std::this_thread::sleep_for(std::chrono::microseconds(WaitUs));
            return;
        }
        ::WaitForSingleObject(Timer, INFINITE);
    }
}

uint32_t ThreadManager::ResolveIoThreadCount()
{
#pragma warning(suppress : 4996)
    const char* const Override = std::getenv("NET_IO_THREADS");
    if (!Override)
    {
        return NET_IOCP_THREAD_COUNT;
    }

    const int Parsed = std::atoi(Override);
    if (Parsed < 1 || Parsed > 64)
    {
        return NET_IOCP_THREAD_COUNT;
    }
    return static_cast<uint32_t>(Parsed);
}

ThreadManager::ThreadManager()
{
}

void ThreadManager::SetupNetworkService(std::shared_ptr<INetworkService> Service)
{
    networkService = Service;
}

void ThreadManager::Run()
{
    ThreadContainer.emplace_back(&ThreadManager::TickLoop, this, networkService);

    IoThreadCount = ResolveIoThreadCount();
    for (uint32_t Count = 0; Count < IoThreadCount; ++Count)
    {
        ThreadContainer.emplace_back(&ThreadManager::IOLoop, this, networkService);
    }
}

void ThreadManager::TickLoop(std::shared_ptr<INetworkService> Service)
{
    while (!bStopRequested)
    {
        try
        {
            Service->TickSweep();

            const unsigned long long WaitUs = Service->UsUntilNextTick();
            if (WaitUs > 0)
            {
                WaitOnHighResTimer(WaitUs);
            }
        }
        catch (const std::exception& e)
        {
            std::cerr << "Exception in tick thread: " << e.what() << std::endl;
        }
        catch (...)
        {
            std::cerr << "Unknown exception in tick thread" << std::endl;
        }
    }
}

void ThreadManager::IOLoop(std::shared_ptr<INetworkService> Service)
{
    constexpr unsigned int PumpTimeoutMs = 50;

    while (!bStopRequested)
    {
        try
        {
            Service->PumpIO(PumpTimeoutMs);
        }
        catch (const std::exception& e)
        {
            std::cerr << "Exception in IO thread: " << e.what() << std::endl;
        }
        catch (...)
        {
            std::cerr << "Unknown exception in IO thread" << std::endl;
        }
    }
}

void ThreadManager::Stop()
{
    if (bStopRequested.exchange(true))
    {
        return; // already stopping
    }

    if (networkService)
    {
        for (uint32_t Count = 0; Count < IoThreadCount; ++Count)
        {
            networkService->WakeIO();
        }
    }
}

void ThreadManager::Clear()
{
    Stop();

    for (auto& It : ThreadContainer)
    {
        if (It.joinable())
        {
            It.join();
        }
    }
    ThreadContainer.clear();
}
