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
		InnerWork = nullptr;
		ParentPool->Schedule(this);
	}

	virtual void Abandon() override
	{
		InnerWork->Abandon();
		InnerWork = nullptr;
		ParentPool->Schedule(this);
	}

	IQueuedWork* InnerWork = nullptr;

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

bool FQueuedThreadPoolWrapper::Create(uint32 InNumQueuedThreads, uint32 StackSize, EThreadPriority ThreadPriority)
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
	}

	// We can't delete our WorkPool elements
	// if they're still referenced by a threadpool.
	// No choice to wait until they're all finished.
	if (CurrentConcurrency)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FQueuedThreadPoolWrapper::DestroyWait);
		while (CurrentConcurrency)
		{
			FPlatformProcess::Sleep(0.1f);
		}
	}

	{
		FRWScopeLock ScopeLock(Lock, SLT_Write);
		for (FScheduledWork* ScheduledWork : WorkPool)
		{
			check(ScheduledWork->InnerWork == nullptr);
			delete ScheduledWork;
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
	FRWScopeLock ScopeLock(Lock, SLT_Write);
	return QueuedWork.Retract(InQueuedWork);
}

int32 FQueuedThreadPoolWrapper::GetNumThreads() const
{
	return MaxConcurrency;
}

void FQueuedThreadPoolWrapper::Schedule(FScheduledWork* Work)
{
	FRWScopeLock ScopeLock(Lock, SLT_Write);

	// It's important to reduce the current concurrency before entering the loop
	// especially when MaxConcurrency is 1 to ensure new work is scheduled and don't 
	// end up never being called again.
	if (Work != nullptr)
	{
		CurrentConcurrency--;
		WorkPool.Push(Work);
		Work = nullptr;
	}

	while (CurrentConcurrency < MaxConcurrency.Load(EMemoryOrder::Relaxed) && (MaxTaskToSchedule == -1 || MaxTaskToSchedule > 0))
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

			Work->InnerWork = InnerWork;
			WrappedQueuedThreadPool->AddQueuedWork(Work, PriorityMapper(WorkPriority));
			Work = nullptr;

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
