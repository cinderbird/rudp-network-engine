#include "pch.h"
#include "CorePch.h"
#include "NetMinimalPch.h"

#include "service/NetworkService.h"
#include "thread_manager/ThreadManager.h"

#include <csignal>
#include <cstdlib>

namespace { std::atomic<bool> GShutdownRequested{ false }; }

int main(int argc, char** argv)
{

    int RemoteCount = 1;
    if (argc > 1)
    {
        const int Parsed = std::atoi(argv[1]);
        if (Parsed > 0)
        {
            RemoteCount = Parsed;
        }
    }

    std::vector<std::shared_ptr<NetworkService>> Services;
    std::vector<std::shared_ptr<ThreadManager>> TMs;

    for (int i = 0; i < RemoteCount; ++i)
    {
        GNetworkService = std::make_shared<NetworkService>();
        Services.push_back(GNetworkService);
        GNetworkService->Start();
        
        std::shared_ptr<ThreadManager> TM = std::make_shared<ThreadManager>();
        TMs.push_back(TM);
        TM->SetupNetworkService(GNetworkService);
        TM->Run();
    }

    // Ctrl+C에서 깔끔한 종료 -- GameServer.cpp의 대응 노트 참고.
    std::signal(SIGINT, [](int) { GShutdownRequested.store(true); });
    std::signal(SIGTERM, [](int) { GShutdownRequested.store(true); });

    while (!GShutdownRequested.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

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

