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

		// using 16 bytes here because it fits the vtable and one additional pointer
		using FConditional = TTaskDelegate<16, bool>;

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

		//try to launch the task, the return value will specify if the task was in the ready state and has been launced
		inline bool TryLaunch(FTask& Task, EQueuePreference QueuePreference = EQueuePreference::DefaultPreference);
		
		//try to cancel the task and launching it if the task was in ready state
		//you still need to wait for task completion before recycling the handle
		//you can alternatively use FTask::TryCancel if you want to launch the task manually
		inline bool TryCancelAndLaunch(FTask& Task, EQueuePreference QueuePreference = EQueuePreference::DefaultPreference);		

		//tries to do some work until the Task is completed
		template<typename TaskType>
		inline void BusyWait(const TaskType& Task);

		//tries to do some work until the Conditional return true
		template<typename Conditional>
		inline void BusyWaitUntil(Conditional&& Cond);

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
		CORE_API void LaunchInternal(FTask& Task, EQueuePreference QueuePreference);
		CORE_API void BusyWaitInternal(const FConditional& Conditional);
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

	FORCEINLINE_DEBUGGABLE bool TryLaunch(FTask& Task, EQueuePreference QueuePreference = EQueuePreference::DefaultPreference)
	{
		return FScheduler::Get().TryLaunch(Task, QueuePreference);
	}

	FORCEINLINE_DEBUGGABLE bool TryCancelAndLaunch(FTask& Task, EQueuePreference QueuePreference = EQueuePreference::DefaultPreference)
	{
		return FScheduler::Get().TryCancelAndLaunch(Task, QueuePreference);
	}

	FORCEINLINE_DEBUGGABLE void BusyWaitForTask(const FTask& Task)
	{
		FScheduler::Get().BusyWait(Task);
	}

	template<typename Conditional>
	FORCEINLINE_DEBUGGABLE void BusyWaitUntil(Conditional&& Cond)
	{
		FScheduler::Get().BusyWaitUntil<Conditional>(Forward<Conditional>(Cond));
	}

	template<typename TaskType>
	FORCEINLINE_DEBUGGABLE void BusyWaitForTasks(const TArrayView<const TaskType>& Tasks)
	{
		FScheduler::Get().BusyWait<TaskType>(Tasks);
	}

   /******************
	* IMPLEMENTATION *
	******************/
	inline bool FScheduler::TryLaunch(FTask& Task, EQueuePreference QueuePreference)
	{
		if(Task.TryPrepareLaunch())
		{
			FScheduler::Get().LaunchInternal(Task, QueuePreference);
			return true;
		}
		return false;
	}

	inline bool FScheduler::TryCancelAndLaunch(FTask& Task, EQueuePreference QueuePreference)
	{
		bool WasCanceled = Task.TryCancel();
		if(WasCanceled && Task.TryPrepareLaunch())
		{
			Task.ExecuteTask();
			return true;
		}
		return WasCanceled;
	}

	inline uint32 FScheduler::GetNumWorkers() const
	{
		return ActiveWorkers.load(std::memory_order_relaxed);
	}

	template<typename TaskType>
	inline void FScheduler::BusyWait(const TaskType& Task)
	{
		if(!Task.IsCompleted())
		{
			FLocalQueueInstaller Installer(*this);
			FScheduler::BusyWaitInternal([&Task](){ return Task.IsCompleted(); });
		}
	}

	template<typename Conditional>
	inline void FScheduler::BusyWaitUntil(Conditional&& Cond)
	{
		static_assert(TIsInvocable<Conditional>::Value, "Conditional is not invocable");
		static_assert(TIsSame<decltype(Cond()), bool>::Value, "Conditional must return a boolean");
		
		if(!Cond())
		{
			FLocalQueueInstaller Installer(*this);
			FScheduler::BusyWaitInternal(Forward<Conditional>(Cond));
		}
	}

	template<typename TaskType>
	inline void FScheduler::BusyWait(const TArrayView<const TaskType>& Tasks)
	{
		auto AllTasksCompleted = [Index(0), &Tasks]() mutable
		{
			while (Index < Tasks.Num())
			{
				if (!Tasks[Index].IsCompleted())
				{
					return false;
				}
				Index++;
			}
			return true;
		};

		if (!AllTasksCompleted())
		{
			FLocalQueueInstaller Installer(*this);
			FScheduler::BusyWaitInternal([&AllTasksCompleted](){ return AllTasksCompleted(); });
		}
	}

	inline void FScheduler::TrySleeping(FSleepEvent* WorkerEvent, uint32& WaitCount)
	{
		ESleepState DrowsingState = ESleepState::Drowsing;
		ESleepState RunningState  = ESleepState::Running;
		if(WorkerEvent->SleepState.compare_exchange_strong(DrowsingState, ESleepState::Sleeping, std::memory_order_relaxed))
		{
			TRACE_CPUPROFILER_EVENT_SCOPE("Sleeping Worker");
			WaitCount = 0;
			WorkerEvent->SleepEvent->Wait(); // State two: ((Running -> Drowsing) -> Sleeping)
		}
		else if(WorkerEvent->SleepState.compare_exchange_strong(RunningState, ESleepState::Drowsing, std::memory_order_relaxed))
		{
			TRACE_CPUPROFILER_EVENT_SCOPE("Drowsing Worker");
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

