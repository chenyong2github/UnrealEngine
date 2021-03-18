// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/Atomic.h"
#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"
#include "Misc/CoreMiscDefines.h"
#include "Containers/Array.h"


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

#define WITH_PER_SYSTEM_PARTICLE_PERF_STATS (WITH_PARTICLE_PERF_STATS && !UE_BUILD_SHIPPING)
#define WITH_PER_COMPONENT_PARTICLE_PERF_STATS (WITH_PARTICLE_PERF_STATS && !UE_BUILD_SHIPPING)

struct FParticlePerfStats;
class UWorld;
class UFXSystemAsset;
class UFXSystemComponent;

#if WITH_PARTICLE_PERF_STATS

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

	FORCEINLINE static bool GetStatsEnabled() { return bStatsEnabled.Load(EMemoryOrder::Relaxed); }
	FORCEINLINE static bool GetGatherWorldStats() { return WorldStatsReaders.Load(EMemoryOrder::Relaxed) > 0; }
	FORCEINLINE static bool GetGatherSystemStats() { return SystemStatsReaders.Load(EMemoryOrder::Relaxed) > 0; }
	FORCEINLINE static bool GetGatherComponentStats() { return ComponentStatsReaders.Load(EMemoryOrder::Relaxed) > 0; }
	FORCEINLINE static bool ShouldGatherStats() 
	{
		return GetStatsEnabled() && 
			(GetGatherWorldStats() 
			#if WITH_PER_SYSTEM_PARTICLE_PERF_STATS
			|| GetGatherSystemStats() 
			#endif
			#if WITH_PER_COMPONENT_PARTICLE_PERF_STATS
			|| GetGatherComponentStats()
			#endif
			);
	}

	FORCEINLINE static void SetStatsEnabled(bool bEnabled) { bStatsEnabled.Store(bEnabled); }
	FORCEINLINE static void AddWorldStatReader() { ++WorldStatsReaders; }
	FORCEINLINE static void RemoveWorldStatReader() { --WorldStatsReaders; }
	FORCEINLINE static void AddSystemStatReader() { ++SystemStatsReaders; }
	FORCEINLINE static void RemoveSystemStatReader() { --SystemStatsReaders; }
	FORCEINLINE static void AddComponentStatReader() { ++ComponentStatsReaders; }
	FORCEINLINE static void RemoveComponentStatReader() { --ComponentStatsReaders; }

	static FORCEINLINE FParticlePerfStats* GetStats(UWorld* World)
	{
		if (World && GetGatherWorldStats() && GetStatsEnabled())
		{
			return GetWorldPerfStats(World);
		}
		return nullptr;
	}
	
	static FORCEINLINE FParticlePerfStats* GetStats(UFXSystemAsset* System)
	{
	#if WITH_PER_SYSTEM_PARTICLE_PERF_STATS
		if (System && GetGatherSystemStats() && GetStatsEnabled())
		{
			return GetSystemPerfStats(System);
		}
	#endif
		return nullptr;
	}

	static FORCEINLINE FParticlePerfStats* GetStats(UFXSystemComponent* Component)
	{
	#if WITH_PER_COMPONENT_PARTICLE_PERF_STATS
		if (Component && GetGatherComponentStats() && GetStatsEnabled())
		{
			return GetComponentPerfStats(Component);
		}
	#endif
		return nullptr;
	}

	static TAtomic<bool>	bStatsEnabled;
	static TAtomic<int32>	WorldStatsReaders;
	static TAtomic<int32>	SystemStatsReaders;
	static TAtomic<int32>	ComponentStatsReaders;

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

private:

	static FParticlePerfStats* GetWorldPerfStats(UWorld* World);
	static FParticlePerfStats* GetSystemPerfStats(UFXSystemAsset* FXAsset);
	static FParticlePerfStats* GetComponentPerfStats(UFXSystemComponent* FXComponent);

};

struct FParticlePerfStatsContext
{
	FORCEINLINE FParticlePerfStatsContext(FParticlePerfStats* InWorldStats, FParticlePerfStats* InSystemStats, FParticlePerfStats* InComponentStats)
	{
		SetWorldStats(InWorldStats);
		SetSystemStats(InSystemStats);
		SetComponentStats(InComponentStats);
	}

	FORCEINLINE FParticlePerfStatsContext(FParticlePerfStats* InWorldStats, FParticlePerfStats* InSystemStats)
	{
		SetWorldStats(InWorldStats);
		SetSystemStats(InSystemStats);
	}

	FORCEINLINE FParticlePerfStatsContext(FParticlePerfStats* InComponentStats)
	{
		SetComponentStats(InComponentStats);
	}

	FORCEINLINE FParticlePerfStatsContext(UWorld* InWorld, UFXSystemAsset* InSystem, UFXSystemComponent* InComponent)
	{
		SetWorldStats(FParticlePerfStats::GetStats(InWorld));
		SetSystemStats(FParticlePerfStats::GetStats(InSystem));
		SetComponentStats(FParticlePerfStats::GetStats(InComponent));
	}

	FORCEINLINE FParticlePerfStatsContext(UWorld* InWorld, UFXSystemAsset* InSystem)
	{
		SetWorldStats(FParticlePerfStats::GetStats(InWorld));
		SetSystemStats(FParticlePerfStats::GetStats(InSystem));
	}

	FORCEINLINE FParticlePerfStatsContext(UFXSystemComponent* InComponent)
	{
		SetComponentStats(FParticlePerfStats::GetStats(InComponent));
	}

	FORCEINLINE bool IsValid()
	{
		return GetWorldStats() != nullptr || GetSystemStats() != nullptr || GetComponentStats() != nullptr;
	}

