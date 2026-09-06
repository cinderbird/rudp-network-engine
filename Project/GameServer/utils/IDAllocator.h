#pragma once
#include <atomic>
#include <queue>


namespace Game
{
	class IDAllocator
	{
	public:
		IDAllocator(uint32_t start = 1);

		uint32_t Allocate();

		void Free(uint32_t id);

	private:
		std::atomic<uint32_t> counter;
		std::queue<uint32_t> freeIDs;
	};
}