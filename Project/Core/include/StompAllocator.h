#pragma once#include <windows.h>

/*
[가드][할당정보][정렬gap][데이터][정렬gap][가드]
 ^FullAllocPtr              ^ReturnedPtr
*/
class StompAllocator
{
	const size_t  PageSize;
	const bool bUseUnderrunMode;
	struct AllocationData;
	size_t  VirtualAddressCursor = 0;
	size_t  VirtualAddressMax = 0;
	static constexpr size_t VirtualAddressBlockSize = 1 * 1024 * 1024 * 1024; // 1 GB blocks

	enum { PAGE_SIZE = 0x1000 };

public:
	StompAllocator();

	void* Malloc(size_t  Size, uint32_t Alignment);
	void Free(void* InPtr);
};


