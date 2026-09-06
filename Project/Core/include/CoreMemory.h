#pragma once
#include "Memory.h"
#include "Allocator.h"


namespace CORE
{
	template<typename Type, typename... Types>
	Type* New(Types&&... Arg)
	{
		void* memory = LMemory::Allocate(sizeof(Type));

		if (!memory)
		{
			throw std::bad_alloc();
		}

		return LMemory::Construct<Type>(memory, std::forward<Types>(Arg)...);
	}

	template<typename Type>
	void Delete(Type* Arg)
	{
		if (Arg)
		{
			LMemory::Destroy(Arg);

			LMemory::Deallocate(Arg);
		}
	}

	/*
		allocate_shared, MSDN : https://learn.microsoft.com/ko-kr/cpp/standard-library/memory-functions?view=msvc-170#allocate_shared
	*/
	template <class Type, typename... Types>
	std::shared_ptr<Type> TMakeShared(Types&&... Arg)
	{
		return std::allocate_shared<Type>(Mallocator<Type>(), std::forward<Types>(Arg)...);
	}

};
