#pragma once
#include "pch.h"
#include <nlohmann/json.hpp>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <deque>
#include "SListQueue.h"


#define NET_TELEMETRY_EMIT(...) \
	do { if (::Network::Telemetry::TelemetrySink::Get().IsEnabled()) { ::Network::Telemetry::TelemetrySink::Get().Emit(__VA_ARGS__); } } while (0)


namespace Network::Telemetry
{
	using ControlCallback = std::function<void(const nlohmann::json&)>;

	class TelemetrySink
	{
	public:
		static TelemetrySink& Get();

		void Start(uint16_t Port);
		void Stop();

		bool IsEnabled() const { return bRunning.load(std::memory_order_relaxed); }
		bool HasSubscribers() const { return bHasSubscribers.load(std::memory_order_relaxed); }

		void Emit(nlohmann::json Event);

		void SetControlHandler(ControlCallback Callback);

	private:
		TelemetrySink() = default;
		~TelemetrySink();
		TelemetrySink(const TelemetrySink&) = delete;
		TelemetrySink& operator=(const TelemetrySink&) = delete;

		void AcceptLoop();
		void WriterLoop();
		void ReaderLoop(SOCKET ClientSocket);
		void RemoveClient(SOCKET ClientSocket);

		std::atomic<bool> bRunning{ false };
		std::atomic<bool> bHasSubscribers{ false };
		uint16_t Port = 0;

		SOCKET ListenSocket = INVALID_SOCKET;
		std::thread AcceptThread;
		std::thread WriterThread;

		std::mutex ClientsMutex;
		std::vector<SOCKET> ClientSockets;

		Network::Thread::Queue::SListQueue<std::string> EmitQueue;

		static constexpr size_t kRecentEventCapacity = 300;
		std::mutex RecentMutex;
		std::deque<std::string> RecentEvents;

		std::mutex ControlMutex;
		ControlCallback Control;
	};
}
