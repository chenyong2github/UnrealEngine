// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/Atomic.h"

#ifndef WITH_PARTICLE_PERF_STATS
	#define WITH_PARTICLE_PERF_STATS (!UE_BUILD_SHIPPING)
#endif

#if WITH_PARTICLE_PERF_STATS

#include "Containers/Array.h"
#include "Templates/Atomic.h"

/** Stats gathered on the game thread or game thread spawned tasks. */
struct ENGINE_API FParticlePerfStats_GT
{
	uint32 NumInstances;
	uint64 TickGameThreadCycles;
	TAtomic<uint64> TickConcurrentCycles;
	uint64 FinalizeCycles;
	TAtomic<uint64> EndOfFrameCycles;

	FParticlePerfStats_GT() { Reset(); }
	
	FParticlePerfStats_GT(const FParticlePerfStats_GT& Other)
	{
		NumInstances = Other.NumInstances;
		TickGameThreadCycles = Other.TickGameThreadCycles;
		TickConcurrentCycles = Other.TickConcurrentCycles.Load();
		FinalizeCycles = Other.FinalizeCycles;
		EndOfFrameCycles = Other.EndOfFrameCycles.Load();
	}

	FParticlePerfStats_GT& operator=(const FParticlePerfStats_GT& Other)
	{
		NumInstances = Other.NumInstances;
		TickGameThreadCycles = Other.TickGameThreadCycles;
		TickConcurrentCycles = Other.TickConcurrentCycles.Load();
		FinalizeCycles = Other.FinalizeCycles;
		EndOfFrameCycles = Other.EndOfFrameCycles.Load();
		return *this;
	}

	FParticlePerfStats_GT(FParticlePerfStats_GT&& Other)
	{
		*this = Other;
		Other.Reset();
	}

	FParticlePerfStats_GT& operator=(FParticlePerfStats_GT&& Other)
	{
		*this = Other;
		Other.Reset();		
		return *this;
	}

	FORCEINLINE void Reset()
	{
		NumInstances = 0;
		TickGameThreadCycles = 0;
		TickConcurrentCycles = 0;
		FinalizeCycles = 0;
		EndOfFrameCycles = 0;
	}
	FORCEINLINE uint64 GetTotalCycles()const { return TickGameThreadCycles + TickConcurrentCycles + FinalizeCycles + EndOfFrameCycles; }
	FORCEINLINE uint64 GetPerInstanceAvgCycles()const { return NumInstances > 0 ? (TickGameThreadCycles + TickConcurrentCycles + FinalizeCycles + EndOfFrameCycles) / NumInstances : 0; }
};

/** Stats gathered on the render thread. */
struct ENGINE_API FParticlePerfStats_RT
{
	uint32 NumInstances = 0;
	uint64 RenderUpdateCycles = 0;
	uint64 GetDynamicMeshElementsCycles = 0;
	
	FParticlePerfStats_RT()	{ Reset();	}
	FORCEINLINE void Reset()
	{
		NumInstances = 0;
		RenderUpdateCycles = 0;
		GetDynamicMeshElementsCycles = 0;
	}
	FORCEINLINE uint64 GetTotalCycles()const { return RenderUpdateCycles + GetDynamicMeshElementsCycles; }
	FORCEINLINE uint64 GetPerInstanceAvgCycles()const { return NumInstances > 0 ? (RenderUpdateCycles + GetDynamicMeshElementsCycles) / NumInstances : 0; }
};

struct ENGINE_API FParticlePerfStats
{
	FParticlePerfStats();

	void Reset(bool bSyncWithRT);
	void ResetGT();
	void ResetRT();
	void Tick();
	void TickRT();

	static FParticlePerfStats* GetPerfStats(class UFXSystemAsset* Asset);
	FORCEINLINE static bool GetStatsEnabled() { return bStatsEnabled.Load(); }
	FORCEINLINE static bool GetGatherStats() { return bGatherStats.Load(); }
	FORCEINLINE static bool ShouldGatherStats() { return GetStatsEnabled() && GetGatherStats(); }

	FORCEINLINE static void SetStatsEnabled(bool bEnabled) { bStatsEnabled.Store(bEnabled); }
	FORCEINLINE static void SetGatherStats(bool bEnabled) { bGatherStats.Store(bEnabled); }

	static TAtomic<bool>	bStatsEnabled;
	static TAtomic<bool>	bGatherStats;

	/** Stats on GT and GT spawned concurrent work. */
	FParticlePerfStats_GT GameThreadStats;

	/** Stats on RT work. */
	FParticlePerfStats_RT RenderThreadStats;

	/** Returns the current frame Game Thread stats. */
	FORCEINLINE FParticlePerfStats_GT& GetGameThreadStats()
	{
		return GameThreadStats; 
	}

	/** Returns the current frame Render Thread stats. */
	FORCEINLINE FParticlePerfStats_RT& GetRenderThreadStats()
	{
		return RenderThreadStats;
	}
};

//-TODO: Need to remove the task graph WakeUp cost otherwise it skews results!

#define PARTICLE_PERF_STAT_INSTANCE_COMMON(STATS, COUNT, THREAD) \
	FParticlePerfStats::GetPerfStats(STATS)->Get##THREAD##Stats().NumInstances += COUNT

#define PARTICLE_PERF_STAT_INSTANCE_COUNT_GT(STATS, COUNT) PARTICLE_PERF_STAT_INSTANCE_COMMON(STATS, COUNT, GameThread)
#define PARTICLE_PERF_STAT_INSTANCE_COUNT_RT(STATS, COUNT) PARTICLE_PERF_STAT_INSTANCE_COMMON(STATS, COUNT, RenderThread)

#define PARTICLE_PERF_STAT_CYCLES_COMMON(STATS, NAME, THREAD) \
	struct FScopedParticleStat_##NAME \
	{ \
		FORCEINLINE FScopedParticleStat_##NAME(class UFXSystemAsset* InSystemAsset) \
			: SystemAsset(nullptr) \
			, StartCycles(0) \
		{ \
			if(FParticlePerfStats::ShouldGatherStats()) \
			{ \
				SystemAsset = InSystemAsset; \
				StartCycles = FPlatformTime::Cycles64(); \
			} \
		} \
		FORCEINLINE ~FScopedParticleStat_##NAME() \
		{ \
			if(SystemAsset)\
			{\
				FParticlePerfStats::GetPerfStats(SystemAsset)->Get##THREAD##Stats().NAME##Cycles += FPlatformTime::Cycles64() - StartCycles; \
			}\
		} \
		class UFXSystemAsset* SystemAsset; \
		uint64 StartCycles; \
	} NAME##Stat(STATS);

#define PARTICLE_PERF_STAT_CYCLES_GT(STATS, NAME) PARTICLE_PERF_STAT_CYCLES_COMMON(STATS, NAME, GameThread)
#define PARTICLE_PERF_STAT_CYCLES_RT(STATS, NAME) PARTICLE_PERF_STAT_CYCLES_COMMON(STATS, NAME, RenderThread)

#else //WITH_PARTICLE_PERF_STATS

#define PARTICLE_PERF_STAT_INSTANCE_COUNT_GT(STATS, COUNT)
#define PARTICLE_PERF_STAT_INSTANCE_COUNT_RT(STATS, COUNT)
#define PARTICLE_PERF_STAT_CYCLES_GT(STATS, NAME)
#define PARTICLE_PERF_STAT_CYCLES_RT(STATS, NAME) 

#endif //WITH_PARTICLE_PERF_STATS
