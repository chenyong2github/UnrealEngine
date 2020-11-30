// Copyright Epic Games, Inc. All Rights Reserved.

#include "Particles/ParticlePerfStats.h"
#include "Particles/ParticlePerfStatsManager.h"

#include "CoreMinimal.h"

#if WITH_PARTICLE_PERF_STATS

#include "Misc/ScopeLock.h"
#include "RenderingThread.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Particles/ParticleSystem.h"
#include "UObject/UObjectIterator.h"
#include "Particles/ParticleSystem.h"
#include "Misc/CoreDelegates.h"
#include "Misc/OutputDeviceArchiveWrapper.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "ProfilingDebugging/ProfilingHelpers.h"
#include "Containers/UnrealString.h"
#include "Misc/Paths.h"
#include "CanvasTypes.h"
#include "Engine/Font.h"


TAtomic<bool> FParticlePerfStats::bStatsEnabled(true);
TAtomic<bool> FParticlePerfStats::bGatherStats(false);

FDelegateHandle FParticlePerfStatsManager::BeginFrameHandle;
#if CSV_PROFILER
FDelegateHandle FParticlePerfStatsManager::CSVStartHandle;
FDelegateHandle FParticlePerfStatsManager::CSVEndHandle;
#endif

FCriticalSection FParticlePerfStatsManager::SystemToPerfStatsGuard;
TMap<TWeakObjectPtr<UFXSystemAsset>, TUniquePtr<FParticlePerfStats>> FParticlePerfStatsManager::SystemToPerfStats;
TArray<FParticlePerfStatsListenerPtr, TInlineAllocator<8>> FParticlePerfStatsManager::Listeners;

#if ENABLE_PARTICLE_PERF_STATS_RENDER
TMap<TWeakObjectPtr<UWorld>, TSharedPtr<FParticlePerfStatsListener_DebugRender, ESPMode::ThreadSafe>> FParticlePerfStatsManager::DebugRenderListenerUsers;
#endif


bool GbLocalStatsEnabled = FParticlePerfStats::bStatsEnabled;
static FAutoConsoleVariableRef CVarParticlePerfStatsEnabled(
	TEXT("fx.ParticlePerfStats.Enabled"),
	GbLocalStatsEnabled,
	TEXT("Used to control if stat gathering is enabled or not.\n"),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* CVar)
		{
			check(CVar);
			FParticlePerfStats::SetStatsEnabled(CVar->GetBool());
		}),
	ECVF_Default
);

static FAutoConsoleCommandWithWorldAndArgs GParticlePerfStatsRunTest(
	TEXT("fx.ParticlePerfStats.RunTest"),
	TEXT("Runs for a number of frames then logs out the results"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() != 1)
			{
				return;
			}
			const int32 NumFrames = FCString::Atoi(*Args[0]);
			if (NumFrames <= 0)
			{
				return;
			}

			FParticlePerfStatsListenerPtr NewTimedTest = MakeShared<FParticlePerfStatsListener_TimedTest, ESPMode::ThreadSafe>(NumFrames);
			FParticlePerfStatsManager::AddListener(NewTimedTest);
		}
	)
);

void FParticlePerfStatsManager::AddListener(FParticlePerfStatsListenerPtr Listener, bool bReset)
{
	if (bReset)
	{
		Reset();
	}

	if (FParticlePerfStats::GetStatsEnabled())
	{
		Listeners.Add(Listener);
		Listener->Begin();

		//Ensure we're gathering stats.
		FParticlePerfStats::SetGatherStats(true);
	}
}

void FParticlePerfStatsManager::RemoveListener(FParticlePerfStatsListener* Listener)
{
	RemoveListener(Listener->AsShared());
}

void FParticlePerfStatsManager::RemoveListener(FParticlePerfStatsListenerPtr Listener)
{
	//Pass a ptr off to the RT just so we can ensure it's lifetime past any RT commands it may have issued.
	ENQUEUE_RENDER_COMMAND(FRemoveParticlePerfStatsListenerCmd)
	(
		[Listener](FRHICommandListImmediate& RHICmdList)mutable
		{
			Listener.Reset();
		}
	);

	Listener->End();
	Listeners.Remove(Listener);

	//If we have no listeners then stop gathering.
	if (Listeners.Num() == 0)
	{
		FParticlePerfStats::SetGatherStats(false);
	}
}

