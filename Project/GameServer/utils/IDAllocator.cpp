#include "IDAllocator.h"

namespace Game
{
	IDAllocator::IDAllocator(uint32_t start)
		: counter(start)
	{
	}

	uint32_t IDAllocator::Allocate()
	{
		uint32_t id{};

		if (!freeIDs.empty())
		{
			auto QId = freeIDs.front();
			freeIDs.pop();
			return QId;
		}
		else
		{
			return counter.fetch_add(1, std::memory_order_relaxed);
		}
	}

	void IDAllocator::Free(uint32_t id)
	{
		freeIDs.push(id);
	}
}