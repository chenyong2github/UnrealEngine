// Copyright Epic Games, Inc. All Rights Reserved.

#include "Particles/ParticlePerfStats.h"
#include "CoreMinimal.h"

#if WITH_PARTICLE_PERF_STATS

#include "Misc/ScopeLock.h"
#include "RenderingThread.h"
#include "Particles/ParticlePerfStats.h"
#include "HAL/IConsoleManager.h"
#include "Particles/ParticleSystem.h"
#include "UObject/UObjectIterator.h"
#include "Particles/ParticleSystem.h"
#include "Misc/CoreDelegates.h"

namespace ParticlePerfStatsLocal
{
	static FDelegateHandle BeginFrameHandle;
	static int32 GbParticlePerfStatsEnabled = 0;
	static int32 GbParticlePerfStatsEnabledLatch = 0;
	static int32 GParticlePerfStatsFramesRemaining = 0;
	static FCriticalSection SystemToPerfStatsGuard;
	static TMap<FName, TUniquePtr<FParticlePerfStats>> SystemToPerfStats;

	static void DumpParticlePerfStats(FOutputDevice& Ar)
	{
		FlushRenderingCommands();
		FScopeLock ScopeLock(&ParticlePerfStatsLocal::SystemToPerfStatsGuard);

		FString tempString;

		Ar.Logf(TEXT("**** Particle Performance Stats"));
		Ar.Logf(TEXT(",Name,Average PerFrame GameThread,Average PerInstance GameThread,Average PerFrame RenderThread,Average PerInstance RenderThread,NumFrames,Total Instances,Total Tick GameThread,Total Tick Concurrent,Total Finalize,Total End Of Frame,Total Render Update,Total Get Dynamic Mesh Elements,Max PerFrame GameThread,Max Range PerFrame GameThread,Max PerFrame RenderThread,Max Range PerFrame RenderThread"));

		for (auto it = SystemToPerfStats.CreateIterator(); it; ++it)
		{
			const FParticlePerfStats& PerfStats = *it.Value();

			if (PerfStats.AccumulatedNumFrames == 0)
			{
				continue;
			}

			FString SystemName = it.Key().ToString();

			const uint64 TotalGameThread = PerfStats.AccumulatedStats.TickGameThreadCycles + PerfStats.AccumulatedStats.TickConcurrentCycles + PerfStats.AccumulatedStats.FinalizeCycles + PerfStats.AccumulatedStats.EndOfFrameCycles;
			const uint64 TotalRenderThread = PerfStats.AccumulatedStats.RenderUpdateCycles + PerfStats.AccumulatedStats.GetDynamicMeshElementsCycles;

			const uint64 MaxPerFrameTotalGameThreadFirst = PerfStats.MaxPerFrameTotalGameThreadCycles.Num() > 0 ? PerfStats.MaxPerFrameTotalGameThreadCycles[0] : 0;
			const uint64 MaxPerFrameTotalRenderThreadFirst = PerfStats.MaxPerFrameTotalRenderThreadCycles.Num() > 0 ? PerfStats.MaxPerFrameTotalRenderThreadCycles[0] : 0;
			const uint64 MaxPerFrameTotalGameThreadLast = PerfStats.MaxPerFrameTotalGameThreadCycles.Num() > 0 ? PerfStats.MaxPerFrameTotalGameThreadCycles.Last() : 0;
			const uint64 MaxPerFrameTotalRenderThreadLast = PerfStats.MaxPerFrameTotalRenderThreadCycles.Num() > 0 ? PerfStats.MaxPerFrameTotalRenderThreadCycles.Last() : 0;

			tempString.Reset();
			tempString.Appendf(TEXT(",%s"), *SystemName);
			tempString.Appendf(TEXT(",%u"), uint32(FPlatformTime::ToMilliseconds64(TotalGameThread) * 1000.0 / double(PerfStats.AccumulatedNumFrames)));
			tempString.Appendf(TEXT(",%u"), uint32(FPlatformTime::ToMilliseconds64(TotalGameThread) * 1000.0 / double(PerfStats.AccumulatedStats.NumInstances)));
			tempString.Appendf(TEXT(",%u"), uint32(FPlatformTime::ToMilliseconds64(TotalRenderThread) * 1000.0 / double(PerfStats.AccumulatedNumFrames)));
			tempString.Appendf(TEXT(",%u"), uint32(FPlatformTime::ToMilliseconds64(TotalRenderThread) * 1000.0 / double(PerfStats.AccumulatedStats.NumInstances)));
			tempString.Appendf(TEXT(",%u"), uint32(PerfStats.AccumulatedNumFrames));
			tempString.Appendf(TEXT(",%u"), uint32(PerfStats.AccumulatedStats.NumInstances));
			tempString.Appendf(TEXT(",%u"), uint32(FPlatformTime::ToMilliseconds64(PerfStats.AccumulatedStats.TickGameThreadCycles) * 1000.0));
			tempString.Appendf(TEXT(",%u"), uint32(FPlatformTime::ToMilliseconds64(PerfStats.AccumulatedStats.TickConcurrentCycles) * 1000.0));
			tempString.Appendf(TEXT(",%u"), uint32(FPlatformTime::ToMilliseconds64(PerfStats.AccumulatedStats.FinalizeCycles) * 1000.0));
			tempString.Appendf(TEXT(",%u"), uint32(FPlatformTime::ToMilliseconds64(PerfStats.AccumulatedStats.EndOfFrameCycles) * 1000.0));
			tempString.Appendf(TEXT(",%u"), uint32(FPlatformTime::ToMilliseconds64(PerfStats.AccumulatedStats.RenderUpdateCycles) * 1000.0));
			tempString.Appendf(TEXT(",%u"), uint32(FPlatformTime::ToMilliseconds64(PerfStats.AccumulatedStats.GetDynamicMeshElementsCycles) * 1000.0));

			tempString.Appendf(TEXT(",%u,[ "), uint32(FPlatformTime::ToMilliseconds64(PerfStats.MaxPerFrameTotalGameThreadCycles.Num() > 0 ? PerfStats.MaxPerFrameTotalGameThreadCycles[0] : 0) * 1000.0));
			for (uint64 v : PerfStats.MaxPerFrameTotalGameThreadCycles)
			{
				tempString.Appendf(TEXT("%u "), uint32(FPlatformTime::ToMilliseconds64(v) * 1000.0));
			}
			tempString.Append(TEXT("]"));

			tempString.Appendf(TEXT(",%u,[ "), uint32(FPlatformTime::ToMilliseconds64(PerfStats.MaxPerFrameTotalRenderThreadCycles.Num() > 0 ? PerfStats.MaxPerFrameTotalRenderThreadCycles[0] : 0) * 1000.0));
			for (uint64 v : PerfStats.MaxPerFrameTotalRenderThreadCycles)
			{
				tempString.Appendf(TEXT("%u "), uint32(FPlatformTime::ToMilliseconds64(v) * 1000.0));
			}
			tempString.Append(TEXT("]"));

			Ar.Log(*tempString);
		}
	}

