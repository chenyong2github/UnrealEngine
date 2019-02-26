// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
RenderAssetUpdate.h: Base class of helpers to stream in and out texture/mesh LODs
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeCounter.h"
#include "Async/AsyncWork.h"
#include "Engine/StreamableRenderAsset.h"
#include "RenderingThread.h"

/** SRA stands for StreamableRenderAsset */
#define SRA_UPDATE_CALLBACK(FunctionName) [this](const FContext& C){ FunctionName(C); }

 // Allows yield to lower priority threads
#define RENDER_ASSET_STREAMING_SLEEP_DT (0.010f)

ENGINE_API bool IsAssetStreamingSuspended();

/**
* This class provides a framework for loading and unloading the texture/mesh LODs.
* Each thread essentially calls Tick() until the job is done.
* The object can be safely deleted when IsCompleted() returns true.
*/
template <typename FUpdateContext>
class TRenderAssetUpdate
{
public:
	typedef FUpdateContext FContext;

	/** A thread type used for doing a part of the update process.  */
	enum EThreadType
	{
		TT_None,	// No thread.
		TT_Render,	// The render thread.
		TT_Async	// An async work thread.
	};

	/**  The state of scheduled work for the update process. */
	enum ETaskState
	{
		TS_None,		// Nothing to do.
		TS_Pending,		// The next task (or update step) is configured, but a callback has not been scheduled yet.
		TS_Scheduled,	// The next task (or update step) is configured and a callback has been scheduled on either the renderthread or async thread.
		TS_Locked,		// The object is locked, and no one is allowed to process or look at the next task.
	};

	TRenderAssetUpdate(UStreamableRenderAsset* InAsset, int32 InRequestedMips)
		: PendingFirstMip(INDEX_NONE)
		, RequestedMips(INDEX_NONE)
		, ScheduledTaskCount(0)
		, LockOwningThreadID(FPlatformTLS::GetCurrentThreadId())
		, bIsCancelled(false)
		, TaskState(TS_Locked) // The object is created in the locked state to follow the Tick path
		, PendingTaskState(TS_None)
		, TaskThread(TT_None)
		, CancelationThread(TT_None)
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

	~TRenderAssetUpdate()
	{
		// Work must be done here because derived destructors have been called now and so derived members are invalid.
		ensure(ScheduledTaskCount <= 0);
	}

	/**
	* Do or schedule any pending work for a given texture.
	*
	* @param InAsset - the texture/mesh being updated, this must be the same texture/mesh as the texture/mesh used to create this object.
	* @param InCurrentThread - the thread from which the tick is being called. Using TT_None ensures that no work will be immediately performed.
	*/
	void Tick(UStreamableRenderAsset* InAsset, EThreadType InCurrentThread)
	{
		if (TaskState == TS_None || (TaskSynchronization.GetValue() > 0 && InCurrentThread == TT_None))
		{
			// Early exit if the task is not ready to execute and we are ticking from non executing thread.
			// Executing thread must not early exit in order to make sure that tasks are correctly scheduled.
			// Otherwise, this assumes that the game thread regularly ticks.
			return;
		}

		// Acquire the lock if there is work to do and if it is allowed to wait for the lock
		if (DoConditionalLock(InCurrentThread))
		{
			ensure(PendingTaskState == TS_Scheduled || PendingTaskState == TS_Pending);

			// If the task is ready to execute. 
			// If this is the renderthread and we shouldn't run renderthread task, mark as pending.
			// This will require a tick from the game thread to reschedule the task.
			if (TaskSynchronization.GetValue() <= 0 && !IsAssetStreamingSuspended())
			{
				FContext Context(InAsset, InCurrentThread);

				// The task params can not change at this point but the bIsCancelled state could change.
				// To ensure coherency, the cancel state is cached as it affects which thread is relevant.
				const bool bCachedIsCancelled = bIsCancelled;
				const EThreadType RelevantThread = !bCachedIsCancelled ? TaskThread : CancelationThread;

				if (RelevantThread == TT_None)
				{
					ClearTask();
				}
				else if (InCurrentThread == RelevantThread)
				{
					FCallback CachedCallback = !bCachedIsCancelled ? TaskCallback : CancelationCallback;
					ClearTask();
					CachedCallback(Context); // Valid if the thread is valid.
				}
				else if (PendingTaskState != TS_Scheduled || InCurrentThread != TT_None)
				{
					// If the task was never scheduled (because synchro was not ready) schedule now.
					// We also reschedule if this is an executing thread and it end up not being the good thread.
					// This can happen when a task gets cancelled between scheduling and execution.
					// We enforce that executing threads either execute or reschedule to prevent a possible stalls
					// since game thread will not reschedule after the first time.
					// It's completely safe to schedule several time as task execution can only ever happen once.
					// we also keep track of how many callbacks are scheduled through ScheduledTaskCount to prevent
					// deleting the object while another thread is scheduled to access it.
					ScheduleTick(Context, RelevantThread);
				}
				else // Otherwise unlock the task for the executing thread to process it.
				{
					PendingTaskState = TS_Scheduled;
				}
			}
			else // If synchro is not ready, mark the task as pending to be scheduled.
			{
				PendingTaskState = TS_Pending;
			}

			DoUnlock();
		}
	}