void FParticlePerfStatsManager::Reset()
{
	FlushRenderingCommands();

	FScopeLock ScopeLock(&FParticlePerfStatsManager::SystemToPerfStatsGuard);
	for (TObjectIterator<UFXSystemAsset> SystemIt; SystemIt; ++SystemIt)
	{
		SystemIt->ParticlePerfStats = nullptr;
	}
	SystemToPerfStats.Empty();
}

void FParticlePerfStatsManager::Tick()
{
	if (FParticlePerfStats::ShouldGatherStats())
	{
		check(Listeners.Num() > 0);

		//Tick our listeners so they can consume the finished frame data.
		TArray<FParticlePerfStatsListenerPtr, TInlineAllocator<8>> ToRemove;
		for (FParticlePerfStatsListenerPtr& Listener : Listeners)
		{
			if (Listener->Tick() == false)
			{
				ToRemove.Add(Listener);
			}
		}

		//Make a copy of the listener shared ptrs to ensure their lifetime. 
		TArray<FParticlePerfStatsListenerPtr, TInlineAllocator<8>> ListenersGT = Listeners;
		//Kick off the RT tick for listeners and stats
		ENQUEUE_RENDER_COMMAND(FParticlePerfStatsListenersRTTick)
		(
			[ListenersRT=MoveTemp(ListenersGT)](FRHICommandListImmediate& RHICmdList)
			{
				for (FParticlePerfStatsListenerPtr Listener : ListenersRT)
				{
					Listener->TickRT();
				}

				//Reset current frame data
				{
					FScopeLock ScopeLock(&FParticlePerfStatsManager::SystemToPerfStatsGuard);
					for (auto it = SystemToPerfStats.CreateIterator(); it; ++it)
					{
						it.Value()->TickRT();
					}
				}
			}
		);

		//Reset current frame data
		{
			FScopeLock ScopeLock(&FParticlePerfStatsManager::SystemToPerfStatsGuard);
			for (auto it = SystemToPerfStats.CreateIterator(); it; ++it)
			{
				it.Value()->Tick();
			}
		}

		//Remove any listeners that are done.
		for (FParticlePerfStatsListenerPtr& Listener : ToRemove)
		{
			RemoveListener(Listener);
		}
	}
	else
	{
		//Ensure any existing listeners are removed if stats have been disabled.
		while (Listeners.Num())
		{
			RemoveListener(Listeners.Last());
		}
	}
}

FParticlePerfStats* FParticlePerfStatsManager::GetPerfStats(class UFXSystemAsset* Asset)
{
	static FParticlePerfStats Dummy;

	if (Asset == nullptr)
	{
		return &Dummy;
	}

	if (Asset->ParticlePerfStats == nullptr)
	{
		FScopeLock ScopeLock(&SystemToPerfStatsGuard);
		TUniquePtr<FParticlePerfStats>& PerfStats = SystemToPerfStats.FindOrAdd(Asset);
		if (PerfStats == nullptr)
		{
			PerfStats.Reset(new FParticlePerfStats());
		}
		Asset->ParticlePerfStats = PerfStats.Get();
		
		for (auto& Listener : Listeners)
		{
			Listener->OnAddSystem(Asset);
		}
	}
	return Asset->ParticlePerfStats;
}

void FParticlePerfStatsManager::TogglePerfStatsRender(UWorld* World)
{
#if ENABLE_PARTICLE_PERF_STATS_RENDER
	if (auto* Found = DebugRenderListenerUsers.Find(World))
	{
		//Already have an entry so we're toggling rendering off. Remove.
		RemoveListener(*Found);
		DebugRenderListenerUsers.Remove(World);
	}
	else
	{
		//Need not found. Add a new listener for this world.
		TSharedPtr<FParticlePerfStatsListener_DebugRender, ESPMode::ThreadSafe> NewListener = MakeShared<FParticlePerfStatsListener_DebugRender, ESPMode::ThreadSafe>();
		DebugRenderListenerUsers.Add(World) = NewListener;
		AddListener(NewListener);
	}
#endif
}

