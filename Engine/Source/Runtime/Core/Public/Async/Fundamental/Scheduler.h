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

		using FQueueRegistry	= TLocalQueueRegistry<>;
		using FLocalQueueType	= FQueueRegistry::TLocalQueue;

		static thread_local FLocalQueueType* LocalQueue;
		static thread_local FTask* ActiveTask;
		static thread_local FScheduler* ActiveScheduler;

		enum class EWorkerType
		{
			None,
			Background,
			Foreground,
		};

		static thread_local EWorkerType WorkerType;
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
		CORE_API void StartWorkers(uint32 NumForegroundWorkers = 0, uint32 NumBackgroundWorkers = 0, EThreadPriority WorkerPriority = EThreadPriority::TPri_Normal,  EThreadPriority BackgroundPriority = EThreadPriority::TPri_BelowNormal, bool bIsForkable = false);
		CORE_API void StopWorkers();

		//try to launch the task, the return value will specify if the task was in the ready state and has been launced
		inline bool TryLaunch(FTask& Task, EQueuePreference QueuePreference = EQueuePreference::DefaultPreference, bool bWakeUpWorker = true);
		
		//try to cancel the task and launching it if the task was in ready state
		//you still need to wait for task completion before recycling the handle
		//you can alternatively use FTask::TryCancel if you want to launch the task manually
		inline bool TryCancelAndLaunchContinuation(FTask& Task, EQueuePreference QueuePreference = EQueuePreference::DefaultPreference);		

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
		CORE_API static const FTask* GetActiveTask();

		CORE_API bool IsWorkerThread() const;

	private: //Private Interface of the Scheduler	
		enum class ESleepState
		{
			Running,
			Drowsing,
			Sleeping,
		};

		struct FSleepEvent
		{
			FEventRef SleepEvent;
			std::atomic<ESleepState> SleepState { ESleepState::Running };
		};
		using FEventQueueType = FAAArrayQueue<FSleepEvent>;

	public:
		FScheduler() = default;
		~FScheduler();

	private: 
		TUniquePtr<FThread> CreateWorker(FLocalQueueType* ExternalWorkerLocalQueue = nullptr, EThreadPriority Priority = EThreadPriority::TPri_Normal, bool bPermitBackgroundWork = false, bool bIsForkable = false);
		void WorkerMain(struct FSleepEvent* WorkerEvent, FLocalQueueType* ExternalWorkerLocalQueue, uint32 WaitCycles, bool bPermitBackgroundWork);
		CORE_API void LaunchInternal(FTask& Task, EQueuePreference QueuePreference, bool bWakeUpWorker);
		CORE_API void BusyWaitInternal(const FConditional& Conditional);
		FORCENOINLINE void TrySleeping(FSleepEvent* WorkerEvent, FQueueRegistry::FOutOfWork& OutOfWork, bool& Drowsing, bool bBackgroundWorker);
		inline bool WakeUpWorker(bool bBackgroundWorker);

		template<FTask* (FLocalQueueType::*DequeueFunction)(bool), bool bIsBusyWaiting>
		bool TryExecuteTaskFrom(FLocalQueueType* Queue, FQueueRegistry::FOutOfWork& OutOfWork, bool bPermitBackgroundWork);

	private:
		FEventQueueType 			SleepEventQueue[2];
		FQueueRegistry 				QueueRegistry;
		FCriticalSection 			WorkerThreadsCS;
		TArray<TUniquePtr<FThread>> WorkerThreads;
		TArray<FLocalQueueType>		WorkerLocalQueues;
		std::atomic_uint			ActiveWorkers { 0 };
		std::atomic_uint			NextWorkerId { 0 };
	};

	FORCEINLINE_DEBUGGABLE bool TryLaunch(FTask& Task, EQueuePreference QueuePreference = EQueuePreference::DefaultPreference, bool bWakeUpWorker = true)
	{
		return FScheduler::Get().TryLaunch(Task, QueuePreference, bWakeUpWorker);
	}

	FORCEINLINE_DEBUGGABLE bool TryCancelAndLaunchContinuation(FTask& Task, EQueuePreference QueuePreference = EQueuePreference::DefaultPreference)
	{
		return FScheduler::Get().TryCancelAndLaunchContinuation(Task, QueuePreference);
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
	inline bool FScheduler::TryLaunch(FTask& Task, EQueuePreference QueuePreference, bool bWakeUpWorker)
	{
		if(Task.TryPrepareLaunch())
		{
			FScheduler::Get().LaunchInternal(Task, QueuePreference, bWakeUpWorker);
			return true;
		}
		return false;
	}

	inline bool FScheduler::TryCancelAndLaunchContinuation(FTask& Task, EQueuePreference QueuePreference)
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

	inline void FScheduler::TrySleeping(FSleepEvent* WorkerEvent, FQueueRegistry::FOutOfWork& OutOfWork, bool& Drowsing, bool bBackgroundWorker)
	{
		ESleepState DrowsingState1 = ESleepState::Drowsing;
		ESleepState DrowsingState2 = ESleepState::Drowsing;
		ESleepState RunningState  = ESleepState::Running;
		if(!Drowsing && WorkerEvent->SleepState.compare_exchange_strong(DrowsingState1, ESleepState::Drowsing, std::memory_order_relaxed)) //continue drowsing
		{
			verifySlow(OutOfWork.Stop());
			Drowsing = true; // Alternative State one: ((Running -> Drowsing) -> Drowsing)
		}
		else if(WorkerEvent->SleepState.compare_exchange_strong(DrowsingState2, ESleepState::Sleeping, std::memory_order_relaxed))
		{
			verifySlow(!OutOfWork.Stop());
			Drowsing = false;
			WorkerEvent->SleepEvent->Wait(); // State two: ((Running -> Drowsing) -> Sleeping)
		}
		else if(WorkerEvent->SleepState.compare_exchange_strong(RunningState, ESleepState::Drowsing, std::memory_order_relaxed))
		{
			OutOfWork.Stop();
			Drowsing = true;
			SleepEventQueue[bBackgroundWorker].enqueue(WorkerEvent); // State one: (Running -> Drowsing)
		}
		else
		{
			checkf(false, TEXT("Worker was supposed to be running or drowsing: %d"), WorkerEvent->SleepState.load(std::memory_order_relaxed));
		}
	}

	inline bool FScheduler::WakeUpWorker(bool bBackgroundWorker)
	{
		FSleepEvent* WorkerEvent = SleepEventQueue[bBackgroundWorker].dequeue();
		if (WorkerEvent)
		{
			// Solving State : (Running -> Drowsing) -> Running  OR ((Running -> Drowsing) -> Drowsing) -> Running OR (((Running -> Drowsing) -> Sleeping) -> Running)
			ESleepState SleepState = WorkerEvent->SleepState.exchange(ESleepState::Running, std::memory_order_relaxed);
			if (SleepState == ESleepState::Sleeping)
			{
				WorkerEvent->SleepEvent->Trigger(); // Solving State two: (((Running -> Drowsing) -> Sleeping) -> Running)
				return true;
			}
			checkf(SleepState == ESleepState::Drowsing, TEXT("Worker was not drowsing: %d"), WorkerEvent->SleepState.load(std::memory_order_relaxed));
			return true;
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
}

