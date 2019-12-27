// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/UnrealMemory.h"
#include "Containers/Array.h"
#include "TraceServices/Containers/Allocators.h"

class FSlabAllocator
	: public Trace::ILinearAllocator
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

	virtual void* Allocate(uint64 Size) override
	{
		uint64 AllocationSize = Size + (16 - 1) / 16;
		if (!CurrentSlab || CurrentSlabAllocatedSize + AllocationSize > SlabSize)
		{
			TotalAllocatedSize += SlabSize;
			void* Allocation = FMemory::Malloc(SlabSize, 16);
			CurrentSlab = reinterpret_cast<uint8*>(Allocation);
			CurrentSlabAllocatedSize = 0;
			Slabs.Add(CurrentSlab);
		}
		void* Allocation = CurrentSlab + CurrentSlabAllocatedSize;
		CurrentSlabAllocatedSize += AllocationSize;
		return Allocation;
	}

private:
	TArray<void*> Slabs;
	uint8* CurrentSlab = nullptr;
	const uint64 SlabSize;
	uint64 CurrentSlabAllocatedSize = 0;
	uint64 TotalAllocatedSize = 0;
};