int32 FParticlePerfStatsManager::RenderStats(class UWorld* World, class FViewport* Viewport, class FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation, const FRotator* ViewRotation)
{
#if ENABLE_PARTICLE_PERF_STATS_RENDER
	//We shouldn't get into this rendering function unless we have registered users.
	if (auto* DebugRenderListener = DebugRenderListenerUsers.Find(World))
	{
		return (*DebugRenderListener)->RenderStats(World, Viewport, Canvas, X, Y, ViewLocation, ViewRotation);
	}
#endif 
	return Y;
}

void FParticlePerfStatsManager::OnStartup()
{
	BeginFrameHandle = FCoreDelegates::OnBeginFrame.AddStatic(Tick);
#if CSV_PROFILER
	if (FCsvProfiler* CSVProfiler = FCsvProfiler::Get())
	{
		CSVStartHandle = CSVProfiler->OnCSVProfileStart().AddStatic(FParticlePerfStatsListener_CSVProfiler::OnCSVStart);
		CSVEndHandle = CSVProfiler->OnCSVProfileEnd().AddStatic(FParticlePerfStatsListener_CSVProfiler::OnCSVEnd);
	}
#endif
}

void FParticlePerfStatsManager::OnShutdown()
{
	FCoreDelegates::OnBeginFrame.Remove(BeginFrameHandle);
#if CSV_PROFILER
	if (FCsvProfiler* CSVProfiler = FCsvProfiler::Get())
	{
		CSVProfiler->OnCSVProfileStart().Remove(CSVStartHandle);
		CSVProfiler->OnCSVProfileEnd().Remove(CSVEndHandle);
	}
#endif
}

//////////////////////////////////////////////////////////////////////////

FParticlePerfStats* FParticlePerfStats::GetPerfStats(class UFXSystemAsset* Asset)
{
	return FParticlePerfStatsManager::GetPerfStats(Asset);
}

void FParticlePerfStats::ResetGT()
{
	check(IsInGameThread());
	GetGameThreadStats().Reset();
}

void FParticlePerfStats::ResetRT()
{
	check(IsInActualRenderingThread());
	GetRenderThreadStats().Reset();
}

FParticlePerfStats::FParticlePerfStats()
{
}

void FParticlePerfStats::Reset(bool bSyncWithRT)
{
	check(IsInGameThread());

	ResetGT();

	if (bSyncWithRT)
	{
		FlushRenderingCommands();
		ResetRT();
	}
	else
	{
		ENQUEUE_RENDER_COMMAND(FResetParticlePerfStats)
		(
			[&](FRHICommandListImmediate& RHICmdList)
			{
				ResetRT();
			}
		);
	}
}

void FParticlePerfStats::Tick()
{
	check(IsInGameThread());
	GetGameThreadStats().Reset();
}

void FParticlePerfStats::TickRT()
{	
	check(IsInActualRenderingThread());
	GetRenderThreadStats().Reset();
}

//////////////////////////////////////////////////////////////////////////

FAccumulatedParticlePerfStats_GT::FAccumulatedParticlePerfStats_GT()
{
	Reset();
}

void FAccumulatedParticlePerfStats_GT::Reset()
{
	FAccumulatedParticlePerfStats::ResetMaxArray(MaxPerFrameTotalCycles);
	FAccumulatedParticlePerfStats::ResetMaxArray(MaxPerInstanceCycles);

	NumFrames = 0;
	AccumulatedStats.Reset();
}

void FAccumulatedParticlePerfStats_GT::Tick(FParticlePerfStats& Stats)
{
	FParticlePerfStats_GT& GTStats = Stats.GetGameThreadStats();
	if (GTStats.NumInstances > 0)
	{
		++NumFrames;
		AccumulatedStats.NumInstances += GTStats.NumInstances;
		AccumulatedStats.TickGameThreadCycles += GTStats.TickGameThreadCycles;
		AccumulatedStats.TickConcurrentCycles += GTStats.TickConcurrentCycles;
		AccumulatedStats.FinalizeCycles += GTStats.FinalizeCycles;
		AccumulatedStats.EndOfFrameCycles += GTStats.EndOfFrameCycles;

		FAccumulatedParticlePerfStats::AddMax(MaxPerFrameTotalCycles, GTStats.GetTotalCycles());
		FAccumulatedParticlePerfStats::AddMax(MaxPerInstanceCycles, GTStats.GetPerInstanceAvgCycles());
	}
}

