// Copyright Epic Games, Inc. All Rights Reserved.

#include "Async/Fundamental/ReserveScheduler.h"
#include "Misc/ScopeLock.h"

namespace LowLevelTasks
{

FReserveScheduler FReserveScheduler::Singleton;

TUniquePtr<FThread> FReserveScheduler::CreateWorker(bool bIsForkable, FSchedulerTls::FLocalQueueType* WorkerLocalQueue, EThreadPriority Priority)
{
	uint32 WorkerId = NextWorkerId++;
	return MakeUnique<FThread>
	(
		*FString::Printf(TEXT("Reserve Worker #%d"), WorkerId),
		[this, WorkerLocalQueue]
		{
			FSchedulerTls::ActiveScheduler = this;
			FSchedulerTls::LocalQueue = WorkerLocalQueue;

			FYieldedWork ReserveEvent;
			while (true)
			{
				EventStack.Push(&ReserveEvent);
				ReserveEvent.SleepEvent->Wait();
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(FReserveScheduler::BusyWaitUntil);
					FSchedulerTls::WorkerType = ReserveEvent.bPermitBackgroundWork ? FSchedulerTls::EWorkerType::Background : FSchedulerTls::EWorkerType::Foreground;

					if (ActiveWorkers.load(std::memory_order_relaxed) == 0)
					{
						break;
					}

					BusyWaitUntil(MoveTemp(ReserveEvent.CompletedDelegate), ReserveEvent.bPermitBackgroundWork);
				}
			}
			FSchedulerTls::WorkerType = EWorkerType::None;
			FSchedulerTls::ActiveScheduler = nullptr;
			FSchedulerTls::LocalQueue = nullptr;
		}, 0, Priority, FThreadAffinity{ FPlatformAffinity::GetTaskGraphThreadMask(), 0 }, bIsForkable
	);
}

bool FReserveScheduler::DoReserveWorkUntil(FConditional&& Condition)
{
	if (FYieldedWork* WorkerEvent = EventStack.Pop())
	{
		WorkerEvent->CompletedDelegate = MoveTemp(Condition);
		// become a background worker if the reserve worker is replacing a blocked background worker
		WorkerEvent->bPermitBackgroundWork = FSchedulerTls::IsBackgroundWorker();
		WorkerEvent->SleepEvent->Trigger();
		return true;
	}
	return false;
}

void FReserveScheduler::StartWorkers(FScheduler& MainScheduler, uint32 NumWorkers, bool bIsForkable, EThreadPriority WorkerPriority)
{
	if (NumWorkers == 0)
	{
		NumWorkers = FMath::Min(FPlatformMisc::NumberOfWorkerThreadsToSpawn(), 64);
	}

	uint32 OldActiveWorkers = ActiveWorkers.load(std::memory_order_relaxed);
	if(OldActiveWorkers == 0 && FPlatformProcess::SupportsMultithreading() && ActiveWorkers.compare_exchange_strong(OldActiveWorkers, NumWorkers, std::memory_order_relaxed))
	{
		FScopeLock Lock(&WorkerThreadsCS);
		check(!WorkerThreads.Num());
		check(!WorkerLocalQueues.Num());
		check(NextWorkerId == 0);

		WorkerLocalQueues.Reserve(NumWorkers);
		UE::Trace::ThreadGroupBegin(TEXT("Reserve Workers"));
		for (uint32 WorkerId = 0; WorkerId < NumWorkers; ++WorkerId)
		{
			WorkerLocalQueues.Emplace(MainScheduler.GetQueueRegistry(), ELocalQueueType::EBusyWait, nullptr);
			WorkerThreads.Add(CreateWorker(bIsForkable, &WorkerLocalQueues.Last(), WorkerPriority));
		}
		UE::Trace::ThreadGroupEnd();
	}
}

void FReserveScheduler::StopWorkers()
{
	uint32 OldActiveWorkers = ActiveWorkers.load(std::memory_order_relaxed);
	if(OldActiveWorkers != 0 && ActiveWorkers.compare_exchange_strong(OldActiveWorkers, 0, std::memory_order_relaxed))
	{
		FScopeLock Lock(&WorkerThreadsCS);
		while (FYieldedWork* Event = EventStack.Pop()) 
		{
			Event->SleepEvent->Trigger();
		}

		for (TUniquePtr<FThread>& Thread : WorkerThreads)
		{
			Thread->Join();
		}
		NextWorkerId = 0;
		WorkerThreads.Reset();
		WorkerLocalQueues.Reset();
	}
}

}