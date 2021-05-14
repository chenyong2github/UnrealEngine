// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/Fundamental/Task.h"
#include "Async/Fundamental/Scheduler.h"
#include "Async/TaskTrace.h"
#include "Templates/Invoke.h"
#include "Templates/TypeCompatibleBytes.h"
#include "Misc/Timeout.h"
#include "Containers/ClosableMpscQueue.h"
#include "Templates/UnrealTemplate.h"
#include "CoreTypes.h"

#include <atomic>
#include <type_traits>

namespace UE { namespace Tasks
{
	using LowLevelTasks::ETaskPriority;

	// BEGIN: forward declarations

	class FTaskEvent;
	class FPipe;

	template<typename T>
	class TTask;

	template<typename TaskBodyType>
	TTask<TInvokeResult_T<TaskBodyType>> Launch(
		const TCHAR* DebugName, 
		TaskBodyType&& TaskBody, 
		LowLevelTasks::ETaskPriority Priority = LowLevelTasks::ETaskPriority::Normal
	);

	template<typename TaskBodyType, typename PrerequisitesCollectionType>
	TTask<TInvokeResult_T<TaskBodyType>> Launch(
		const TCHAR* DebugName, 
		TaskBodyType&& TaskBody, 
		PrerequisitesCollectionType&& Prerequisites, 
		LowLevelTasks::ETaskPriority Priority = LowLevelTasks::ETaskPriority::Normal
	);

	template<typename ResultType>
	class TTaskBase;

	template<typename... TaskTypes>
	class TPrerequisites;

	// END: forward declarations

	namespace Private
	{
		// Task class implementation with piping support.
		class FTaskBase
		{
			UE_NONCOPYABLE(FTaskBase);

			// Can be used only as a base class.
		protected:
			FTaskBase() = default;

			~FTaskBase()
			{
				check(IsCompleted());
			}

		public:
			// a special internal task priority for "inline" task execution - a task is executed as soon as it's launched and has no 
			// pending dependencies, "inline", w/o scheduling
			static const ETaskPriority InlineTaskPriority{ ETaskPriority::Count };
			
			// initialises the task but doesn't launches it
			template<typename TaskBodyType, typename DeleterType>
			void Init(const TCHAR* DebugName, TaskBodyType&& TaskBody, DeleterType&& Deleter, LowLevelTasks::ETaskPriority Priority)
			{
				// to not store a copy of `TaskBody` that is stored inside `LowLevelTask` anyway, and to implement task retraction
				// (see `Wait()` method), we have to use LowLevelTask's "Continuation" parameter instead of "Runnable". This way we can make
				// the scheduler immediately execute retracted task.
				LowLevelTask.Init(DebugName, Priority, 
					[] {}, // Runnable
					[
						this, 
						TaskBody = Forward<TaskBodyType>(TaskBody), 
						// releasing scheduler's task reference can cause task's automatic destruction and so must be done after the task is 
						// flagged as completed. The task is flagged as completed after the continuation is executed but before its destroyed.
						// `Deleter` is captured by value and is destroyed along with the continuation, calling the given functor 
						// on destruction
						Deleter = Forward<DeleterType>(Deleter)
					]() mutable
					{
						Execute(MoveTemp(TaskBody));

					} // Continuation
				);
			}

			// the task will be executed only when all prerequisites are completed
			template<typename T>
			void AddPrerequisites(const TTaskBase<T>& Prerequisite)
			{
				checkf(NumLocks.load(std::memory_order_relaxed) >= NumInitialLocks, TEXT("Prerequisites can be added only before the task is launched"));

				// registering the task as a subsequent of the given prerequisite can cause its immediate launch by the prerequisite
				// (if the prerequisite has been completed on another thread), so we need to keep the task locked by assuming that the 
				// prerequisite can be added successfully, and release the lock if it wasn't
				NumLocks.fetch_add(1, std::memory_order_acquire); // `acquire` to make it happen before the task is registered as a subsequent

				if (!Prerequisite.Pimpl->AddSubsequent(*this))
				{
					// failed to add the prerequisite (too late), correct the number
					NumLocks.fetch_sub(1, std::memory_order_release); // `release` to make it happen after the task is registered as a subsequent
				}
			}

