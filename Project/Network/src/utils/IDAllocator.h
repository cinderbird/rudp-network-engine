#pragma once
#include <atomic>
#include <mutex>
#include <queue>

class IDAllocator
{
public:
	IDAllocator(uint32_t start = 1);

	uint32_t Allocate();

	void Free(uint32_t id);

private:
	std::atomic<uint32_t> counter;

	mutable std::mutex Mutex; // freeIDs 보호
	std::queue<uint32_t> freeIDs;
};
