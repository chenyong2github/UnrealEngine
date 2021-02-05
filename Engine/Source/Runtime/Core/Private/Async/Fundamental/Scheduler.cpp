// Copyright Epic Games, Inc. All Rights Reserved.

#include "Async/Fundamental/Scheduler.h"
#include "Async/Fundamental/Task.h"
#include "Logging/LogMacros.h"
#include "Misc/ScopeLock.h" 
#include "CoreGlobals.h"

namespace LowLevelTasks
{
	DEFINE_LOG_CATEGORY(LowLevelTasks);

	thread_local FScheduler::FLocalQueueType* FScheduler::LocalQueue = nullptr;
	thread_local FTask* FScheduler::ActiveTask = nullptr;
	thread_local FScheduler::EWorkerType FScheduler::WorkerType = FScheduler::EWorkerType::None;

	FScheduler FScheduler::Singleton;

	FScheduler::FLocalQueueInstaller::FLocalQueueInstaller(FScheduler& Scheduler)
	{
		RegisteredLocalQueue = LocalQueue == nullptr;
		if (RegisteredLocalQueue)
		{
			bool bPermitBackgroundWork = ActiveTask && ActiveTask->IsBackgroundTask();
			LocalQueue = FLocalQueueType::AllocateLocalQueue(Scheduler.QueueRegistry, bPermitBackgroundWork);
		}
	}

	FScheduler::FLocalQueueInstaller::~FLocalQueueInstaller()
	{
		if (RegisteredLocalQueue)
		{
			bool bPermitBackgroundWork = ActiveTask && ActiveTask->IsBackgroundTask();
			FLocalQueueType::DeleteLocalQueue(LocalQueue, bPermitBackgroundWork);
			LocalQueue = nullptr;
		}
	}

	TUniquePtr<FThread> FScheduler::CreateWorker(FLocalQueueType* ExternalWorkerLocalQueue, EThreadPriority Priority, bool bPermitBackgroundWork, bool bIsForkable)
	{
		uint32 WorkerId = NextWorkerId++;
		const uint32 WaitTimes[8] = { 23, 31, 41, 37, 47, 29, 19, 43 };
		uint32 WaitTime = WaitTimes[WorkerId % 8];
		uint64 ThreadAffinityMask = FPlatformAffinity::GetTaskGraphThreadMask();
		return MakeUnique<FThread>
		(
			bPermitBackgroundWork ? *FString::Printf(TEXT("Background Worker #%d"), WorkerId) : *FString::Printf(TEXT("Foreground Worker #%d"), WorkerId),
			[this, ExternalWorkerLocalQueue, WaitTime, bPermitBackgroundWork]
			{ 
				FSleepEvent Event;
				WorkerMain(&Event, ExternalWorkerLocalQueue, WaitTime, bPermitBackgroundWork);
			}, 0, Priority, ThreadAffinityMask
		);
	}

	void FScheduler::StartWorkers(uint32 NumWorkers, uint32 NumBackgroundWorkers, EThreadPriority WorkerPriority,  EThreadPriority BackgroundPriority, bool bIsForkable)
	{
		if (NumWorkers == 0)
		{
			NumWorkers = 2;
			NumBackgroundWorkers = FMath::Max(1, FPlatformMisc::NumberOfWorkerThreadsToSpawn() - 2);
		}

		uint32 OldActiveWorkers = ActiveWorkers.load(std::memory_order_relaxed);
		if(OldActiveWorkers == 0 && FPlatformProcess::SupportsMultithreading() && ActiveWorkers.compare_exchange_strong(OldActiveWorkers, NumWorkers + NumBackgroundWorkers, std::memory_order_relaxed))
		{
			FScopeLock Lock(&WorkerThreadsCS);
			check(!WorkerThreads.Num());
			check(!WorkerLocalQueues.Num());
			check(NextWorkerId == 0);

			WorkerThreads.Reserve(NumWorkers + NumBackgroundWorkers);
			WorkerLocalQueues.Reserve(NumWorkers + NumBackgroundWorkers);
			UE::Trace::ThreadGroupBegin(TEXT("Foreground Workers"));
			for (uint32 WorkerId = 0; WorkerId < NumWorkers; ++WorkerId)
			{
				WorkerLocalQueues.Emplace(QueueRegistry, false);
				WorkerThreads.Add(CreateWorker(&WorkerLocalQueues.Last(), WorkerPriority, false, bIsForkable));
			}
			UE::Trace::ThreadGroupEnd();
			UE::Trace::ThreadGroupBegin(TEXT("Background Workers"));
			for (uint32 WorkerId = 0; WorkerId < NumBackgroundWorkers; ++WorkerId)
			{
				WorkerLocalQueues.Emplace(QueueRegistry, true);
				WorkerThreads.Add(CreateWorker(&WorkerLocalQueues.Last(), BackgroundPriority, true, bIsForkable));
			}
			UE::Trace::ThreadGroupEnd();
		}
	}

