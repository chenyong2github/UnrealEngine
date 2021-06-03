// Copyright Epic Games, Inc. All Rights Reserved.

#include "Async/Fundamental/Scheduler.h"
#include "Async/Fundamental/Task.h"
#include "Async/TaskTrace.h"
#include "Logging/LogMacros.h"
#include "Misc/ScopeLock.h"
#include "CoreGlobals.h"

namespace LowLevelTasks
{
	DEFINE_LOG_CATEGORY(LowLevelTasks);

	thread_local FScheduler::FLocalQueueType* FScheduler::LocalQueue = nullptr;
	thread_local FTask* FSchedulerTls::ActiveTask = nullptr;
	thread_local FSchedulerTls* FSchedulerTls::ActiveScheduler = nullptr;
	thread_local FSchedulerTls::EWorkerType FSchedulerTls::WorkerType = FSchedulerTls::EWorkerType::None;
	thread_local uint32 FSchedulerTls::BusyWaitingDepth = 0;

	FScheduler FScheduler::Singleton;

	FScheduler::FLocalQueueInstaller::FLocalQueueInstaller(FScheduler& Scheduler)
	{
		RegisteredLocalQueue = LocalQueue == nullptr;
		if (RegisteredLocalQueue)
		{
			bool bPermitBackgroundWork = FSchedulerTls::PermitBackgroundWork();
			LocalQueue = FLocalQueueType::AllocateLocalQueue(Scheduler.QueueRegistry, bPermitBackgroundWork);
		}
	}

	FScheduler::FLocalQueueInstaller::~FLocalQueueInstaller()
	{
		if (RegisteredLocalQueue)
		{
			bool bPermitBackgroundWork = FSchedulerTls::PermitBackgroundWork();
			FLocalQueueType::DeleteLocalQueue(LocalQueue, bPermitBackgroundWork);
			LocalQueue = nullptr;
		}
	}

	TUniquePtr<FThread> FScheduler::CreateWorker(FLocalQueueType* ExternalWorkerLocalQueue, EThreadPriority Priority, bool bPermitBackgroundWork, bool bIsForkable)
	{
		uint32 WorkerId = NextWorkerId++;
		const uint32 WaitTimes[8] = { 719, 991, 1361, 1237, 1597, 953, 587, 1439 };
		uint32 WaitTime = WaitTimes[WorkerId % 8];
		uint64 ThreadAffinityMask = FPlatformAffinity::GetTaskGraphThreadMask();

		const FProcessorGroupDesc& ProcessorGroups = FPlatformMisc::GetProcessorGroupDesc();
		int32 CpuGroupCount = ProcessorGroups.NumProcessorGroups;
		uint16 CpuGroup = 0;

		//offset the firt set of workers to leave space for Game, RHI and Renderthread.
		uint64 GroupWorkerId = WorkerId + 2;
		for (uint16 GroupIndex = 0; GroupIndex < CpuGroupCount; GroupIndex++)
		{
			CpuGroup = GroupIndex;

			uint32 CpusInGroup = FMath::CountBits(ProcessorGroups.ThreadAffinities[GroupIndex]);
			if(GroupWorkerId < CpusInGroup)
			{
				if (CpuGroup != 0) //pin larger groups workers to a core and leave first group as is for legacy reasons
				{
					ThreadAffinityMask = 1ull << GroupWorkerId;
				}		
				break;
			}
			GroupWorkerId -= CpusInGroup;
		}
		
		return MakeUnique<FThread>
		(
			bPermitBackgroundWork ? *FString::Printf(TEXT("Background Worker #%d"), WorkerId) : *FString::Printf(TEXT("Foreground Worker #%d"), WorkerId),
			[this, ExternalWorkerLocalQueue, WaitTime, bPermitBackgroundWork]
			{ 
				FSleepEvent Event;
				WorkerMain(&Event, ExternalWorkerLocalQueue, WaitTime, bPermitBackgroundWork);
			}, 0, Priority, FThreadAffinity{ ThreadAffinityMask & ProcessorGroups.ThreadAffinities[CpuGroup], CpuGroup }, bIsForkable
		);
	}