//////////////////////////////////////////////////////////////////////////

FAccumulatedParticlePerfStats_RT::FAccumulatedParticlePerfStats_RT()
{
	Reset();
}

void FAccumulatedParticlePerfStats_RT::Reset()
{
	FAccumulatedParticlePerfStats::ResetMaxArray(MaxPerFrameTotalCycles);
	FAccumulatedParticlePerfStats::ResetMaxArray(MaxPerInstanceCycles);

	NumFrames = 0;
	AccumulatedStats.Reset();
}

void FAccumulatedParticlePerfStats_RT::Tick(FParticlePerfStats& Stats)
{
	FParticlePerfStats_RT& RTStats = Stats.GetRenderThreadStats();
	if (RTStats.NumInstances > 0)
	{
		++NumFrames;
		AccumulatedStats.NumInstances += RTStats.NumInstances;
		AccumulatedStats.RenderUpdateCycles += RTStats.RenderUpdateCycles;
		AccumulatedStats.GetDynamicMeshElementsCycles += RTStats.GetDynamicMeshElementsCycles;

		FAccumulatedParticlePerfStats::AddMax(MaxPerFrameTotalCycles, RTStats.GetTotalCycles());
		FAccumulatedParticlePerfStats::AddMax(MaxPerInstanceCycles, RTStats.GetPerInstanceAvgCycles());
	}
}

//////////////////////////////////////////////////////////////////////////

FAccumulatedParticlePerfStats::FAccumulatedParticlePerfStats()
{
	ResetGT();
	ResetRT();
}

void FAccumulatedParticlePerfStats::ResetGT()
{
	GameThreadStats.Reset();
}

void FAccumulatedParticlePerfStats::ResetRT()
{
	RenderThreadStats.Reset();
}

void FAccumulatedParticlePerfStats::Reset(bool bSyncWithRT)
{
	ResetGT();

	if (bSyncWithRT)
	{
		FlushRenderingCommands();
		ResetRT();
	}
	else
	{
		//Not syncing with RT so must update these on the RT.
		ENQUEUE_RENDER_COMMAND(FResetAccumulatedParticlePerfMaxRT)
		(
			[&](FRHICommandListImmediate& RHICmdList)
			{
				ResetRT();
			}
		);
	}
}

void FAccumulatedParticlePerfStats::Tick(FParticlePerfStats& Stats)
{
	check(IsInGameThread());
	GameThreadStats.Tick(Stats);
}

void FAccumulatedParticlePerfStats::TickRT(FParticlePerfStats& Stats)
{
	check(IsInActualRenderingThread());
	RenderThreadStats.Tick(Stats);
}

void FAccumulatedParticlePerfStats::AddMax(TArray<uint64, TInlineAllocator<ACCUMULATED_PARTICLE_PERF_STAT_MAX_SAMPLES>>& MaxArray, int64 NewValue)
{
	int32 InsertIndex;
	InsertIndex = MaxArray.IndexOfByPredicate([&](uint32 v) {return NewValue > v; });
	if (InsertIndex != INDEX_NONE)
	{
		MaxArray.Pop(false);
		MaxArray.Insert(NewValue, InsertIndex);
	}
};

void FAccumulatedParticlePerfStats::ResetMaxArray(TArray<uint64, TInlineAllocator<ACCUMULATED_PARTICLE_PERF_STAT_MAX_SAMPLES>>& MaxArray)
{
	MaxArray.SetNumUninitialized(ACCUMULATED_PARTICLE_PERF_STAT_MAX_SAMPLES);
	for (int32 i = 0; i < ACCUMULATED_PARTICLE_PERF_STAT_MAX_SAMPLES; ++i)
	{
		MaxArray[i] = 0;
	}
};

//////////////////////////////////////////////////////////////////////////

void FParticlePerfStatsListener_GatherAll::Begin()
{
	//Init our map of accumulated stats.
	FScopeLock Lock(&AccumulatedStatsGuard);
	for (auto& Pair : FParticlePerfStatsManager::GetCurrentFrameStats())
	{
		UFXSystemAsset* Asset = Pair.Key.Get();
		AccumulatedStats.Add(Asset) = MakeUnique<FAccumulatedParticlePerfStats>();
	}
}

