// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Particles/ParticlePerfStats.h"
#include "CoreMinimal.h"

#if WITH_PARTICLE_PERF_STATS

namespace ParticlePerfStatsLocal
{
	static FDelegateHandle BeginFrameHandle;
	static int32 GbParticlePerfStatsEnabled = 0;
	static int32 GbParticlePerfStatsEnabledLatch = 0;
	static int32 GParticlePerfStatsFramesRemaining = 0;

	static void DumpParticlePerfStats(FOutputDevice& Ar)
	{
		FlushRenderingCommands();

		FString tempString;

		Ar.Logf(TEXT("**** Particle Performance Stats"));
		Ar.Logf(TEXT("Name,Average PerFrame GameThread,Average PerInstance GameThread,Average PerFrame RenderThread,Average PerInstance RenderThread,NumFrames,Total Instances,Total Tick GameThread,Total Tick Concurrent,Total Finalize,Total End Of Frame,Total Render Update,Total Get Dynamic Mesh Elements"));
		for (TObjectIterator<UFXSystemAsset> SystemIt; SystemIt; ++SystemIt)
		{
			const FParticlePerfStats& PerfStats = SystemIt->ParticlePerfStats;
			if (PerfStats.AccumulatedStats.NumInstances == 0)
				continue;

			const uint64 TotalGameThread = PerfStats.AccumulatedStats.TickGameThreadCycles + PerfStats.AccumulatedStats.TickConcurrentCycles + PerfStats.AccumulatedStats.FinalizeCycles + PerfStats.AccumulatedStats.EndOfFrameCycles;
			const uint64 TotalRenderThread = PerfStats.AccumulatedStats.RenderUpdateCycles + PerfStats.AccumulatedStats.GetDynamicMeshElementsCycles;

			tempString.Reset();
			tempString.Append(*SystemIt->GetFullName());
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
			Ar.Log(*tempString);
		}
	}

	static void ResetParticlePerfStats()
	{
		FlushRenderingCommands();

		for (TObjectIterator<UFXSystemAsset> SystemIt; SystemIt; ++SystemIt)
		{
			SystemIt->ParticlePerfStats.Reset();
		}
	}

	static void TickParticlePerfStats()
	{
		if (GbParticlePerfStatsEnabledLatch != 0)
		{
			for (TObjectIterator<UFXSystemAsset> SystemIt; SystemIt; ++SystemIt)
			{
				SystemIt->ParticlePerfStats.Tick();
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

FParticlePerfStats	FParticlePerfStats::Dummy;

void FParticlePerfStats::Reset()
{
	AccumulatedNumFrames = 0;
	AccumulatedStats.Reset();
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
