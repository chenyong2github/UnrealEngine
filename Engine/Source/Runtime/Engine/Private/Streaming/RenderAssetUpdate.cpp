// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
RenderAssetUpdate.cpp: Base class of helpers to stream in and out texture/mesh LODs.
=============================================================================*/

#include "RenderAssetUpdate.h"
#include "Engine/StreamableRenderAsset.h"
#include "Streaming/TextureStreamingHelpers.h"
#include "UObject/UObjectIterator.h"

float GStreamingFlushTimeOut = 3.00f;
static FAutoConsoleVariableRef CVarStreamingFlushTimeOut(
	TEXT("r.Streaming.FlushTimeOut"),
	GStreamingFlushTimeOut,
	TEXT("Time before we timeout when flushing streaming (default=3)"),
	ECVF_Default
);

static TAutoConsoleVariable<int32> CVarStreamingStressTestExtraAsyncLatency(
	TEXT("r.Streaming.StressTest.ExtraAsyncLatency"),
	0,
	TEXT("An extra latency in milliseconds for each async task when doing the stress test."),
	ECVF_Cheat);


volatile int32 GRenderAssetStreamingSuspension = 0;

bool IsAssetStreamingSuspended()
{
	return GRenderAssetStreamingSuspension > 0;
}


void SuspendRenderAssetStreaming()
{
	ensure(IsInGameThread());

	if (FPlatformAtomics::InterlockedIncrement(&GRenderAssetStreamingSuspension) == 1)
	{
		bool bHasPendingUpdate = false;

		// Wait for all assets to have their update lock unlocked. 
		TArray<UStreamableRenderAsset*> LockedAssets;
		for (TObjectIterator<UStreamableRenderAsset> It; It; ++It)
		{
			UStreamableRenderAsset* CurrentAsset = *It;
			if (CurrentAsset && CurrentAsset->HasPendingUpdate())
			{
				bHasPendingUpdate = true;
				if (CurrentAsset->IsPendingUpdateLocked())
				{
					LockedAssets.Add(CurrentAsset);
				}
			}
		}

		// If an asset stays locked for  GStreamingFlushTimeOut, 
		// we conclude there is a deadlock or that the object is never going to recover.

		const float TimeIncrement = 0.010f;
		float TimeLimit = GStreamingFlushTimeOut;

		while (LockedAssets.Num() && (TimeLimit > 0 || GStreamingFlushTimeOut <= 0))
		{
			FPlatformProcess::Sleep(TimeIncrement);
			FlushRenderingCommands();
			
			TimeLimit -= TimeIncrement;

			for (int32 LockedIndex = 0; LockedIndex < LockedAssets.Num(); ++LockedIndex)
			{
				UStreamableRenderAsset* CurrentAsset = LockedAssets[LockedIndex];
				if (!CurrentAsset || !CurrentAsset->IsPendingUpdateLocked())
				{
					LockedAssets.RemoveAtSwap(LockedIndex);
					--LockedIndex;
				}
			}
		}

		if (TimeLimit <= 0 && GStreamingFlushTimeOut > 0)
		{
			UE_LOG(LogContentStreaming, Error, TEXT("SuspendRenderAssetStreaming timed out while waiting for asset:"));
			for (int32 LockedIndex = 0; LockedIndex < LockedAssets.Num(); ++LockedIndex)
			{
				UStreamableRenderAsset* CurrentAsset = LockedAssets[LockedIndex];
				if (CurrentAsset)
				{
					if (CurrentAsset->IsPendingKill() || CurrentAsset->HasAnyFlags(RF_BeginDestroyed|RF_FinishDestroyed))
					{
						UE_LOG(LogContentStreaming, Error, TEXT("	%s"), *CurrentAsset->GetFullName());
					}
					else
					{
						UE_LOG(LogContentStreaming, Error, TEXT("	%s (PendingKill)"), *CurrentAsset->GetFullName());
					}
				}
			}
		}

		// At this point, no more rendercommands or IO requests can be generated before a call to ResumeRenderAssetStreamingRenderTasksInternal().

		if (bHasPendingUpdate)
		{
			// Ensure any pending render command executes.
			FlushRenderingCommands();
		}
	}
}

void ResumeRenderAssetStreaming()
{
	FPlatformAtomics::InterlockedDecrement(&GRenderAssetStreamingSuspension);
	ensure(GRenderAssetStreamingSuspension >= 0);
}

FRenderAssetUpdate::FRenderAssetUpdate(UStreamableRenderAsset* InAsset, int32 InRequestedMips)
	: PendingFirstMip(INDEX_NONE)
	, RequestedMips(INDEX_NONE)
	, ScheduledGTTasks(0)
	, ScheduledRenderTasks(0)
	, ScheduledAsyncTasks(0)
	, StreamableAsset(InAsset)
	, bIsCancelled(false)
	, bDeferExecution(false)
	, TaskState(TS_Init)
{
	check(InAsset);

	const int32 NonStreamingMipCount = InAsset->GetNumNonStreamingMips();
	const int32 MaxMipCount = InAsset->GetNumMipsForStreaming();
	InRequestedMips = FMath::Clamp<int32>(InRequestedMips, NonStreamingMipCount, MaxMipCount);

	if (InRequestedMips > 0 && InRequestedMips != InAsset->GetNumResidentMips() && InAsset->bIsStreamable)
	{
		RequestedMips = InRequestedMips;
		PendingFirstMip = MaxMipCount - RequestedMips;
	}
	else // This shouldn't happen but if it does, then the update is canceled
	{
		bIsCancelled = true;
	}
}

FRenderAssetUpdate::~FRenderAssetUpdate()
{
	// Work must be done here because derived destructors have been called now and so derived members are invalid.
	check(TaskSynchronization.GetValue() == 0 && TaskState == TS_Done);
}