	void FScheduler::StopWorkers()
	{
		uint32 OldActiveWorkers = ActiveWorkers.load(std::memory_order_relaxed);
		if(OldActiveWorkers != 0 && ActiveWorkers.compare_exchange_strong(OldActiveWorkers, 0, std::memory_order_relaxed))
		{
			FScopeLock Lock(&WorkerThreadsCS);
			while (WakeUpWorker(true)) {}
			while (WakeUpWorker(false)) {}

			for (TUniquePtr<FThread>& Thread : WorkerThreads)
			{
				Thread->Join();
			}
			NextWorkerId = 0;
			WorkerThreads.Reset();
			WorkerLocalQueues.Reset();
			for (FTask* Task = QueueRegistry.Dequeue(); Task != nullptr; Task = QueueRegistry.Dequeue())
			{
				Task->ExecuteTask();
			}
		}
	}

	void FScheduler::LaunchInternal(FTask& Task, EQueuePreference QueuePreference, bool bWakeUpWorker)
	{
		if (ActiveWorkers.load(std::memory_order_relaxed))
		{			
			const bool bIsBackgroundTask = Task.IsBackgroundTask();
			const bool bIsBackgroundWorker = WorkerType == EWorkerType::Background;
			if (bIsBackgroundTask && !bIsBackgroundWorker)
			{
				QueuePreference = EQueuePreference::GlobalQueuePreference;
			}

			bWakeUpWorker |= LocalQueue == nullptr;

			if (LocalQueue && QueuePreference != EQueuePreference::GlobalQueuePreference)
			{
				if (LocalQueue->Enqueue(&Task, uint32(Task.GetPriority())))
				{
					if(bWakeUpWorker && !WakeUpWorker(bIsBackgroundTask) && !bIsBackgroundTask)
					{
						WakeUpWorker(true);
					}
				}
			}
			else
			{
				if (QueueRegistry.Enqueue(&Task, uint32(Task.GetPriority())))
				{
					if (bWakeUpWorker && !WakeUpWorker(bIsBackgroundTask) && !bIsBackgroundTask)
					{
						WakeUpWorker(true);
					}
				}
			}
		}
		else
		{
			Task.ExecuteTask();
		}
	}

	const FTask* FScheduler::GetActiveTask() const
	{
		return ActiveTask;
	}

	bool FScheduler::IsWorkerThread() const
	{
		return WorkerType != EWorkerType::None;
	}

