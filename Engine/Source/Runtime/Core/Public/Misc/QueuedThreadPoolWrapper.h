// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/GenericPlatformAffinity.h"
#include "QueuedThreadPool.h"
#include "ScopeRWLock.h"
#include "Async/TaskGraphInterfaces.h"
#include "Misc/IQueuedWork.h"
#include "Async/Fundamental/Scheduler.h"
#include "Experimental/Containers/FAAArrayQueue.h"

#include <atomic>

/** ThreadPool wrapper implementation allowing to schedule
  * up to MaxConcurrency tasks at a time making sub-partitioning
  * another thread-pool a breeze and allowing more fine-grained control
  * over scheduling by effectively giving another set of priorities.
  */
class CORE_API FQueuedThreadPoolWrapper : public FQueuedThreadPool
{
public:
	/**
	 * InWrappedQueuedThreadPool  Underlying thread pool to schedule task to.
	 * InMaxConcurrency           Maximum number of concurrent tasks allowed, -1 will limit concurrency to number of threads available in the underlying thread pool.
	 * InPriorityMapper           Thread-safe function used to map any priority from this Queue to the priority that should be used when scheduling the task on the underlying thread pool.
	 */
	FQueuedThreadPoolWrapper(FQueuedThreadPool* InWrappedQueuedThreadPool, int32 InMaxConcurrency = -1, TFunction<EQueuedWorkPriority(EQueuedWorkPriority)> InPriorityMapper = [](EQueuedWorkPriority InPriority) { return InPriority; });
	~FQueuedThreadPoolWrapper();

	/**
	 *  Queued task are not scheduled against the wrapped thread-pool until resumed
	 */
	void Pause();

	/**
	 *  Resume a specified amount of queued work, or -1 to unpause.
	 */
	void Resume(int32 InNumQueuedWork = -1);

	/**
	 *  Dynamically adjust the maximum number of concurrent tasks, -1 for unlimited.
	 */
	void SetMaxConcurrency(int32 MaxConcurrency = -1);

	void AddQueuedWork(IQueuedWork* InQueuedWork, EQueuedWorkPriority InPriority = EQueuedWorkPriority::Normal) override;
	bool RetractQueuedWork(IQueuedWork* InQueuedWork) override;
	int32 GetNumThreads() const override;

protected:
	FRWLock Lock;
	FThreadPoolPriorityQueue QueuedWork;

	// Can be overriden to dynamically control the maximum concurrency
	virtual int32 GetMaxConcurrency() const { return MaxConcurrency.Load(EMemoryOrder::Relaxed); }
private:
	struct FScheduledWork;

	bool Create(uint32 InNumQueuedThreads, uint32 StackSize, EThreadPriority ThreadPriority, const TCHAR* Name) override;
	void Destroy() override;
	void Schedule(FScheduledWork* Work = nullptr);
	void ReleaseWorkNoLock(FScheduledWork* Work);
	
	TFunction<EQueuedWorkPriority(EQueuedWorkPriority)> PriorityMapper;

	FQueuedThreadPool* WrappedQueuedThreadPool;
	TArray<FScheduledWork*> WorkPool;
	TMap<IQueuedWork*, FScheduledWork*> ScheduledWork;
	TAtomic<int32> MaxConcurrency;
	int32 MaxTaskToSchedule;
	TAtomic<int32> CurrentConcurrency;
	EQueuedWorkPriority WrappedQueuePriority;
};

/** ThreadPool wrapper implementation allowing to schedule
  * up to MaxConcurrency tasks at a time making sub-partitioning
  * another thread-pool a breeze and allowing more fine-grained control
  * over scheduling by giving full control of task reordering.
  */
