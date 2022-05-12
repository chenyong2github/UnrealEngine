// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/TaskPrivate.h"
#include "Tasks/Pipe.h"

#if TASKGRAPH_NEW_FRONTEND
#include "Async/TaskGraphInterfaces.h"
#endif

namespace UE::Tasks::Private
{
	void FTaskBase::Schedule()
	{
#if TASKGRAPH_NEW_FRONTEND
		if (IsNamedThreadTask())
		{
			ENamedThreads::Type ConversionMap[] =
			{
				ENamedThreads::GameThread,
				(ENamedThreads::Type)(ENamedThreads::GameThread | ENamedThreads::HighTaskPriority),
				(ENamedThreads::Type)(ENamedThreads::GameThread | ENamedThreads::LocalQueue),
				(ENamedThreads::Type)(ENamedThreads::GameThread | ENamedThreads::HighTaskPriority | ENamedThreads::LocalQueue),

				ENamedThreads::GetRenderThread(),
				(ENamedThreads::Type)(ENamedThreads::GetRenderThread() | ENamedThreads::HighTaskPriority),
				(ENamedThreads::Type)(ENamedThreads::GetRenderThread() | ENamedThreads::LocalQueue),
				(ENamedThreads::Type)(ENamedThreads::GetRenderThread() | ENamedThreads::HighTaskPriority | ENamedThreads::LocalQueue),

				ENamedThreads::RHIThread,
				(ENamedThreads::Type)(ENamedThreads::RHIThread | ENamedThreads::HighTaskPriority),
				(ENamedThreads::Type)(ENamedThreads::RHIThread | ENamedThreads::LocalQueue),
				(ENamedThreads::Type)(ENamedThreads::RHIThread | ENamedThreads::HighTaskPriority | ENamedThreads::LocalQueue)
			};

			FTaskGraphInterface::Get().QueueTask(static_cast<FBaseGraphTask*>(this), true, ConversionMap[(int32)ExtendedPriority - (int32)EExtendedTaskPriority::GameThreadNormalPri]);
			return;
		}
#endif

		LowLevelTasks::FScheduler::Get().TryLaunch(LowLevelTask, LowLevelTasks::EQueuePreference::GlobalQueuePreference, /*bWakeUpWorker=*/ true);
	}

	FTaskBase* FTaskBase::TryPushIntoPipe()
	{
		return GetPipe()->PushIntoPipe(*this);
	}

	void FTaskBase::StartPipeExecution()
	{
		GetPipe()->ExecutionStarted();
	}

	void FTaskBase::FinishPipeExecution()
	{
		GetPipe()->ExecutionFinished();
	}

	void FTaskBase::ClearPipe()
	{
		GetPipe()->ClearTask(*this);
	}

	static thread_local FTaskBase* CurrentTask = nullptr;

	FTaskBase* GetCurrentTask()
	{
		return CurrentTask;
	}

	FTaskBase* ExchangeCurrentTask(FTaskBase* Task)
	{
		FTaskBase* PrevTask = CurrentTask;
		CurrentTask = Task;
		return PrevTask;
	}

	bool TryWaitOnNamedThread(FTaskBase& Task)
	{
#if TASKGRAPH_NEW_FRONTEND
		// handle waiting only on a named thread and if not called from inside a task
		FTaskGraphInterface& TaskGraph = FTaskGraphInterface::Get();
		ENamedThreads::Type CurrentThread = TaskGraph.GetCurrentThreadIfKnown();
		if (CurrentThread < ENamedThreads::ActualRenderingThread /* is a named thread? */ && !TaskGraph.IsThreadProcessingTasks(CurrentThread))
		{
			// execute other tasks of this named thread while waiting
			ETaskPriority Dummy;
			EExtendedTaskPriority ExtendedPriority;
			FBaseGraphTask::TranslatePriority(CurrentThread, Dummy, ExtendedPriority);

			auto TaskBody = [CurrentThread, &TaskGraph] { TaskGraph.RequestReturn(CurrentThread); };
			using FReturnFromNamedThreadTask = TExecutableTask<decltype(TaskBody)>;
			FReturnFromNamedThreadTask ReturnTask{ TEXT("ReturnFromNamedThreadTask"), MoveTemp(TaskBody), ETaskPriority::High, ExtendedPriority };
			ReturnTask.AddPrerequisites(Task);
			ReturnTask.TryLaunch(); // the result doesn't matter

			TaskGraph.ProcessThreadUntilRequestReturn(CurrentThread);
			return true;
		}
#endif

		return false;
	}
}
