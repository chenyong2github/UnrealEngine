// Copyright Epic Games, Inc. All Rights Reserved.

#include "TraceServices/Model/TasksProfiler.h"
#include "Model/TasksProfilerPrivate.h"
#include "AnalysisServicePrivate.h"
#include "Misc/ScopeLock.h"
#include "Misc/Timespan.h"
#include "Algo/BinarySearch.h"
#include "Common/Utils.h"
#include "Async/TaskGraphInterfaces.h"

namespace TraceServices
{
	FTasksProvider::FTasksProvider(IAnalysisSession& InSession)
		: Session(InSession)
		, CounterProvider(EditCounterProvider(Session))
	{
#if 0
		////////////////////////////////
		// tests

		FAnalysisSessionEditScope _(Session);

		const int32 ValidThreadId = 42;
		const int32 InvalidThreadId = 0;
		const double InvalidTimestamp = 0;

		TArray64<TaskTrace::FId>& Thread = ExecutionThreads.FindOrAdd(ValidThreadId);

		auto MockTaskExecution = [this, Thread = &Thread](TaskTrace::FId TaskId, double StartedTimestamp, double FinishedTimestamp) -> FTaskInfo&
		{
			FTaskInfo& Task = GetOrCreateTask(TaskId);
			Task.Id = TaskId;
			Task.StartedTimestamp = StartedTimestamp;
			Task.FinishedTimestamp = FinishedTimestamp;
			Thread->Add(Task.Id);
			return Task;
		};

		FTaskInfo& Task1 = MockTaskExecution(0, 5, 10);
		FTaskInfo& Task2 = MockTaskExecution(1, 15, 20);
		
		check(GetTask(InvalidThreadId, InvalidTimestamp) == nullptr);
		check(GetTask(ValidThreadId, InvalidTimestamp) == nullptr);
		check(GetTask(ValidThreadId, 5) == &Task1);
		check(GetTask(ValidThreadId, 7) == &Task1);
		check(GetTask(ValidThreadId, 10) == nullptr);
		check(GetTask(ValidThreadId, 12) == nullptr);
		check(GetTask(ValidThreadId, 17) == &Task2);
		check(GetTask(ValidThreadId, 22) == nullptr);

		// reset
		FirstTaskId = TaskTrace::InvalidId;
		ExecutionThreads.Empty();
		Tasks.Empty();
#endif
	}

	void FTasksProvider::CreateCounters()
	{
		FAnalysisSessionEditScope _(Session);

		WaitingForPrerequisitesTasksCounter = CounterProvider.CreateCounter();
		WaitingForPrerequisitesTasksCounter->SetName(TEXT("Tasks::WaitingForPrerequisitesTasks"));
		WaitingForPrerequisitesTasksCounter->SetDescription(TEXT("Tasks: the number of tasks waiting for prerequisites (blocked by dependency)"));
		WaitingForPrerequisitesTasksCounter->SetIsFloatingPoint(false);

		TaskLatencyCounter = CounterProvider.CreateCounter();
		TaskLatencyCounter->SetName(TEXT("Tasks::TaskLatency"));
		TaskLatencyCounter->SetDescription(TEXT("Tasks: tasks latency - the time from scheduling to execution start"));
		TaskLatencyCounter->SetIsFloatingPoint(true);

		ScheduledTasksCounter = CounterProvider.CreateCounter();
		ScheduledTasksCounter->SetName(TEXT("Tasks::ScheduledTasks"));
		ScheduledTasksCounter->SetDescription(TEXT("Tasks: number of scheduled tasks excluding named threads (the size of the queue)"));
		ScheduledTasksCounter->SetIsFloatingPoint(false);

		NamedThreadsScheduledTasksCounter = CounterProvider.CreateCounter();
		NamedThreadsScheduledTasksCounter->SetName(TEXT("Tasks::NamedThreadsScheduledTasks"));
		NamedThreadsScheduledTasksCounter->SetDescription(TEXT("Tasks: number of scheduled tasks for named threads"));
		NamedThreadsScheduledTasksCounter->SetIsFloatingPoint(false);

		RunningTasksCounter = CounterProvider.CreateCounter();
		RunningTasksCounter->SetName(TEXT("Tasks::RunningTasks"));
		RunningTasksCounter->SetDescription(TEXT("Tasks: level of parallelism - the number of tasks being executed"));
		RunningTasksCounter->SetIsFloatingPoint(false);
	}

	void FTasksProvider::Init(uint32 InVersion)
	{
		Version = InVersion;

		CreateCounters();
	}

