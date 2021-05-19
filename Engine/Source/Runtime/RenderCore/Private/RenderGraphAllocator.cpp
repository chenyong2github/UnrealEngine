// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderGraphAllocator.h"
#include "RenderGraphPrivate.h"

FRDGAllocator& FRDGAllocator::Get()
{
	static FRDGAllocator Instance;
	return Instance;
}

FRDGAllocator::~FRDGAllocator()
{
	check(MemStack.IsEmpty());
}

void FRDGAllocator::ReleaseAll()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FRDGAllocator::ReleaseAll);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE_CONDITIONAL(RDGAllocator_Clear, GRDGVerboseCSVStats != 0);
	for (int32 Index = TrackedAllocs.Num() - 1; Index >= 0; --Index)
	{
		TrackedAllocs[Index]->~FTrackedAlloc();
	}
	TrackedAllocs.Reset();
	MemStack.Flush();
}