class CORE_API FQueuedThreadPoolDynamicWrapper : public FQueuedThreadPoolWrapper
{
public:
	/**
	 * InWrappedQueuedThreadPool  Underlying thread pool to schedule task to.
	 * InMaxConcurrency           Maximum number of concurrent tasks allowed, -1 will limit concurrency to number of threads available in the underlying thread pool.
	 * InPriorityMapper           Thread-safe function used to map any priority from this Queue to the priority that should be used when scheduling the task on the underlying thread pool.
	 */
	FQueuedThreadPoolDynamicWrapper(FQueuedThreadPool* InWrappedQueuedThreadPool, int32 InMaxConcurrency = -1, TFunction<EQueuedWorkPriority(EQueuedWorkPriority)> InPriorityMapper = [](EQueuedWorkPriority InPriority) { return InPriority; })
		: FQueuedThreadPoolWrapper(InWrappedQueuedThreadPool, InMaxConcurrency, InPriorityMapper)
	{
	}

	void AddQueuedWork(IQueuedWork* InQueuedWork, EQueuedWorkPriority InPriority = EQueuedWorkPriority::Normal) override
	{
		// Override priority to make sure all elements are in the same buckets and can then be sorted all together.
		FQueuedThreadPoolWrapper::AddQueuedWork(InQueuedWork, EQueuedWorkPriority::Normal);
	}

	/**
	 * Apply sort predicate to reorder the queued tasks
	 */
	void Sort(TFunctionRef<bool(const IQueuedWork* Lhs, const IQueuedWork* Rhs)> Predicate)
	{
		FRWScopeLock ScopeLock(Lock, SLT_Write);
		QueuedWork.Sort(EQueuedWorkPriority::Normal, Predicate);
	}
};

/** ThreadPool wrapper implementation allowing to schedule thread-pool tasks on the task graph.
  */
class CORE_API FQueuedThreadPoolTaskGraphWrapper : public FQueuedThreadPool
{
public:
	/**
	 * InPriorityMapper           Thread-safe function used to map any priority from this Queue to the priority that should be used when scheduling the task on the task graph.
	 */
	FQueuedThreadPoolTaskGraphWrapper(TFunction<ENamedThreads::Type (EQueuedWorkPriority)> InPriorityMapper = nullptr)
		: TaskCount(0)
		, bIsExiting(0)
	{
		if (InPriorityMapper)
		{
			PriorityMapper = InPriorityMapper;
		}
		else
		{
			PriorityMapper = [this](EQueuedWorkPriority InPriority) { return GetDefaultPriorityMapping(InPriority); };
		}
	}

	/**
	 * InDesiredThread           The task-graph desired thread and priority.
	 */
	FQueuedThreadPoolTaskGraphWrapper(ENamedThreads::Type InDesiredThread)
		: TaskCount(0)
		, bIsExiting(0)
	{
		PriorityMapper = [InDesiredThread](EQueuedWorkPriority InPriority) { return InDesiredThread; };
	}

