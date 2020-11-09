// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/QueuedThreadPoolWrapper.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/IQueuedWork.h"
#include "HAL/PlatformProcess.h"

struct FQueuedThreadPoolWrapper::FScheduledWork : public IQueuedWork
{
	FScheduledWork(FQueuedThreadPoolWrapper* InParentPool)
		: ParentPool(InParentPool)
	{
	}

	void DoThreadedWork() override
	{
		InnerWork->DoThreadedWork();
		ParentPool->Schedule(this);
	}

	void Abandon() override
	{
		InnerWork->Abandon();
		ParentPool->Schedule(this);
	}

	IQueuedWork* InnerWork = nullptr;
	EQueuedWorkPriority Priority = EQueuedWorkPriority::Normal;
private:
	FQueuedThreadPoolWrapper* ParentPool;
};

FQueuedThreadPoolWrapper::FQueuedThreadPoolWrapper(FQueuedThreadPool* InWrappedQueuedThreadPool, int32 InMaxConcurrency, TFunction<EQueuedWorkPriority(EQueuedWorkPriority)> InPriorityMapper)
	: PriorityMapper(InPriorityMapper)
	, WrappedQueuedThreadPool(InWrappedQueuedThreadPool)
	, MaxConcurrency(InMaxConcurrency == -1 ? InWrappedQueuedThreadPool->GetNumThreads() : InMaxConcurrency)
	, MaxTaskToSchedule(-1)
	, CurrentConcurrency(0)
{
}

FQueuedThreadPoolWrapper::~FQueuedThreadPoolWrapper()
{
	Destroy();
}

bool FQueuedThreadPoolWrapper::Create(uint32 InNumQueuedThreads, uint32 StackSize, EThreadPriority ThreadPriority, const TCHAR* Name)
{
	return true;
}

void FQueuedThreadPoolWrapper::Destroy()
{
	{
		FRWScopeLock ScopeLock(Lock, SLT_Write);
		// Clean up all queued objects
		while (IQueuedWork* WorkItem = QueuedWork.Dequeue())
		{
			WorkItem->Abandon();
		}

		QueuedWork.Reset();

		// Try to retract anything already in flight
		TArray<FScheduledWork*> ScheduledWorkToRetract;
		ScheduledWork.GenerateValueArray(ScheduledWorkToRetract);

		for (FScheduledWork* Work : ScheduledWorkToRetract)
		{
			if (WrappedQueuedThreadPool->RetractQueuedWork(Work))
			{
				Work->InnerWork->Abandon();
				ReleaseWorkNoLock(Work);
			}
		}
	}

	if (CurrentConcurrency)
	{
		// We can't delete our WorkPool elements
		// if they're still referenced by a threadpool.
		// Retraction didn't work, so no choice to wait until they're all finished.
		TRACE_CPUPROFILER_EVENT_SCOPE(FQueuedThreadPoolWrapper::DestroyWait);
		while (CurrentConcurrency)
		{
			FPlatformProcess::Sleep(0.1f);
		}
	}

	{
		FRWScopeLock ScopeLock(Lock, SLT_Write);
		for (FScheduledWork* Work : WorkPool)
		{
			check(Work->InnerWork == nullptr);
			delete Work;
		}
		WorkPool.Empty();
	}
}

void FQueuedThreadPoolWrapper::SetMaxConcurrency(int32 InMaxConcurrency)
{
	MaxConcurrency = InMaxConcurrency == -1 ? WrappedQueuedThreadPool->GetNumThreads() : InMaxConcurrency;

	// In case we just increased the concurrency, we might need to schedule new tasks
	Schedule();
}

void FQueuedThreadPoolWrapper::Pause()
{
	FRWScopeLock ScopeLock(Lock, SLT_Write);
	MaxTaskToSchedule = 0;
}

void FQueuedThreadPoolWrapper::Resume(int32 InNumQueuedWork)
{
	{
		FRWScopeLock ScopeLock(Lock, SLT_Write);
		MaxTaskToSchedule = InNumQueuedWork;
	}

	Schedule();
}

