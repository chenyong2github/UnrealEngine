// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

struct FCachedOSPageAllocator
{
protected:
	struct FFreePageBlock
	{
		void*  Ptr;
		SIZE_T ByteSize;

		FFreePageBlock() 
		{
			Ptr      = nullptr;
			ByteSize = 0;
		}
	};

	void* AllocateImpl(SIZE_T Size, uint32 CachedByteLimit, FFreePageBlock* First, FFreePageBlock* Last, uint32& FreedPageBlocksNum, SIZE_T& CachedTotal);
	void FreeImpl(void* Ptr, SIZE_T Size, uint32 NumCacheBlocks, uint32 CachedByteLimit, FFreePageBlock* First, uint32& FreedPageBlocksNum, SIZE_T& CachedTotal);
	void FreeAllImpl(FFreePageBlock* First, uint32& FreedPageBlocksNum, SIZE_T& CachedTotal);
};

template <uint32 NumCacheBlocks, uint32 CachedByteLimit>
struct TCachedOSPageAllocator : private FCachedOSPageAllocator
{
	TCachedOSPageAllocator()
		: CachedTotal(0)
		, FreedPageBlocksNum(0)
	{
	}

	FORCEINLINE void* Allocate(SIZE_T Size)
	{
		return AllocateImpl(Size, CachedByteLimit, FreedPageBlocks, FreedPageBlocks + FreedPageBlocksNum, FreedPageBlocksNum, CachedTotal);
	}

	void Free(void* Ptr, SIZE_T Size)
	{
		return FreeImpl(Ptr, Size, NumCacheBlocks, CachedByteLimit, FreedPageBlocks, FreedPageBlocksNum, CachedTotal);
	}
	void FreeAll()
	{
		return FreeAllImpl(FreedPageBlocks, FreedPageBlocksNum, CachedTotal);
	}

	uint64 GetCachedFreeTotal()
	{
		return CachedTotal;
	}

private:
	FFreePageBlock FreedPageBlocks[NumCacheBlocks];
	SIZE_T         CachedTotal;
	uint32         FreedPageBlocksNum;
};
