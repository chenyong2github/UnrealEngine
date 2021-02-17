// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Async/Fundamental/Task.h"
#include "Async/Fundamental/Scheduler.h"
#include "Experimental/Containers/FAAArrayQueue.h"
#include "Templates/RefCounting.h"
#include <atomic>

template<typename LAMBDA>
class TYCombinator
{
	LAMBDA Lambda;

public:
	inline TYCombinator(LAMBDA&& InLambda) : Lambda(Forward<LAMBDA>(InLambda)) {}

	template <class... ARGS>
	inline decltype(auto) operator()(ARGS&&... Args) const 
	{
		return Lambda(*this, Forward<ARGS>(Args)...);
	}

	template <class... ARGS>
	inline decltype(auto) operator()(ARGS&&... Args) 
	{
		return Lambda(*this, Forward<ARGS>(Args)...);
	}
};

template<typename LAMBDA>
inline TYCombinator<std::decay_t<LAMBDA>> MakeYCombinator(LAMBDA&& Lambda)
{
	return TYCombinator<std::decay_t<LAMBDA>>(Forward<LAMBDA>(Lambda));
}

template<typename TaskType>
class TLocalWorkQueue
{
	UE_NONCOPYABLE(TLocalWorkQueue)

	struct FInternalData : public FThreadSafeRefCountedObject
	{
		FAAArrayQueue<TaskType> TaskQueue;	
		std::atomic_int ActiveWorkers {0};
		std::atomic_bool CheckDone {false};
	};

	TRefCountPtr<FInternalData> InternalData;
	LowLevelTasks::ETaskPriority Priority;
	TFunctionRef<void(TaskType*)>* DoWork = nullptr;

public:
	TLocalWorkQueue(TaskType* InitialWork, LowLevelTasks::ETaskPriority InPriority = LowLevelTasks::ETaskPriority::Count) : Priority(InPriority)
	{
		if (Priority == LowLevelTasks::ETaskPriority::Count)
		{
			const LowLevelTasks::FTask* ActiveTask = LowLevelTasks::FScheduler::Get().GetActiveTask();
			if (ActiveTask)
			{
				Priority = ActiveTask->GetPriority();
				if (Priority == LowLevelTasks::ETaskPriority::BackgroundLow)
				{
					Priority = LowLevelTasks::ETaskPriority::BackgroundNormal;
				}
				else if (Priority == LowLevelTasks::ETaskPriority::BackgroundNormal)
				{
					Priority = LowLevelTasks::ETaskPriority::BackgroundHigh;
				}
			}
			else
			{
				Priority = LowLevelTasks::ETaskPriority::Default;
			}
		}

		InternalData = new FInternalData();

		AddTask(InitialWork);
	}

public:
	void AddTask(TaskType* NewWork)
	{
		check(!InternalData->CheckDone.load(std::memory_order_relaxed));
		InternalData->TaskQueue.enqueue(NewWork);
	}

	void AddWorkers(uint16 NumWorkers)
	{
		check(!InternalData->CheckDone.load(std::memory_order_relaxed));
		check(DoWork != nullptr);

		for(uint16 i = 0; i < NumWorkers; i++)
		{
			using FTaskHandle = TSharedPtr<LowLevelTasks::FTask, ESPMode::ThreadSafe>;
			FTaskHandle TaskHandle = MakeShared<LowLevelTasks::FTask, ESPMode::ThreadSafe>();

			TFunctionRef<void(TaskType*)>* LocalDoWork = DoWork;
			TaskHandle->Init(TEXT("TLocalWorkQueue::AddWorkers"), Priority, [LocalDoWork, InternalData = InternalData, TaskHandle]()
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(TLocalWorkQueue::AddWorkers);
				InternalData->ActiveWorkers.fetch_add(1, std::memory_order_acquire);
				while(true)
				{
					TaskType* Work = InternalData->TaskQueue.dequeue();
					if (Work == nullptr)
					{
						break;
					}
					check(!InternalData->CheckDone.load(std::memory_order_relaxed));
					(*LocalDoWork)(Work);				
				}		
				InternalData->ActiveWorkers.fetch_sub(1, std::memory_order_release);
			});
			verify(TryLaunch(*TaskHandle, LowLevelTasks::EQueuePreference::GlobalQueuePreference));
		}
	}

	void Run(TFunctionRef<void(TaskType*)> InDoWork)
	{
		DoWork = &InDoWork;
		LowLevelTasks::BusyWaitUntil([&InDoWork, InternalData = InternalData]()
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(TLocalWorkQueue::Run);
			bool Completed = false;
			while(true)
			{
				Completed = InternalData->ActiveWorkers.load(std::memory_order_acquire) == 0;
				TaskType* Work = InternalData->TaskQueue.dequeue();
				if (Work == nullptr)
				{
					Completed = Completed && InternalData->ActiveWorkers.load(std::memory_order_acquire) == 0;
					break;
				}			
				InDoWork(Work);	
			}
			return Completed; 
		});

		InternalData->CheckDone.store(true);
		check(InternalData->TaskQueue.dequeue() == nullptr);
	}
};