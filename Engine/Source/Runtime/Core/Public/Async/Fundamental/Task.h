// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Logging/LogMacros.h"
#include "TaskDelegate.h"
#include "CoreTypes.h"
#include <atomic>

namespace LowLevelTasks
{
	DECLARE_LOG_CATEGORY_EXTERN(LowLevelTasks, Log, All);

	enum class ETaskPriority : int32
	{
		High,
		Normal,
		Default = Normal,
		BackgroundHigh,
		ForegroundCount = BackgroundHigh,
		BackgroundNormal,
		BackgroundLow,
		Count
	};

	enum class ETaskState : int32
	{
		CanceledAndCompleted,	//means the task is completed with execution of it's continuation but the runnable was cancelled
		Completed,				//means the task is completed with execution or the default when we create a handle
		Ready,					//means the Task is ready to be launched
		Canceled,				//means the task was canceled and launched and therefore queued for execution by a worker (which already might be executing it's continuation)
		CanceledAndReady,		//means the task was canceled and is ready to be launched (it still is required to be launched)
		Scheduled,				//means the task is launched and therefore queued for execution by a worker
		Running,				//means the task is executing it's runnable and continuation by a worker 
		Count
	};
	
	//Generic implementation of a Deleter, it often comes up that one has to call a function to cleanup after a Task finished
	//this can be done by capturing a TDeleter like this in the lambda of the Continuation: [Deleter(LowLevelTasks::TDeleter<Type, &Type::DeleteFunction>(value))](){}
	template<typename Type, void (Type::*DeleteFunction)()>
	class TDeleter
	{
		Type* Value;

	public:
		TDeleter(Type* InValue) : Value(InValue)
		{
		}

		TDeleter(const TDeleter&) = delete;
		TDeleter(TDeleter&& Other) : Value(Other.Value)
		{
			Other.Value = nullptr;
		}

		~TDeleter() 
		{
			if(Value)
			{
				(Value->*DeleteFunction)();
			}
		}
	};

	//this class is just here to hide some variables away
	//because we don't want to become too close friends with the FScheduler
	class FTask;
	namespace Tasks_Impl
	{
	class FTaskBase
	{
		friend class ::LowLevelTasks::FTask;
		UE_NONCOPYABLE(FTaskBase); //means non movable

		class FPackedData
		{
			uintptr_t DebugName			: 57;
			uintptr_t Priority			: 3;
			uintptr_t State				: 3;
			uintptr_t bAllowBusyWaiting : 1;
			
		public:
			FPackedData() : FPackedData(nullptr,  ETaskPriority::Default, ETaskState::Completed, true)
			{
				static_assert(!PLATFORM_32BITS, "32bit Platforms are not supported");
				static_assert(uintptr_t(ETaskPriority::Count) <= (1ull << 3), "Not enough bits to store ETaskPriority");
				static_assert(uintptr_t(ETaskState::Count) <= (1ull << 3), "Not enough bits to store ETaskState");
			}

			FPackedData(const TCHAR* InDebugName, ETaskPriority InPriority, ETaskState InState, bool bInAllowBusyWaiting)
				: DebugName(reinterpret_cast<uintptr_t>(InDebugName))
				, Priority((uintptr_t)InPriority)
				, State((uintptr_t)InState)
				, bAllowBusyWaiting(bInAllowBusyWaiting)
			{
				checkSlow(reinterpret_cast<uintptr_t>(InDebugName) < (1ull << 57));
				checkSlow((uintptr_t)InPriority < (1ull << 3));
				checkSlow((uintptr_t)InState < (1ull << 3));
				static_assert(sizeof(FPackedData) == sizeof(uintptr_t), "Packed data needs to be pointer size");
			}

			FPackedData(FPackedData& Other, ETaskState State)
				: FPackedData(Other.GetDebugName(), Other.GetPriority(), State, Other.AllowBusyWaiting())
			{
			}

			const TCHAR* GetDebugName() const
			{
				return reinterpret_cast<const TCHAR*>(DebugName);
			}

