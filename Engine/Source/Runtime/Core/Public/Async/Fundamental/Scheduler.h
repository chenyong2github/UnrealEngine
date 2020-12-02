// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 
#include "Async/Fundamental/Task.h"
#include "HAL/Thread.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "LocalQueue.h"
#include "Containers/Array.h"
#include "Templates/UniquePtr.h"
#include "Templates/Function.h"

namespace LowLevelTasks
{
	enum class EQueuePreference
	{
		GlobalQueuePreference,
		LocalQueuePreference,
		DefaultPreference = LocalQueuePreference,
	};

	class FScheduler
	{
		UE_NONCOPYABLE(FScheduler);
		static constexpr uint32 WorkerSpinCycles = 53;

		using FQueueRegistry	= TLocalQueueRegistry<FTask, uint32(ETaskPriority::Count)>;
		using FLocalQueueType	= FQueueRegistry::TLocalQueue;

		static thread_local FLocalQueueType* LocalQueue;
		static thread_local FTask* ActiveTask;
		static CORE_API FScheduler Singleton;

	private: 
		//the FLocalQueueInstaller installs a LocalQueue into the current thread
		struct FLocalQueueInstaller
		{
			bool RegisteredLocalQueue = false;
			CORE_API FLocalQueueInstaller(FScheduler& Scheduler);
			CORE_API ~FLocalQueueInstaller();
		};

	public: // Public Interface of the Scheduler
		FORCEINLINE_DEBUGGABLE static FScheduler& Get();

		//start number of workers where 0 is the system default
		CORE_API void StartWorkers(uint32 NumWorkers = 0, EThreadPriority Priority = EThreadPriority::TPri_Normal, bool bIsForkable = false);
		CORE_API void StopWorkers();

		//Launching the Task and queuing it.
		CORE_API void Launch(FTask& Task, EQueuePreference QueuePreference);

		//tries to do some work until the Task is completed
		inline void BusyWait(const FTask& Task);

		//tries to do some work until the Conditional return true
		template<typename Conditional>
		inline void BusyWaitUntil(const Conditional& Cond);

		//tries to do some work until all the Tasks are completed
		//the template parameter can be any Type that has a const conversion operator to FTask
		template<typename TaskType>
		inline void BusyWait(const TArrayView<const TaskType>& Tasks);

		//number of instantiated workers
		inline uint32 GetNumWorkers() const;

		//get the active task if any
		CORE_API const FTask* GetActiveTask() const;

	private: //Private Interface of the Scheduler	
		enum class ESleepState
		{
			Running,
			Drowsing,
			Sleeping,
		};

		struct FSleepEvent
		{
			FEvent* SleepEvent;
			std::atomic<ESleepState> SleepState { ESleepState::Running };
			inline FSleepEvent();
			inline ~FSleepEvent();
		};
		using FEventQueueType = FAAArrayQueue<FSleepEvent>;

	private:
		FScheduler() = default;
		~FScheduler();

	private: 
		TUniquePtr<FThread> CreateWorker(FLocalQueueType* ExternalWorkerLocalQueue = nullptr, EThreadPriority Priority = EThreadPriority::TPri_Normal, bool bIsForkable = false);
		void WorkerMain(struct FSleepEvent* WorkerEvent, FLocalQueueType* ExternalWorkerLocalQueue, uint32 WaitCycles);	
		CORE_API bool BusyWaitInternal(const FTask& Task);
		FORCENOINLINE void TrySleeping(FSleepEvent* WorkerEvent, uint32& WaitCount);
		inline bool WakeUpWorker();
		template<FTask* (FLocalQueueType::*DequeueFunction)()>
		FORCEINLINE_DEBUGGABLE static bool TryExecuteTaskFrom(FLocalQueueType* Queue);

	private:
		FEventQueueType SleepEventQueue;
		FQueueRegistry QueueRegistry;
		FCriticalSection WorkerThreadsCS;
		TArray<TUniquePtr<FThread>> WorkerThreads;
		TArray<FLocalQueueType> WorkerLocalQueues;
		std::atomic_uint ActiveWorkers { 0 };
		std::atomic_uint NextWorkerId { 0 };
	};

	template<EQueuePreference QueuePreference = EQueuePreference::DefaultPreference>
	FORCEINLINE_DEBUGGABLE void LaunchTask(FTask& Task)
	{
		FScheduler::Get().Launch(Task, QueuePreference);
	}

	FORCEINLINE_DEBUGGABLE void BusyWaitForTask(const FTask& Task)
	{
		FScheduler::Get().BusyWait(Task);
	}

	template<typename Conditional>
	FORCEINLINE_DEBUGGABLE void BusyWaitUntil(const Conditional& Cond)
	{
		FScheduler::Get().BusyWaitUntil<Conditional>(Cond);
	}

