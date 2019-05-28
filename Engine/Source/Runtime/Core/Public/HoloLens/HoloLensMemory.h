// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


/*=============================================================================================
HoloLensPlatformMemory.h: HoloLens platform memory functions
==============================================================================================*/

#pragma once
#include "GenericPlatform/GenericPlatformMemory.h"
#include "HoloLens/HoloLensSystemIncludes.h"

/**
*	HoloLens implementation of the FGenericPlatformMemoryStats.
*	At this moment it's just the same as the FGenericPlatformMemoryStats.
*	Can be extended as shown in the following example.
*/
struct FPlatformMemoryStats : public FGenericPlatformMemoryStats
{
	/** Default constructor, clears all variables. */
	FPlatformMemoryStats()
		: FGenericPlatformMemoryStats()
		, HoloLensSpecificMemoryStat(0)
	{}

	/** Memory stat specific only for HoloLens. */
	SIZE_T HoloLensSpecificMemoryStat;
};

/**
* HoloLens implementation of the memory OS functions
**/
struct CORE_API FHoloLensPlatformMemory : public FGenericPlatformMemory
{
	enum EMemoryCounterRegion
	{
		MCR_Invalid, // not memory
		MCR_Physical, // main system memory
		MCR_GPU, // memory directly a GPU (graphics card, etc)
		MCR_GPUSystem, // system memory directly accessible by a GPU
		MCR_TexturePool, // presized texture pools
		MCR_StreamingPool, // amount of texture pool available for streaming.
		MCR_UsedStreamingPool, // amount of texture pool used for streaming.
		MCR_GPUDefragPool, // presized pool of memory that can be defragmented.
		MCR_SamplePlatformSpecifcMemoryRegion,
		MCR_PhysicalLLM, // total physical memory displayed in the LLM stats (on consoles CPU + GPU)
		MCR_MAX
	};

	/**
	* HoloLens representation of a shared memory region
	*/
	struct FHoloLensSharedMemoryRegion : public FSharedMemoryRegion
	{
		/** Returns the handle to file mapping object. */
		HANDLE GetMapping() const { return Mapping; }

		FHoloLensSharedMemoryRegion(const FString& InName, uint32 InAccessMode, void* InAddress, SIZE_T InSize, HANDLE InMapping)
			: FSharedMemoryRegion(InName, InAccessMode, InAddress, InSize)
			, Mapping(InMapping)
		{}

	protected:

		/** Handle of a file mapping object */
		HANDLE				Mapping;
	};

	//~ Begin FGenericPlatformMemory Interface
	static void Init();
	static bool SupportBackupMemoryPool()
	{
		return true;
	}
	static class FMalloc* BaseAllocator();
	static FPlatformMemoryStats GetStats();
	static void GetStatsForMallocProfiler(FGenericMemoryStats& out_Stats);
	static const FPlatformMemoryConstants& GetConstants();
	static void* BinnedAllocFromOS(SIZE_T Size);
	static void BinnedFreeToOS(void* Ptr, SIZE_T Size);
	static FSharedMemoryRegion* MapNamedSharedMemoryRegion(const FString& InName, bool bCreate, uint32 AccessMode, SIZE_T Size);
	static bool UnmapNamedSharedMemoryRegion(FSharedMemoryRegion * MemoryRegion);
protected:
	friend struct FGenericStatsUpdater;

	static void InternalUpdateStats(const FPlatformMemoryStats& MemoryStats);
	//~ End FGenericPlatformMemory Interface
};

typedef FHoloLensPlatformMemory FPlatformMemory;