	template<FTask* (FScheduler::FLocalQueueType::*DequeueFunction)(bool)>
	bool FScheduler::TryExecuteTaskFrom(FLocalQueueType* Queue, FQueueRegistry::FOutOfWork& OutOfWork, bool bPermitBackgroundWork)
	{
		FTask* Task = (Queue->*DequeueFunction)(bPermitBackgroundWork);
		if (Task)
		{	
			OutOfWork.Stop();		
			FTask* OldTask = ActiveTask;
			ActiveTask = Task;
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(ExecuteTask);
				Task->ExecuteTask();
			}
			ActiveTask = OldTask;
			return true;
		}
		return false;
	}

	void FScheduler::WorkerMain(FSleepEvent* WorkerEvent, FLocalQueueType* ExternalWorkerLocalQueue, uint32 WaitCycles, bool bPermitBackgroundWork)
	{
		FTaskTagScope WorkerScope(ETaskTag::EWorkerThread);

		FMemory::SetupTLSCachesOnCurrentThread();
		WorkerType = bPermitBackgroundWork ? EWorkerType::Background : EWorkerType::Foreground;

		checkSlow(LocalQueue == nullptr);
		if(ExternalWorkerLocalQueue)
		{
			LocalQueue = ExternalWorkerLocalQueue;
		}
		else
		{
			LocalQueue = FLocalQueueType::AllocateLocalQueue(QueueRegistry, bPermitBackgroundWork);
		}
		FLocalQueueType* WorkerLocalQueue = LocalQueue;

		bool Drowsing = false;
		uint32 WaitCount = 0;
		FQueueRegistry::FOutOfWork OutOfWork = QueueRegistry.GetOutOfWorkScope(bPermitBackgroundWork);
		while (true)
		{
			while(TryExecuteTaskFrom<&FLocalQueueType::DequeueLocal>(WorkerLocalQueue, OutOfWork, bPermitBackgroundWork)
			   || TryExecuteTaskFrom<&FLocalQueueType::DequeueGlobal>(WorkerLocalQueue, OutOfWork, bPermitBackgroundWork))
			{		
				Drowsing = false;
				WaitCount = 0;
			}

			while(TryExecuteTaskFrom<&FLocalQueueType::DequeueLocal>(WorkerLocalQueue, OutOfWork, bPermitBackgroundWork)
			   || TryExecuteTaskFrom<&FLocalQueueType::DequeueSteal>(WorkerLocalQueue, OutOfWork, bPermitBackgroundWork))
			{
				Drowsing = false;
				WaitCount = 0;
			}

			if (ActiveWorkers.load(std::memory_order_relaxed) == 0)
			{
				break;
			}

			if (WaitCount < WorkerSpinCycles)
			{
				OutOfWork.Start();
				for (uint32 i = 0; i < WaitCycles; i++)
				{
					FPlatformProcess::Yield();
				}
				WaitCount++;
				continue;
			}

			TrySleeping(WorkerEvent, OutOfWork, Drowsing, bPermitBackgroundWork);
		}

		while (WakeUpWorker(bPermitBackgroundWork)) {}

		FLocalQueueType::DeleteLocalQueue(WorkerLocalQueue, bPermitBackgroundWork, ExternalWorkerLocalQueue != nullptr);
		LocalQueue = nullptr;

		WorkerType = EWorkerType::None;
		FMemory::ClearAndDisableTLSCachesOnCurrentThread();
	}

	void FScheduler::BusyWaitInternal(const FConditional& Conditional)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FScheduler::BusyWaitInternal);
		FTaskTagScope WorkerScope(ETaskTag::EWorkerThread);

		checkSlow(LocalQueue != nullptr);
		check(ActiveWorkers.load(std::memory_order_relaxed));
		FLocalQueueType* WorkerLocalQueue = LocalQueue;

		uint32 WaitCount = 0;
		bool bPermitBackgroundWork = ActiveTask && ActiveTask->IsBackgroundTask();
		FQueueRegistry::FOutOfWork OutOfWork = QueueRegistry.GetOutOfWorkScope(bPermitBackgroundWork);
		while (true)
		{
			while(TryExecuteTaskFrom<&FLocalQueueType::DequeueLocal>(WorkerLocalQueue, OutOfWork, bPermitBackgroundWork)
			   || TryExecuteTaskFrom<&FLocalQueueType::DequeueGlobal>(WorkerLocalQueue, OutOfWork, bPermitBackgroundWork))
			{
				if (Conditional())
				{
					return;
				}
				WaitCount = 0;
			}

			while(TryExecuteTaskFrom<&FLocalQueueType::DequeueLocal>(WorkerLocalQueue, OutOfWork, bPermitBackgroundWork)
			   || TryExecuteTaskFrom<&FLocalQueueType::DequeueSteal>(WorkerLocalQueue, OutOfWork, bPermitBackgroundWork))
			{
				if (Conditional())
				{
					return;
				}
				WaitCount = 0;
			}

			if (Conditional())
			{
				return;
			}

			const bool bIsBackgroundWorker = WorkerType == EWorkerType::Background;
			if (WaitCount < WorkerSpinCycles)
			{
				OutOfWork.Start();
				FPlatformProcess::Yield();
				FPlatformProcess::Yield();
				WaitCount++;
			}
			else if (!bPermitBackgroundWork && bIsBackgroundWorker)
			{
				bPermitBackgroundWork = true;
			}
			else
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(BusyWaitInternal::SleepNoStats);
				WakeUpWorker(true); //wake up a backgoundworker in case we were waiting on background work.
				FPlatformProcess::SleepNoStats(0);
				WaitCount = 0;
			}
		}
	}
}