uint32 FRenderAssetUpdate::Release() const
{
	uint32 NewValue = (uint32)NumRefs.Decrement();
	if (NewValue == 0)
	{
		if (ensure(TaskState == TS_Done) && !TaskSynchronization.GetValue())
		{
			delete this;
		}
		else
		{
			// Can't delete this object if some other system has some token to decrement.
			UE_LOG(LogContentStreaming, Error, TEXT("RenderAssetUpdate is leaking (%s, State=%d)"), *StreamableAsset->GetFullName(), (int32)TaskState);
		}
	}
	return NewValue;
}

void FRenderAssetUpdate::Tick(EThreadType InCurrentThread)
{
	if (TaskState != TS_Done)
	{
		bool bIsLocked = true;

		// Should we do aynthing about FApp::ShouldUseThreadingForPerformance()? For example to prevent async thread from stalling game/renderthreads.
		// When the renderthread is the gamethread, don't lock if this is the renderthread to prevent stalling on low priority async tasks.
		if (InCurrentThread == TT_None || (InCurrentThread == TT_Render && !GIsThreadedRendering))
		{
			bIsLocked = CS.TryLock();
		}
		else if (InCurrentThread == TT_GameRunningAsync)
		{
			// When the GameThread tries to execute the async task, in GC, allow several attempts.
			bIsLocked = CS.TryLock();
			InCurrentThread = TT_Async;
		}
		else
		{
			CS.Lock();
		}
		
		if (bIsLocked)
		{
			// This will happens in PushTask() or when ScheduleRenderTask() ends up executing the command.
			const bool bWasAlreadyLocked = (TaskState == TS_Locked);
			TaskState = TS_Locked;

			ETaskState TickResult;
			do // Iterate as longs as there is progress
			{
				// Only test for suspension the first time and in normal progress.
				// When cancelled, we want the update to complete without interruptions, allowing reference to be freed.
				TickResult = TickInternal(InCurrentThread, !bWasAlreadyLocked && !bIsCancelled);
			} 
			while (TickResult == TS_Locked);

			// We do this to prevent updating the TaskState while in the Lock.
			if (!bWasAlreadyLocked)
			{
				TaskState = TickResult;
			}
			CS.Unlock();
		}
	}
}

class FRenderAssetUpdateTickGTTask
{
public:
	FORCEINLINE FRenderAssetUpdateTickGTTask(FRenderAssetUpdate* InUpdate)
		: PendingUpdate(InUpdate)
	{}

	static FORCEINLINE TStatId GetStatId()
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FRenderAssetUpdateTickGTTask, STATGROUP_TaskGraphTasks);
	}

	static FORCEINLINE ENamedThreads::Type GetDesiredThread()
	{
		return ENamedThreads::GameThread;
	}

	static FORCEINLINE ESubsequentsMode::Type GetSubsequentsMode()
	{
		return ESubsequentsMode::FireAndForget;
	}

	void DoTask(ENamedThreads::Type CurThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		check(PendingUpdate);
		PendingUpdate->Tick(FRenderAssetUpdate::TT_GameThread);
		--PendingUpdate->ScheduledGTTasks;
	}

private:
	TRefCountPtr<FRenderAssetUpdate> PendingUpdate;
};

void FRenderAssetUpdate::ScheduleGTTask()
{
	check(TaskState == TS_Locked);

	if (IsInGameThread())
	{
		Tick(TT_GameThread);
	}
	else
	{
		// Notify that a tick is scheduled on the game thread
		++ScheduledGTTasks;

		TGraphTask<FRenderAssetUpdateTickGTTask>::CreateTask().ConstructAndDispatchWhenReady(this);
	}
}

void FRenderAssetUpdate::ScheduleRenderTask()
{
	check(TaskState == TS_Locked);

	// Notify that a tick is scheduled on the render thread.
	++ScheduledRenderTasks;
	// Increment refcount because we don't use a TRefCountPtr with ENQUEUE_RENDER_COMMAND.
	AddRef();

	ENQUEUE_RENDER_COMMAND(RenderAssetUpdateCommand)(
		[&](FRHICommandListImmediate&)
	{
		// Recompute the context has things might have changed!
		Tick(TT_Render);

		--ScheduledRenderTasks;

		// Decrement refcount because we don't use a TRefCountPtr with ENQUEUE_RENDER_COMMAND.
		Release();
	});
}

void FRenderAssetUpdate::ScheduleAsyncTask()
{
	check(TaskState == TS_Locked);

	// Notify that an async tick is scheduled.
	++ScheduledAsyncTasks;
	(new FAsyncMipUpdateTask(this))->StartBackgroundTask();
}

void FRenderAssetUpdate::FMipUpdateTask::DoWork()
{
	check(PendingUpdate.IsValid());

#if !UE_BUILD_SHIPPING
	const int32 ExtraSyncLatency = CVarStreamingStressTestExtraAsyncLatency.GetValueOnAnyThread();
	if (ExtraSyncLatency > 0)
	{
		// Slow down the async. Used to test GC issues.
		FPlatformProcess::Sleep(ExtraSyncLatency * .001f);
	}
#endif

	// Recompute the context has things might have changed!
	PendingUpdate->Tick(FRenderAssetUpdate::TT_Async);
	
	--PendingUpdate->ScheduledAsyncTasks;
}

FRenderAssetUpdate::ETaskState FRenderAssetUpdate::DoLock() 
{  
	CS.Lock(); 
	ETaskState PreviousTaskState = TaskState;
	TaskState = TS_Locked; 
	return PreviousTaskState;
}

void FRenderAssetUpdate::DoUnlock(ETaskState PreviousTaskState)
{
	TaskState = PreviousTaskState; 
	CS.Unlock();
}
