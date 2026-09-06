#include "StompAllocator.h"
#include "AlignTemplate.h"

struct StompAllocator::AllocationData
{
	void* FullAllocationPointer;
	size_t	FullSize;
	size_t	Size;
	size_t	Sentinel;
};

StompAllocator::StompAllocator() : PageSize([] { SYSTEM_INFO si; GetSystemInfo(&si); return si.dwPageSize; }()),
bUseUnderrunMode(false)
{
}

void* StompAllocator::Malloc(size_t Size, uint32_t Alignment = PAGE_SIZE)
{
	//1. 요청받은 크기를 기준으로 전체 할당 크기 계산
	//2. 미리 선언한 메모리(MEM_RESERVE)에 여유공간이 없으면 새로 요청
	//3. 이후 언더런/오버런(언더플로우/오버플로우) 모드에 맞추어 메모리 할당(MEM_COMMIT) 후 반환


	//크기 0은 1로 고정(malloc(0) 지원)
	if (Size == 0U)
	{
		Size = 1U;
	}
	//할당정보 구조체 크기
	constexpr static size_t AllocationDataSize = sizeof(AllocationData);

	//정렬 단위로 데이터 크기 올림
	const size_t AlignedSize = Alignment ? ((Size + Alignment - 1) & -(int32_t)Alignment) : Size;
	//페이지보다 큰 정렬이면, 추가 gap 계산
	const size_t AlignmentSize = Alignment > PageSize ? Alignment - PageSize : 0;
	//데이터+정렬gap+할당정보+페이지 -> 페이지 경계로 올림
	const size_t AllocFullPageSize = (AlignedSize + AlignmentSize + AllocationDataSize + PageSize - 1) & -(ptrdiff_t)PageSize;
	//마지막 가드 페이지 한 개 추가(총 할당 크기)
	const size_t TotalAllocationSize = AllocFullPageSize + PageSize;

	//예약된 가상주소 풀에서 가져올 수 있으면 바로 할당
	void* FullAllocationPointer = nullptr;
	if (VirtualAddressCursor + TotalAllocationSize <= VirtualAddressMax)
	{
		FullAllocationPointer = (void*)(VirtualAddressCursor);
	}
	else
	{
		//부족하면 새로 가상 주소 블록을 할당(1GB)
		const SIZE_T ReserveSize = VirtualAddressBlockSize;

		FullAllocationPointer = VirtualAlloc(nullptr, ReserveSize, MEM_RESERVE, PAGE_NOACCESS);

		VirtualAddressCursor = SIZE_T(FullAllocationPointer);
		VirtualAddressMax = VirtualAddressCursor + ReserveSize;
	}
	//커서를 할당 끝으로 이동(풀 할당 영역 갱신)
	VirtualAddressCursor += TotalAllocationSize;

	//예약 실패 시 nullptr 반환
	if (!FullAllocationPointer)
	{
		return nullptr;
	}

	void* ReturnedPointer = nullptr;
	if (bUseUnderrunMode)
	{
		//underrun 모드: [가드][할당정보][정렬gap][데이터][가드]
		ReturnedPointer = Align((uint8_t*)FullAllocationPointer + PageSize + AllocationDataSize, Alignment);
		//할당정보 위치: 데이터 바로 앞
		void* AllocDataPointerStart = static_cast<AllocationData*>(ReturnedPointer) - 1;
		//데이터/할당정보 영역만 할당
		void* CommittedMemory = VirtualAlloc(AllocDataPointerStart, AllocationDataSize + AlignedSize, MEM_COMMIT, PAGE_READWRITE);
		if (!CommittedMemory)
		{
			return nullptr;
		}
	}
	else
	{
		//overrun 모드: [가드][정렬gap][데이터][정렬gap][할당정보][가드]
		ReturnedPointer = AlignDown((uint8_t*)FullAllocationPointer + AllocFullPageSize - AlignedSize, Alignment);
		//데이터 끝 위치
		void* ReturnedPointerEnd = (uint8_t*)ReturnedPointer + AlignedSize;
		//할당정보 위치
		void* AllocDataPointerStart = static_cast<AllocationData*>(ReturnedPointer) - 1;
		//실제 사용범위만 커밋
		void* CommitPointerStart = AlignDown(AllocDataPointerStart, PageSize);
		void* CommittedMemory = VirtualAlloc(CommitPointerStart, SIZE_T((uint8_t*)ReturnedPointerEnd - (uint8_t*)CommitPointerStart), MEM_COMMIT, PAGE_READWRITE);
		if (!CommittedMemory)
		{
			return nullptr;
		}
	}

	//할당정보(AllocationData) 설정
	AllocationData* AllocData = static_cast<AllocationData*>(ReturnedPointer) - 1;
	AllocData->FullAllocationPointer = FullAllocationPointer;
	AllocData->FullSize = TotalAllocationSize;
	AllocData->Size = AlignedSize;
	//XOR 연산으로 간단하게 검증
	AllocData->Sentinel = reinterpret_cast<size_t>(FullAllocationPointer) ^ TotalAllocationSize ^ AlignedSize;

	//포인터 반환
	return ReturnedPointer;
}

void StompAllocator::Free(void* InPtr)
{
	//방어 코드
	if (InPtr == nullptr)
	{
		return;
	}
	//할당정보 위치: 데이터 바로 앞
	AllocationData* AllocDataPtr = reinterpret_cast<AllocationData*>(InPtr);
	AllocDataPtr--;
	//메모리 정보 체크
	SIZE_T expected = reinterpret_cast<size_t>(AllocDataPtr->FullAllocationPointer) ^ AllocDataPtr->FullSize ^ AllocDataPtr->Size;
	if (AllocDataPtr->Sentinel != expected)
	{
		abort();  //치명적 메모리 손상
	}
	//VirtualAlloc으로 예약한 전체 주소 공간 해제(MEM_RELEASE는 반드시 size=0)
	VirtualFree(AllocDataPtr->FullAllocationPointer, 0, MEM_RELEASE);
}