			// the task will be executed only when all prerequisites are completed
			// @param Prerequisites - an iterable collection of tasks
			template<typename PrerequisiteCollectionType, decltype(std::declval<PrerequisiteCollectionType&>().begin())* = nullptr>
			void AddPrerequisites(const PrerequisiteCollectionType& Prerequisites)
			{
				checkf(NumLocks.load(std::memory_order_relaxed) >= NumInitialLocks, TEXT("Prerequisites can be added only before the task is launched"));

				// registering the task as a subsequent of the given prerequisite can cause its immediate launch by the prerequisite
				// (if the prerequisite has been completed on another thread), so we need to keep the task locked by assuming that the 
				// prerequisite can be added successfully, and release the lock if it wasn't
				NumLocks.fetch_add(GetNum(Prerequisites), std::memory_order_acquire); // `acquire` to make it happen before the task is registered as a subsequent

				uint32 NumCompletedPrerequisites = 0;
				for (auto& Prereq : Prerequisites)
				{
					// prerequisites can be either `FTaskBase*` or its Pimpl handle
					FTaskBase* Prerequisite;
					using FPrerequisiteType = std::decay_t<decltype(*std::declval<PrerequisiteCollectionType&>().begin())>;
					if constexpr (std::is_same_v<FPrerequisiteType, FTaskBase*>)
					{
						Prerequisite = Prereq;
					}
					else
					{
						Prerequisite = Prereq.Pimpl;
					}

					if (!Prerequisite->AddSubsequent(*this))
					{
						++NumCompletedPrerequisites;
					}
				}

				// unlock for prerequisites that weren't added
				NumLocks.fetch_sub(NumCompletedPrerequisites, std::memory_order_release); // `release` to make it happen after the task is registered as a subsequent
			}

			// the task unlocks all its subsequents on completion.
			// return false if the task is already completed
			bool AddSubsequent(FTaskBase& Subsequent)
			{
				return Subsequents.Enqueue(&Subsequent);
			}

			// A piped task will be executed after the previous task from this pipe is completed. Tasks from the same pipe are not executed
			// concurrently (so don't require synchronization), but not necessarily on the same thread.
			// @See FPipe
			void SetPipe(FPipe& InPipe)
			{
				Pipe = &InPipe;
			}

			FPipe* GetPipe() const
			{
				return Pipe;
			}

			// Tasks w/o incomplete prerequisites and not blocked by a pipe are scheduled immediately
			bool TryLaunch()
			{
				TaskTrace::Launched(GetTraceId(), LowLevelTask.GetDebugName(), true, (ENamedThreads::Type)0xff);
				return TryUnlock();
			}

			// if the task execution is already completed
			bool IsCompleted() const
			{
				return Subsequents.IsClosed();
			}

			// waits until the task is completed
			void Wait()
			{
				// we try to retract the task from the scheduler, if it's execution hasn't started yet, to execute it immediately. This makes
				// the task skip waiting in the scheduler's queue.
				// we can't retract it right here because it can be not scheduled yet (e.g. when it's blocked by a pipe), so we need to wait for this
				// `!IsReady()` here means "the task is scheduled". The task can be completed w/o being scheduled at all (e.g. if it has 
				// an empty body) so we need to check for this too
				LowLevelTasks::BusyWaitUntil([this] { return !LowLevelTask.IsReady(); });
				LowLevelTask.TryCancel(); //This will try to cancel the task and run it's continuation (thereby executing the workload) 
				LowLevelTasks::BusyWaitUntil([this] { return IsCompleted(); });
			}

			// waits until the task is completed or waiting timed out
			// @see `void Wait()`
			bool Wait(FTimespan InTimeout)
			{
				FTimeout Timeout{ InTimeout };
				LowLevelTasks::BusyWaitUntil([this, Timeout] { return !LowLevelTask.IsReady() || Timeout; });

				if (LowLevelTask.IsReady()) // timeout
				{
					return false;
				}

				LowLevelTask.TryCancel(); //This will try to cancel the task and run it's continuation (thereby executing the workload) 
				LowLevelTasks::BusyWaitUntil([this, Timeout] { return IsCompleted() || Timeout; });
				return IsCompleted();
			}

