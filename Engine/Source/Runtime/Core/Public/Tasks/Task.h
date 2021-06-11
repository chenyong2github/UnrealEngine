// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tasks/TaskPrivate.h"
#include "Async/Fundamental/Task.h"
#include "Templates/RefCounting.h"
#include "Containers/StaticArray.h"
#include "HAL/Event.h"
#include "CoreTypes.h"

namespace UE { namespace Tasks
{
	template<typename ResultType>
	class TTask;

	template<typename TaskBodyType>
	TTask<TInvokeResult_T<TaskBodyType>> Launch(const TCHAR* DebugName, TaskBodyType&& TaskBody, LowLevelTasks::ETaskPriority Priority = LowLevelTasks::ETaskPriority::Normal);

	template<typename TaskBodyType, typename PrerequisitesCollectionType>
	TTask<TInvokeResult_T<TaskBodyType>> Launch(const TCHAR* DebugName, TaskBodyType&& TaskBody, PrerequisitesCollectionType&& Prerequisites, LowLevelTasks::ETaskPriority Priority = LowLevelTasks::ETaskPriority::Normal);

	// a common part of the generic `TTask<ResultType>` and its `TTask<void>` specialisation
	template<typename ResultType>
	class TTaskBase
	{
		friend Private::FTaskBase;

		template<typename... TaskTypes>
		friend class TPrerequisites;

		template<typename TaskCollectionType>
		friend bool Private::TryRetractAndExecute(TaskCollectionType&& Tasks);

	protected:
		TTaskBase() = default;

		explicit TTaskBase(Private::TTaskWithResult<ResultType>* Other)
			: Pimpl(Other, /*bAddRef = */false)
		{}

	public:
		bool IsValid() const
		{
			return Pimpl.IsValid();
		}

		// checks if task's execution is done
		bool IsCompleted() const
		{
			return !IsValid() || Pimpl->IsCompleted();
		}

		// waits for task's completion, with optional timeout
		// @return true if the task is completed
		bool Wait(FTimespan Timeout = FTimespan::MaxValue())
		{
			if (!IsValid() || IsCompleted())
			{
				return true;
			}

			// "zero timeout" - just check if the task is completed
			if (Timeout == FTimespan::Zero())
			{
				return false;
			}

			// waiting with timeout can't do retraction, otherwise timeout value is unreliable
			if (Timeout == FTimespan::MaxValue() && Pimpl->TryRetractAndExecute())
			{
				return true;
			}

			// the event must be alive for the task and this function lifetime, we don't know which one will be finished first as waiting can time out
			// before the waiting task is completed
			FSharedEventRef CompletionEvent;

			TRefCountPtr<Private::TTaskWithResult<void>> WaitingTask{ new Private::TTaskWithResult<void>, /*bAddRef=*/ false };
			WaitingTask->Init(TEXT("Waiting Task"), [CompletionEvent] { CompletionEvent->Trigger(); }, Private::FTaskBase::InlineTaskPriority);
			WaitingTask->AddPrerequisites(*this);

			if (WaitingTask->TryLaunch())
			{	// was executed inline
				check(WaitingTask->IsCompleted());
				return true;
			}

			return CompletionEvent->Wait(Timeout);
		}

		// waits for task's completion while executing other tasks. Shouldn't be used inside a latency-sensitive task
		void BusyWait()
		{
			if (IsValid())
			{
				Pimpl->BusyWait();
			}
		}

		// waits for task's completion at least specified amount of time, while executing other tasks.
		// the call can return much later than the given timeout
		// @return true if the task is completed
		bool BusyWait(FTimespan Timeout)
		{
			return !IsValid() || Pimpl->BusyWait(Timeout);
		}

		// waits for task's completion or the given condition becomes true, while executing other tasks.
		// the call can return much later than the given condition became true
		// @return true if the task is completed
		template<typename ConditionType>
		bool BusyWait(ConditionType&& Condition)
		{
			return !IsValid() || Pimpl->BusyWait(Forward<ConditionType>(Condition));
		}

	protected:
		TRefCountPtr<Private::TTaskWithResult<ResultType>> Pimpl;
	};