	void FTasksProvider::TaskCreated(TaskTrace::FId TaskId, double Timestamp, uint32 ThreadId)
	{
		UE_LOG(LogTraceServices, Verbose, TEXT("TaskCreated(TaskId: %d, Timestamp %.6f)"), TaskId, Timestamp);

		InitTaskIdToIndexConversion(TaskId);

		FTaskInfo* Task = TryGetOrCreateTask(TaskId);
		if (Task == nullptr)
		{
			UE_LOG(LogTraceServices, Log, TEXT("TaskCreated(TaskId %d, Timestamp %d) skipped"), TaskId, Timestamp);
			return;
		}

		checkf(Task->CreatedTimestamp == FTaskInfo::InvalidTimestamp, TEXT("%d"), TaskId);

		Task->Id = TaskId;
		Task->CreatedTimestamp = Timestamp;
		Task->CreatedThreadId = ThreadId;
	}

	void FTasksProvider::TaskLaunched(TaskTrace::FId TaskId, const TCHAR* DebugName, bool bTracked, int32 ThreadToExecuteOn, double Timestamp, uint32 ThreadId)
	{
		UE_LOG(LogTraceServices, Verbose, TEXT("TaskLaunched(TaskId: %d, DebugName: %s, bTracked: %d, Timestamp %.6f)"), TaskId, DebugName, bTracked, Timestamp);

		InitTaskIdToIndexConversion(TaskId);

		FTaskInfo* Task = TryGetOrCreateTask(TaskId);
		if (Task == nullptr)
		{
			UE_LOG(LogTraceServices, Log, TEXT("TaskLaunched(TaskId %d, DebugName %s, bTracked %d, Timestamp %.6f) skipped"), TaskId, DebugName, bTracked, Timestamp);
			return;
		}

		checkf(Task->LaunchedTimestamp == FTaskInfo::InvalidTimestamp, TEXT("%d"), TaskId);
			
		if (Task->Id == TaskTrace::InvalidId) // created and launched in one go
		{
			Task->Id = TaskId;
			Task->CreatedTimestamp = Timestamp;
			Task->CreatedThreadId = ThreadId;
		}

		Task->DebugName = DebugName;
		Task->bTracked = bTracked;
		Task->ThreadToExecuteOn = ThreadToExecuteOn;
		Task->LaunchedTimestamp = Timestamp;
		Task->LaunchedThreadId = ThreadId;

		WaitingForPrerequisitesTasksCounter->SetValue(Timestamp, ++WaitingForPrerequisitesTasksNum);
	}

	void FTasksProvider::TaskScheduled(TaskTrace::FId TaskId, double Timestamp, uint32 ThreadId)
	{
		if (!TryRegisterEvent(TEXT("TaskScheduled"), TaskId, &FTaskInfo::ScheduledTimestamp, Timestamp, &FTaskInfo::ScheduledThreadId, ThreadId))
		{
			return;
		}

		WaitingForPrerequisitesTasksCounter->SetValue(Timestamp, --WaitingForPrerequisitesTasksNum);
		if (IsNamedThread(TryGetTask(TaskId)->ThreadToExecuteOn))
		{
			NamedThreadsScheduledTasksCounter->SetValue(Timestamp, ++ScheduledTasksNum);
		}
		else
		{
			ScheduledTasksCounter->SetValue(Timestamp, ++ScheduledTasksNum);
		}
	}

	void FTasksProvider::SubsequentAdded(TaskTrace::FId TaskId, TaskTrace::FId SubsequentId, double Timestamp, uint32 ThreadId)
	{
		// when FGraphEvent is used to wait for a notification, it doesn't have an associated task and so is not created or launched. 
		// In this case we need to create it and initialise it before registering the completion event
		FTaskInfo* Task = TryGetOrCreateTask(TaskId);
		if (Task == nullptr)
		{
			UE_LOG(LogTraceServices, Log, TEXT("SubsequentAdded(TaskId %d, SubsequentId %d, Timestamp %.6f) skipped"), TaskId, SubsequentId, Timestamp);
			return;
		}

		Task->Id = TaskId;

		AddRelative(TEXT("Subsequent"), TaskId, &FTaskInfo::Subsequents, SubsequentId, Timestamp, ThreadId);
	}