void FParticlePerfStatsListener_GatherAll::End()
{
	FScopeLock Lock(&AccumulatedStatsGuard);
	AccumulatedStats.Empty();
}

bool FParticlePerfStatsListener_GatherAll::Tick()
{
	FScopeLock Lock(&AccumulatedStatsGuard);

	TArray<TWeakObjectPtr<UFXSystemAsset>, TInlineAllocator<8>> ToRemove;
	for (auto& Pair : AccumulatedStats)
	{
		if (UFXSystemAsset* Asset = Pair.Key.Get())
		{
			FAccumulatedParticlePerfStats& Stats = *Pair.Value;

			if (FParticlePerfStats* CurrentFrameStats = Asset->ParticlePerfStats)
			{
				Stats.Tick(*CurrentFrameStats);
			}
		}
		else
		{
			ToRemove.Add(Pair.Key);
		}
	}
	for (auto& Asset : ToRemove)
	{
		AccumulatedStats.Remove(Asset);
	}

	return true;
}

void FParticlePerfStatsListener_GatherAll::TickRT()
{
	FScopeLock Lock(&AccumulatedStatsGuard);

	TArray<TWeakObjectPtr<UFXSystemAsset>, TInlineAllocator<8>> ToRemove;
	for (auto& Pair : AccumulatedStats)
	{
		if (UFXSystemAsset* Asset = Pair.Key.Get())
		{
			FAccumulatedParticlePerfStats& Stats = *Pair.Value;

			if (FParticlePerfStats* CurrentFrameStats = Asset->ParticlePerfStats)
			{
				Stats.TickRT(*CurrentFrameStats);
			}
		}
		else
		{
			ToRemove.Add(Pair.Key);
		}
	}

	for (auto& Asset : ToRemove)
	{
		AccumulatedStats.Remove(Asset);
	}
}

void FParticlePerfStatsListener_GatherAll::OnAddSystem(UFXSystemAsset* NewSystem)
{
	FScopeLock Lock(&AccumulatedStatsGuard);
	AccumulatedStats.Add(NewSystem) = MakeUnique<FAccumulatedParticlePerfStats>();
}

