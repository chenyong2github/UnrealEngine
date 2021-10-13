// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ProfilingDebugging/MemoryTrace.h"

#if UE_MEMORY_TRACE_ENABLED

#include "CoreTypes.h"
#include "HAL/Platform.h"
#include "Trace/Trace.inl"

#include <atomic>

////////////////////////////////////////////////////////////////////////////////
class FAllocationTrace
{
public:
	void	Initialize() const;
	void	EnableTracePump();
	HeapId	HeapSpec(HeapId ParentId, const TCHAR* Name, EMemoryTraceHeapFlags Flags = EMemoryTraceHeapFlags::None) const;
	HeapId	RootHeapSpec(const TCHAR* Name, EMemoryTraceHeapFlags = EMemoryTraceHeapFlags::None) const;
   	void	MarkAllocAsHeap(void* Address, HeapId Heap, EMemoryTraceHeapAllocationFlags Flags = EMemoryTraceHeapAllocationFlags::None);
   	void	UnmarkAllocAsHeap(void* Address, HeapId Heap);
	void	Alloc(void* Address, size_t Size, uint32 Alignment, uint32 Owner, HeapId RootHeap = EMemoryTraceRootHeap::SystemMemory);
	void	Free(void* Address, HeapId RootHeap = EMemoryTraceRootHeap::SystemMemory);
	void	ReallocAlloc(void* Address, size_t NewSize, uint32 Alignment, uint32 Owner, HeapId RootHeap = EMemoryTraceRootHeap::SystemMemory);
	void	ReallocFree(void* Address, HeapId RootHeap = EMemoryTraceRootHeap::SystemMemory);

private:
	void				Update();
	uint64				BaseCycle;
	std::atomic<uint32>	MarkerCounter;
	bool				bPumpTrace = false;
};

#endif // UE_MEMORY_TRACE_ENABLED
