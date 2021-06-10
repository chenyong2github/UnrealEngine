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

	thread_local FSchedulerTls::FLocalQueueType* FSchedulerTls::LocalQueue = nullptr;
	thread_local FTask* FSchedulerTls::ActiveTask = nullptr;
	thread_local FSchedulerTls* FSchedulerTls::ActiveScheduler = nullptr;
	thread_local FSchedulerTls::EWorkerType FSchedulerTls::WorkerType = FSchedulerTls::EWorkerType::None;
	thread_local uint32 FSchedulerTls::BusyWaitingDepth = 0;

	FScheduler FScheduler::Singleton;

	FScheduler::FLocalQueueInstaller::FLocalQueueInstaller(FScheduler& Scheduler)
	{
		RegisteredLocalQueue = FSchedulerTls::LocalQueue == nullptr;
		if (RegisteredLocalQueue)
		{
			bool bPermitBackgroundWork = FSchedulerTls::PermitBackgroundWork();
			FSchedulerTls::LocalQueue = FSchedulerTls::FLocalQueueType::AllocateLocalQueue(Scheduler.QueueRegistry, bPermitBackgroundWork ? ELocalQueueType::EBackground : ELocalQueueType::EForeground);
		}
	}

	FScheduler::FLocalQueueInstaller::~FLocalQueueInstaller()
	{
		if (RegisteredLocalQueue)
		{
			bool bPermitBackgroundWork = FSchedulerTls::PermitBackgroundWork();
			FSchedulerTls::FLocalQueueType::DeleteLocalQueue(FSchedulerTls::LocalQueue, bPermitBackgroundWork ? ELocalQueueType::EBackground : ELocalQueueType::EForeground);
			FSchedulerTls::LocalQueue = nullptr;
		}
	}

	TUniquePtr<FThread> FScheduler::CreateWorker(FSchedulerTls::FLocalQueueType* ExternalWorkerLocalQueue, EThreadPriority Priority, bool bPermitBackgroundWork, bool bIsForkable)
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
				WorkerLocalQueues.Emplace(QueueRegistry, ELocalQueueType::EForeground);
				WorkerThreads.Add(CreateWorker(&WorkerLocalQueues.Last(), WorkerPriority, false, bIsForkable));
			}
			UE::Trace::ThreadGroupEnd();
			UE::Trace::ThreadGroupBegin(TEXT("Background Workers"));
			for (uint32 WorkerId = 0; WorkerId < NumBackgroundWorkers; ++WorkerId)
			{
				WorkerLocalQueues.Emplace(QueueRegistry, ELocalQueueType::EBackground);
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

			bWakeUpWorker |= FSchedulerTls::LocalQueue == nullptr;

			if (FSchedulerTls::LocalQueue && QueuePreference != EQueuePreference::GlobalQueuePreference)
			{
				if (FSchedulerTls::LocalQueue->Enqueue(&Task, uint32(Task.GetPriority())))
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

	template<FTask* (FSchedulerTls::FLocalQueueType::*DequeueFunction)(bool), bool bIsBusyWaiting>
	bool FScheduler::TryExecuteTaskFrom(FSchedulerTls::FLocalQueueType* Queue, FSchedulerTls::FQueueRegistry::FOutOfWork& OutOfWork, bool bPermitBackgroundWork)
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

	void FScheduler::WorkerMain(FSleepEvent* WorkerEvent, FSchedulerTls::FLocalQueueType* ExternalWorkerLocalQueue, uint32 WaitCycles, bool bPermitBackgroundWork)
	{
		FTaskTagScope WorkerScope(ETaskTag::EWorkerThread);
		FSchedulerTls::ActiveScheduler = this;

		FMemory::SetupTLSCachesOnCurrentThread();
		FSchedulerTls::WorkerType = bPermitBackgroundWork ? FSchedulerTls::EWorkerType::Background : FSchedulerTls::EWorkerType::Foreground;

		checkSlow(FSchedulerTls::LocalQueue == nullptr);
		if(ExternalWorkerLocalQueue)
		{
			FSchedulerTls::LocalQueue = ExternalWorkerLocalQueue;
		}
		else
		{
			FSchedulerTls::LocalQueue = FSchedulerTls::FLocalQueueType::AllocateLocalQueue(QueueRegistry, bPermitBackgroundWork ? ELocalQueueType::EBackground : ELocalQueueType::EForeground);
		}
		FSchedulerTls::FLocalQueueType* WorkerLocalQueue = FSchedulerTls::LocalQueue;

		bool Drowsing = false;
		uint32 WaitCount = 0;
		FSchedulerTls::FQueueRegistry::FOutOfWork OutOfWork = QueueRegistry.GetOutOfWorkScope(bPermitBackgroundWork ? ELocalQueueType::EBackground : ELocalQueueType::EForeground);
		while (true)
		{
			while(TryExecuteTaskFrom<&FSchedulerTls::FLocalQueueType::DequeueLocal, false>(WorkerLocalQueue, OutOfWork, bPermitBackgroundWork)
			   || TryExecuteTaskFrom<&FSchedulerTls::FLocalQueueType::DequeueGlobal, false>(WorkerLocalQueue, OutOfWork, bPermitBackgroundWork))
			{		
				Drowsing = false;
				WaitCount = 0;
			}

			while(TryExecuteTaskFrom<&FSchedulerTls::FLocalQueueType::DequeueLocal, false>(WorkerLocalQueue, OutOfWork, bPermitBackgroundWork)
			   || TryExecuteTaskFrom<&FSchedulerTls::FLocalQueueType::DequeueSteal, false>(WorkerLocalQueue, OutOfWork, bPermitBackgroundWork))
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

		FSchedulerTls::FLocalQueueType::DeleteLocalQueue(WorkerLocalQueue, bPermitBackgroundWork ? ELocalQueueType::EBackground : ELocalQueueType::EForeground, ExternalWorkerLocalQueue != nullptr);
		FSchedulerTls::LocalQueue = nullptr;

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

		checkSlow(FSchedulerTls::LocalQueue != nullptr);
		check(ActiveWorkers.load(std::memory_order_relaxed));
		FSchedulerTls::FLocalQueueType* WorkerLocalQueue = FSchedulerTls::LocalQueue;

		uint32 WaitCount = 0;
		bool HasWokenEmergencyWorker = false;
		const bool bIsBackgroundWorker = FSchedulerTls::IsBackgroundWorker();
		bool bPermitBackgroundWork = FSchedulerTls::PermitBackgroundWork() || ForceAllowBackgroundWork;
		FSchedulerTls::FQueueRegistry::FOutOfWork OutOfWork = QueueRegistry.GetOutOfWorkScope(bPermitBackgroundWork ? ELocalQueueType::EBackground : ELocalQueueType::EForeground);
		while (true)
		{
			while(TryExecuteTaskFrom<&FSchedulerTls::FLocalQueueType::DequeueLocal, true>(WorkerLocalQueue, OutOfWork, bPermitBackgroundWork)
			   || TryExecuteTaskFrom<&FSchedulerTls::FLocalQueueType::DequeueGlobal, true>(WorkerLocalQueue, OutOfWork, bPermitBackgroundWork))
			{
				if (Conditional())
				{
					return;
				}
				WaitCount = 0;
			}

			while(TryExecuteTaskFrom<&FSchedulerTls::FLocalQueueType::DequeueLocal, true>(WorkerLocalQueue, OutOfWork, bPermitBackgroundWork)
			   || TryExecuteTaskFrom<&FSchedulerTls::FLocalQueueType::DequeueSteal, true>(WorkerLocalQueue, OutOfWork, bPermitBackgroundWork))
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