// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderGraphAllocator.h"

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
	for (int32 Index = TrackedAllocs.Num() - 1; Index >= 0; --Index)
	{
		TrackedAllocs[Index]->~FTrackedAlloc();
	}
	TrackedAllocs.Reset();
	MemStack.Flush();
}