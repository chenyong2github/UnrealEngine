// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Particles/ParticlePerfStats.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "UObject/WeakObjectPtr.h"
#include "RenderingThread.h"

#if WITH_PARTICLE_PERF_STATS

#define ENABLE_PARTICLE_PERF_STATS_RENDER !UE_BUILD_SHIPPING

class UWorld;
class FParticlePerfStatsListener_DebugRender;


#define ACCUMULATED_PARTICLE_PERF_STAT_MAX_SAMPLES 10

struct ENGINE_API FAccumulatedParticlePerfStats_GT
{
	uint32 NumFrames;
	TArray<uint64, TInlineAllocator<ACCUMULATED_PARTICLE_PERF_STAT_MAX_SAMPLES>> MaxPerFrameTotalCycles;
	TArray<uint64, TInlineAllocator<ACCUMULATED_PARTICLE_PERF_STAT_MAX_SAMPLES>> MaxPerInstanceCycles;

	FParticlePerfStats_GT AccumulatedStats;

	FAccumulatedParticlePerfStats_GT();
	void Reset();
	void Tick(FParticlePerfStats& Stats);

	/** Returns the total cycles used by all GameThread stats. */
	FORCEINLINE uint64 GetTotalCycles()const { return AccumulatedStats.GetTotalCycles(); }

	/** Returns the average cycles per frame by all GameThread stats. */
	FORCEINLINE uint64 GetPerFrameAvgCycles()const { return NumFrames > 0 ? AccumulatedStats.GetTotalCycles() / NumFrames : 0; }
	/** Returns the max cycles per frame by all GameThread stats. */
	FORCEINLINE uint64 GetPerFrameMaxCycles(int32 Index = 0)const { return MaxPerFrameTotalCycles[Index]; }

	/** Returns the average time in µs per frame by all GameThread stats. */
	FORCEINLINE double GetPerFrameAvg()const { return FPlatformTime::ToMilliseconds64(GetPerFrameAvgCycles()) * 1000.0; }
	/** Returns the max time in µs per frame by all GameThread stats. */
	FORCEINLINE double GetPerFrameMax(int32 Index = 0)const { return FPlatformTime::ToMilliseconds64(GetPerFrameMaxCycles(Index)) * 1000.0; }

	/** Returns the average cycles per instance by all GameThread stats. */
	FORCEINLINE uint64 GetPerInstanceAvgCycles()const { return AccumulatedStats.GetPerInstanceAvgCycles(); }
	/** Returns the max cycles per instance by all GameThread stats. */
	FORCEINLINE uint64 GetPerInstanceMaxCycles(int32 Index = 0)const { return MaxPerInstanceCycles[Index]; }

	/** Returns the average time in µs per instance by all GameThread stats. */
	FORCEINLINE double GetPerInstanceAvg()const { return FPlatformTime::ToMilliseconds64(GetPerInstanceAvgCycles()) * 1000.0; }
	/** Returns the max time in µs per instance by all GameThread stats. */
	FORCEINLINE double GetPerInstanceMax(int32 Index = 0)const { return FPlatformTime::ToMilliseconds64(GetPerInstanceMaxCycles(Index)) * 1000.0; }
};

struct ENGINE_API FAccumulatedParticlePerfStats_RT
{
	uint32 NumFrames;
	FParticlePerfStats_RT AccumulatedStats;

	TArray<uint64, TInlineAllocator<ACCUMULATED_PARTICLE_PERF_STAT_MAX_SAMPLES>>	MaxPerFrameTotalCycles;
	TArray<uint64, TInlineAllocator<ACCUMULATED_PARTICLE_PERF_STAT_MAX_SAMPLES>>	MaxPerInstanceCycles;

	FAccumulatedParticlePerfStats_RT();
	FORCEINLINE void Reset();
	void Tick(FParticlePerfStats& Stats);

	/** Returns the total cycles used by all RenderThread stats. */
	FORCEINLINE uint64 GetTotalCycles()const { return AccumulatedStats.GetTotalCycles(); }