	~FQueuedThreadPoolTaskGraphWrapper()
	{
		Destroy();
	}
private:
	void AddQueuedWork(IQueuedWork* InQueuedWork, EQueuedWorkPriority InPriority = EQueuedWorkPriority::Normal) override
	{
		check(bIsExiting == false);
		TaskCount++;
		FFunctionGraphTask::CreateAndDispatchWhenReady(
			[this, InQueuedWork](ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
			{
				FMemMark Mark(FMemStack::Get());
				InQueuedWork->DoThreadedWork();
				OnTaskCompleted(InQueuedWork);
			},
			QUICK_USE_CYCLE_STAT(FQueuedThreadPoolTaskGraphWrapper, STATGROUP_ThreadPoolAsyncTasks),
			nullptr,
			PriorityMapper(InPriority)
		);
	}

	bool RetractQueuedWork(IQueuedWork* InQueuedWork) override
	{
		// The task graph doesn't support retraction for now
		return false;
	}

	void OnTaskCompleted(IQueuedWork* InQueuedWork)
	{
		--TaskCount;
	}

	int32 GetNumThreads() const override
	{
		return FTaskGraphInterface::Get().GetNumWorkerThreads();
	}

	ENamedThreads::Type GetDefaultPriorityMapping(EQueuedWorkPriority InQueuedWorkPriority)
	{
		ENamedThreads::Type DesiredThread = ENamedThreads::AnyNormalThreadNormalTask;
		if (InQueuedWorkPriority > EQueuedWorkPriority::Normal)
		{
			DesiredThread = ENamedThreads::AnyBackgroundThreadNormalTask;
		}
		else if (InQueuedWorkPriority < EQueuedWorkPriority::Normal)
		{
			DesiredThread = ENamedThreads::AnyHiPriThreadNormalTask;
		}
		return DesiredThread;
	}
protected:
	bool Create(uint32 InNumQueuedThreads, uint32 StackSize, EThreadPriority ThreadPriority, const TCHAR* Name) override
	{
		return true;
	}

	void Destroy() override
	{
		bIsExiting = true;
		while (TaskCount != 0)
		{
			FPlatformProcess::Sleep(0.01f);
		}
	}
private:
	TFunction<ENamedThreads::Type (EQueuedWorkPriority)> PriorityMapper;
	TAtomic<uint32> TaskCount;
	TAtomic<bool> bIsExiting;
};

/** ThreadPool wrapper implementation allowing to schedule thread-pool tasks on the the low level backend which is also used by the taskgraph.
*/
class CORE_API FQueuedLowLevelThreadPool : public FQueuedThreadPool
{
	/* Internal data of the scheduler used for cancellation */
	struct FQueuedWorkInternalData : IQueuedWorkInternalData
	{
		LowLevelTasks::FTask Task;

		virtual bool Retract()
		{
			return Task.TryCancel();
		}
	};
public:
	/**
    * InMaxConcurrency           Maximum number of concurrent tasks allowed, -1 will limit concurrency to number of threads available in the underlying thread pool.
	* InPriorityMapper           Thread-safe function used to map any priority from this Queue to the priority that should be used when scheduling the task on the underlying thread pool.
	**/
	FQueuedLowLevelThreadPool(uint32 InMaxConcurrency = ~0, TFunction<EQueuedWorkPriority(EQueuedWorkPriority)> InPriorityMapper = [](EQueuedWorkPriority InPriority) { return InPriority; }) 
		: PriorityMapper(InPriorityMapper)
	{
		MaxConcurrency = InMaxConcurrency;
	}

	~FQueuedLowLevelThreadPool()
	{
		Destroy();
	}

	/**
	*  Queued task are not scheduled against the wrapped thread-pool until resumed
	*/
	void Pause()
	{
		bIsPaused = true;
	}

	/**
	*  Dynamically adjust the maximum number of concurrent tasks, -1 for unlimited.
	*/
	void SetMaxConcurrency(uint32 InMaxConcurrency = ~0)
	{
		MaxConcurrency = InMaxConcurrency;
	}

