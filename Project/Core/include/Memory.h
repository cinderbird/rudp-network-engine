#pragma once
#include <memory>

class LMemory
{
public:
	static void* Allocate(size_t size)
	{
		return std::malloc(size);
	}

	static void Deallocate(void* ptr)
	{
		std::free(ptr);
	}

	template<typename T, typename... Args>
	static T* Construct(void* ptr, Args&&... args)
	{
		return new(ptr) T(std::forward<Args>(args)...);
	}

	template<typename T>
	static void Destroy(T* obj)
	{
		if (obj)
		{
			obj->~T();
		}
	}
};
