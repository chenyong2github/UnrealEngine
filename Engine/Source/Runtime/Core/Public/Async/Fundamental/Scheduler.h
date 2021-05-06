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

		// number of busy-waiting calls in the call-stack
		static thread_local uint32 BusyWaitingDepth;

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

		// returns true if the current thread execution is in the context of busy-waiting
		inline static bool IsBusyWaiting();

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

		//the struct is naturally 8 bytes aligned, the extra alignment just 
		//re-enforces this assumption and will error if it changes in the future
		struct alignas(8) FSleepEvent
		{
			FEventRef SleepEvent;
			std::atomic<ESleepState> SleepState { ESleepState::Running };
			FSleepEvent* Next = nullptr;
		};

		//implementation of a treiber stack
		//(https://en.wikipedia.org/wiki/Treiber_stack)
		class FEventStack
		{
		private:
			struct FTopNode
			{
				uintptr_t Address  : 45; //all CPUs we care about use less than 48 bits for their addressing, and the lower 3 bits are unused due to alignment
				uintptr_t Revision : 19; //Tagging is used to avoid ABA (the wrap around is several minutes for this use-case (https://en.wikipedia.org/wiki/ABA_problem#Tagged_state_reference))
			};
			std::atomic<FTopNode> Top { FTopNode{0, 0} };

		public:
			FSleepEvent* Pop()
			{
				FTopNode LocalTop = Top.load(std::memory_order_relaxed);
				while (true) 
				{			
					if (LocalTop.Address == 0)
					{
						return nullptr;
					}
#if DO_CHECK
					int64 LastRevision = int64(LocalTop.Revision); 
#endif
					FSleepEvent* Item = reinterpret_cast<FSleepEvent*>(LocalTop.Address << 3);
					if (Top.compare_exchange_weak(LocalTop, FTopNode { reinterpret_cast<uintptr_t>(Item->Next) >> 3, uintptr_t(LocalTop.Revision + 1) }, std::memory_order_acquire, std::memory_order_relaxed))
					{
						Item->Next = nullptr;
						return Item;
					}
#if DO_CHECK
					int64 NewRevision = int64(LocalTop.Revision) < LastRevision ? ((1ll << 19) + int64(LocalTop.Revision)) : int64(LocalTop.Revision);
					ensureMsgf((NewRevision - LastRevision) < (1ll << 18), TEXT("Dangerously close to the wraparound: %d, %d"), LastRevision, NewRevision);
#endif
				}
			}

			void Push(FSleepEvent* Item)
			{
				checkSlow(Item != nullptr);
#if !USING_CODE_ANALYSIS //MS SA thowing warning C6011 on Item->Next access, even when it is validated or branched over
				checkSlow(reinterpret_cast<uintptr_t>(Item) < (1ull << 48));
				checkSlow((reinterpret_cast<uintptr_t>(Item) & 0x7) == 0);
#endif
				checkSlow(Item->Next == nullptr);
			
				FTopNode LocalTop = Top.load(std::memory_order_relaxed);
				while (true) 
				{
#if DO_CHECK
					int64 LastRevision = int64(LocalTop.Revision); 
#endif
					Item->Next = reinterpret_cast<FSleepEvent*>(LocalTop.Address << 3);
					if (Top.compare_exchange_weak(LocalTop, FTopNode { reinterpret_cast<uintptr_t>(Item) >> 3, uintptr_t(LocalTop.Revision + 1) }, std::memory_order_release, std::memory_order_acquire))  
					{
						return;
					}
#if DO_CHECK
					int64 NewRevision = int64(LocalTop.Revision) < LastRevision ? ((1ll << 19) + int64(LocalTop.Revision)) : int64(LocalTop.Revision);
					ensureMsgf((NewRevision - LastRevision) < (1ll << 18), TEXT("Dangerously close to the wraparound: %d, %d"), LastRevision, NewRevision);
#endif
				}
			}
		};

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
		FEventStack 				SleepEventStack[2];
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

	inline bool FScheduler::IsBusyWaiting()
	{
		return BusyWaitingDepth != 0;
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
			SleepEventStack[bBackgroundWorker].Push(WorkerEvent); // State one: (Running -> Drowsing)
		}
		else
		{
			checkf(false, TEXT("Worker was supposed to be running or drowsing: %d"), WorkerEvent->SleepState.load(std::memory_order_relaxed));
		}
	}

	inline bool FScheduler::WakeUpWorker(bool bBackgroundWorker)
	{
		FSleepEvent* WorkerEvent = SleepEventStack[bBackgroundWorker].Pop();
		if (WorkerEvent)
		{
			ESleepState SleepState = WorkerEvent->SleepState.exchange(ESleepState::Running, std::memory_order_relaxed);
			if (SleepState == ESleepState::Sleeping)
			{
				WorkerEvent->SleepEvent->Trigger(); 
				return true; // Solving State two: (((Running -> Drowsing) -> Sleeping) -> Running)
			}
			checkf(SleepState == ESleepState::Drowsing, TEXT("Worker was not drowsing: %d"), WorkerEvent->SleepState.load(std::memory_order_relaxed));
			return true; // Solving State one: (Running -> Drowsing) -> Running  OR ((Running -> Drowsing) -> Drowsing) -> Running
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