	void FTasksProvider::TaskStarted(TaskTrace::FId TaskId, double Timestamp, uint32 ThreadId)
	{
		if (!TryRegisterEvent(TEXT("TaskStarted"), TaskId, &FTaskInfo::StartedTimestamp, Timestamp, &FTaskInfo::StartedThreadId, ThreadId))
		{
			return;
		}

		ExecutionThreads.FindOrAdd(ThreadId).Add(TaskId);

		FTaskInfo* Task = TryGetTask(TaskId);

		if (IsNamedThread(Task->ThreadToExecuteOn))
		{
			NamedThreadsScheduledTasksCounter->SetValue(Timestamp, --ScheduledTasksNum);
		}
		else
		{
			ScheduledTasksCounter->SetValue(Timestamp, --ScheduledTasksNum);
		}
		RunningTasksCounter->SetValue(Timestamp, ++RunningTasksNum);

		double LatencyMicrosecs = Task->StartedTimestamp * 1000000 - Task->ScheduledTimestamp * 1000000;
		TaskLatencyCounter->SetValue(Timestamp, LatencyMicrosecs);
	}

	void FTasksProvider::NestedAdded(TaskTrace::FId TaskId, TaskTrace::FId NestedId, double Timestamp, uint32 ThreadId)
	{
		AddRelative(TEXT("Nested"), TaskId, &FTaskInfo::NestedTasks, NestedId, Timestamp, ThreadId);
	}

	void FTasksProvider::TaskFinished(TaskTrace::FId TaskId, double Timestamp)
	{
		if (!TryRegisterEvent(TEXT("TaskFinished"), TaskId, &FTaskInfo::FinishedTimestamp, Timestamp))
		{
			return;
		}

		RunningTasksCounter->SetValue(Timestamp, --RunningTasksNum);
	}

	void FTasksProvider::TaskCompleted(TaskTrace::FId TaskId, double Timestamp, uint32 ThreadId)
	{
		// when FGraphEvent is used to wait for a notification, it doesn't have an associated task and so is not created or launched. 
		// In this case we need to create it and initialise it before registering the completion event
		FTaskInfo* Task = TryGetOrCreateTask(TaskId);
		if (Task == nullptr)
		{
			UE_LOG(LogTraceServices, Log, TEXT("TaskCompleted(TaskId %d, Timestamp %.6f) skipped"), TaskId, Timestamp);
			return;
		}

		Task->Id = TaskId;

		TryRegisterEvent(TEXT("TaskCompleted"), TaskId, &FTaskInfo::CompletedTimestamp, Timestamp, &FTaskInfo::CompletedThreadId, ThreadId);
	}

	void FTasksProvider::WaitingStarted(TArray<TaskTrace::FId> InTasks, double Timestamp, uint32 ThreadId)
	{
		FWaitingForTasks Waiting;
		Waiting.Tasks = MoveTemp(InTasks);
		Waiting.StartedTimestamp = Timestamp;

		WaitingThreads.FindOrAdd(ThreadId).Add(MoveTemp(Waiting));
	}

	void FTasksProvider::WaitingFinished(double Timestamp, uint32 ThreadId)
	{
		TArray64<FWaitingForTasks>* Thread = WaitingThreads.Find(ThreadId);
		checkf(Thread != nullptr, TEXT("%d"), ThreadId);

		Thread->Last().FinishedTimestamp = Timestamp;
	}

	void FTasksProvider::InitTaskIdToIndexConversion(TaskTrace::FId InFirstTaskId)
	{
		check(InFirstTaskId != TaskTrace::InvalidId);
		if (FirstTaskId == TaskTrace::InvalidId)
		{
			FirstTaskId = InFirstTaskId;
		}
	}

	int64 FTasksProvider::GetTaskIndex(TaskTrace::FId TaskId) const
	{
		return (int64)TaskId - FirstTaskId;
	}

	const FTaskInfo* FTasksProvider::TryGetTask(TaskTrace::FId TaskId) const
	{
		check(TaskId != TaskTrace::InvalidId);
		int64 TaskIndex = GetTaskIndex(TaskId);
		return Tasks.IsValidIndex(TaskIndex) ? &Tasks[TaskIndex] : nullptr;
	}

	FTaskInfo* FTasksProvider::TryGetTask(TaskTrace::FId TaskId)
	{
		return const_cast<FTaskInfo*>(const_cast<const FTasksProvider*>(this)->TryGetTask(TaskId)); // reuse the const version
	}

	FTaskInfo* FTasksProvider::TryGetOrCreateTask(TaskTrace::FId TaskId)
	{
		int64 TaskIndex = GetTaskIndex(TaskId);
		// traces can race, it's possible a trace with `TaskId = X` can come first, initialize `FirstTaskId` and only then a trace with 
		// `TaskId = X - 1` arrives. This will produce `TaskIndex < 0`. Such traces can happen only at the very beginning of the capture 
		// and are ignored
		if (TaskIndex < 0)
		{
			return nullptr;
		}

		if (TaskIndex >= Tasks.Num())
		{
			Tasks.AddDefaulted(TaskIndex - Tasks.Num() + 1);
		}

		return &Tasks[TaskIndex];
	}