	template<typename TaskType>
	FORCEINLINE_DEBUGGABLE void BusyWaitForTasks(const TArrayView<const TaskType>& Tasks)
	{
		FScheduler::Get().BusyWait<TaskType>(Tasks);
	}

   /******************
	* IMPLEMENTATION *
	******************/

	inline uint32 FScheduler::GetNumWorkers() const
	{
		return ActiveWorkers.load(std::memory_order_relaxed);
	}

	inline void FScheduler::BusyWait(const FTask& Task)
	{
		if(!Task.IsCompleted())
		{
			FLocalQueueInstaller Installer(*this);
			FScheduler::BusyWaitInternal(Task);
		}
	}

	template<typename Conditional>
	inline void FScheduler::BusyWaitUntil(const Conditional& Cond)
	{
		static_assert(TIsInvocable<Conditional>::Value, "Conditional is not invocable");
		static_assert(TIsSame<decltype(Cond()), bool>::Value, "Conditional must return a boolean");

		TOptional<FLocalQueueInstaller> Installer;
		uint32 WaitCount = 0;
		FTask Dummy;
		while(!Cond())
		{
			if (!Installer.IsSet())
			{
				Installer.Emplace(*this);
			}
			if(FScheduler::BusyWaitInternal(Dummy))
			{
				WaitCount = 0;
			}
			if (!Cond())
			{
				if (WaitCount < WorkerSpinCycles)
				{
					WaitCount++;
					FPlatformProcess::Yield();
				}
				else
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("Sleeping Conditional Busy Wait"));
					WaitCount = 0;
					FPlatformProcess::SleepNoStats(0);		
				}
			}
		}
	}

	template<typename TaskType>
	inline void FScheduler::BusyWait(const TArrayView<const TaskType>& Tasks)
	{
		TOptional<FLocalQueueInstaller> Installer;
		for(const FTask& Task : Tasks)
		{
			if(!Task.IsCompleted())
			{
				if (!Installer.IsSet())
				{
					Installer.Emplace(*this);
				}
				FScheduler::BusyWaitInternal(Task);
			}
		}
	}

	inline void FScheduler::TrySleeping(FSleepEvent* WorkerEvent, uint32& WaitCount)
	{
		ESleepState DrowsingState = ESleepState::Drowsing;
		ESleepState RunningState  = ESleepState::Running;
		if(WorkerEvent->SleepState.compare_exchange_strong(DrowsingState, ESleepState::Sleeping, std::memory_order_relaxed))
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("Sleeping Worker"));
			WaitCount = 0;
			WorkerEvent->SleepEvent->Wait(); // State two: ((Running -> Drowsing) -> Sleeping)
		}
		else if(WorkerEvent->SleepState.compare_exchange_strong(RunningState, ESleepState::Drowsing, std::memory_order_relaxed))
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("Drowsing Worker"));
			SleepEventQueue.enqueue(WorkerEvent); // State one: (Running -> Drowsing)
			FPlatformProcess::SleepNoStats(0);	
		}
		else
		{
			checkf(false, TEXT("Worker was supposed to be running or drowsing: %d"), WorkerEvent->SleepState.load(std::memory_order_relaxed));
		}
	}

	inline bool FScheduler::WakeUpWorker()
	{
		FSleepEvent* WorkerEvent = SleepEventQueue.dequeue();
		if (WorkerEvent)
		{
			ESleepState DrowsingState = ESleepState::Drowsing;
			ESleepState SleepingState = ESleepState::Sleeping;
			if (WorkerEvent->SleepState.compare_exchange_strong(DrowsingState, ESleepState::Running, std::memory_order_relaxed))
			{
				return true; // Solving State one: (Running -> Drowsing) -> Running
			}
			else if (WorkerEvent->SleepState.compare_exchange_strong(SleepingState, ESleepState::Running, std::memory_order_relaxed))
			{
				WorkerEvent->SleepEvent->Trigger();
				return true; // Solving State two: (((Running -> Drowsing) -> Sleeping) -> Running)
			}
			else
			{
				checkf(false, TEXT("Worker was not sleeping or drowsing: %d"), WorkerEvent->SleepState.load(std::memory_order_relaxed));
				return true;
			}		
		}
		return false;
	}

	inline FScheduler& FScheduler::Get()
	{
		return Singleton;
	}

	inline FScheduler::~FScheduler()
	{
		StopWorkers();
	}

	inline FScheduler::FSleepEvent::FSleepEvent() : SleepEvent(FPlatformProcess::GetSynchEventFromPool())
	{
	}

	inline FScheduler::FSleepEvent::~FSleepEvent()
	{
		FPlatformProcess::ReturnSynchEventToPool(SleepEvent);
	}
}