	/** Returns whether the task has finished executing and there is no other thread possibly accessing it. */
	bool IsCompleted() const
	{
		return ScheduledTaskCount <= 0 && TaskState == TS_None;
	}

	/** Cancel the current update. Will also attempt to cancel pending IO requests, see FTexture2DStreamIn_IO::Abort(). */
	void Abort()
	{
		MarkAsCancelled();
	}

	/** Returns whether the task was aborted through Abort() or cancelled.  */
	bool IsCancelled() const
	{
		return bIsCancelled;
	}

	/**
	* Perform a lock on the object, preventing any other thread from processing a pending task in Tick().
	* The lock will stall if there was another thread already processing the task data.
	*/
	void DoLock()
	{
		DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FRenderAssetUpdate::DoLock"), STAT_FRenderAssetUpdate_DoLock, STATGROUP_StreamingDetails);

		// Can't lock twice on the same thread or we will deadlock
		check(LockOwningThreadID != FPlatformTLS::GetCurrentThreadId());

		// Acquire the lock
		int32 CachedTaskState = TS_None;
		do
		{
			// Sleep in between retries.
			if (CachedTaskState != TS_None)
			{
				FPlatformProcess::Sleep(0);
			}
			// Cache the task state.
			CachedTaskState = TaskState;

		} while (CachedTaskState == TS_Locked || FPlatformAtomics::InterlockedCompareExchange(&TaskState, TS_Locked, CachedTaskState) != CachedTaskState);

		// If we just acquired the lock, nothing should be in the process.
		ensure(PendingTaskState == TS_None);
		LockOwningThreadID = FPlatformTLS::GetCurrentThreadId();
		PendingTaskState = (ETaskState)CachedTaskState;
	}

	/** Release any lock on the object, allowing other thread to modify it. */
	void DoUnlock()
	{
		// Make sure lock and unlock happens on the same thread
		check(LockOwningThreadID == FPlatformTLS::GetCurrentThreadId());

		ensure(TaskState == TS_Locked && PendingTaskState != TS_Locked);

		ETaskState CachedPendingTaskState = PendingTaskState;

		// Reset the pending task state first to prevent racing condition that could fail ensure(PendingTaskState == TS_None) in DoLock()
		PendingTaskState = TS_None;
		LockOwningThreadID = InvalidLockOwningThreadID;
		TaskState = CachedPendingTaskState;
	}

	bool IsLocked() const
	{
		return TaskState == TS_Locked;
	}
	/** Get the number of requested mips for this update, ignoring cancellation attempts. */
	int32 GetNumRequestedMips() const
	{
		return RequestedMips;
	}

protected:
	/** A callback used to perform a task in the update process. Each task must be executed on a specific thread. */
	typedef TFunction<void(const FContext& Context)> FCallback;

	/** Set the task state as cancelled. This is internally called in Abort() and when any critical conditions are not met when performing the update. */
	void MarkAsCancelled()
	{
		bIsCancelled = true;
	}