void FParticlePerfStatsListener_GatherAll::DumpStatsToDevice(FOutputDevice& Ar)
{
	FlushRenderingCommands();

	FString tempString;

	Ar.Logf(TEXT(",**** Particle Performance Stats"));
	Ar.Logf(TEXT(",Name,Average PerFrame GameThread,Average PerInstance GameThread,Average PerFrame RenderThread,Average PerInstance RenderThread,NumFrames,Total Instances,Total Tick GameThread,Total Tick Concurrent,Total Finalize,Total End Of Frame,Total Render Update,Total Get Dynamic Mesh Elements,Max PerFrame GameThread,Max Range PerFrame GameThread,Max PerFrame RenderThread,Max Range PerFrame RenderThread"));

	for (auto it = AccumulatedStats.CreateIterator(); it; ++it)
	{
		FAccumulatedParticlePerfStats& PerfStats = *it.Value();

		const FAccumulatedParticlePerfStats_GT& GTSTats = PerfStats.GetGameThreadStats();
		const FAccumulatedParticlePerfStats_RT& RTSTats = PerfStats.GetRenderThreadStats_GameThread();

		if ((GTSTats.NumFrames == 0 && RTSTats.NumFrames == 0) || (GTSTats.AccumulatedStats.NumInstances == 0 && RTSTats.AccumulatedStats.NumInstances == 0))
		{
			continue;
		}

		UFXSystemAsset* System = it.Key().Get();
		FString SystemName = System ? System->GetFName().ToString() : TEXT("nullptr");

		const uint64 TotalGameThread = GTSTats.GetTotalCycles();
		const uint64 TotalRenderThread = RTSTats.GetTotalCycles();

		const uint64 MaxPerFrameTotalGameThreadFirst = GTSTats.MaxPerFrameTotalCycles.Num() > 0 ? GTSTats.MaxPerFrameTotalCycles[0] : 0;
		const uint64 MaxPerFrameTotalGameThreadLast = GTSTats.MaxPerFrameTotalCycles.Num() > 0 ? GTSTats.MaxPerFrameTotalCycles.Last() : 0;

		const uint64 MaxPerFrameTotalRenderThreadFirst = RTSTats.MaxPerFrameTotalCycles.Num() > 0 ? RTSTats.MaxPerFrameTotalCycles[0] : 0;
		const uint64 MaxPerFrameTotalRenderThreadLast = RTSTats.MaxPerFrameTotalCycles.Num() > 0 ? RTSTats.MaxPerFrameTotalCycles.Last() : 0;
		//TODO: Add per instance max?

		tempString.Reset();
		tempString.Appendf(TEXT(",%s"), *SystemName);
		tempString.Appendf(TEXT(",%u"), uint32(FPlatformTime::ToMilliseconds64(GTSTats.GetPerFrameAvgCycles()) * 1000.0));
		tempString.Appendf(TEXT(",%u"), uint32(FPlatformTime::ToMilliseconds64(GTSTats.GetPerInstanceAvgCycles()) * 1000.0));
		tempString.Appendf(TEXT(",%u"), uint32(FPlatformTime::ToMilliseconds64(RTSTats.GetPerFrameAvgCycles()) * 1000.0));
		tempString.Appendf(TEXT(",%u"), uint32(FPlatformTime::ToMilliseconds64(RTSTats.GetPerInstanceAvgCycles()) * 1000.0));
		tempString.Appendf(TEXT(",%u"), uint32(GTSTats.NumFrames));
		tempString.Appendf(TEXT(",%u"), uint32(GTSTats.AccumulatedStats.NumInstances));
		tempString.Appendf(TEXT(",%u"), uint32(FPlatformTime::ToMilliseconds64(GTSTats.AccumulatedStats.TickGameThreadCycles) * 1000.0));
		tempString.Appendf(TEXT(",%u"), uint32(FPlatformTime::ToMilliseconds64(GTSTats.AccumulatedStats.TickConcurrentCycles) * 1000.0));
		tempString.Appendf(TEXT(",%u"), uint32(FPlatformTime::ToMilliseconds64(GTSTats.AccumulatedStats.FinalizeCycles) * 1000.0));
		tempString.Appendf(TEXT(",%u"), uint32(FPlatformTime::ToMilliseconds64(GTSTats.AccumulatedStats.EndOfFrameCycles) * 1000.0));
		tempString.Appendf(TEXT(",%u"), uint32(FPlatformTime::ToMilliseconds64(RTSTats.AccumulatedStats.RenderUpdateCycles) * 1000.0));
		tempString.Appendf(TEXT(",%u"), uint32(FPlatformTime::ToMilliseconds64(RTSTats.AccumulatedStats.GetDynamicMeshElementsCycles) * 1000.0));

		tempString.Appendf(TEXT(",%u,[ "), uint32(FPlatformTime::ToMilliseconds64(GTSTats.MaxPerFrameTotalCycles.Num() > 0 ? GTSTats.MaxPerFrameTotalCycles[0] : 0) * 1000.0));
		for (uint64 v : GTSTats.MaxPerFrameTotalCycles)
		{
			tempString.Appendf(TEXT("%u "), uint32(FPlatformTime::ToMilliseconds64(v) * 1000.0));
		}
		tempString.Append(TEXT("]"));

		tempString.Appendf(TEXT(",%u,[ "), uint32(FPlatformTime::ToMilliseconds64(RTSTats.MaxPerInstanceCycles.Num() > 0 ? RTSTats.MaxPerInstanceCycles[0] : 0) * 1000.0));
		for (uint64 v : RTSTats.MaxPerFrameTotalCycles)
		{
			tempString.Appendf(TEXT("%u "), uint32(FPlatformTime::ToMilliseconds64(v) * 1000.0));
		}
		tempString.Append(TEXT("]"));

		Ar.Log(*tempString);
	}
}

void FParticlePerfStatsListener_GatherAll::DumpStatsToFile()
{
	const FString PathName = FPaths::ProfilingDir() + TEXT("ParticlePerf");
	IFileManager::Get().MakeDirectory(*PathName);

	const FString Filename = FString::Printf(TEXT("ParticlePerf-%s.csv"), *FDateTime::Now().ToString(TEXT("%d-%H.%M.%S")));
	const FString FilePath = PathName / Filename;

	if (FArchive* FileAr = IFileManager::Get().CreateDebugFileWriter(*FilePath))
	{
		TUniquePtr<FOutputDeviceArchiveWrapper> FileArWrapper(new FOutputDeviceArchiveWrapper(FileAr));
		DumpStatsToDevice(*FileArWrapper.Get());
		delete FileAr;
	}
}