	/** Returns the average cycles per frame by all RenderThread stats. */
	FORCEINLINE uint64 GetPerFrameAvgCycles()const { return NumFrames > 0 ? AccumulatedStats.GetTotalCycles() / NumFrames : 0; }
	/** Returns the max cycles per frame by all RenderThread stats. */
	FORCEINLINE uint64 GetPerFrameMaxCycles(int32 Index = 0)const { return MaxPerFrameTotalCycles[Index]; }

	/** Returns the average time in µs per frame by all RenderThread stats. */
	FORCEINLINE double GetPerFrameAvg()const { return FPlatformTime::ToMilliseconds64(GetPerFrameAvgCycles()) * 1000.0; }
	/** Returns the max time in µs per frame by all RenderThread stats. */
	FORCEINLINE double GetPerFrameMax(int32 Index = 0)const { return FPlatformTime::ToMilliseconds64(GetPerFrameMaxCycles(Index)) * 1000.0; }

	/** Returns the average cycles per instance by all RenderThread stats. */
	FORCEINLINE uint64 GetPerInstanceAvgCycles()const { return AccumulatedStats.GetPerInstanceAvgCycles(); }
	/** Returns the max cycles per instance by all RenderThread stats. */
	FORCEINLINE uint64 GetPerInstanceMaxCycles(int32 Index = 0)const { return MaxPerInstanceCycles[Index];  }

	/** Returns the average time in µs per instance by all RenderThread stats. */
	FORCEINLINE double GetPerInstanceAvg()const { return FPlatformTime::ToMilliseconds64(GetPerInstanceAvgCycles()) * 1000.0; }
	/** Returns the max time in µs per instance by all RenderThread stats. */
	FORCEINLINE double GetPerInstanceMax(int32 Index = 0)const { return FPlatformTime::ToMilliseconds64(GetPerInstanceMaxCycles(Index)) * 1000.0; }
};

/** Utility class for accumulating many frames worth of stats data. */
struct ENGINE_API FAccumulatedParticlePerfStats
{
	FAccumulatedParticlePerfStats();

	void Reset(bool bSyncWithRT);
	void ResetGT();
	void ResetRT();
	void Tick(FParticlePerfStats& Stats);
	void TickRT(FParticlePerfStats& Stats);

	FAccumulatedParticlePerfStats_GT GameThreadStats;
	FAccumulatedParticlePerfStats_RT RenderThreadStats;

	static void AddMax(TArray<uint64, TInlineAllocator<ACCUMULATED_PARTICLE_PERF_STAT_MAX_SAMPLES>>& MaxArray, int64 NewValue);
	static void ResetMaxArray(TArray<uint64, TInlineAllocator<ACCUMULATED_PARTICLE_PERF_STAT_MAX_SAMPLES>>& MaxArray);

	/** Returns the current Game Thread stats. */
	FORCEINLINE FAccumulatedParticlePerfStats_GT& GetGameThreadStats()
	{
		return GameThreadStats;
	}

	/** Returns the RT stats. Must be called on the Render Thread. */
	FORCEINLINE FAccumulatedParticlePerfStats_RT& GetRenderThreadStats()
	{
		return RenderThreadStats;
	}

	/** Returns the RenderThread stats for use on the GameThread. Optional sync with the RenderThread. */
	FORCEINLINE const FAccumulatedParticlePerfStats_RT& GetRenderThreadStats_GameThread(bool bSyncRT=false)const
	{
		if (bSyncRT)
		{
			FlushRenderingCommands();
		}
		return RenderThreadStats;
	}
};

// ENGINE_API FString ToStringGT(FAccumulatedParticlePerfStats& Stats);
// ENGINE_API FString ToStringRT(FAccumulatedParticlePerfStats& Stats);

class ENGINE_API FParticlePerfStatsListener : public TSharedFromThis<FParticlePerfStatsListener, ESPMode::ThreadSafe>
{
public:
	virtual ~FParticlePerfStatsListener() {}

	/** Called when the listener begins receiving data. */
	virtual void Begin(){}
	/** Called when the listener stops receiving data. */
	virtual void End(){}
	/** Called every frame with the current frame data. Returns true if we should continue listening. If false is returned the listener will be removed. */
	virtual bool Tick() { return true; }
	/** Called every frame from the render thread gather any RT stats. */
	virtual void TickRT() {}
	/** Called when a new system is seen for the first time. */
	virtual void OnAddSystem(UFXSystemAsset* NewSystem){}
};

