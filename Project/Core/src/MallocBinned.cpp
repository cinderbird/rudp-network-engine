#include "MallocBinned.h"

struct MallocBinned::PoolInfo
{
    void* PoolMemory = nullptr;
    SLIST_HEADER FreeList{};
    PoolInfo* Next = nullptr;
    uint32_t BlockSize = 0;
    uint32_t FreeBlockCount = 0;

    PoolInfo(uint32_t InBlockSize) : BlockSize(InBlockSize)
    {
        InitializeSListHead(&FreeList);
    }
    SLIST_HEADER& GetFreeList() { return FreeList; }
};

MallocBinned::PoolTable* MallocBinned::FindTable(size_t Size)
{
    for (uint32_t i = 0; i < POOL_COUNT; ++i)
        if (Size <= BlockSizes[i])
            return &Table[i];
    return nullptr;
}

MallocBinned::MallocBinned()
{
    for (uint32_t i = 0; i < POOL_COUNT; ++i)
        Table[i].BlockSize = BlockSizes[i];
}

void* MallocBinned::Malloc(size_t Size, uint32_t Alignment)
{
    Alignment = std::max(Alignment, (uint32_t)SLIST_ALIGNMENT);
    Size = std::max(Size, size_t(1));

    PoolTable* table = FindTable(Size);
    if (!table)
    {
        return ::_aligned_malloc(Size, Alignment);
    }
    PoolInfo* pool = table->FirstPool;
    if (!pool || pool->FreeBlockCount == 0)
        pool = AllocatePool(*table, PoolAddressMap);

    void* block = AllocateBlockFromPool(*pool);
    return block;
}

void MallocBinned::Free(void* Ptr)
{
    if (!Ptr) return;

    // 1. Pool base address 역추적
    uintptr_t addr = reinterpret_cast<uintptr_t>(Ptr);
    uintptr_t pool_base = addr & ~(BINNED_ALLOC_POOL_SIZE - 1);

    // 2. PoolInfo 얻기
    auto it = PoolAddressMap.find(reinterpret_cast<void*>(pool_base));

    if (it == PoolAddressMap.end())
    {
        ::_aligned_free(Ptr);
        return;
    }
    PoolInfo* pool = it->second;
    
    // 3. SLIST에 push
    InterlockedPushEntrySList(&pool->GetFreeList(), static_cast<PSLIST_ENTRY>(Ptr));
    ++pool->FreeBlockCount;
}

// Pool 생성
MallocBinned::PoolInfo* MallocBinned::AllocatePool(PoolTable& Table, std::unordered_map<void*, PoolInfo*>& Map)
{
    PoolInfo* pool = new PoolInfo(Table.BlockSize);
    pool->PoolMemory = _aligned_malloc(BINNED_ALLOC_POOL_SIZE, BINNED_ALLOC_POOL_SIZE);
    assert(pool->PoolMemory && "Pool allocation failed");

    // Pool base 주소 -> PoolInfo 등록
    Map[pool->PoolMemory] = pool;

    // 블록들을 SLIST에 미리 등록
    size_t block_count = BINNED_ALLOC_POOL_SIZE / Table.BlockSize;
    pool->FreeBlockCount = (uint32_t)block_count;
    uint8_t* base = static_cast<uint8_t*>(pool->PoolMemory);
    for (size_t i = 0; i < block_count; ++i)
    {
        FreeBlock* block = reinterpret_cast<FreeBlock*>(base + i * Table.BlockSize);
        InterlockedPushEntrySList(&pool->GetFreeList(), block);
    }

    // PoolTable 리스트에 연결 (pool chaining)
    pool->Next = Table.FirstPool;
    Table.FirstPool = pool;

    return pool;
}

// Pool에서 free block pop
void* MallocBinned::AllocateBlockFromPool(PoolInfo& Pool)
{
    PSLIST_ENTRY entry = InterlockedPopEntrySList(&Pool.GetFreeList());

    if (entry)
    {
        --Pool.FreeBlockCount;
    }

    return entry ? (void*)entry : nullptr;
}