//////////////////////////////////////////////////////////////////////////

FParticlePerfStatsListener_TimedTest::FParticlePerfStatsListener_TimedTest(int32 NumFrames)
	: FramesRemaining(NumFrames)
{

}

void FParticlePerfStatsListener_TimedTest::End()
{
	//TODO: Move this stuff into the listeners themselves with some utilities in the manager so each listener can customize it's output more.
	if (GLog != nullptr)
	{
		DumpStatsToDevice(*GLog);
	}
	DumpStatsToFile();
}

bool FParticlePerfStatsListener_TimedTest::Tick()
{
	FParticlePerfStatsListener_GatherAll::Tick();

	return --FramesRemaining > 0;
}

//////////////////////////////////////////////////////////////////////////

#if CSV_PROFILER

FParticlePerfStatsListenerPtr FParticlePerfStatsListener_CSVProfiler::CSVListener;
void FParticlePerfStatsListener_CSVProfiler::OnCSVStart()
{
	CSVListener = MakeShared<FParticlePerfStatsListener_CSVProfiler, ESPMode::ThreadSafe>();
	FParticlePerfStatsManager::AddListener(CSVListener);
}

void FParticlePerfStatsListener_CSVProfiler::OnCSVEnd()
{
	FParticlePerfStatsManager::RemoveListener(CSVListener.Get());
}

void FParticlePerfStatsListener_CSVProfiler::End()
{
	if (GLog != nullptr)
	{
		DumpStatsToDevice(*GLog);
	}
	DumpStatsToFile();
}

#endif

//////////////////////////////////////////////////////////////////////////