typedef TSharedPtr<FParticlePerfStatsListener, ESPMode::ThreadSafe> FParticlePerfStatsListenerPtr;
class ENGINE_API FParticlePerfStatsManager
{
public:
	static FDelegateHandle BeginFrameHandle;
#if CSV_PROFILER
	static FDelegateHandle CSVStartHandle;
	static FDelegateHandle CSVEndHandle;
#endif
	static int32 StatsEnabled;
	static FCriticalSection SystemToPerfStatsGuard;
	static TMap<TWeakObjectPtr<UFXSystemAsset>, TUniquePtr<FParticlePerfStats>> SystemToPerfStats;

	static TArray<FParticlePerfStatsListenerPtr, TInlineAllocator<8>> Listeners;

	static const TMap<TWeakObjectPtr<UFXSystemAsset>, TUniquePtr<FParticlePerfStats>>& GetCurrentFrameStats() { return SystemToPerfStats; }
	static FParticlePerfStats* GetPerfStats(class UFXSystemAsset* Asset);

	static void OnStartup();
	static void OnShutdown();

	static void TogglePerfStatsRender(UWorld* World);
	static int32 RenderStats(UWorld* World, class FViewport* Viewport, class FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation, const FRotator* ViewRotation);

	static void Reset();
	static void Tick();

	static void AddListener(FParticlePerfStatsListenerPtr Listener, bool bReset = true);
	static void RemoveListener(FParticlePerfStatsListener* Listener);
	static void RemoveListener(FParticlePerfStatsListenerPtr Listener);

#if ENABLE_PARTICLE_PERF_STATS_RENDER
	/** Track active worlds needing to render stats. Though we only create one listener. TODO: Maybe better to track by viewport than world? */
	static TMap<TWeakObjectPtr<UWorld>, TSharedPtr<FParticlePerfStatsListener_DebugRender ,ESPMode::ThreadSafe>> DebugRenderListenerUsers;
#endif
};

/** Base class for listeners that gather stats on all systems in the scene. */
class ENGINE_API FParticlePerfStatsListener_GatherAll: public FParticlePerfStatsListener
{
public:
	virtual void Begin();
	virtual void End();
	virtual bool Tick();
	virtual void TickRT();
	virtual void OnAddSystem(UFXSystemAsset* NewSystem);

	void DumpStatsToDevice(FOutputDevice& Ar);
	void DumpStatsToFile();

protected:
	FCriticalSection AccumulatedStatsGuard;

	//Accumulated stats for all systems.
	TMap<TWeakObjectPtr<UFXSystemAsset>, TUniquePtr<FAccumulatedParticlePerfStats>> AccumulatedStats;
};

/** Simple stats listener that will gather stats on all systems for N frames and dump the results to a CSV and the Log. */
class ENGINE_API FParticlePerfStatsListener_TimedTest : public FParticlePerfStatsListener_GatherAll
{
public:
	FParticlePerfStatsListener_TimedTest(int32 NumFrames);

	virtual void End();
	virtual bool Tick();
private:
	int32 FramesRemaining;
};

/** Listener that hooks into the engine wide CSV Profiling systems. */
class ENGINE_API FParticlePerfStatsListener_CSVProfiler : public FParticlePerfStatsListener_GatherAll
{
public:

	virtual void End();

#if CSV_PROFILER
	static void OnCSVStart();
	static void OnCSVEnd();
#endif

private:
	static FParticlePerfStatsListenerPtr CSVListener;
};

/**
This listener displays stats onto a debug canvas in a viewport.
It does not sync with the Render Thread and so RT stats are one or more frames delayed.
*/
class ENGINE_API FParticlePerfStatsListener_DebugRender : public FParticlePerfStatsListener_GatherAll
{
public:
	int32 RenderStats(UWorld* World, class FViewport* Viewport, class FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation, const FRotator* ViewRotation);
};

#else

struct ENGINE_API FAccumulatedParticlePerfStats{};

#endif