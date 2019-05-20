// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/UnrealMemory.h"
#include "Containers/Array.h"

class FSlabAllocator
{
public:
	FSlabAllocator(uint64 InSlabSize)
		: SlabSize(InSlabSize)
	{

	}

	~FSlabAllocator()
	{
		for (void* Slab : Slabs)
		{
			FMemory::Free(Slab);
		}
	}

	template<typename T>
	T* Allocate(uint64 Count)
	{
		uint64 AllocationSize = Count * sizeof(T) + (16 - 1) / 16;
		if (!CurrentSlab || CurrentSlabAllocatedSize + AllocationSize > SlabSize)
		{
			TotalAllocatedSize += SlabSize;
			void* Allocation = FMemory::Malloc(SlabSize, 16);
			CurrentSlab = reinterpret_cast<uint8*>(Allocation);
			CurrentSlabAllocatedSize = 0;
			Slabs.Add(CurrentSlab);
		}
		T* Allocation = reinterpret_cast<T*>(CurrentSlab + CurrentSlabAllocatedSize);
		CurrentSlabAllocatedSize += AllocationSize;
		TotalItemCount += Count;
		return Allocation;
	}

private:
	TArray<void*> Slabs;
	uint8* CurrentSlab = nullptr;
	const uint64 SlabSize;
	uint64 CurrentSlabAllocatedSize = 0;
	uint64 TotalItemCount = 0;
	uint64 TotalAllocatedSize = 0;
};
