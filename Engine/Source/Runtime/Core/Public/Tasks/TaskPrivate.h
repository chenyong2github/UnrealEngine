// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/Fundamental/Task.h"
#include "Async/Fundamental/Scheduler.h"
#include "Templates/Invoke.h"
#include "Templates/TypeCompatibleBytes.h"
#include "Misc/Timeout.h"
#include "CoreTypes.h"

#include <atomic>
#include <type_traits>

namespace UE { namespace Tasks
{
	using LowLevelTasks::ETaskPriority;

	// BEGIN: forward declarations

	class FPipe;

	template<typename T>
	class TTask;

	template<typename TaskBodyType>
	TTask<TInvokeResult_T<TaskBodyType>> Launch(
		const TCHAR* DebugName, 
		TaskBodyType&& TaskBody, 
		LowLevelTasks::ETaskPriority Priority = LowLevelTasks::ETaskPriority::Normal
	);

	// END: forward declarations

	namespace Private
	{
		// Task class implementation with piping support.
		class FTaskBase
		{
			UE_NONCOPYABLE(FTaskBase);

			// Can be used only as a base class.
		protected:
			FTaskBase()
				: PipePtr(reinterpret_cast<uintptr_t>(nullptr))
				, bPushedIntoPipe(false)
			{
			}

			~FTaskBase()
			{
				check(IsCompleted());
			}

		public:
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
						StartPipeExecution();
						Invoke(TaskBody);
						FinishPipeExecution();
						FTaskBase* Subsequent = CloseAndReturnSubsequent();
						if (Subsequent != nullptr)
						{
							check(TryPushIntoPipe());
							LowLevelTasks::TryLaunch(Subsequent->LowLevelTask, LowLevelTasks::EQueuePreference::DefaultPreference, /*bWakeUpWorker =*/ false);
						}
					} // Continuation
				);
			}

			// A piped task will be executed after the previous task from this pipe is completed. Tasks from the same pipe are not executed
			// concurrently (so don't require synchronization), but not necessarily on the same thread.
			// @See FPipe
			void SetPipe(FPipe& Pipe)
			{
				PipePtr = reinterpret_cast<uintptr_t>(&Pipe);
			}

			FPipe* GetPipe() const
			{
				return reinterpret_cast<FPipe*>(PipePtr);
			}

			// allows the task to be scheduled and executed. Tasks w/o pending dependencies (not blocked by a pipe) are 
			// scheduled immediately
			bool TryLaunch()
			{
				if (TryPushIntoPipe())
				{
					// scheduler's reference was accounted on task creation
					return LowLevelTasks::TryLaunch(LowLevelTask);
				}
				return false;
			}

			// sets a subsequent task that will be launched automatically when this task is done
			// returns true only if the task is not closed yet ("closed" = is executed and has already checked its subsequent),
			// if returned false the subsequent is not blocked by this task
			bool SetSubsequent(FTaskBase& Task)
			{
#if DO_CHECK
				FTaskBase* CurrentState = SubsequentAndState.load(std::memory_order_relaxed);
				checkf(CurrentState == GetEmptyOpenState() || CurrentState == GetClosedState(), TEXT("only a single subsequent can be set"));
#endif

				FTaskBase* EmptyNotCompleted = GetEmptyOpenState();
				FTaskBase* NotCompletedWithTask = &Task;
				return SubsequentAndState.compare_exchange_strong(EmptyNotCompleted, NotCompletedWithTask, std::memory_order_release, std::memory_order_relaxed);
			}

			bool IsCompleted() const
			{
				return LowLevelTask.IsCompleted();
			}

			// waits until the task is completed
			void Wait()
			{
				// we try to retract the task from the scheduler, if it's execution hasn't started yet, to execute it immediately. This makes
				// the task skip waiting in the scheduler's queue.
				// we can't retract it right here because it can be not scheduled yet (e.g. when it's blocked by a pipe), so we need to wait for this
				LowLevelTasks::BusyWaitUntil([this] { return !LowLevelTask.IsReady(); }); // `!IsReady()` here means "the task is scheduled"
				LowLevelTask.TryCancel(); //This will try to cancel the task and run it's continuation (thereby executing the workload) 
				LowLevelTasks::BusyWaitUntil([this] { return LowLevelTask.IsCompleted(); });
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
				LowLevelTasks::BusyWaitUntil([this, Timeout] { return LowLevelTask.IsCompleted() || Timeout; });
				return LowLevelTask.IsCompleted();
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
					[this, Condition = Forward<ConditionType>(Condition)] { return LowLevelTask.IsCompleted() || Condition(); }
				);
				return LowLevelTask.IsCompleted(std::memory_order_relaxed);
			}

		private:
			// checks if the task is ready to be launched by trying to push it into the pipe
			bool TryPushIntoPipe()
			{
				if (GetPipe() == nullptr)
				{
					return true;
				}

				if (!bPushedIntoPipe)
				{
					bPushedIntoPipe = true;
					return PushIntoPipe();
				}

				return true;
			}

			CORE_API bool PushIntoPipe();
			CORE_API void StartPipeExecution();
			CORE_API void FinishPipeExecution();

			// after being executed the task checks if it has a subsequent to launch. it's the only time the task will do this check so
			// it "closes" itself and setting a subsequent fill fail after that.
			// atomic operation
			FTaskBase* CloseAndReturnSubsequent()
			{
				FTaskBase* Subsequent_Local = SubsequentAndState.exchange(GetClosedState(), std::memory_order_acq_rel);
				checkf(Subsequent_Local != GetClosedState(), TEXT("Task can be closed only once"));
				return Subsequent_Local;
			}

			// no subsequent, still open
			static FTaskBase* GetEmptyOpenState()
			{
				return nullptr;
			}

			static FTaskBase* GetClosedState()
			{
				// nullptr with the most significant bit set to indicate the closed state
				return UE_LAUNDER(reinterpret_cast<FTaskBase*>(1ull << 63));
			}

		private:
			LowLevelTasks::FTask LowLevelTask;

			// packed pipe's ptr and the flag if the task is already pushed into pipe
			uintptr_t PipePtr : 63; // actually `FPipe*`
			uintptr_t bPushedIntoPipe : 1;	// bool, if the task is pushed into pipe

			// a pointer to subsequent task with the most significant bit borrowed to be used as a close flag
			// `LowLevelTask.IsCompleted()` flag can't be used as a "close" flag because we need to check if the task still can 
			// accept a subsequent and to set the subsequent atomically.
			// can be "empty and open" (`nullptr`), "have subsequent and open" (subsequent ptr) and closed (`nullptr` with the most significant
			// bit set)
			std::atomic<FTaskBase*> SubsequentAndState{ GetEmptyOpenState() };
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