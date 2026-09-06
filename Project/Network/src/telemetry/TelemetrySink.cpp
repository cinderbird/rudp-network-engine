#include "TelemetrySink.h"
#include "AyncyLog.h"
#include <chrono>

namespace Network::Telemetry
{
	TelemetrySink& TelemetrySink::Get()
	{
		static TelemetrySink Instance;
		return Instance;
	}

	TelemetrySink::~TelemetrySink()
	{
		Stop();
	}

	void TelemetrySink::Start(uint16_t InPort)
	{
		if (InPort == 0)
		{
			return; // 비활성 -- 기본값, 대부분의 스트레스 테스트가 이 경우
		}

		if (bRunning.load(std::memory_order_relaxed) && Port == InPort)
		{
			return; // 이미 이 포트로 실행 중
		}

		if (bRunning.load(std::memory_order_relaxed))
		{
			Stop(); // 포트 전환 -- 기존 리스너부터 정리
		}

		WSADATA wsaData{};

		if (::WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		{
			NETWORK_LOG_WARN("[Telemetry] WSAStartup failed, telemetry disabled");
			return;
		}

		ListenSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (ListenSocket == INVALID_SOCKET)
		{
			NETWORK_LOG_WARN("[Telemetry] socket() failed: {}", ::WSAGetLastError());
			return;
		}

		BOOL bReuse = TRUE;
		::setsockopt(ListenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&bReuse), sizeof(bReuse));

		sockaddr_in Addr{};
		Addr.sin_family = AF_INET;
		Addr.sin_port = ::htons(InPort);
		// 127.0.0.1 전용 -- 로컬 데모/디버그용, 외부에서 접근할 일 없음.
		::inet_pton(AF_INET, "127.0.0.1", &Addr.sin_addr);

		if (::bind(ListenSocket, reinterpret_cast<sockaddr*>(&Addr), sizeof(Addr)) == SOCKET_ERROR)
		{
			NETWORK_LOG_WARN("[Telemetry] bind() to 127.0.0.1:{} failed: {}", InPort, ::WSAGetLastError());
			::closesocket(ListenSocket);
			ListenSocket = INVALID_SOCKET;
			return;
		}

		if (::listen(ListenSocket, 4) == SOCKET_ERROR)
		{
			NETWORK_LOG_WARN("[Telemetry] listen() failed: {}", ::WSAGetLastError());
			::closesocket(ListenSocket);
			ListenSocket = INVALID_SOCKET;
			return;
		}

		Port = InPort;
		bRunning.store(true, std::memory_order_relaxed);

		AcceptThread = std::thread(&TelemetrySink::AcceptLoop, this);
		WriterThread = std::thread(&TelemetrySink::WriterLoop, this);

		NETWORK_LOG_INFO("[Telemetry] listening on 127.0.0.1:{}", InPort);
	}

	void TelemetrySink::Stop()
	{
		if (!bRunning.exchange(false, std::memory_order_relaxed))
		{
			return;
		}

		if (ListenSocket != INVALID_SOCKET)
		{
			::closesocket(ListenSocket); // AcceptLoop의 accept()를 풀어줌
			ListenSocket = INVALID_SOCKET;
		}

		if (AcceptThread.joinable())
		{
			AcceptThread.join();
		}
		if (WriterThread.joinable())
		{
			WriterThread.join();
		}

		std::lock_guard Lock(ClientsMutex);
		for (SOCKET s : ClientSockets)
		{
			::closesocket(s); // 각 detached ReaderLoop의 recv()를 풀어줌
		}
		ClientSockets.clear();
		bHasSubscribers.store(false, std::memory_order_relaxed);
	}

	void TelemetrySink::Emit(nlohmann::json Event)
	{
		if (!bRunning.load(std::memory_order_relaxed))
		{
			return;
		}

		const auto NowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
		Event["t"] = NowMs;
		std::string Line = Event.dump();

		{
			std::lock_guard Lock(RecentMutex);
			RecentEvents.push_back(Line);
			if (RecentEvents.size() > kRecentEventCapacity)
			{
				RecentEvents.pop_front();
			}
		}

		if (bHasSubscribers.load(std::memory_order_relaxed))
		{
			EmitQueue.PushBack(std::move(Line));
		}
	}