			ETaskPriority GetPriority() const
			{
				return ETaskPriority(Priority);
			}

			ETaskState GetState() const
			{
				return ETaskState(State);
			}

			bool AllowBusyWaiting() const
			{
				return bAllowBusyWaiting;
			}
		};

	private:
		using FTaskDelegate = TTaskDelegate<PLATFORM_CACHE_LINE_SIZE - sizeof(FPackedData) - sizeof(void*), bool>;
		FTaskDelegate Runnable;
		mutable void* UserData;
		std::atomic<FPackedData> PackedData { FPackedData() };

	private:
		FTaskBase()
		{
			static_assert(sizeof(FTaskBase) == PLATFORM_CACHE_LINE_SIZE, "Require FTaskBase to be cacheline size");
		}

	private:
		inline bool IsCanceled() const { return PackedData.load(std::memory_order_relaxed).GetState() == ETaskState::Canceled; }
		inline bool IsScheduled() const { return PackedData.load(std::memory_order_relaxed).GetState() == ETaskState::Scheduled; }
	};
	}

	//minimal core task interface
	class FTask final : private Tasks_Impl::FTaskBase
	{		
		friend class FScheduler;
		UE_NONCOPYABLE(FTask); //means non movable

	public: //Public Interface
		//means the task is completed and this taskhandle can be recycled
		inline bool IsCompleted() const
		{
			ETaskState State = PackedData.load(std::memory_order_seq_cst).GetState();
			return State == ETaskState::CanceledAndCompleted || State == ETaskState::Completed;
		}
		
		//means the task was canceled but might still need to be launched 
		inline bool WasCanceled() 		
		{
			ETaskState State = PackedData.load(std::memory_order_relaxed).GetState();
			return State == ETaskState::CanceledAndReady || State == ETaskState::Canceled || State == ETaskState::CanceledAndCompleted;
		}

		//means the task is ready to be launched but might already been canceled 
		inline bool IsReady() const 
		{
			ETaskState State = PackedData.load(std::memory_order_relaxed).GetState();
			return State == ETaskState::Ready || State == ETaskState::CanceledAndReady; 
		}

		
		//try to cancel the task without launching it. Even if the task is canceled it sill needs to be launched manually
		//to launch the task manually you use FScheduler::TryLaunch
		//you can alternatively use FScheduler::TryCancelAndLaunch if you want to launch the task in this case automatically
		inline bool TryCancel();

		template<typename TRunnable, typename TContinuation>
		inline void Init(const TCHAR* InDebugName, ETaskPriority InPriority, TRunnable&& InRunnable, TContinuation&& InContinuation, bool bAllowBusyWaiting);

		template<typename TRunnable>
		inline void Init(const TCHAR* InDebugName, ETaskPriority InPriority, TRunnable&& InRunnable, bool bAllowBusyWaiting);

		template<typename TRunnable, typename TContinuation>
		inline void Init(const TCHAR* InDebugName, TRunnable&& InRunnable, TContinuation&& InContinuation, bool bAllowBusyWaiting);

		template<typename TRunnable>
		inline void Init(const TCHAR* InDebugName, TRunnable&& InRunnable, bool bAllowBusyWaiting);

		template<typename TRunnable, typename TContinuation>
		inline void Init(const TCHAR* InDebugName, ETaskPriority InPriority, TRunnable&& InRunnable, TContinuation&& InContinuation);

		template<typename TRunnable>
		inline void Init(const TCHAR* InDebugName, ETaskPriority InPriority, TRunnable&& InRunnable);

		template<typename TRunnable, typename TContinuation>
		inline void Init(const TCHAR* InDebugName, TRunnable&& InRunnable, TContinuation&& InContinuation);

		template<typename TRunnable>
		inline void Init(const TCHAR* InDebugName, TRunnable&& InRunnable);

		inline const TCHAR* GetDebugName() const;
		inline ETaskPriority GetPriority() const;
		inline bool IsBackgroundTask() const;

		void* GetUserData() const { return UserData; }
		void SetUserData(void* NewUserData) const { UserData = NewUserData; }

