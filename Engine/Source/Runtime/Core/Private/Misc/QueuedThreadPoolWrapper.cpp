// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/QueuedThreadPoolWrapper.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/IQueuedWork.h"
#include "HAL/PlatformProcess.h"

struct FQueuedThreadPoolWrapper::FScheduledWork : public IQueuedWork
{
	FScheduledWork(FQueuedThreadPoolWrapper* InParentPool, IQueuedWork* InWork, EQueuedWorkPriority InPriority)
		: ParentPool(InParentPool)
		, Work(InWork)
		, Priority(InPriority)
		, RequiredMemory(InWork->GetRequiredMemory())
	{
	}

	void DoThreadedWork() override
	{
		Work->DoThreadedWork();
		ParentPool->Schedule(this);
	}

	void Abandon() override
	{
		Work->Abandon();
		ParentPool->Schedule(this);
	}

	EQueuedWorkFlags GetQueuedWorkFlags() const override 
	{
		return Work->GetQueuedWorkFlags();
	}

	int64 GetRequiredMemory() const override 
	{
		return RequiredMemory;
	}

	IQueuedWork* GetInnerWork() const
	{
		return Work;
	}
	
	EQueuedWorkPriority GetPriority() const
	{
		return Priority;
	}

	void Reset()
	{
		Work = nullptr;
	}
private:
	FQueuedThreadPoolWrapper* ParentPool;
	IQueuedWork* Work;
	EQueuedWorkPriority Priority;
	
	// Store the memory of the inner task to ensure it stays constant
	int64 RequiredMemory;
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
				Work->GetInnerWork()->Abandon();
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
			check(Work->GetInnerWork() == nullptr);
			delete Work;
		}
		WorkPool.Empty();
	}
}

void FQueuedThreadPoolWrapper::SetMaxConcurrency(int32 InMaxConcurrency)
{
	MaxConcurrency = InMaxConcurrency == -1 ? WrappedQueuedThreadPool->GetNumThreads() : InMaxConcurrency;

	// We might need to schedule or unshedule tasks depending on how MaxConcurrency has changed.
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
	OnUnscheduled(Work);
	
	ScheduledWork.Remove(Work->GetInnerWork());
	Work->Reset();
	WorkPool.Push(Work);
}

bool FQueuedThreadPoolWrapper::CanSchedule(EQueuedWorkPriority Priority) const
{
	return (MaxTaskToSchedule == -1 || MaxTaskToSchedule > 0 || Priority == EQueuedWorkPriority::Blocking) && CurrentConcurrency < GetMaxConcurrency();
}

FQueuedThreadPoolWrapper::FScheduledWork* FQueuedThreadPoolWrapper::AllocateWork(IQueuedWork* InnerWork, EQueuedWorkPriority Priority)
{
	if (WorkPool.Num() > 0)
	{
		FScheduledWork* Work = WorkPool.Pop(false);
		*Work = FScheduledWork(this, InnerWork, Priority);
		return Work;
	}
	
	return new FScheduledWork(this, InnerWork, Priority);
}

bool FQueuedThreadPoolWrapper::TryRetractWorkNoLock(EQueuedWorkPriority InPriority)
{
	// Scheduled work is bound by MaxConcurrency which is normally limited by Core count. 
	// The linear scan should be small and pretty fast.
	for (TTuple<IQueuedWork*, FScheduledWork*>& Pair : ScheduledWork)
	{
		// higher number means lower priority
		if (Pair.Value->GetPriority() > InPriority)
		{
			if (WrappedQueuedThreadPool->RetractQueuedWork(Pair.Value))
			{
				QueuedWork.Enqueue(Pair.Key, Pair.Value->GetPriority());
				ReleaseWorkNoLock(Pair.Value);

				if (MaxTaskToSchedule != -1)
				{
					MaxTaskToSchedule++;
				}

				return true;
			}
		}
	}

	return false;
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

	// If a higher priority task comes in, try to retract lower priority ones if possible to make room
	EQueuedWorkPriority NextWorkPriority;
	IQueuedWork* NextWork = QueuedWork.Peek(&NextWorkPriority);
	if (NextWork)
	{
		// Continue retracting more work until nothing can be retracted anymore or we can finally squeeze the higher priority task in
		while (!CanSchedule(NextWorkPriority) && TryRetractWorkNoLock(NextWorkPriority))
		{
		}
	}

	// Schedule as many tasks we can fit
	while (CanSchedule(NextWorkPriority))
	{
		EQueuedWorkPriority WorkPriority;
		IQueuedWork* InnerWork = QueuedWork.Dequeue(&WorkPriority);

		if (InnerWork)
		{
			CurrentConcurrency++;
			
			Work = AllocateWork(InnerWork, WorkPriority);
			ScheduledWork.Add(InnerWork, Work);
			OnScheduled(Work);
			WrappedQueuedThreadPool->AddQueuedWork(Work, WorkPriority == EQueuedWorkPriority::Blocking ? WorkPriority : PriorityMapper(WorkPriority));

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