void FQueuedThreadPoolWrapper::AddQueuedWork(IQueuedWork* InQueuedWork, EQueuedWorkPriority InPriority)
{
	{
		FRWScopeLock ScopeLock(Lock, SLT_Write);
		QueuedWork.Enqueue(InQueuedWork, InPriority);
	}

	Schedule();
}

bool FQueuedThreadPoolWrapper::RetractQueuedWork(IQueuedWork* InQueuedWork)
{
	FScheduledWork* Retracted = nullptr;
	{
		FRWScopeLock ScopeLock(Lock, SLT_Write);
		if (QueuedWork.Retract(InQueuedWork))
		{
			return true;
		}
		
		Retracted = ScheduledWork.FindRef(InQueuedWork);
		if (Retracted && !WrappedQueuedThreadPool->RetractQueuedWork(Retracted))
		{
			Retracted = nullptr;
		}
	}

	if (Retracted)
	{
		Schedule(Retracted);
	}
	return Retracted != nullptr;
}

int32 FQueuedThreadPoolWrapper::GetNumThreads() const
{
	return MaxConcurrency;
}

void FQueuedThreadPoolWrapper::ReleaseWorkNoLock(FScheduledWork* Work)
{
	CurrentConcurrency--;
	ScheduledWork.Remove(Work->InnerWork);
	Work->InnerWork = nullptr;
	WorkPool.Push(Work);
}

void FQueuedThreadPoolWrapper::Schedule(FScheduledWork* Work)
{
	FRWScopeLock ScopeLock(Lock, SLT_Write);

	// It's important to reduce the current concurrency before entering the loop
	// especially when MaxConcurrency is 1 to ensure new work is scheduled and don't 
	// end up never being called again.
	if (Work != nullptr)
	{
		ReleaseWorkNoLock(Work);
	}

	// If a higher priority task comes in, try to retract a lower priority one if possible to make room
	if (CurrentConcurrency >= GetMaxConcurrency() && (MaxTaskToSchedule == -1 || MaxTaskToSchedule > 0))
	{
		EQueuedWorkPriority NextWorkPriority;
		IQueuedWork* NextWork = QueuedWork.Peek(&NextWorkPriority);
		if (NextWork)
		{
			// Scheduled work is bound by MaxConcurrency which is normally limited by Core count. 
			// The linear scan should be small and pretty fast.
			for (TTuple<IQueuedWork*, FScheduledWork*>& Pair : ScheduledWork)
			{
				// higher number means lower priority
				if (Pair.Value->Priority > NextWorkPriority)
				{
					if (WrappedQueuedThreadPool->RetractQueuedWork(Pair.Value))
					{
						QueuedWork.Enqueue(Pair.Key, Pair.Value->Priority);
						ReleaseWorkNoLock(Pair.Value);

						if (MaxTaskToSchedule != -1)
						{
							MaxTaskToSchedule++;
						}
						break;
					}
				}
			}
		}
	}

	while (CurrentConcurrency < GetMaxConcurrency() && (MaxTaskToSchedule == -1 || MaxTaskToSchedule > 0))
	{
		EQueuedWorkPriority WorkPriority;
		IQueuedWork* InnerWork = QueuedWork.Dequeue(&WorkPriority);

		if (InnerWork)
		{
			CurrentConcurrency++;
			if (WorkPool.Num() > 0)
			{
				Work = WorkPool.Pop(false);
			}
			else
			{
				Work = new FScheduledWork(this);
			}

			Work->Priority = WorkPriority;
			Work->InnerWork = InnerWork;
			ScheduledWork.Add(InnerWork, Work);
			WrappedQueuedThreadPool->AddQueuedWork(Work, PriorityMapper(WorkPriority));

			if (MaxTaskToSchedule > 0)
			{
				MaxTaskToSchedule--;
			}
		}
		else
		{
			break;
		}
	}
}