	public:
		FTask()
		{
			static_assert(sizeof(FTask) == PLATFORM_CACHE_LINE_SIZE, "Require FTask to be cacheline size");
		}
		inline ~FTask();

	private: //Interface of the Scheduler
		inline bool TryPrepareLaunch();
		//after calling this function the task can be considered dead
		inline void ExecuteTask();
		CORE_API void PropagateUserData();
		inline bool AllowBusyWaiting() const;
	};

   /******************
	* IMPLEMENTATION *
	******************/

	template<typename TRunnable, typename TContinuation>
	inline void FTask::Init(const TCHAR* InDebugName, ETaskPriority InPriority, TRunnable&& InRunnable, TContinuation&& InContinuation, bool bAllowBusyWaiting)
	{
		checkf(IsCompleted(), TEXT("State: %d"), PackedData.load(std::memory_order_relaxed).GetState());
		checkSlow(!Runnable.IsSet());
		Runnable = [this, LocalRunnable = Forward<TRunnable>(InRunnable), LocalContinuation = Forward<TContinuation>(InContinuation)]() mutable -> bool
		{
			checkSlow(IsScheduled() || IsCanceled());
			FPackedData LocalPackedData = PackedData.load(std::memory_order_relaxed);
			FPackedData ScheduledState(LocalPackedData, ETaskState::Scheduled);
			if (PackedData.compare_exchange_strong(ScheduledState, FPackedData(LocalPackedData, ETaskState::Running), std::memory_order_relaxed))
			{
				LocalRunnable();
				LocalContinuation();
				return false;
			}
			else
			{
				checkSlow(IsCanceled());
				LocalContinuation();
				return true;
			}
		};
		PropagateUserData();
		PackedData.store(FPackedData(InDebugName, InPriority, ETaskState::Ready, bAllowBusyWaiting), std::memory_order_release);
	}

	template<typename TRunnable>
	inline void FTask::Init(const TCHAR* InDebugName, ETaskPriority InPriority, TRunnable&& InRunnable, bool bAllowBusyWaiting)
	{
		checkf(IsCompleted(), TEXT("State: %d"), PackedData.load(std::memory_order_relaxed).GetState());
		checkSlow(!Runnable.IsSet());
		Runnable = [this, LocalRunnable = Forward<TRunnable>(InRunnable)]() -> bool
		{
			checkSlow(IsScheduled() || IsCanceled());
			FPackedData LocalPackedData = PackedData.load(std::memory_order_relaxed);
			FPackedData ScheduledState(LocalPackedData, ETaskState::Scheduled);
			if (PackedData.compare_exchange_strong(ScheduledState, FPackedData(LocalPackedData, ETaskState::Running), std::memory_order_relaxed))
			{
				LocalRunnable();
				return false;
			}
			else
			{
				checkSlow(IsCanceled());
				return true;
			}
		};
		PropagateUserData();
		PackedData.store(FPackedData(InDebugName, InPriority, ETaskState::Ready, bAllowBusyWaiting), std::memory_order_release);
	}

	template<typename TRunnable, typename TContinuation>
	inline void FTask::Init(const TCHAR* InDebugName, TRunnable&& InRunnable, TContinuation&& InContinuation, bool bAllowBusyWaiting)
	{
		Init(InDebugName, ETaskPriority::Default, Forward<TRunnable>(InRunnable), Forward<TContinuation>(InContinuation), bAllowBusyWaiting);
	}

	template<typename TRunnable>
	inline void FTask::Init(const TCHAR* InDebugName, TRunnable&& InRunnable, bool bAllowBusyWaiting)
	{
		Init(InDebugName, ETaskPriority::Default, Forward<TRunnable>(InRunnable), bAllowBusyWaiting);
	}

	template<typename TRunnable, typename TContinuation>
	inline void FTask::Init(const TCHAR* InDebugName, ETaskPriority InPriority, TRunnable&& InRunnable, TContinuation&& InContinuation)
	{
		Init(InDebugName, InPriority, Forward<TRunnable>(InRunnable), Forward<TContinuation>(InContinuation), true);
	}