	static void ResetParticlePerfStats()
	{
		FlushRenderingCommands();

		FScopeLock ScopeLock(&ParticlePerfStatsLocal::SystemToPerfStatsGuard);
		for (TObjectIterator<UFXSystemAsset> SystemIt; SystemIt; ++SystemIt)
		{
			SystemIt->ParticlePerfStats = nullptr;
		}
		SystemToPerfStats.Empty();
	}

	static void TickParticlePerfStats()
	{
		if (GbParticlePerfStatsEnabledLatch != 0)
		{
			FScopeLock ScopeLock(&ParticlePerfStatsLocal::SystemToPerfStatsGuard);
			for (auto it=SystemToPerfStats.CreateIterator(); it; ++it)
			{
				it.Value()->Tick();
			}

			bool bDisableGathering = GbParticlePerfStatsEnabled == 0;
			if (GParticlePerfStatsFramesRemaining > 0)
			{
				--GParticlePerfStatsFramesRemaining;
				bDisableGathering = GParticlePerfStatsFramesRemaining == 0;
			}

			if (bDisableGathering)
			{
				DumpParticlePerfStats(*GLog);
				GbParticlePerfStatsEnabledLatch = 0;
			}
		}
		else
		{
			if (GbParticlePerfStatsEnabled != 0 || GParticlePerfStatsFramesRemaining > 0)
			{
				GbParticlePerfStatsEnabledLatch = true;
				ResetParticlePerfStats();
			}
		}
	}

	static FAutoConsoleVariableRef CVarParticlePerfStatsEnabled(
		TEXT("fx.ParticlePerfStats.Enabled"),
		GbParticlePerfStatsEnabled,
		TEXT("Used to control if stat gathering is enabled or not.\n"),
		ECVF_Default
	);

	static FAutoConsoleCommandWithOutputDevice GParticlePerfStatsDump(
		TEXT("fx.ParticlePerfStats.Dump"),
		TEXT("Dumps current particle perf stats to output"),
		FConsoleCommandWithOutputDeviceDelegate::CreateStatic(DumpParticlePerfStats)
	);

	static FAutoConsoleCommand GParticlePerfStatsReset(
		TEXT("fx.ParticlePerfStats.Reset"),
		TEXT("Resets all particle perf stats to zero"),
		FConsoleCommandDelegate::CreateStatic(ResetParticlePerfStats)
	);

	static FAutoConsoleCommandWithWorldAndArgs GParticlePerfStatsRunTest(
		TEXT("fx.ParticlePerfStats.RunTest"),
		TEXT("Runs for a number of frames then logs out the results"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if ( Args.Num() != 1 )
				{
					return;
				}
				const int32 NumFrames = FCString::Atoi(*Args[0]);
				if (NumFrames <= 0)
				{
					return;
				}

				GbParticlePerfStatsEnabledLatch = 0;
				GParticlePerfStatsFramesRemaining = NumFrames;
			}
		)
	);
}

