// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/Array.h"
#include "HAL/CriticalSection.h"
#include "RHIDefinitions.h"
#include "Stats/Stats.h"

struct FRHIDescriptorAllocatorRange;

class RHICORE_API FRHIDescriptorAllocator
{
public:
	FRHIDescriptorAllocator();
	FRHIDescriptorAllocator(uint32 InNumDescriptors, TConstArrayView<TStatId> InStats);
	~FRHIDescriptorAllocator();

	void Init(uint32 InNumDescriptors, TConstArrayView<TStatId> InStats);
	void Shutdown();

	FRHIDescriptorHandle Allocate(ERHIDescriptorHeapType InType);
	void Free(FRHIDescriptorHandle InHandle);

	bool Allocate(uint32 NumDescriptors, uint32& OutSlot);
	void Free(uint32 Slot, uint32 NumDescriptors);

	inline uint32 GetCapacity() const { return Capacity; }

private:
	void RecordAlloc(uint32 Count)
	{
#if STATS
		for (TStatId Stat : Stats)
		{
			INC_DWORD_STAT_BY_FName(Stat.GetName(), Count);
		}
#endif
	}

	void RecordFree(uint32 Count)
	{
#if STATS
		for (TStatId Stat : Stats)
		{
			DEC_DWORD_STAT_BY_FName(Stat.GetName(), Count);
		}
#endif
	}

	TArray<FRHIDescriptorAllocatorRange> Ranges;
	uint32 Capacity = 0;

	FCriticalSection CriticalSection;

#if STATS
	TArray<TStatId> Stats;
#endif
};

class RHICORE_API FRHIHeapDescriptorAllocator : protected FRHIDescriptorAllocator
{
public:
	FRHIHeapDescriptorAllocator() = delete;
	FRHIHeapDescriptorAllocator(ERHIDescriptorHeapType InType, uint32 InDescriptorCount, TConstArrayView<TStatId> InStats);

	FRHIDescriptorHandle Allocate();
	void Free(FRHIDescriptorHandle InHandle);

	bool Allocate(uint32 NumDescriptors, uint32& OutSlot);
	void Free(uint32 Slot, uint32 NumDescriptors);

	using FRHIDescriptorAllocator::GetCapacity;
	inline ERHIDescriptorHeapType GetType() const { return Type; }

	inline bool HandlesAllocation(ERHIDescriptorHeapType InType) const { return GetType() == InType; }

private:
	ERHIDescriptorHeapType Type;
};

class RHICORE_API FRHIOffsetHeapDescriptorAllocator : protected FRHIHeapDescriptorAllocator
{
public:
	FRHIOffsetHeapDescriptorAllocator() = delete;
	FRHIOffsetHeapDescriptorAllocator(ERHIDescriptorHeapType InType, uint32 InDescriptorCount, uint32 InHeapOffset, TConstArrayView<TStatId> InStats);

	FRHIDescriptorHandle Allocate();
	void Free(FRHIDescriptorHandle InHandle);

	using FRHIHeapDescriptorAllocator::GetCapacity;
	using FRHIHeapDescriptorAllocator::GetType;
	using FRHIHeapDescriptorAllocator::HandlesAllocation;

private:
	// Offset from start of heap we belong to
	uint32 HeapOffset;
};