			// waits until the task is completed or the condition returns true
			// @see `void Wait()`
			template<typename ConditionType>
			bool Wait(ConditionType&& Condition)
			{
				LowLevelTasks::BusyWaitUntil(
					[this, Condition = Forward<ConditionType>(Condition)] { return !LowLevelTask.IsReady() || Condition(); }
				);

				if (LowLevelTask.IsReady()) // `Condition` returned true
				{
					return false;
				}

				LowLevelTask.TryCancel(); //This will try to cancel the task and run it's continuation (thereby executing the workload) 
				LowLevelTasks::BusyWaitUntil(
					[this, Condition = Forward<ConditionType>(Condition)] { return IsCompleted() || Condition(); }
				);
				return IsCompleted();
			}

			TaskTrace::FId GetTraceId() const
			{
#if UE_TASK_TRACE_ENABLED
				return TraceId;
#else
				return TaskTrace::InvalidId;
#endif
			}

		private:
			// allows the task to be scheduled and executed, once for each lock. it's called on actual launching, on every prerequisite 
			// completion and when an associated pipe (if any) is unblocked
			bool TryUnlock()
			{
				uint32 LocalNumLocks = NumLocks.fetch_sub(1, std::memory_order_acq_rel) - 1; // `acq_rel` to make it happen after task 
				// preparation and before launching it
				const bool bReadyForPiping = LocalNumLocks <= 1; // the only lock that can remain is pipe's one
				if (!bReadyForPiping)
				{
					return false;
				}

				if (!TryPushIntoPipe(LocalNumLocks))
				{
					return false; // pipe is blocked
				}

				TaskTrace::Scheduled(GetTraceId());

				// "inline" tasks are not scheduled but executed as soon as they are unlocked
				if (LowLevelTask.GetPriority() == InlineTaskPriority)
				{
					// the task is not scheduled yet, so successful retraction is guaranted
					verify(LowLevelTask.TryCancel()); // this will cancel the task for the scheduler and execute LowLevelTask's continuation, 
					// which is actually task execution
					return true;
				}

				// schedule
				return LowLevelTasks::TryLaunch(LowLevelTask);
			}

			template<typename TaskBodyType>
			void Execute(TaskBodyType&& TaskBody)
			{
				TaskTrace::Started(GetTraceId());
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(ExecuteTask);
					StartPipeExecution();
					Invoke(TaskBody);
					FinishPipeExecution();
				}
				TaskTrace::Finished(GetTraceId());

				Close();
			}

			// checks if the task is ready to be launched by trying to push it into the pipe.
			// can be called up to two times: first to push into the blocked pipe and then when the pipe is unblocked
			// `LocalNumLocks` is the value that was used to make a decision to push into the pipe, no need to read `NumLocks` again
			bool TryPushIntoPipe(uint32 LocalNumLocks)
			{
				if (GetPipe() == nullptr)
				{
					// the task is locked for a pipe initially even if eventually there's no pipe
					check(LocalNumLocks == 1);
					// there's no need to set NumLocks to 0 in non-shipping builds as it's not used anymore, but we do this for debugging
					// as it's confusing to see scheduled tasks that are still locked
					check(NumLocks.exchange(0, std::memory_order_relaxed) == 1);

					return true;
				}

				// on the first call we try to push the task into the pipe. if unsuccessful (the pipe is blocked), the second time the method is called
				// only when the pipe is unblocked, so we know that the task is free to be executed

				bool bFirstAttempt = LocalNumLocks == 1;
				if (bFirstAttempt)
				{
					return PushIntoPipe();
				}

				return true;
			}

			CORE_API bool PushIntoPipe();
			CORE_API void StartPipeExecution();
			CORE_API void FinishPipeExecution();

			void Close()
			{
				Subsequents.Close(
					[this](FTaskBase* Subsequent)
					{
						Subsequent->TryUnlock();
					}
				);
			}

		private:
			LowLevelTasks::FTask LowLevelTask;

			// the number of times that the task should be unlocked before it can be scheduled
			// initial count is 1 for launching the task (it can't be scheduled before it's launched) and 1 for a potential blocked pipe
			static const uint32 NumInitialLocks = 1 + 1;
			std::atomic<uint32> NumLocks{ NumInitialLocks };
			TClosableMpscQueue<FTaskBase*> Subsequents;

			FPipe* Pipe{ nullptr };

#if UE_TASK_TRACE_ENABLED
			TaskTrace::FId TraceId = TaskTrace::GenerateTaskId();
#endif
		};

		// Extends FTaskBase by supporting execution result.
		template<typename ResultType>
		class TTaskWithResultBase : public FTaskBase
		{
			UE_NONCOPYABLE(TTaskWithResultBase);