int32 FParticlePerfStatsListener_DebugRender::RenderStats(class UWorld* World, class FViewport* Viewport, class FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation, const FRotator* ViewRotation)
{
	UFont* Font = GEngine->GetSmallFont();
	check(Font != nullptr);

	float CharWidth = 0.0f;
	float CharHeight = 0.0f;
	Font->GetCharSize('W', CharWidth, CharHeight);
	const float ColumnWidth = 32 * CharWidth;
	const int32 FontHeight = Font->GetMaxCharHeight() + 2.0f;

	X = 100;


	// Draw background
	{
		int32 NumRows = 0;
		for (auto it = AccumulatedStats.CreateIterator(); it; ++it)
		{
			FAccumulatedParticlePerfStats* PerfStats = it.Value().Get();
			if (PerfStats == nullptr || PerfStats->GetGameThreadStats().NumFrames == 0)
			{
				continue;
			}
			++NumRows;
		}
	}

	static FLinearColor HeaderBackground = FLinearColor(1.0f, 1.0f, 1.0f, 0.5f);
	static FLinearColor BackgroundColors[] = { FLinearColor(0.6f, 0.6f, 0.6f, 0.5f), FLinearColor(0.4f, 0.4f, 0.4f, 0.5f) };

	// Display Header
	Canvas->DrawTile(X - 2, Y - 1, (ColumnWidth * 5) + 4, FontHeight, 0.0f, 0.0f, 1.0f, 1.0f, HeaderBackground);
	Canvas->DrawShadowedString(X + ColumnWidth * 0, Y, TEXT("System Name"), Font, FLinearColor::Yellow);
	Canvas->DrawShadowedString(X + ColumnWidth * 1, Y, TEXT("Average PerFrame GT | GT CNC | RT"), Font, FLinearColor::Yellow);
	Canvas->DrawShadowedString(X + ColumnWidth * 2, Y, TEXT("Average PerInstance GT | GT CNC | RT"), Font, FLinearColor::Yellow);
	Canvas->DrawShadowedString(X + ColumnWidth * 3, Y, TEXT("Peak PerFrame GT | RT"), Font, FLinearColor::Yellow);
	Y += FontHeight;

	FString tempString;
	int32 RowNum = 0;
	for (auto it = AccumulatedStats.CreateIterator(); it; ++it)
	{
		FAccumulatedParticlePerfStats* PerfStats = it.Value().Get();
		if (PerfStats == nullptr)
		{
			continue;
		}

		const FAccumulatedParticlePerfStats_GT& GTStats = PerfStats->GetGameThreadStats();
		const FAccumulatedParticlePerfStats_RT& RTStats = PerfStats->GetRenderThreadStats_GameThread();
		UFXSystemAsset* System = it.Key().Get();
		if (GTStats.NumFrames == 0 || RTStats.NumFrames == 0 || System == nullptr)
		{
			continue;
		}

		// Background
		++RowNum;
		Canvas->DrawTile(X - 2, Y - 1, (ColumnWidth * 5) + 4, FontHeight, 0.0f, 0.0f, 1.0f, 1.0f, BackgroundColors[RowNum & 1]);

		// System Name
		FString SystemName = System->GetFName().ToString();
		Canvas->DrawShadowedString(X + ColumnWidth * 0, Y, *SystemName, Font, FLinearColor::Yellow);

		// Average Per Frame
		tempString.Reset();
		tempString.Appendf(
			TEXT("%4u | %4u | %4u"),
			uint32(FPlatformTime::ToMilliseconds64(GTStats.AccumulatedStats.TickGameThreadCycles + GTStats.AccumulatedStats.FinalizeCycles) * 1000.0 / double(GTStats.NumFrames)),
			uint32(FPlatformTime::ToMilliseconds64(GTStats.AccumulatedStats.TickConcurrentCycles + GTStats.AccumulatedStats.EndOfFrameCycles) * 1000.0 / double(GTStats.NumFrames)),
			uint32(FPlatformTime::ToMilliseconds64(RTStats.AccumulatedStats.RenderUpdateCycles + RTStats.AccumulatedStats.GetDynamicMeshElementsCycles) * 1000.0 / double(RTStats.NumFrames)),
			uint32(FPlatformTime::ToMilliseconds64(GTStats.AccumulatedStats.TickGameThreadCycles) * 1000.0 / double(GTStats.NumFrames))
		);
		Canvas->DrawShadowedString(X + ColumnWidth * 1, Y, *tempString, Font, FLinearColor::Yellow);

		// Average Per Instances
		tempString.Reset();
		tempString.Appendf(
			TEXT("%4u | %4u | %4u"),
			uint32(FPlatformTime::ToMilliseconds64(GTStats.AccumulatedStats.TickGameThreadCycles + GTStats.AccumulatedStats.FinalizeCycles) * 1000.0 / double(GTStats.AccumulatedStats.NumInstances)),
			uint32(FPlatformTime::ToMilliseconds64(GTStats.AccumulatedStats.TickConcurrentCycles + GTStats.AccumulatedStats.EndOfFrameCycles) * 1000.0 / double(GTStats.AccumulatedStats.NumInstances)),
			uint32(FPlatformTime::ToMilliseconds64(RTStats.AccumulatedStats.RenderUpdateCycles + RTStats.AccumulatedStats.GetDynamicMeshElementsCycles) * 1000.0 / double(RTStats.AccumulatedStats.NumInstances)),
			uint32(FPlatformTime::ToMilliseconds64(GTStats.AccumulatedStats.TickGameThreadCycles) * 1000.0 / double(GTStats.AccumulatedStats.NumInstances))
		);
		Canvas->DrawShadowedString(X + ColumnWidth * 2, Y, *tempString, Font, FLinearColor::Yellow);

		// Peak Per Frame
		tempString.Reset();
		tempString.Append(TEXT("GT[ "));
		for (uint64 v : GTStats.MaxPerFrameTotalCycles)
		{
			tempString.Appendf(TEXT("%4u "), uint32(FPlatformTime::ToMilliseconds64(v) * 1000.0));
		}
		tempString.Append(TEXT("] RT["));
		for (uint64 v : RTStats.MaxPerFrameTotalCycles)
		{
			tempString.Appendf(TEXT("%4u "), uint32(FPlatformTime::ToMilliseconds64(v) * 1000.0));
		}
		tempString.Append(TEXT("]"));
		Canvas->DrawShadowedString(X + ColumnWidth * 3, Y, *tempString, Font, FLinearColor::Yellow);

		Y += FontHeight;
	}

	return Y;
}

//////////////////////////////////////////////////////////////////////////

#endif //WITH_PARTICLE_PERF_STATS
