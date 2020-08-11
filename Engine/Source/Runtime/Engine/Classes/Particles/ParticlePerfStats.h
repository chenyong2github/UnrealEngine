// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/Atomic.h"

#ifndef WITH_PARTICLE_PERF_STATS
	#define WITH_PARTICLE_PERF_STATS 0
#endif

#if WITH_PARTICLE_PERF_STATS

#include "Containers/Array.h"

struct ENGINE_API FParticlePerfStats
{
	struct FGroupedStats
	{
		FGroupedStats()
			: TickConcurrentCycles(0)
			, EndOfFrameCycles(0)
		{
			Reset();
		}

		void Reset()
		{
			NumInstances = 0;

			TickGameThreadCycles = 0;
			TickConcurrentCycles = 0;
			FinalizeCycles = 0;
			EndOfFrameCycles = 0;

			RenderUpdateCycles = 0;
			GetDynamicMeshElementsCycles = 0;
		}

		uint32			NumInstances;					// Total instances that this is for

		uint64			TickGameThreadCycles;			// Total cycles spent during game thread update (Note can include concurrent tick / finalize if we don't tick async)
		TAtomic<uint64>	TickConcurrentCycles;			// Total cycles spent during concurrent thread update
		uint64			FinalizeCycles;					// Total cycles spent finalizing
		TAtomic<uint64>	EndOfFrameCycles;				// Total cycles spent during end of frame updates

		uint64			RenderUpdateCycles;				// Total cycles spent doing render updates
		uint64			GetDynamicMeshElementsCycles;	// Total cycles spent setting up dynamic mesh elements
	};

	static constexpr int32 kNumMaxSamples = 10;

	FParticlePerfStats()
	{
		Reset();
	}

	uint32			AccumulatedNumFrames = 0;
	FGroupedStats	AccumulatedStats;

	TArray<uint64, TInlineAllocator<kNumMaxSamples>>	MaxPerFrameTotalGameThreadCycles;
	TArray<uint64, TInlineAllocator<kNumMaxSamples>>	MaxPerFrameTotalRenderThreadCycles;

	FGroupedStats	CurrentFrameStats;

	static FParticlePerfStats* GetPerfStats(class UFXSystemAsset* Asset);

	void Reset();
	void Tick();

	static void OnStartup();
	static void OnShutdown();
	static int32 RenderStats(class UWorld* World, class FViewport* Viewport, class FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation, const FRotator* ViewRotation);
};

//-TODO: Need to remove the task graph WakeUp cost otherwise it skews results!
#define PARTICLE_PERF_STAT_INSTANCE_COUNT(STATS, COUNT) \
	FParticlePerfStats::GetPerfStats(STATS)->CurrentFrameStats.NumInstances += COUNT

#define PARTICLE_PERF_STAT_CYCLES(STATS, NAME) \
	struct FScopedParticleStat_##NAME \
	{ \
		FORCEINLINE FScopedParticleStat_ ## NAME(class UFXSystemAsset* InSystemAsset) \
			: SystemAsset(InSystemAsset) \
			, StartCycles(FPlatformTime::Cycles64()) \
		{ \
		} \
		FORCEINLINE ~FScopedParticleStat_##NAME() \
		{ \
			FParticlePerfStats::GetPerfStats(SystemAsset)->CurrentFrameStats.NAME##Cycles += FPlatformTime::Cycles64() - StartCycles; \
		} \
		class UFXSystemAsset* SystemAsset; \
		uint64 StartCycles; \
	} NAME##Stat(STATS);

#else //WITH_PARTICLE_PERF_STATS

#define PARTICLE_PERF_STAT_INSTANCE_COUNT(STATS, COUNT)
#define PARTICLE_PERF_STAT_CYCLES(STATS, NAME)

#endif //WITH_PARTICLE_PERF_STATS
