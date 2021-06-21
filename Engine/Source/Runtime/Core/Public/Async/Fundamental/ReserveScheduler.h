// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 
#include "Scheduler.h"

namespace LowLevelTasks
{
	class FReserveScheduler final : public FSchedulerTls
	{
		UE_NONCOPYABLE(FReserveScheduler);
		static CORE_API FReserveScheduler Singleton;

	public:
		FReserveScheduler() = default;
		~FReserveScheduler();

	public:
		using FConditional = TTaskDelegate<16, bool>;

	private:
		struct alignas(8) FYieldedWork
		{
			FEventRef		SleepEvent;
			FConditional	CompletedDelegate = []() { return true; };
			FYieldedWork*	Next = nullptr;
			bool			bPermitBackgroundWork = false;
		};
	
	public:
		FORCEINLINE_DEBUGGABLE static FReserveScheduler& Get();

		//start number of reserve workers where 0 is the system default
		CORE_API void StartWorkers(FScheduler& MainScheduler, uint32 ReserveWorkers = 0, EThreadPriority WorkerPriority = EThreadPriority::TPri_Normal, bool bIsForkable = false);
		CORE_API void StopWorkers();

		//tries to yield this thread using the YieldEvent and do busywork on a reserve worker.
		CORE_API bool DoReserveWorkUntil(FConditional&& Condition);

	private: 
		TUniquePtr<FThread> CreateWorker(FSchedulerTls::FLocalQueueType* WorkerLocalQueue, EThreadPriority Priority = EThreadPriority::TPri_Normal, bool bIsForkable = false);

	private:
		TEventStack<FYieldedWork> 				EventStack;
		FCriticalSection 						WorkerThreadsCS;
		TArray<FSchedulerTls::FLocalQueueType>	WorkerLocalQueues;
		TArray<TUniquePtr<FThread>>				WorkerThreads;
		std::atomic_uint						ActiveWorkers { 0 };
		std::atomic_uint						NextWorkerId { 0 };
	};

	inline FReserveScheduler& FReserveScheduler::Get()
	{
		return Singleton;
	}

	FORCEINLINE_DEBUGGABLE bool DoReserveWorkUntil(FReserveScheduler::FConditional&& Condition)
	{
		return FReserveScheduler::Get().DoReserveWorkUntil(MoveTemp(Condition));
	}

	inline FReserveScheduler::~FReserveScheduler()
	{
		StopWorkers();
	}
}