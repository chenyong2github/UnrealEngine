// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tasks/TaskPrivate.h"
#include "Async/Fundamental/Task.h"
#include "Templates/RefCounting.h"
#include "Containers/StaticArray.h"
#include "CoreTypes.h"

namespace UE { namespace Tasks
{
	// a common part of the generic `TTask<ResultType>` and its `TTask<void>` specialisation
	template<typename ResultType>
	class TTaskBase
	{
		friend Private::FTaskBase;

		template<typename... TaskTypes>
		friend class TPrerequisites;

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

		// waits for task's completion
		void Wait()
		{
			if (IsValid())
			{
				Pimpl->Wait();
			}
		}

		// waits for task's completion at least specified amount of time.
		// the call can return much later than the given timeout
		// @return true if the task is completed
		bool Wait(FTimespan Timeout)
		{
			return !IsValid() || Pimpl->Wait(Timeout);
		}

		// waits for task's completion or the given condition becomes true
		// the call can return much later than the given condition became true
		// @return true if the task is completed
		template<typename ConditionType>
		bool Wait(ConditionType&& Condition)
		{
			return !IsValid() || Pimpl->Wait(Forward<ConditionType>(Condition));
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

	template<typename TaskCollectionType>
	void Wait(TaskCollectionType&& Tasks)
	{
		for (auto& Task : Tasks)
		{
			Task.Wait();
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