FParticlePerfStats* FParticlePerfStats::GetPerfStats(class UFXSystemAsset* Asset)
{
	static FParticlePerfStats Dummy;

	if (Asset == nullptr)
	{
		return &Dummy;
	}

	if (Asset->ParticlePerfStats == nullptr)
	{
		FScopeLock ScopeLock(&ParticlePerfStatsLocal::SystemToPerfStatsGuard);
		TUniquePtr<FParticlePerfStats>& PerfStats = ParticlePerfStatsLocal::SystemToPerfStats.FindOrAdd(Asset->GetFName());
		if (PerfStats == nullptr)
		{
			PerfStats.Reset(new FParticlePerfStats());
		}
		Asset->ParticlePerfStats = PerfStats.Get();
	}
	return Asset->ParticlePerfStats;
}

void FParticlePerfStats::Reset()
{
	AccumulatedNumFrames = 0;
	AccumulatedStats.Reset();
	MaxPerFrameTotalGameThreadCycles.Empty();
	MaxPerFrameTotalRenderThreadCycles.Empty();
	CurrentFrameStats.Reset();
}

void FParticlePerfStats::Tick()
{
	// Nothing to do if we have no instances
	if (CurrentFrameStats.NumInstances == 0)
		return;

	++AccumulatedNumFrames;

	AccumulatedStats.NumInstances += CurrentFrameStats.NumInstances;
	AccumulatedStats.TickGameThreadCycles += CurrentFrameStats.TickGameThreadCycles;
	AccumulatedStats.TickConcurrentCycles += CurrentFrameStats.TickConcurrentCycles;
	AccumulatedStats.FinalizeCycles += CurrentFrameStats.FinalizeCycles;
	AccumulatedStats.EndOfFrameCycles += CurrentFrameStats.EndOfFrameCycles;

	const uint64 ThisFrameMaxTotalGameThread = CurrentFrameStats.TickGameThreadCycles + CurrentFrameStats.TickConcurrentCycles + CurrentFrameStats.FinalizeCycles + CurrentFrameStats.EndOfFrameCycles;
	const uint64 ThisFrameMaxTotalRenderThread = CurrentFrameStats.RenderUpdateCycles + CurrentFrameStats.GetDynamicMeshElementsCycles;

	// Update Max Samples
	{
		int32 InsertIndex;
		InsertIndex = MaxPerFrameTotalGameThreadCycles.IndexOfByPredicate([&](uint32 v) {return ThisFrameMaxTotalGameThread > v; });
		if (InsertIndex != INDEX_NONE)
		{
			MaxPerFrameTotalGameThreadCycles.Pop(false);
			MaxPerFrameTotalGameThreadCycles.Insert(ThisFrameMaxTotalGameThread, InsertIndex);
		}
		else if (MaxPerFrameTotalGameThreadCycles.Num() < kNumMaxSamples)
		{
			MaxPerFrameTotalGameThreadCycles.Add(ThisFrameMaxTotalGameThread);
		}

		InsertIndex = MaxPerFrameTotalRenderThreadCycles.IndexOfByPredicate([&](uint32 v) {return ThisFrameMaxTotalRenderThread > v; });
		if (InsertIndex != INDEX_NONE)
		{
			MaxPerFrameTotalRenderThreadCycles.Pop(false);
			MaxPerFrameTotalRenderThreadCycles.Insert(ThisFrameMaxTotalRenderThread, InsertIndex);
		}
		else if (MaxPerFrameTotalRenderThreadCycles.Num() < kNumMaxSamples)
		{
			MaxPerFrameTotalRenderThreadCycles.Add(ThisFrameMaxTotalRenderThread);
		}
	}

	CurrentFrameStats.NumInstances = 0;
	CurrentFrameStats.TickGameThreadCycles = 0;
	CurrentFrameStats.TickConcurrentCycles = 0;
	CurrentFrameStats.FinalizeCycles = 0;
	CurrentFrameStats.EndOfFrameCycles = 0;

	// Update RenderThread stats
	ENQUEUE_RENDER_COMMAND(FGatherParticlePerfStats)
		(
			[RTPerfStats=this](FRHICommandListImmediate& RHICmdList)
			{
				RTPerfStats->AccumulatedStats.RenderUpdateCycles += RTPerfStats->CurrentFrameStats.RenderUpdateCycles;
				RTPerfStats->AccumulatedStats.GetDynamicMeshElementsCycles += RTPerfStats->CurrentFrameStats.GetDynamicMeshElementsCycles;

				RTPerfStats->CurrentFrameStats.RenderUpdateCycles = 0;
				RTPerfStats->CurrentFrameStats.GetDynamicMeshElementsCycles = 0;
	}
		);
}

void FParticlePerfStats::OnStartup()
{
	ParticlePerfStatsLocal::BeginFrameHandle = FCoreDelegates::OnBeginFrame.AddStatic(ParticlePerfStatsLocal::TickParticlePerfStats);
}

void FParticlePerfStats::OnShutdown()
{
	FCoreDelegates::OnBeginFrame.Remove(ParticlePerfStatsLocal::BeginFrameHandle);
}

#endif //WITH_PARTICLE_PERF_STATS