			using FSelf = TTaskWithResultBase<ResultType>;

			// Can be used only as a base class
		protected:
			TTaskWithResultBase() = default;

			~TTaskWithResultBase()
			{
				// Every task instance must be launched. This is quaranteed by the the high level task API (`UE::Tasks::TTask`)
				checkf(bLaunched, TEXT("Every task instance must be launched"));
				DestructItem(ResultStorage.GetTypedPtr());
			}

		public:
			template<typename TaskBodyType, typename DeleterType>
			void Init(const TCHAR* DebugName, TaskBodyType&& TaskBody, DeleterType&& Deleter, LowLevelTasks::ETaskPriority Priority)
			{
				FTaskBase::Init(
					DebugName, 
					[this, TaskBody = Forward<TaskBodyType>(TaskBody)]() mutable
					{ 
						new(&ResultStorage) ResultType{ Invoke(TaskBody) }; 
					},
					Forward<DeleterType>(Deleter),
					Priority
				);
			}

			bool TryLaunch()
			{
#if DO_CHECK
				bLaunched = true;
#endif
				return FTaskBase::TryLaunch();
			}

			ResultType& GetResult()
			{
				check(bLaunched);
				Wait();

				return *ResultStorage.GetTypedPtr();
			}

		private:
			TTypeCompatibleBytes<ResultType> ResultStorage;

#if DO_CHECK
			bool bLaunched = false;
#endif
		};

		template<>
		class TTaskWithResultBase<void> : public FTaskBase
		{
		protected:
			TTaskWithResultBase() = default; // instances can be created only on the heap by a ref-counting handler

		public:
			void GetResult()
			{
				Wait();
			}
		};

		// intrusive atomic reference-counting base class.
		// doesn't require derived classes to be polymorphic
		template<typename ReferenceType>
		class TRefCountedBase
		{
		public:
			explicit TRefCountedBase(uint32 InitRefCount = 0)
				: RefCount{ InitRefCount }
			{
				static_assert(std::is_final<ReferenceType>::value, "ReferenceType should be `final` to guarantee no slicing during destruction");
			}

			void AddRef()
			{
				RefCount.fetch_add(1, std::memory_order_relaxed);
			}

			void Release()
			{
				uint32 LocalRefCount = RefCount.fetch_sub(1, std::memory_order_release) - 1;
				if (LocalRefCount == 0)
				{
					std::atomic_thread_fence(std::memory_order_acquire);
					delete static_cast<ReferenceType*>(this);
				}
			}

		private:
			std::atomic<uint32> RefCount;
		};

		// A final task class that must be used only with `TRefCountPtr`, thus only friends are allowed to construct instances.
		// @see UE::Tasks::Task<T>
		template<typename ResultType>
		class TTaskWithResult final : public TTaskWithResultBase<ResultType>, public TRefCountedBase<TTaskWithResult<ResultType>>
		{
			UE_NONCOPYABLE(TTaskWithResult);

			using FSelf = TTaskWithResult<ResultType>;

		private:
			template<typename TaskBodyType>
			friend TTask<TInvokeResult_T<TaskBodyType>> UE::Tasks::Launch(const TCHAR* DebugName, TaskBodyType&& TaskBody, LowLevelTasks::ETaskPriority Priority);

			template<typename TaskBodyType, typename PrerequisitesCollectionType>
			friend TTask<TInvokeResult_T<TaskBodyType>> UE::Tasks::Launch(const TCHAR* DebugName, TaskBodyType&& TaskBody, PrerequisitesCollectionType&& Prerequisites, LowLevelTasks::ETaskPriority Priority);

			friend FTaskEvent;
			friend FPipe;

			TTaskWithResult()
				: TRefCountedBase<FSelf>(2) // one for the initial reference (we don't increment it when passing to `TRefCountPtr`) and 
				// one for scheduler's reference which will be released on task completion
			{
			}

		public:
			template<typename TaskBodyType>
			void Init(const TCHAR* DebugName, TaskBodyType&& TaskBody, LowLevelTasks::ETaskPriority Priority)
			{
				TTaskWithResultBase<ResultType>::Init(
					DebugName,
					Forward<TaskBodyType>(TaskBody),
					LowLevelTasks::TDeleter<TRefCountedBase<FSelf>, &TRefCountedBase<FSelf>::Release>{ this },
					Priority
				);
			}
		};
	}
}}