	FParticlePerfStats* WorldStats = nullptr;
	FORCEINLINE FParticlePerfStats* GetWorldStats() { return WorldStats; }
	FORCEINLINE void SetWorldStats(FParticlePerfStats* Stats) { WorldStats = Stats; }

#if WITH_PER_SYSTEM_PARTICLE_PERF_STATS
	FParticlePerfStats* SystemStats = nullptr;
	FORCEINLINE FParticlePerfStats* GetSystemStats() { return SystemStats; }
	FORCEINLINE void SetSystemStats(FParticlePerfStats* Stats) { SystemStats = Stats; }
#else
	FORCEINLINE FParticlePerfStats* GetSystemStats() { return nullptr; }
	FORCEINLINE void SetSystemStats(FParticlePerfStats* Stats) { }
#endif

#if WITH_PER_COMPONENT_PARTICLE_PERF_STATS
	FParticlePerfStats* ComponentStats = nullptr;
	FORCEINLINE FParticlePerfStats* GetComponentStats() { return ComponentStats; }
	FORCEINLINE void SetComponentStats(FParticlePerfStats* Stats) { ComponentStats = Stats; }
#else
	FORCEINLINE FParticlePerfStats* GetComponentStats() { return nullptr; }
	FORCEINLINE void SetComponentStats(FParticlePerfStats* Stats) { }
#endif
};

typedef TFunction<void(FParticlePerfStats* Stats, uint64 Cycles)> FParticlePerfStatsWriterFunc;

struct FParticlePerfStatScope
{
	FORCEINLINE FParticlePerfStatScope(FParticlePerfStatsContext InContext, FParticlePerfStatsWriterFunc InWriter)
	: Writer(InWriter)
	, Context(InContext)
	, StartCycles(INDEX_NONE)
	{
		if (Context.IsValid())
		{
			StartCycles = FPlatformTime::Cycles64();
		}
	}

	FORCEINLINE ~FParticlePerfStatScope()
	{
		if (StartCycles != INDEX_NONE)
		{
			uint64 Cycles = FPlatformTime::Cycles64() - StartCycles;
			Writer(Context.GetWorldStats(), Cycles);
			Writer(Context.GetSystemStats(), Cycles);
			Writer(Context.GetComponentStats(), Cycles);
		}
	}
	
	FParticlePerfStatsWriterFunc Writer;
	FParticlePerfStatsContext Context;
	uint64 StartCycles = 0;
};

#define PARTICLE_PERF_STAT_CYCLES_COMMON(CONTEXT, THREAD, NAME)\
FParticlePerfStatScope ANONYMOUS_VARIABLE(ParticlePerfStatScope##THREAD##NAME)(CONTEXT,\
[](FParticlePerfStats* Stats, uint64 Cycles)\
{\
	if(Stats){ Stats->Get##THREAD##Stats().NAME##Cycles += Cycles; }\
});

#define PARTICLE_PERF_STAT_CYCLES_WITH_COUNT_COMMON(CONTEXT, THREAD, NAME, COUNT)\
FParticlePerfStatScope ANONYMOUS_VARIABLE(ParticlePerfStatScope##THREAD##NAME)(CONTEXT,\
[=](FParticlePerfStats* Stats, uint64 Cycles)\
{\
	if(Stats)\
	{\
		Stats->Get##THREAD##Stats().NAME##Cycles += Cycles; \
		Stats->Get##THREAD##Stats().NumInstances += COUNT; \
	}\
});

#define PARTICLE_PERF_STAT_CYCLES_GT(CONTEXT, NAME) PARTICLE_PERF_STAT_CYCLES_COMMON(CONTEXT, GameThread, NAME)
#define PARTICLE_PERF_STAT_CYCLES_RT(CONTEXT, NAME) PARTICLE_PERF_STAT_CYCLES_COMMON(CONTEXT, RenderThread, NAME)

#define PARTICLE_PERF_STAT_CYCLES_WITH_COUNT_GT(CONTEXT, NAME, COUNT) PARTICLE_PERF_STAT_CYCLES_WITH_COUNT_COMMON(CONTEXT, GameThread, NAME, COUNT)
#define PARTICLE_PERF_STAT_CYCLES_WITH_COUNT_RT(CONTEXT, NAME, COUNT) PARTICLE_PERF_STAT_CYCLES_WITH_COUNT_COMMON(CONTEXT, RenderThread, NAME, COUNT)

#else //WITH_PARTICLE_PERF_STATS

#define PARTICLE_PERF_STAT_CYCLES_GT(CONTEXT, NAME)
#define PARTICLE_PERF_STAT_CYCLES_RT(CONTEXT, NAME)

#define PARTICLE_PERF_STAT_CYCLES_WITH_COUNT_GT(CONTEXT, NAME, COUNT)
#define PARTICLE_PERF_STAT_CYCLES_WITH_COUNT_RT(CONTEXT, NAME, COUNT)

struct FParticlePerfStatsContext
{
	FORCEINLINE FParticlePerfStatsContext(FParticlePerfStats* InWorldStats, FParticlePerfStats* InSystemStats, FParticlePerfStats* InComponentStats){}
	FORCEINLINE FParticlePerfStatsContext(FParticlePerfStats* InWorldStats, FParticlePerfStats* InSystemStats) {}
	FORCEINLINE FParticlePerfStatsContext(FParticlePerfStats* InComponentStats) {}
	FORCEINLINE FParticlePerfStatsContext(UWorld* InWorld, UFXSystemAsset* InSystem, UFXSystemComponent* InComponent) {}
	FORCEINLINE FParticlePerfStatsContext(UWorld* InWorld, UFXSystemAsset* InSystem) {}
	FORCEINLINE FParticlePerfStatsContext(UFXSystemComponent* InComponent) {}
};

#endif //WITH_PARTICLE_PERF_STATS
