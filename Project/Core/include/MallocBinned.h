#pragma once
#include <cstdint>
#include <unordered_map>
#include <Windows.h>
#include <cassert>

class MallocBinned
{
    struct PoolInfo;
    struct FreeBlock;

    struct PoolTable
    {
        PoolInfo* FirstPool = nullptr;
        uint32_t BlockSize = 0;
    };

    enum { POOL_COUNT = 41 };
    enum { SLIST_ALIGNMENT = 16 };
    enum { BINNED_ALLOC_POOL_SIZE = 65536 }; // 64KB Pool 단위
    
    const uint32_t BlockSizes[POOL_COUNT] =
    {
        16, 32, 48, 64, 80, 96, 112, 128,
        160, 192, 224, 256, 288, 320, 384, 448,
        512, 576, 640, 704, 768, 896, 1024, 1168,
        1360, 1632, 2048, 2336, 2720, 3264, 4096, 4672,
        5456, 6544, 8192, 9360, 10912, 13104, 16384, 21840, 32768
    };

public:
    MallocBinned();
    void* Malloc(size_t Size, uint32_t Alignment = SLIST_ALIGNMENT);
    void Free(void* Ptr);

private:
    PoolTable Table[POOL_COUNT]{};
    std::unordered_map<void*, PoolInfo*> PoolAddressMap;

    PoolTable* FindTable(size_t Size);

    static PoolInfo* AllocatePool(PoolTable& Table, std::unordered_map<void*, PoolInfo*>& Map);
    static void* AllocateBlockFromPool(PoolInfo& Pool);
};

struct alignas(MallocBinned::SLIST_ALIGNMENT) MallocBinned::FreeBlock : public SLIST_ENTRY {};