	void TelemetrySink::SetControlHandler(ControlCallback Callback)
	{
		std::lock_guard Lock(ControlMutex);
		Control = std::move(Callback);
	}

	void TelemetrySink::AcceptLoop()
	{
		while (bRunning.load(std::memory_order_relaxed))
		{
			sockaddr_in PeerAddr{};
			int PeerAddrLen = sizeof(PeerAddr);
			SOCKET ClientSocket = ::accept(ListenSocket, reinterpret_cast<sockaddr*>(&PeerAddr), &PeerAddrLen);
			if (ClientSocket == INVALID_SOCKET)
			{
				break; // 리스닝 소켓이 닫혔거나(Stop()) 진짜 에러 -- 어느 쪽이든 중단
			}

			{
				std::lock_guard Lock(ClientsMutex);
				ClientSockets.push_back(ClientSocket);
				bHasSubscribers.store(true, std::memory_order_relaxed);
			}

			{
				std::lock_guard Lock(RecentMutex);
				for (const auto& Buffered : RecentEvents)
				{
					std::string Line = Buffered;
					Line.push_back('\n');
					::send(ClientSocket, Line.c_str(), static_cast<int>(Line.size()), 0);
				}
			}

			NETWORK_LOG_INFO("[Telemetry] viewer connected");

			std::thread(&TelemetrySink::ReaderLoop, this, ClientSocket).detach();
		}
	}

	void TelemetrySink::WriterLoop()
	{
		using namespace std::chrono_literals;

		while (bRunning.load(std::memory_order_relaxed))
		{
			auto Line = EmitQueue.TryPopFront();
			if (!Line.has_value())
			{
				std::this_thread::sleep_for(10ms);
				continue;
			}

			Line->push_back('\n');

			std::lock_guard Lock(ClientsMutex);
			for (auto it = ClientSockets.begin(); it != ClientSockets.end(); )
			{
				const int Sent = ::send(*it, Line->c_str(), static_cast<int>(Line->size()), 0);
				if (Sent == SOCKET_ERROR)
				{
					::closesocket(*it);
					it = ClientSockets.erase(it);
				}
				else
				{
					++it;
				}
			}
			bHasSubscribers.store(!ClientSockets.empty(), std::memory_order_relaxed);
		}
	}

	void TelemetrySink::ReaderLoop(SOCKET ClientSocket)
	{
		std::string Buffer;
		char Chunk[512];

		while (bRunning.load(std::memory_order_relaxed))
		{
			const int Received = ::recv(ClientSocket, Chunk, sizeof(Chunk), 0);
			if (Received <= 0)
			{
				break; // 상대가 닫았거나 리스너가 정리됨
			}

			Buffer.append(Chunk, static_cast<size_t>(Received));

			size_t NewlinePos;
			while ((NewlinePos = Buffer.find('\n')) != std::string::npos)
			{
				std::string LineStr = Buffer.substr(0, NewlinePos);
				Buffer.erase(0, NewlinePos + 1);

				if (LineStr.empty())
				{
					continue;
				}

				ControlCallback CallbackCopy;
				{
					std::lock_guard Lock(ControlMutex);
					CallbackCopy = Control;
				}

				if (CallbackCopy)
				{
					try
					{
						CallbackCopy(nlohmann::json::parse(LineStr));
					}
					catch (const std::exception& e)
					{
						NETWORK_LOG_WARN("[Telemetry] failed to parse control line: {}", e.what());
					}
				}
			}
		}

		RemoveClient(ClientSocket);
	}

	void TelemetrySink::RemoveClient(SOCKET ClientSocket)
	{
		std::lock_guard Lock(ClientsMutex);
		const auto Removed = std::erase(ClientSockets, ClientSocket);
		bHasSubscribers.store(!ClientSockets.empty(), std::memory_order_relaxed);
		if (Removed > 0)
		{
			::closesocket(ClientSocket);
		}
	}
}