	template<typename TRunnable>
	inline void FTask::Init(const TCHAR* InDebugName, ETaskPriority InPriority, TRunnable&& InRunnable)
	{
		Init(InDebugName, InPriority, Forward<TRunnable>(InRunnable), true);
	}

	template<typename TRunnable, typename TContinuation>
	inline void FTask::Init(const TCHAR* InDebugName, TRunnable&& InRunnable, TContinuation&& InContinuation)
	{
		Init(InDebugName, ETaskPriority::Default, Forward<TRunnable>(InRunnable), Forward<TContinuation>(InContinuation), true);
	}

	template<typename TRunnable>
	inline void FTask::Init(const TCHAR* InDebugName, TRunnable&& InRunnable)
	{
		Init(InDebugName, ETaskPriority::Default, Forward<TRunnable>(InRunnable), true);
	}

	inline FTask::~FTask()
	{
		checkf(IsCompleted(), TEXT("State: %d"), PackedData.load(std::memory_order_relaxed).GetState());
	}

	inline bool FTask::TryPrepareLaunch()
	{
		FPackedData LocalPackedData = PackedData.load(std::memory_order_relaxed);
		FPackedData ReadyState = FPackedData(LocalPackedData, ETaskState::Ready);
		FPackedData CanceledAndReadyState = FPackedData(LocalPackedData, ETaskState::CanceledAndReady);
		return PackedData.compare_exchange_strong(ReadyState, FPackedData(LocalPackedData, ETaskState::Scheduled), std::memory_order_acquire, std::memory_order_relaxed)
			|| PackedData.compare_exchange_strong(CanceledAndReadyState, FPackedData(LocalPackedData, ETaskState::Canceled), std::memory_order_acquire, std::memory_order_relaxed);
	}

	inline bool FTask::TryCancel()
	{
		FPackedData LocalPackedData = PackedData.load(std::memory_order_relaxed);
		FPackedData ReadyState(LocalPackedData, ETaskState::Ready);
		FPackedData ScheduledState(LocalPackedData, ETaskState::Scheduled);
		//we can use memory_order_relaxed in this case because, when a task is canceled it does not launch the task
		//to launch a canceled  task it has to go though TryPrepareLaunch which is doing the memory_order_acquire
		return PackedData.compare_exchange_strong(ReadyState, FPackedData(LocalPackedData, ETaskState::CanceledAndReady), std::memory_order_relaxed)
			|| PackedData.compare_exchange_strong(ScheduledState, FPackedData(LocalPackedData, ETaskState::Canceled), std::memory_order_relaxed);
	}

	inline void FTask::ExecuteTask()
	{
		checkSlow(Runnable.IsSet());
		alignas(PLATFORM_CACHE_LINE_SIZE) 
		FTaskDelegate LocalRunnable;
		bool Canceled = Runnable.CallAndMove(LocalRunnable);
		checkSlow(!Runnable.IsSet());
		//do not access the task again after this call
		//as by defitition the task can be considered dead
		FPackedData LocalPackedData = PackedData.load(std::memory_order_relaxed);
		PackedData.store(FPackedData(LocalPackedData, Canceled ? ETaskState::CanceledAndCompleted : ETaskState::Completed), std::memory_order_seq_cst);
	}

	inline ETaskPriority FTask::GetPriority() const 
	{ 
		return PackedData.load(std::memory_order_relaxed).GetPriority(); 
	}

	inline const TCHAR* FTask::GetDebugName() const
	{
		return PackedData.load(std::memory_order_relaxed).GetDebugName(); 
	}

	inline bool FTask::IsBackgroundTask() const
	{
		return PackedData.load(std::memory_order_relaxed).GetPriority() >= ETaskPriority::ForegroundCount;
	}

	inline bool FTask::AllowBusyWaiting() const
	{
		return PackedData.load(std::memory_order_relaxed).AllowBusyWaiting();
	}	
}