	void FScheduler::StartWorkers(uint32 NumForegroundWorkers, uint32 NumBackgroundWorkers, EThreadPriority WorkerPriority,  EThreadPriority BackgroundPriority, bool bIsForkable)
	{
		TaskTrace::Init();

		if (NumForegroundWorkers == 0 && NumBackgroundWorkers == 0)
		{
			NumForegroundWorkers = FMath::Max<int32>(1, FMath::Min<int32>(2, FPlatformMisc::NumberOfWorkerThreadsToSpawn() - 1));
			NumBackgroundWorkers = FMath::Max<int32>(1, FPlatformMisc::NumberOfWorkerThreadsToSpawn() - NumForegroundWorkers);
		}

		uint32 OldActiveWorkers = ActiveWorkers.load(std::memory_order_relaxed);
		if(OldActiveWorkers == 0 && FPlatformProcess::SupportsMultithreading() && ActiveWorkers.compare_exchange_strong(OldActiveWorkers, NumForegroundWorkers + NumBackgroundWorkers, std::memory_order_relaxed))
		{
			FScopeLock Lock(&WorkerThreadsCS);
			check(!WorkerThreads.Num());
			check(!WorkerLocalQueues.Num());
			check(NextWorkerId == 0);

			WorkerThreads.Reserve(NumForegroundWorkers + NumBackgroundWorkers);
			WorkerLocalQueues.Reserve(NumForegroundWorkers + NumBackgroundWorkers);
			UE::Trace::ThreadGroupBegin(TEXT("Foreground Workers"));
			for (uint32 WorkerId = 0; WorkerId < NumForegroundWorkers; ++WorkerId)
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
			const bool bIsBackgroundWorker = FSchedulerTls::IsBackgroundWorker();
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

	const FTask* FSchedulerTls::GetActiveTask() 
	{
		return ActiveTask;
	}

	bool FSchedulerTls::IsWorkerThread() const
	{
		return WorkerType != FSchedulerTls::EWorkerType::None && ActiveScheduler == this;
	}

	bool FSchedulerTls::IsBusyWaiting()
	{
		return BusyWaitingDepth != 0;
	}

	void FTask::PropagateUserData()
	{
		const FTask* ActiveTask = FSchedulerTls::GetActiveTask();
		if (ActiveTask != nullptr)
		{
			UserData = ActiveTask->GetUserData();
		}
		else
		{
			UserData = nullptr;
		}
	}

	template<FTask* (FScheduler::FLocalQueueType::*DequeueFunction)(bool), bool bIsBusyWaiting>
	bool FScheduler::TryExecuteTaskFrom(FLocalQueueType* Queue, FQueueRegistry::FOutOfWork& OutOfWork, bool bPermitBackgroundWork)
	{
		for(int i = 0; i < 2; i++) // one retry if we pick up a task that cannot used during busy waiting.
		{
			FTask* Task = (Queue->*DequeueFunction)(bPermitBackgroundWork);
			if (Task)
			{	
				if (bIsBusyWaiting && !Task->AllowBusyWaiting())
				{
					QueueRegistry.Enqueue(Task, uint32(Task->GetPriority()));
					continue;
				}
				OutOfWork.Stop();		
				FTask* OldTask = FSchedulerTls::ActiveTask;
				FSchedulerTls::ActiveTask = Task;
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(ExecuteTask);
					Task->ExecuteTask();
				}
				FSchedulerTls::ActiveTask = OldTask;
				return true;
			}
			return false;
		}
		return false;
	}

	void FScheduler::WorkerMain(FSleepEvent* WorkerEvent, FLocalQueueType* ExternalWorkerLocalQueue, uint32 WaitCycles, bool bPermitBackgroundWork)
	{
		FTaskTagScope WorkerScope(ETaskTag::EWorkerThread);
		FSchedulerTls::ActiveScheduler = this;

		FMemory::SetupTLSCachesOnCurrentThread();
		FSchedulerTls::WorkerType = bPermitBackgroundWork ? FSchedulerTls::EWorkerType::Background : FSchedulerTls::EWorkerType::Foreground;

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
			while(TryExecuteTaskFrom<&FLocalQueueType::DequeueLocal, false>(WorkerLocalQueue, OutOfWork, bPermitBackgroundWork)
			   || TryExecuteTaskFrom<&FLocalQueueType::DequeueGlobal, false>(WorkerLocalQueue, OutOfWork, bPermitBackgroundWork))
			{		
				Drowsing = false;
				WaitCount = 0;
			}

			while(TryExecuteTaskFrom<&FLocalQueueType::DequeueLocal, false>(WorkerLocalQueue, OutOfWork, bPermitBackgroundWork)
			   || TryExecuteTaskFrom<&FLocalQueueType::DequeueSteal, false>(WorkerLocalQueue, OutOfWork, bPermitBackgroundWork))
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
				FPlatformProcess::YieldCycles(WaitCycles);
				WaitCount++;
				continue;
			}

			TrySleeping(WorkerEvent, OutOfWork, Drowsing, bPermitBackgroundWork);
		}