	bool FTasksProvider::IsNamedThread(int32 Thread)
	{
		return ENamedThreads::GetThreadIndex((ENamedThreads::Type)Thread) != ENamedThreads::AnyThread;
	}

	bool FTasksProvider::TryRegisterEvent(const TCHAR* EventName, TaskTrace::FId TaskId, double FTaskInfo::* TimestampPtr, double TimestampValue, uint32 FTaskInfo::* ThreadIdPtr/* = nullptr*/, uint32 ThreadIdValue/* = 0*/)
	{
		UE_LOG(LogTraceServices, Verbose, TEXT("%s(TaskId: %d, Timestamp %.6f)"), EventName, TaskId, TimestampValue);

		FTaskInfo* Task = TryGetTask(TaskId);
		if (Task == nullptr)
		{
			UE_LOG(LogTraceServices, Log, TEXT("%s(TaskId %d, Timestamp %.6f) skipped"), EventName, TaskId, TimestampValue);
			return false;
		}

		checkf(Task->*TimestampPtr == FTaskInfo::InvalidTimestamp, TEXT("TaskId %d, old TS %.6f, new TS %.6f"), TaskId, Task->*TimestampPtr, TimestampValue);
		Task->*TimestampPtr = TimestampValue;
		if (ThreadIdPtr != nullptr)
		{
			Task->*ThreadIdPtr = ThreadIdValue;
		}

		return true;
	}

	void FTasksProvider::AddRelative(const TCHAR* RelationType, TaskTrace::FId TaskId, TArray<FTaskInfo::FRelationInfo> FTaskInfo::* RelationsPtr, TaskTrace::FId RelativeId, double Timestamp, uint32 ThreadId)
	{
		UE_LOG(LogTraceServices, Verbose, TEXT("%s (%d) added to TaskId: %d, Timestamp %.6f)"), RelationType, RelativeId, TaskId, Timestamp);

		FTaskInfo* Task = TryGetTask(TaskId);
		if (Task == nullptr)
		{
			UE_LOG(LogTraceServices, Log, TEXT("Add%s(TaskId %d, OtherId: %d, Timestamp %.6f) skipped"), RelationType, TaskId, RelativeId, Timestamp);
			return;
		}

		(Task->*RelationsPtr).Emplace(RelativeId, Timestamp, ThreadId);
	}

	/////////////////////////////////////////////////////////////////////////////////
	// ITasksProvider impl

	const FTaskInfo* FTasksProvider::TryGetTask(uint32 ThreadId, double Timestamp) const
	{
		const TArray64<TaskTrace::FId>* Thread = ExecutionThreads.Find(ThreadId);
		if (Thread == nullptr)
		{
			return nullptr;
		}

		int64 NextTaskIndex = Algo::LowerBound(*Thread, Timestamp, 
			[this](TaskTrace::FId TaskId, double Timestamp)
			{
				const FTaskInfo* Task = TryGetTask(TaskId);
				return Task != nullptr && Task->StartedTimestamp <= Timestamp;
			}
		);

		if (NextTaskIndex == 0)
		{
			return nullptr;
		}

		TaskTrace::FId TaskId = (*Thread)[NextTaskIndex - 1];
		const FTaskInfo* Task = TryGetTask(TaskId);
		return Task != nullptr && Task->FinishedTimestamp > Timestamp ? Task : nullptr;
	}

	const FWaitingForTasks* FTasksProvider::TryGetWaiting(uint32 ThreadId, double Timestamp) const
	{
		const TArray64<FWaitingForTasks>* Thread = WaitingThreads.Find(ThreadId);
		if (Thread == nullptr)
		{
			return nullptr;
		}

		int64 NextWaitingIndex = Algo::LowerBound(*Thread, Timestamp,
			[this](const FWaitingForTasks& Waiting, double Timestamp)
			{
				return Waiting.StartedTimestamp <= Timestamp;
			}
		);

		if (NextWaitingIndex == 0)
		{
			return nullptr;
		}

		const FWaitingForTasks& Waiting = (*Thread)[NextWaitingIndex - 1];
		return Waiting.FinishedTimestamp > Timestamp || Waiting.FinishedTimestamp == FTaskInfo::InvalidTimestamp ? &Waiting : nullptr;
	}
}
