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
	void	Initialize();
	void	EnableTracePump();
	void	CoreAdd(void* Base, size_t Size, void* Owner);
	void	CoreRemove(void* Base, size_t Size, void* Owner);
	void	Alloc(void* Address, size_t Size, uint32 Alignment, void* Owner);
	void	Free(void* Address);
	void	ReallocAlloc(void* Address, size_t Size, uint32 Alignment, void* Owner);
	void	ReallocFree(void* Address);

private:
	void				Update();
	uint64				BaseCycle;
	std::atomic<uint32>	MarkerCounter;
	bool				bPumpTrace = false;
	static const uint32 MarkerSamplePeriod	= (4 << 10) - 1;
	static const uint32 SizeShift = 3;
};

#endif // UE_MEMORY_TRACE_ENABLED