	/**
	* Defines the next step to be executed. The next step will be executed by calling the callback on the specified thread.
	* The callback (for both success and cancelation) will only be executed if TaskSynchronization reaches 0.
	* If all requirements are immediately satisfied when calling the PushTask the relevant callback will be called immediately.
	*
	* @param Context - The context defining which texture is being updated and on which thread this is being called.
	* @param InTaskThread - The thread on which to call the next step of the update, being TaskCallback.
	* @param InTaskCallback - The callback that will perform the next step of the update.
	* @param InCancelationThread - The thread on which to call the cancellation of the update (only if the update gets cancelled).
	* @param InCancelationCallback - The callback handling the cancellation of the update (only if the update gets cancelled).
	*/
	void PushTask(const FContext& Context, EThreadType InTaskThread, const FCallback& InTaskCallback, EThreadType InCancelationThread, const FCallback& InCancelationCallback)
	{
		// PushTask can only be called by the one thread/callback that is doing the processing. 
		// This means we don't need to check whether other threads could be trying to push tasks.
		check(TaskState == TS_Locked);
		checkSlow((bool)InTaskCallback == (InTaskThread != TT_None));
		checkSlow((bool)InCancelationCallback == (InCancelationThread != TT_None));

		// To ensure coherency, the cancel state is cached as it affects which thread is relevant.
		const bool bCachedIsCancelled = bIsCancelled;
		const EThreadType RelevantThread = !bCachedIsCancelled ? InTaskThread : InCancelationThread;

		// TaskSynchronization is expected to be set before call this.
		// If the update is suspended, delay the scheduling until not suspended anymore.
		const bool bCanExecuteNow = TaskSynchronization.GetValue() <= 0 && !IsAssetStreamingSuspended();

		if (RelevantThread == TT_None)
		{
			// Nothing to do.
		}
		else if (bCanExecuteNow && Context.GetCurrentThread() == RelevantThread)
		{
			FCallback CachedCallback = !bCachedIsCancelled ? InTaskCallback : InCancelationCallback;
			// Not null otherwise relevant thread would be TT_None.
			CachedCallback(Context);
		}
		else
		{
			TaskThread = InTaskThread;
			TaskCallback = InTaskCallback;
			CancelationThread = InCancelationThread;
			CancelationCallback = InCancelationCallback;

			if (bCanExecuteNow)
			{
				ScheduleTick(Context, RelevantThread);
			}
			else
			{
				PendingTaskState = TS_Pending;
			}
		}
	}

private:
	/**
	* Locks the object only if there is work to do. If the calling thread is has no capability to actually perform any work,
	* the lock attempt will also fails if the object is already locked. This is used essentially to prevent the gamethread
	* from being blocked when ticking the update while the update is already being ticked on another thread.
	*
	* @param InCurrentThread - the thread trying to lock the object. Passing TS_None may end up preventing to acquired the object.
	* @return Whether the lock succeeded.
	*/
	bool DoConditionalLock(EThreadType InCurrentThread)
	{
		DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FRenderAssetUpdate::DoConditionalLock"), STAT_FRenderAssetUpdate_DoConditionalLock, STATGROUP_StreamingDetails);

		// Can't lock twice on the same thread or we will deadlock
		if (LockOwningThreadID == FPlatformTLS::GetCurrentThreadId())
		{
			// We are trying to execute the task on the current thread but failed. Ask for a reschedule on next Tick.
			// It is safe to modify PendingTaskState here because current thread has the lock
			if (PendingTaskState == TS_Scheduled)
			{
				PendingTaskState = TS_Pending;
			}
			return false;
		}

		// Acquire the lock if there is work to do and if it is allowed to wait for the lock
		int32 CachedTaskState = TS_None;
		do
		{
			// Sleep in between retries.
			if (CachedTaskState != TS_None)
			{
				FPlatformProcess::Sleep(0);
			}
			// Cache the task state.
			CachedTaskState = TaskState;

			// Return immediately if there is no work to do or if it is locked and we are not on an executing thread.
			// When the renderthread is the gamethread, don't lock if this is the renderthread to prevent stalling on low priority async tasks.
			if (CachedTaskState == TS_None || (CachedTaskState == TS_Locked && (InCurrentThread == TT_None || (InCurrentThread == TT_Render && !GIsThreadedRendering))))
			{
				return false;
			}
		} while (CachedTaskState == TS_Locked || FPlatformAtomics::InterlockedCompareExchange(&TaskState, TS_Locked, CachedTaskState) != CachedTaskState);

		ensure(PendingTaskState == TS_None);
		LockOwningThreadID = FPlatformTLS::GetCurrentThreadId();
		PendingTaskState = (ETaskState)CachedTaskState;

		return true;
	}

