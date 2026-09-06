#include "IDAllocator.h"

IDAllocator::IDAllocator(uint32_t start)
	: counter(start)
{
}

uint32_t IDAllocator::Allocate()
{
	// empty/front/pop 시퀀스는 하나의 원자적 단계여야 한다 -- 헤더 주석
	// 참고. 안 그러면 둘 다 비어있지 않다고 본 두 스레드가 같은 id를
	// pop하거나(같은 ConnectionId를 두 커넥션에 넘김) 빈 큐를 pop할 수
	// 있다.
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		if (!freeIDs.empty())
		{
			const uint32_t QId = freeIDs.front();
			freeIDs.pop();
			return QId;
		}
	}

	return counter.fetch_add(1, std::memory_order_relaxed);
}

void IDAllocator::Free(uint32_t id)
{
	std::lock_guard<std::mutex> Lock(Mutex);
	freeIDs.push(id);
}
