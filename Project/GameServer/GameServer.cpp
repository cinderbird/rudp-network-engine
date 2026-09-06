#include "pch.h"
#include "CorePch.h"
#include "NetMinimalPch.h"

#include "service/NetworkService.h"
#include "thread_manager/ThreadManager.h"

#include <csignal>

namespace { std::atomic<bool> GShutdownRequested{ false }; }

int main()
{
    const int ServerCount = 1;

    std::vector<std::shared_ptr<NetworkService>> Services;
    std::vector<std::shared_ptr<ThreadManager>> TMs;

    for (int i = 0; i < ServerCount; ++i)
    {
        GNetworkService = std::make_shared<NetworkService>();
        Services.push_back(GNetworkService);
        GNetworkService->Start();
        
        std::shared_ptr<ThreadManager> TM = std::make_shared<ThreadManager>();
        TMs.push_back(TM);
        TM->SetupNetworkService(GNetworkService);
        TM->Run();
    }

    //부드러운 종료 
    std::signal(SIGINT, [](int) { GShutdownRequested.store(true); });
    std::signal(SIGTERM, [](int) { GShutdownRequested.store(true); });

    while (!GShutdownRequested.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    //쓰레드 먼저 정리
    for (auto& TM : TMs)
    {
        TM->Clear();
    }
    for (auto& Service : Services)
    {
        Service->Shutdown();
    }

    return 0;
}