	// a movable/copyable handle of `Private::TTaskWithResult*` with the API adopted for public usage.
	// implements Pimpl idiom
	template<typename ResultType>
	class TTask : public TTaskBase<ResultType>
	{
		using FSuper = TTaskBase<ResultType>;

	public:
		TTask() = default;

		// waits until the task is completed and returns task's result
		ResultType& GetResult()
		{
			check(FSuper::IsValid());
			FSuper::Wait();
			return FSuper::Pimpl->GetResult();
		}

	private:
		template<typename TaskBodyType>
		friend TTask<TInvokeResult_T<TaskBodyType>> Launch(const TCHAR* DebugName, TaskBodyType&& TaskBody, LowLevelTasks::ETaskPriority Priority/* = LowLevelTasks::ETaskPriority::Normal*/);

		template<typename TaskBodyType, typename PrerequisitesCollectionType>
		friend TTask<TInvokeResult_T<TaskBodyType>> Launch(const TCHAR* DebugName, TaskBodyType&& TaskBody, PrerequisitesCollectionType&& Prerequisites, LowLevelTasks::ETaskPriority Priority/* = LowLevelTasks::ETaskPriority::Normal*/);

		friend FPipe;

		// private constructor, valid instances can be created only by launching (see friends)
		explicit TTask(Private::TTaskWithResult<ResultType>* Other)
			: FSuper(Other)
		{}
	};

	class FTaskEvent;

	template<>
	class TTask<void> : public TTaskBase<void>
	{
	public:
		TTask() = default;
	
		void GetResult()
		{
			check(IsValid()); // to be consistent with a generic `TTask<ResultType>::GetResult()`
			Wait();
		}

	private:
		template<typename TaskBodyType>
		friend TTask<TInvokeResult_T<TaskBodyType>> Launch(const TCHAR* DebugName, TaskBodyType&& TaskBody, LowLevelTasks::ETaskPriority Priority/* = LowLevelTasks::ETaskPriority::Normal*/);

		template<typename TaskBodyType, typename PrerequisitesCollectionType>
		friend TTask<TInvokeResult_T<TaskBodyType>> Launch(const TCHAR* DebugName, TaskBodyType&& TaskBody, PrerequisitesCollectionType&& Prerequisites, LowLevelTasks::ETaskPriority Priority/* = LowLevelTasks::ETaskPriority::Normal*/);

		friend FPipe;
		friend FTaskEvent;

		// private constructor, valid instances can be created only by launching (see friends)
		explicit TTask(Private::TTaskWithResult<void>* Other)
			: TTaskBase<void>(Other)
		{}
	};

	// a synchronisation primitive similar to `FEvent` that uses "busy waiting" - executing tasks while waiting
	class FTaskEvent : public TTask<void>
	{
	public:
		explicit FTaskEvent(const TCHAR* DebugName)
			: TTask<void>(new Private::TTaskWithResult<void>)
		{
			Pimpl->Init(DebugName, [] {}, Private::FTaskBase::InlineTaskPriority);
		}

		void Trigger()
		{
			verify(Pimpl->TryLaunch());
		}
	};

	// launches a task for asynchronous execution
	// @param DebugName - a unique name for task identification in debugger and profiler, is compiled out in test/shipping builds
	// @param TaskBody - a functor that will be executed asynchronously
	// @param Priority - task priority that affects when the task will be executed
	// @return a trivially relocatable instance that can be used to wait for task completion or to obtain task execution result
	template<typename TaskBodyType>
	TTask<TInvokeResult_T<TaskBodyType>> Launch(
		const TCHAR* DebugName,
		TaskBodyType&& TaskBody,
		LowLevelTasks::ETaskPriority Priority/* = LowLevelTasks::ETaskPriority::Normal*/
	)
	{
		using FResult = TInvokeResult_T<TaskBodyType>;
		auto* Task{ new Private::TTaskWithResult<FResult> };
		Task->Init(DebugName, Forward<TaskBodyType>(TaskBody), Priority);
		Task->TryLaunch();
		return TTask<FResult>{ Task };
	}

