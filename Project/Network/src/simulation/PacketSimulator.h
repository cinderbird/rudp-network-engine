#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <vector>
#include "WindowsPlatformTime.h"

class RecvPacketReader;

namespace Network
{
	namespace Simulation
	{
		struct PacketSimulationSettings
		{
			uint32_t DropPermille = 0;			// 0-1000, 수신 데이터그램을 버릴 확률
			uint32_t DuplicatePermille = 0;		// 0-1000, 살아남은 데이터그램을 두 번 전달할 확률
			uint32_t ReorderWindowSize = 0;		// 0이면 재정렬 비활성화; 아니면 이만큼 데이터그램을 버퍼링했다가 다 차면 무작위로 하나 방출
			uint32_t ReorderMaxHoldMs = 50;		// 이 시간 동안 안 찼으면 재정렬 창을 강제 flush(트래픽이 잠잠해짐)
		};


		class PacketSimulator
		{
		public:
			explicit PacketSimulator(const PacketSimulationSettings& InSettings = {});

			void Reconfigure(const PacketSimulationSettings& InSettings);
			bool IsEnabled() const;


			std::vector<std::shared_ptr<RecvPacketReader>> Process(std::shared_ptr<RecvPacketReader> Received);

			std::vector<std::shared_ptr<RecvPacketReader>> FlushStale();


			bool ShouldDropOutbound();

			void SetTelemetryLabel(std::string InLabel);

		private:
			bool RollPermille(uint32_t Permille); // 호출자가 Mutex를 잡고 있어야 함

			mutable std::mutex Mutex;

			PacketSimulationSettings Settings;
			std::mt19937 Rng;
			std::uniform_int_distribution<uint32_t> PermilleDist{ 0, 999 };

			std::vector<std::shared_ptr<RecvPacketReader>> ReorderWindow;
			Network::Clock::TimePoint LastWindowActivity{};

			std::string TelemetryLabel; // "inbound" / "outbound", SetTelemetryLabel 참고
		};
	}
}
