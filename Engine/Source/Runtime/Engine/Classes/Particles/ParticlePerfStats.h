// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/Atomic.h"


#define WITH_GLOBAL_RUNTIME_FX_BUDGET (!UE_SERVER)
#ifndef WITH_PARTICLE_PERF_STATS
	#define WITH_PARTICLE_PERF_STATS ((!UE_BUILD_SHIPPING) || WITH_GLOBAL_RUNTIME_FX_BUDGET)
#else
	#if !WITH_PARTICLE_PERF_STATS
		//If perf stats are explicitly disabled then we must also disable the runtime budget tracking.
		#undef WITH_GLOBAL_RUNTIME_FX_BUDGET
		#define WITH_GLOBAL_RUNTIME_FX_BUDGET 0
	#endif
#endif


#if WITH_PARTICLE_PERF_STATS

#include "Containers/Array.h"
#include "Templates/Atomic.h"

struct FParticlePerfStats;

struct ENGINE_API FParticlePerfStatsContext
{
	FORCEINLINE bool IsValid()const { return WorldStats != nullptr && SystemStats != nullptr; }

	FParticlePerfStats* WorldStats = nullptr;
	FParticlePerfStats* SystemStats = nullptr;

	//TODO: Per component stat tracking.
	//FParticlePerfStats* ComponentStats;
};

/** Stats gathered on the game thread or game thread spawned tasks. */
struct ENGINE_API FParticlePerfStats_GT
{
	uint64 NumInstances;
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
	FORCEINLINE uint64 GetTotalCycles_GTOnly()const { return TickGameThreadCycles + FinalizeCycles; }
	FORCEINLINE uint64 GetTotalCycles()const { return TickGameThreadCycles + TickConcurrentCycles + FinalizeCycles + EndOfFrameCycles; }
	FORCEINLINE uint64 GetPerInstanceAvgCycles()const { return NumInstances > 0 ? (TickGameThreadCycles + TickConcurrentCycles + FinalizeCycles + EndOfFrameCycles) / NumInstances : 0; }
};

/** Stats gathered on the render thread. */
struct ENGINE_API FParticlePerfStats_RT
{
	uint64 NumInstances = 0;
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

	static FParticlePerfStatsContext GetPerfStats(class UWorld* World, class UFXSystemAsset* FXAsset);
	FORCEINLINE static bool GetStatsEnabled() { return bStatsEnabled.Load(EMemoryOrder::Relaxed); }
	FORCEINLINE static bool GetGatherWorldStats() { return WorldStatsReaders.Load(EMemoryOrder::Relaxed) > 0; }
	FORCEINLINE static bool GetGatherSystemStats() { return SystemStatsReaders.Load(EMemoryOrder::Relaxed) > 0; }
	FORCEINLINE static bool ShouldGatherStats() { return GetStatsEnabled() && (GetGatherWorldStats() || GetGatherSystemStats()); }

	FORCEINLINE static void SetStatsEnabled(bool bEnabled) { bStatsEnabled.Store(bEnabled); }
	FORCEINLINE static void AddWorldStatReader() { ++WorldStatsReaders; }
	FORCEINLINE static void RemoveWorldStatReader() { --WorldStatsReaders; }
	FORCEINLINE static void AddSystemStatReader() { ++SystemStatsReaders; }
	FORCEINLINE static void RemoveSystemStatReader() { --SystemStatsReaders; }

	static TAtomic<bool>	bStatsEnabled;
	static TAtomic<int32>	WorldStatsReaders;
	static TAtomic<int32>	SystemStatsReaders;

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

#define PARTICLE_PERF_STAT_INSTANCE_COMMON(CONTEXT, COUNT, THREAD) \
	{\
		if(CONTEXT.IsValid())\
		{\
			CONTEXT.WorldStats->Get##THREAD##Stats().NumInstances += COUNT;\
			CONTEXT.SystemStats->Get##THREAD##Stats().NumInstances += COUNT;\
		}\
	}
	

#define PARTICLE_PERF_STAT_INSTANCE_COUNT_GT(CONTEXT, COUNT) PARTICLE_PERF_STAT_INSTANCE_COMMON(CONTEXT, COUNT, GameThread)
#define PARTICLE_PERF_STAT_INSTANCE_COUNT_RT(CONTEXT, COUNT) PARTICLE_PERF_STAT_INSTANCE_COMMON(CONTEXT, COUNT, RenderThread)

#define PARTICLE_PERF_STAT_CYCLES_COMMON(CONTEXT, NAME, THREAD) \
	struct FScopedParticleStat_##NAME \
	{ \
		FORCEINLINE FScopedParticleStat_##NAME(struct FParticlePerfStatsContext InContext) \
		: StartCycles(0) \
		, Context(InContext) \
		{ \
			if(Context.IsValid())\
			{ \
				StartCycles = FPlatformTime::Cycles64(); \
			} \
		} \
		FORCEINLINE ~FScopedParticleStat_##NAME() \
		{ \
			if(StartCycles != 0) \
			{ \
				uint64 Cycles = FPlatformTime::Cycles64() - StartCycles; \
				Context.WorldStats->Get##THREAD##Stats().NAME##Cycles += Cycles; \
				Context.SystemStats->Get##THREAD##Stats().NAME##Cycles += Cycles; \
			} \
		} \
		uint64 StartCycles = 0; \
		FParticlePerfStatsContext Context; \
	} NAME##Stat(CONTEXT);

#define PARTICLE_PERF_STAT_CYCLES_GT(CONTEXT, NAME) PARTICLE_PERF_STAT_CYCLES_COMMON(CONTEXT, NAME, GameThread)
#define PARTICLE_PERF_STAT_CYCLES_RT(CONTEXT, NAME) PARTICLE_PERF_STAT_CYCLES_COMMON(CONTEXT, NAME, RenderThread)

#else //WITH_PARTICLE_PERF_STATS

#define PARTICLE_PERF_STAT_INSTANCE_COUNT_GT(CONTEXT, COUNT)
#define PARTICLE_PERF_STAT_INSTANCE_COUNT_RT(CONTEXT, COUNT)
#define PARTICLE_PERF_STAT_CYCLES_GT(CONTEXT, NAME)
#define PARTICLE_PERF_STAT_CYCLES_RT(CONTEXT, NAME) 

struct ENGINE_API FParticlePerfStatsContext{};

#endif //WITH_PARTICLE_PERF_STATS