	// launches a task for asynchronous execution, with prerequisites that must be completed before the task is scheduled
	// @param DebugName - a unique name for task identification in debugger and profiler, is compiled out in test/shipping builds
	// @param TaskBody - a functor that will be executed asynchronously
	// @param Prerequisites - tasks or task events that must be completed before the task being launched can be scheduled, accepts any 
	// iterable collection (.begin()/.end()), `Tasks::Prerequisites()` helper is recommended to create such collection on the fly
	// @param Priority - task priority that affects when the task will be executed
	// @return a trivially relocatable instance that can be used to wait for task completion or to obtain task execution result
	template<typename TaskBodyType, typename PrerequisitesCollectionType>
	TTask<TInvokeResult_T<TaskBodyType>> Launch(
		const TCHAR* DebugName,
		TaskBodyType&& TaskBody,
		PrerequisitesCollectionType&& Prerequisites,
		LowLevelTasks::ETaskPriority Priority/* = LowLevelTasks::ETaskPriority::Normal*/
	)
	{
		using FResult = TInvokeResult_T<TaskBodyType>;
		auto* Task{ new Private::TTaskWithResult<FResult> };
		Task->Init(DebugName, Forward<TaskBodyType>(TaskBody), Priority);
		Task->AddPrerequisites(Forward<PrerequisitesCollectionType>(Prerequisites));
		Task->TryLaunch();
		return TTask<FResult>{ Task };
	}

	// wait for multiple tasks, with optional timeout
	template<typename TaskCollectionType>
	bool Wait(TaskCollectionType&& Tasks, FTimespan Timeout = FTimespan::MaxValue())
	{
		// waiting with timeout can't do retraction, otherwise timeout value is unreliable
		if (Timeout == FTimespan::MaxValue() && Private::TryRetractAndExecute(Tasks))
		{
			return true;
		}

		// the event must be alive for the task and this function lifetime, we don't know which one will be finished first as waiting can time out
		// before the waiting task is completed
		FSharedEventRef CompletionEvent;

		TRefCountPtr<Private::TTaskWithResult<void>> WaitingTask{ new Private::TTaskWithResult<void>, /*bAddRef=*/ false };
		WaitingTask->Init(TEXT("Waiting Task"), [CompletionEvent] { CompletionEvent->Trigger(); }, Private::FTaskBase::InlineTaskPriority);
		WaitingTask->AddPrerequisites(Tasks);

		if (WaitingTask->TryLaunch())
		{	// was executed inline
			check(WaitingTask->IsCompleted());
			return true;
		}

		return CompletionEvent->Wait(Timeout);
	}

	// wait for multiple tasks while executing other tasks
	template<typename TaskCollectionType>
	void BusyWait(TaskCollectionType&& Tasks)
	{
		for (auto& Task : Tasks)
		{
			Task.BusyWait();
		}
	}

	using FTask = TTask<void>;

	// a convenient proxy collection for specifying task prerequisites that can include both tasks and task events
	// usage: Launch(UE_SOURCE_LOCATION, [] {}, Prerequisites(Task1, Task2, TaskEvent1, ...));
	template<typename... TaskTypes>
	class TPrerequisites : public TStaticArray<Private::FTaskBase*, sizeof...(TaskTypes)>
	{
	public:
		TPrerequisites(TaskTypes&&... Tasks)
		{
			Fill(0, Forward<TaskTypes>(Tasks)...);
		}

	private:
		template<typename FirstTaskType, typename... OtherTaskTypes>
		void Fill(uint32 Index, FirstTaskType&& FirstTask, OtherTaskTypes&&... OtherTasks)
		{
			(*this)[Index] = FirstTask.Pimpl.GetReference();
			Fill(Index + 1, Forward<OtherTaskTypes>(OtherTasks)...);
		}

		template<typename TaskType>
		void Fill(uint32 Index, TaskType&& Task)
		{
			(*this)[Index] = Task.Pimpl.GetReference();
		}
	};

	template<typename... TaskTypes>
	TPrerequisites<TaskTypes...> Prerequisites(TaskTypes&... Tasks)
	{
		return TPrerequisites<TaskTypes...>{ Forward<TaskTypes>(Tasks)... };
	}
}}

template <typename... TaskTypes>
struct TIsContiguousContainer<UE::Tasks::TPrerequisites<TaskTypes...>>
{
	static constexpr bool Value = true;
};