	/**
	*  Resume a specified amount of queued work, or -1 to unpause.
	*/
	void Resume(int32 InNumQueuedWork = -1)
	{
		for (uint32 i = 0; i < uint32(InNumQueuedWork); i++)
		{
			FQueuedWorkInternalData* QueuedWork = Dequeue();
			if (!QueuedWork)
			{
				break;
			}
			TaskCount++;
			LowLevelTasks::LaunchTask<LowLevelTasks::EQueuePreference::GlobalQueuePreference>(QueuedWork->Task);
		}

		if (InNumQueuedWork == -1)
		{
			bIsPaused = false;
		}
	}

private:
	void AddQueuedWork(IQueuedWork* InQueuedWork, EQueuedWorkPriority InPriority = EQueuedWorkPriority::Normal) override
	{
		check(bIsExiting == false);

		FQueuedWorkInternalData* QueuedWorkInternalData = new FQueuedWorkInternalData();
		InQueuedWork->InternalData = QueuedWorkInternalData;
		
		checkSlow(int32(InPriority) < int32(EQueuedWorkPriority::Count));
		const LowLevelTasks::ETaskPriority Mapping[int32(EQueuedWorkPriority::Count)] = { LowLevelTasks::ETaskPriority::Normal, LowLevelTasks::ETaskPriority::Normal, LowLevelTasks::ETaskPriority::Low, LowLevelTasks::ETaskPriority::Low, LowLevelTasks::ETaskPriority::Low };

		LowLevelTasks::ETaskPriority Priority = Mapping[int32(PriorityMapper(InPriority))];
		QueuedWorkInternalData->Task.Init(TEXT("FQueuedLowLevelThreadPoolTask"), Priority, [InQueuedWork]
		{
			FMemMark Mark(FMemStack::Get());
			InQueuedWork->DoThreadedWork();
		},
		[this, InternalData = InQueuedWork->InternalData]()
		{
			--TaskCount;
			while (TaskCount < MaxConcurrency)
			{
				FQueuedWorkInternalData* QueuedWork = Dequeue();
				if (QueuedWork)
				{
					TaskCount++;
					LowLevelTasks::LaunchTask<LowLevelTasks::EQueuePreference::GlobalQueuePreference>(QueuedWork->Task);
				}
				else
				{
					break;
				}
			}
		});

		if(!bIsPaused && TaskCount <= MaxConcurrency)
		{
			TaskCount++;
			LowLevelTasks::LaunchTask<LowLevelTasks::EQueuePreference::GlobalQueuePreference>(QueuedWorkInternalData->Task);
		}
		else
		{
			Enqueue(Priority, QueuedWorkInternalData);
		}
	}

	bool RetractQueuedWork(IQueuedWork* InQueuedWork) override
	{
		if(InQueuedWork->InternalData.IsValid())
		{
			bool bCancelled = InQueuedWork->InternalData->Retract();
			InQueuedWork->InternalData = nullptr;
			return bCancelled;
		}
		return false;
	}

	int32 GetNumThreads() const override
	{
		return LowLevelTasks::FScheduler::Get().GetNumWorkers();
	}

protected:
	bool Create(uint32 InNumQueuedThreads, uint32 InStackSize, EThreadPriority InThreadPriority, const TCHAR* InName) override
	{
		MaxConcurrency = InNumQueuedThreads;
		return true;
	}

	void Destroy() override
	{
		bIsExiting = true;

		while (true)
		{
			FQueuedWorkInternalData* QueuedWork = Dequeue();
			if (!QueuedWork)
			{
				break;
			}

			verify(QueuedWork->Retract());
			TaskCount++;
			LowLevelTasks::LaunchTask<LowLevelTasks::EQueuePreference::GlobalQueuePreference>(QueuedWork->Task);
		}

		while (TaskCount != 0)
		{
			FPlatformProcess::Sleep(0.01f);
		}
	}

private:
	FAAArrayQueue<FQueuedWorkInternalData> PendingWork[int32(LowLevelTasks::ETaskPriority::Count)];

	inline FQueuedWorkInternalData* Dequeue()
	{
		for (int32 i = 0; i < int32(LowLevelTasks::ETaskPriority::Count); i++)
		{
			FQueuedWorkInternalData* QueuedWork = PendingWork[i].dequeue();
			if (QueuedWork)
			{
				return QueuedWork;
			}
		}
		return nullptr;
	}

	inline void Enqueue(LowLevelTasks::ETaskPriority Priority, FQueuedWorkInternalData* Item)
	{
		PendingWork[int32(Priority)].enqueue(Item);
	}

	TFunction<EQueuedWorkPriority(EQueuedWorkPriority)> PriorityMapper;
	std::atomic_uint MaxConcurrency{~0u};
	std::atomic_uint TaskCount{0};
	std::atomic_bool bIsExiting{false};
	std::atomic_bool bIsPaused{false};
};