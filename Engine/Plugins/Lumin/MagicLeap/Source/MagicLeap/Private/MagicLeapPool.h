// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"

#define DEBUG_MEM_POOL 0

#if DEBUG_MEM_POOL
DEFINE_LOG_CATEGORY_STATIC(MagicLeapMemPool, Log, All);
#endif // DEBUG_MEM_POOL

template <class T>
class FMagicLeapPool
{
public:
	FMagicLeapPool(int32 InPoolSize)
	: Size(InPoolSize)
	{
		Blocks.AddZeroed(1);
		Blocks[0].AddZeroed(Size);
		for (int32 i = 0; i < Size; ++i)
		{
			Free.Push(&Blocks[0][i]);
		}
	}

	T* GetNextFree()
	{
		if (Free.Num() == 0)
		{
#if DEBUG_MEM_POOL
			UE_LOG(MagicLeapMemPool, Log, TEXT("FMagicLeapPool is out of space.  Allocating new block."));
#endif // DEBUG_MEM_POOL
			int32 NewBlockIndex = Blocks.AddZeroed(1);
			Blocks[NewBlockIndex].AddZeroed(Size);
			for (int32 i = 0; i < Size; ++i)
			{
				Free.Push(&Blocks[NewBlockIndex][i]);
			}
		}
#if DEBUG_MEM_POOL
		T* NewAllocation = Free.Pop();
		UE_LOG(MagicLeapMemPool, Log, TEXT("FMagicLeapPool allocated %p."), NewAllocation);
		Allocated.Push(NewAllocation);
		return NewAllocation;
#else
		return Free.Pop();
#endif // DEBUG_MEM_POOL
	}

	void Release(T* InAllocation)
	{
#if DEBUG_MEM_POOL
		checkf(Allocated.Contains(InAllocation), TEXT("Attempting to release memory that was not allocated by this pool!"));
		Allocated.Pop(InAllocation);
		UE_LOG(MagicLeapMemPool, Log, TEXT("FMagicLeapPool released %p."), InAllocation);
#endif // DEBUG_MEM_POOL
		Free.Push(InAllocation);
	}

private:
	const int32 Size;
	TArray<TArray<T>> Blocks;
	TArray<T*> Free;
#if DEBUG_MEM_POOL
	TArray<T*> Allocated;
#endif // DEBUG_MEM_POOL
};