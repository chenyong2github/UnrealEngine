// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FGrowOnlySpanAllocator
{
public:

	FGrowOnlySpanAllocator() :
		MaxSize(0)
	{}

	// Allocate a range.  Returns allocated StartOffset.
	ENGINE_API int32 Allocate(int32 Num);

	// Free an already allocated range.  
	ENGINE_API void Free(int32 BaseOffset, int32 Num);

	int32 GetMaxSize() const
	{
		return MaxSize;
	}

private:
	class FLinearAllocation
	{
	public:

		FLinearAllocation(int32 InStartOffset, int32 InNum) :
			StartOffset(InStartOffset),
			Num(InNum)
		{}

		int32 StartOffset;
		int32 Num;

		bool Contains(FLinearAllocation Other)
		{
			return StartOffset <= Other.StartOffset && (StartOffset + Num) >= (Other.StartOffset + Other.Num);
		}
	};

	// Size of the linear range used by the allocator
	int32 MaxSize;

	// Unordered free list
	TArray<FLinearAllocation, TInlineAllocator<10>> FreeSpans;

	ENGINE_API int32 SearchFreeList(int32 Num);
};