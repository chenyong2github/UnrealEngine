// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/GenericPlatformAffinity.h"
#include "QueuedThreadPool.h"
#include "ScopeRWLock.h"

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
	FQueuedThreadPoolWrapper(FQueuedThreadPool* InWrappedQueuedThreadPool, int32 InMaxConcurrency = -1, TFunction<EQueuedWorkPriority (EQueuedWorkPriority)> InPriorityMapper = [](EQueuedWorkPriority InPriority) { return InPriority; });
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

private:
	struct FScheduledWork;

	bool Create(uint32 InNumQueuedThreads, uint32 StackSize, EThreadPriority ThreadPriority, const TCHAR* Name) override;
	void Destroy() override;
	void Schedule(FScheduledWork* Work = nullptr);

	FRWLock Lock;
	TFunction<EQueuedWorkPriority(EQueuedWorkPriority)> PriorityMapper;
	FThreadPoolPriorityQueue QueuedWork;
	FQueuedThreadPool* WrappedQueuedThreadPool;
	TArray<FScheduledWork*> WorkPool;
	TAtomic<int32> MaxConcurrency;
	int32 MaxTaskToSchedule;
	TAtomic<int32> CurrentConcurrency;
	EQueuedWorkPriority WrappedQueuePriority;
};
