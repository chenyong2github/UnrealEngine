// Copyright Epic Games, Inc. All Rights Reserved.

#include "Experimental/ConcurrentLinearAllocator.h"

void* FOsAllocator::Malloc(SIZE_T Size, uint32 Alignment)
{
	if (UNLIKELY(!GMalloc))
	{
		// There is no public function to create GMalloc and this will do it for us.
		FMemory::Free(FMemory::Malloc(0));
		check(GMalloc);
	}
	void* Pointer = GMalloc->Malloc(Size, Alignment);
	MemoryTrace_Alloc(uint64(Pointer), Size, Alignment, EMemoryTraceRootHeap::SystemMemory);
	MemoryTrace_MarkAllocAsHeap(uint64(Pointer), EMemoryTraceRootHeap::SystemMemory);
	return Pointer;
}

void FOsAllocator::Free(void* Pointer, SIZE_T Size)
{
	MemoryTrace_UnmarkAllocAsHeap(uint64(Pointer), EMemoryTraceRootHeap::SystemMemory);
	MemoryTrace_Free(uint64(Pointer), EMemoryTraceRootHeap::SystemMemory);
	GMalloc->Free(Pointer);
}

FORCENOINLINE void UE::Core::Private::OnInvalidConcurrentLinearArrayAllocatorNum(int32 NewNum, SIZE_T NumBytesPerElement)
{
	UE_LOG(LogCore, Fatal, TEXT("Trying to resize TConcurrentLinearArrayAllocator to an invalid size of %d with element size %" SIZE_T_FMT), NewNum, NumBytesPerElement);
	for (;;);
}
