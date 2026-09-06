#include "PacketSimulator.h"
#include "AyncyLog.h"
#include "TelemetrySink.h"

namespace Network
{
	namespace Simulation
	{
		PacketSimulator::PacketSimulator(const PacketSimulationSettings& InSettings)
			: Settings(InSettings)
			, Rng(std::random_device{}())
		{
		}

		void PacketSimulator::Reconfigure(const PacketSimulationSettings& InSettings)
		{
			std::lock_guard Lock(Mutex);
			Settings = InSettings;
		}

		bool PacketSimulator::IsEnabled() const
		{
			std::lock_guard Lock(Mutex);
			return Settings.DropPermille > 0 || Settings.DuplicatePermille > 0 || Settings.ReorderWindowSize > 0;
		}

		bool PacketSimulator::RollPermille(uint32_t Permille)
		{
			if (Permille == 0)
			{
				return false;
			}
			return PermilleDist(Rng) < Permille;
		}

		void PacketSimulator::SetTelemetryLabel(std::string InLabel)
		{
			std::lock_guard Lock(Mutex);
			TelemetryLabel = std::move(InLabel);
		}

		std::vector<std::shared_ptr<RecvPacketReader>> PacketSimulator::Process(std::shared_ptr<RecvPacketReader> Received)
		{
			std::lock_guard Lock(Mutex);

			std::vector<std::shared_ptr<RecvPacketReader>> Result;

			if (RollPermille(Settings.DropPermille))
			{
				NETWORK_LOG_TRACE("[PacketSimulator] dropped 1 datagram");
				NET_TELEMETRY_EMIT({ {"type", "drop"}, {"direction", TelemetryLabel} });
				return Result;
			}

			std::vector<std::shared_ptr<RecvPacketReader>> ToDispatch;
			ToDispatch.push_back(Received);
			if (RollPermille(Settings.DuplicatePermille))
			{
				NETWORK_LOG_TRACE("[PacketSimulator] duplicated 1 datagram");
				NET_TELEMETRY_EMIT({ {"type", "duplicate"}, {"direction", TelemetryLabel} });
				ToDispatch.push_back(Received);
			}

			if (Settings.ReorderWindowSize == 0)
			{
				return ToDispatch;
			}

			LastWindowActivity = Network::Clock::Now();
			for (auto& Item : ToDispatch)
			{
				ReorderWindow.push_back(std::move(Item));
			}

			if (ReorderWindow.size() < Settings.ReorderWindowSize)
			{
				return Result; // still filling the window; nothing to dispatch yet
			}

			std::uniform_int_distribution<size_t> IndexDist(0, ReorderWindow.size() - 1);
			const size_t PickIndex = IndexDist(Rng);

			Result.push_back(std::move(ReorderWindow[PickIndex]));
			ReorderWindow.erase(ReorderWindow.begin() + PickIndex);

			NETWORK_LOG_TRACE("[PacketSimulator] released 1 datagram from reorder window (window size now {})", ReorderWindow.size());
			NET_TELEMETRY_EMIT({ {"type", "reorder"}, {"direction", TelemetryLabel}, {"windowSize", ReorderWindow.size()} });
			return Result;
		}

		bool PacketSimulator::ShouldDropOutbound()
		{
			std::lock_guard Lock(Mutex);

			const bool bDrop = RollPermille(Settings.DropPermille);
			if (bDrop)
			{
				NETWORK_LOG_TRACE("[PacketSimulator] dropped 1 outbound datagram");
				NET_TELEMETRY_EMIT({ {"type", "drop"}, {"direction", TelemetryLabel} });
			}

			return bDrop;
		}

		std::vector<std::shared_ptr<RecvPacketReader>> PacketSimulator::FlushStale()
		{
			std::lock_guard Lock(Mutex);

			std::vector<std::shared_ptr<RecvPacketReader>> Result;

			if (ReorderWindow.empty() || Settings.ReorderWindowSize == 0)
			{
				return Result;
			}

			const auto now = Network::Clock::Now();
			const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - LastWindowActivity).count();
			if (elapsedMs < static_cast<int64_t>(Settings.ReorderMaxHoldMs))
			{
				return Result;
			}

			// 트래픽이 잠잠해짐 -- 무기한 붙잡고 있는 대신 아직 버퍼에 있는 걸
			// 전부 방출.
			Result = std::move(ReorderWindow);
			ReorderWindow.clear();

			NETWORK_LOG_TRACE("[PacketSimulator] flushed {} stale datagram(s) from reorder window", Result.size());
			return Result;
		}
	}
}
