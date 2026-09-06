#pragma once
#include "pch.h"

class INetworkService;
class IGameService;

class ThreadManager
{
public:
    ThreadManager();

    void SetupNetworkService(std::shared_ptr<INetworkService> Service);

    void Run();

    void TickLoop(std::shared_ptr<INetworkService> Service);

    void IOLoop(std::shared_ptr<INetworkService> Service);

    void Stop();

    void Clear();

    static uint32_t ResolveIoThreadCount();

private:
    std::atomic<bool> bStopRequested = false;
    std::vector<std::thread> ThreadContainer;
    uint32_t IoThreadCount = NET_IOCP_THREAD_COUNT;

    std::shared_ptr<INetworkService> networkService;
};