		while (WakeUpWorker(bPermitBackgroundWork)) {}

		FLocalQueueType::DeleteLocalQueue(WorkerLocalQueue, bPermitBackgroundWork, ExternalWorkerLocalQueue != nullptr);
		LocalQueue = nullptr;

		FSchedulerTls::ActiveScheduler = nullptr;
		FSchedulerTls::WorkerType = FSchedulerTls::EWorkerType::None;
		FMemory::ClearAndDisableTLSCachesOnCurrentThread();
	}

	void FScheduler::BusyWaitInternal(const FConditional& Conditional, bool ForceAllowBackgroundWork)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FScheduler::BusyWaitInternal);
		FTaskTagScope WorkerScope(ETaskTag::EWorkerThread);

		++FSchedulerTls::BusyWaitingDepth;
		ON_SCOPE_EXIT{ --FSchedulerTls::BusyWaitingDepth; };

		checkSlow(LocalQueue != nullptr);
		check(ActiveWorkers.load(std::memory_order_relaxed));
		FLocalQueueType* WorkerLocalQueue = LocalQueue;

		uint32 WaitCount = 0;
		bool HasWokenEmergencyWorker = false;
		const bool bIsBackgroundWorker = FSchedulerTls::IsBackgroundWorker();
		bool bPermitBackgroundWork = FSchedulerTls::PermitBackgroundWork() || ForceAllowBackgroundWork;
		FQueueRegistry::FOutOfWork OutOfWork = QueueRegistry.GetOutOfWorkScope(bIsBackgroundWorker);
		while (true)
		{
			while(TryExecuteTaskFrom<&FLocalQueueType::DequeueLocal, true>(WorkerLocalQueue, OutOfWork, bPermitBackgroundWork)
			   || TryExecuteTaskFrom<&FLocalQueueType::DequeueGlobal, true>(WorkerLocalQueue, OutOfWork, bPermitBackgroundWork))
			{
				if (Conditional())
				{
					return;
				}
				WaitCount = 0;
			}

			while(TryExecuteTaskFrom<&FLocalQueueType::DequeueLocal, true>(WorkerLocalQueue, OutOfWork, bPermitBackgroundWork)
			   || TryExecuteTaskFrom<&FLocalQueueType::DequeueSteal, true>(WorkerLocalQueue, OutOfWork, bPermitBackgroundWork))
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
				if(!HasWokenEmergencyWorker)
				{
					WakeUpWorker(true);
					HasWokenEmergencyWorker = true;
				}
				WaitCount = 0;
			}			
		}
	}
}