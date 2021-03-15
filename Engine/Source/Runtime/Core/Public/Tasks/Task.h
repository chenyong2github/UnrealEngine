// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tasks/TaskPrivate.h"
#include "Async/Fundamental/Task.h"
#include "CoreTypes.h"

namespace UE { namespace Tasks
{
	// a common part of the generic `TTask<ResultType>` and its `TTask<void>` specialisation
	template<typename ResultType>
	class TTaskBase
	{
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

		friend FPipe;

		// private constructor, valid instances can be created only by launching (see friends)
		explicit TTask(Private::TTaskWithResult<ResultType>* Other)
			: FSuper(Other)
		{}
	};

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

		friend FPipe;

		// private constructor, valid instances can be created only by launching (see friends)
		explicit TTask(Private::TTaskWithResult<void>* Other)
			: TTaskBase<void>(Other)
		{}
	};

	// a synchronisation primitive similar to `FEvent` that uses "busy waiting" - executing tasks while waiting
	class FTaskEvent
	{
	public:
		UE_NONCOPYABLE(FTaskEvent)

		FTaskEvent() = default;

		void Trigger()
		{
			bTriggered.store(true, std::memory_order_release);
		}

		bool IsTriggered() const
		{
			return bTriggered.load(std::memory_order_acquire);
		}

		// waits until the event is triggered
		void Wait()
		{
			LowLevelTasks::BusyWaitUntil([this] { return bTriggered.load(std::memory_order_acquire); });
		}

		// waits until the event is triggered or the waiting timed out.
		// the call can return much later than the given timeout
		// @return true if the event was triggered
		bool Wait(FTimespan InTimeout)
		{
			LowLevelTasks::BusyWaitUntil(
				[this, Timeout = FTimeout{ InTimeout }] { return bTriggered.load(std::memory_order_acquire) || Timeout; }
			);
			return bTriggered.load(std::memory_order_relaxed);
		}

		// waits until the event is triggered or the given condition returns true.
		// the call can return much later than the condition has become true
		// @return true if the event was triggered
		template<typename ConditionType>
		bool Wait(ConditionType&& Condition)
		{
			LowLevelTasks::BusyWaitUntil(
				[this, Condition = Forward<ConditionType>(Condition)] { return bTriggered.load(std::memory_order_acquire) || Condition(); }
			);
			return bTriggered.load(std::memory_order_relaxed);
		}

	private:
		std::atomic<bool> bTriggered{ false };
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
}}