	/**
	* Pushes a callback on either the gamethread or an async work thread.
	*
	* @param Context - the context from the caller thread.
	* @param InThread - the thread on which to schedule a tick.
	*/
	void ScheduleTick(const FContext& Context, EThreadType InThread)
	{
		// Task can only be scheduled once the synchronization is completed.
		check(TaskSynchronization.GetValue() <= 0);

		// The pending update needs to be cached because the scheduling can happen in the constructor, before the assignment.

		// When not having many threads, async task should never schedule tasks since they would wake threads with higher priority while still holding the lock.
		if (Context.GetCurrentThread() == TT_Async && !FApp::ShouldUseThreadingForPerformance())
		{
			PendingTaskState = TS_Pending;
		}
		else if (InThread == TT_Render)
		{
			FPlatformAtomics::InterlockedIncrement(&ScheduledTaskCount);
			PendingTaskState = TS_Scheduled;

			UStreamableRenderAsset* RenderAsset = Context.GetRenderAsset();
			TRenderAssetUpdate* CachedPendingUpdate = this;
			ENQUEUE_RENDER_COMMAND(RenderAssetUpdateCommand)(
				[RenderAsset, CachedPendingUpdate](FRHICommandListImmediate&)
			{
				check(RenderAsset && CachedPendingUpdate);

				// Recompute the context has things might have changed!
				CachedPendingUpdate->Tick(RenderAsset, TT_Render);

				FPlatformMisc::MemoryBarrier();
				FPlatformAtomics::InterlockedDecrement(&CachedPendingUpdate->ScheduledTaskCount);
			});
		}
		else // InThread == TT_Async
		{
			check(InThread == TT_Async);

			FPlatformAtomics::InterlockedIncrement(&ScheduledTaskCount);
			PendingTaskState = TS_Scheduled;

			(new FAsyncMipUpdateTask(Context.GetRenderAsset(), this))->StartBackgroundTask();
		}
	}

	/** Clears any pending work. */
	void ClearTask()
	{
		// Reset everything so that the callback setup a new callback
		PendingTaskState = TS_None;
		TaskThread = TT_None;
		TaskCallback = nullptr;
		CancelationThread = TT_None;
		CancelationCallback = nullptr;
		TaskSynchronization.Set(0);
	}

	/** An async task used to call tick on the pending update. */
	class FMipUpdateTask : public FNonAbandonableTask
	{
	public:
		FMipUpdateTask(UStreamableRenderAsset* InAsset, TRenderAssetUpdate* InPendingUpdate)
			: RenderAsset(InAsset)
			, CachedPendingUpdate(InPendingUpdate)
		{}

		void DoWork()
		{
			check(RenderAsset && CachedPendingUpdate);

			// Recompute the context has things might have changed!
			CachedPendingUpdate->Tick(RenderAsset, TRenderAssetUpdate::TT_Async);

			FPlatformMisc::MemoryBarrier();
			FPlatformAtomics::InterlockedDecrement(&CachedPendingUpdate->ScheduledTaskCount);
		}

		FORCEINLINE TStatId GetStatId() const
		{
			RETURN_QUICK_DECLARE_CYCLE_STAT(FMipUpdateTask, STATGROUP_ThreadPoolAsyncTasks);
		}

	protected:
		UStreamableRenderAsset* RenderAsset;
		TRenderAssetUpdate* CachedPendingUpdate;
	};

	/** The async task to update this object, only one can be active at anytime. It just calls Tick(). */
	typedef FAutoDeleteAsyncTask<FMipUpdateTask> FAsyncMipUpdateTask;

protected:
	/** A special value to indicate that no thread is holding the lock. */
	static const uint32 InvalidLockOwningThreadID = 0xffffffff;

	/** The index of mip that will end as being the first mip of the intermediate (future) texture/mesh. */
	int32 PendingFirstMip;
	/** The total number of mips of the intermediate (future) texture/mesh. */
	int32 RequestedMips;

	/** Synchronization used for trigger the task next step execution. */
	FThreadSafeCounter	TaskSynchronization;

	/** The number of scheduled ticks (and exceptionally other work from FCancelIORequestsTask) from the renderthread and async thread. Used to prevent deleting the object while it could be accessed. */
	volatile int32 ScheduledTaskCount;

	/**
	 * The TLS ID of the thread holding the lock (TS_Locked).
	 * Used to prevent calling Tick inside Tick on the same thread, which causes deadlock.
	 * This requires a single call to Tick runs on the same thread from start to end (a.k.a. it doesn't work with Fiber)
	 */
	volatile uint32 LockOwningThreadID;

	/** Whether the task has been cancelled because the update could not proceed or because the user called Abort(). */
	bool bIsCancelled;

	/** The state of the work yet to be performed to complete the update or cancelation. */
	volatile int32 TaskState;
	/** The pending state of future work. Used because the object is in locked state (TS_Locked) when being updated, until the call to Unlock() */
	ETaskState PendingTaskState;

	/** The thread on which to call the next step of the update, being TaskCallback. */
	EThreadType TaskThread;
	/** The callback that will perform the next step of the update. */
	FCallback TaskCallback;
	/** The thread on which to call the cancellation of the update (only if the update gets cancelled). */
	EThreadType CancelationThread; // The thread on which the callbacks should be called.
								   /** The callback handling the cancellation of the update (only if the update gets cancelled). */
	FCallback CancelationCallback;
};

void SuspendRenderAssetStreaming();
void ResumeRenderAssetStreaming();
