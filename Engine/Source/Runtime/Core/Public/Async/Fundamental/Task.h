// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Logging/LogMacros.h"
#include "TaskDelegate.h"
#include "CoreTypes.h"
#include <atomic>

namespace LowLevelTasks
{
	DECLARE_LOG_CATEGORY_EXTERN(LogTasks2, Log, All);

	enum class ETaskPriority : int32
	{
		High,
		Normal,
		Default = Normal,
		Low,
		Count
	};

	enum class ETaskState : int32
	{
		Completed,		//task is finshed or the default when we create a handle
		Ready,			//task is ready for scheduling
		Canceled,		//task was canceled
		Scheduled,		//task is scheduled and can run concurrently any time from now
		Running,		//task is executing
		Continuation,   //task is executing the continuation	
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
#if PLATFORM_32BITS
			uintptr_t DebugName : 32;
#else
			uintptr_t DebugName : 58;
#endif
			uintptr_t Priority	: 2;
			uintptr_t State		: 4;
			
		public:
			FPackedData() : FPackedData(nullptr,  ETaskPriority::Default, ETaskState::Completed)
			{
				static_assert(uintptr_t(ETaskPriority::Count) <= (1ull << 2), "Not enough bits to store ETaskPriority");
				static_assert(uintptr_t(ETaskState::Count) <= (1ull << 4), "Not enough bits to store ETaskState");
			}

			FPackedData(const TCHAR* InDebugName, ETaskPriority InPriority, ETaskState InState)
				: DebugName(reinterpret_cast<uintptr_t>(InDebugName))
				, Priority((uintptr_t)InPriority)
				, State((uintptr_t)InState)
			{
				checkSlow(reinterpret_cast<uintptr_t>(InDebugName) < (1ull << 58));
				checkSlow((uintptr_t)InPriority < (1ull << 2));
				checkSlow((uintptr_t)InState < (1ull << 4));
#if PLATFORM_32BITS
				static_assert(sizeof(FPackedData) == 2 * sizeof(uintptr_t), "Packed data needs to be 8bytes in size");
#else
				static_assert(sizeof(FPackedData) == sizeof(uintptr_t), "Packed data needs to be pointer size");
#endif
			}

			FPackedData(FPackedData& Other, ETaskState State)
				: FPackedData(Other.GetDebugName(), Other.GetPriority(), State)
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
		};

	private:
		using FTaskDelegate = TTaskDelegate<PLATFORM_CACHE_LINE_SIZE - sizeof(FPackedData)>;
		FTaskDelegate Runnable;
		std::atomic<FPackedData> PackedData { FPackedData() };

	private:
		FTaskBase()
		{
			static_assert(sizeof(FTaskBase) == PLATFORM_CACHE_LINE_SIZE, "Require FTaskBase to be cacheline size");
		}
	};
	}

	//minimal core task interface
	class FTask final : private Tasks_Impl::FTaskBase
	{		
		friend class FScheduler;
		UE_NONCOPYABLE(FTask); //means non movable

	public: //Public Interface 
		inline bool IsCompleted() const { return PackedData.load(std::memory_order_seq_cst).GetState() == ETaskState::Completed; }
		inline bool IsReady() const { return PackedData.load(std::memory_order_relaxed).GetState() == ETaskState::Ready; }
		inline bool IsCanceled() const { return PackedData.load(std::memory_order_relaxed).GetState() == ETaskState::Canceled; }
		inline bool IsScheduled() const { return PackedData.load(std::memory_order_relaxed).GetState() == ETaskState::Scheduled; }
		inline bool IsRunning() const { return PackedData.load(std::memory_order_relaxed).GetState() == ETaskState::Running; }
		inline bool IsRunningContinuation() const { return PackedData.load(std::memory_order_relaxed).GetState() == ETaskState::Continuation; }

		//Note even though is canceled we do not pull it out of the scheduler and it's continuation will still run regardless
		//so we still have to wait for completion before we can recycle/reassign the handle.
		inline bool TryCancel();

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

	public:
		FTask()
		{
			static_assert(sizeof(FTask) == PLATFORM_CACHE_LINE_SIZE, "Require FTask to be cacheline size");
		}
		inline ~FTask();

	private: //Interface of the Scheduler
		inline void PrepareLaunch();
		//after calling this function the task can be considered dead
		inline void ExecuteTask();			
	};

   /******************
	* IMPLEMENTATION *
	******************/

	template<typename TRunnable, typename TContinuation>
	inline void FTask::Init(const TCHAR* InDebugName, ETaskPriority InPriority, TRunnable&& InRunnable, TContinuation&& InContinuation)
	{
		checkf(IsCompleted(), TEXT("State: %d"), PackedData.load(std::memory_order_relaxed).GetState());
		checkSlow(!Runnable.IsSet());
		Runnable = [this, LocalRunnable = Forward<TRunnable>(InRunnable), LocalContinuation = Forward<TContinuation>(InContinuation)]()
		{
			checkSlow(IsScheduled() || IsCanceled());
			FPackedData LocalPackedData = PackedData.load(std::memory_order_relaxed);
			FPackedData ScheduledState(LocalPackedData, ETaskState::Scheduled);
			if (PackedData.compare_exchange_strong(ScheduledState, FPackedData(LocalPackedData, ETaskState::Running), std::memory_order_relaxed))
			{
				LocalRunnable();
			}
			else
			{
				checkSlow(IsCanceled());
			}

			PackedData.store(FPackedData(LocalPackedData, ETaskState::Continuation), std::memory_order_relaxed);
			LocalContinuation();
		};
		PackedData.store(FPackedData(InDebugName, InPriority, ETaskState::Ready), std::memory_order_release);
	}

	template<typename TRunnable>
	inline void FTask::Init(const TCHAR* InDebugName, ETaskPriority InPriority, TRunnable&& InRunnable)
	{
		checkf(IsCompleted(), TEXT("State: %d"), PackedData.load(std::memory_order_relaxed).GetState());
		checkSlow(!Runnable.IsSet());
		Runnable = [this, LocalRunnable = Forward<TRunnable>(InRunnable)]()
		{
			checkSlow(IsScheduled() || IsCanceled());
			FPackedData LocalPackedData = PackedData.load(std::memory_order_relaxed);
			FPackedData ScheduledState(LocalPackedData, ETaskState::Scheduled);
			if (PackedData.compare_exchange_strong(ScheduledState, FPackedData(LocalPackedData, ETaskState::Running), std::memory_order_relaxed))
			{
				LocalRunnable();
			}
			else
			{
				checkSlow(IsCanceled());
			}
		};
		PackedData.store(FPackedData(InDebugName, InPriority, ETaskState::Ready), std::memory_order_release);
	}

	template<typename TRunnable, typename TContinuation>
	inline void FTask::Init(const TCHAR* InDebugName, TRunnable&& InRunnable, TContinuation&& InContinuation)
	{
		Init(InDebugName, ETaskPriority::Default, Forward<TRunnable>(InRunnable), Forward<TContinuation>(InContinuation));
	}

	template<typename TRunnable>
	inline void FTask::Init(const TCHAR* InDebugName, TRunnable&& InRunnable)
	{
		Init(InDebugName, ETaskPriority::Default, Forward<TRunnable>(InRunnable));
	}

	inline FTask::~FTask()
	{
		checkf(IsCompleted(), TEXT("State: %d"), PackedData.load(std::memory_order_relaxed).GetState());
	}

	inline bool FTask::TryCancel()
	{
		FPackedData LocalPackedData = PackedData.load(std::memory_order_relaxed);
		FPackedData ReadyState(LocalPackedData, ETaskState::Ready);
		FPackedData ScheduledState(LocalPackedData, ETaskState::Scheduled);
		return PackedData.compare_exchange_strong(ReadyState,	  FPackedData(LocalPackedData, ETaskState::Canceled), std::memory_order_relaxed) 
			|| PackedData.compare_exchange_strong(ScheduledState, FPackedData(LocalPackedData, ETaskState::Canceled), std::memory_order_relaxed)
			|| IsCanceled();
	}

	inline void FTask::PrepareLaunch()
	{
		FPackedData LocalPackedData = PackedData.load(std::memory_order_relaxed);
		FPackedData ReadyState(LocalPackedData, ETaskState::Ready);
		if (!PackedData.compare_exchange_strong(ReadyState, FPackedData(LocalPackedData, ETaskState::Scheduled), std::memory_order_acquire))
		{
			checkf(IsCanceled(), TEXT("State: %d"), PackedData.load(std::memory_order_relaxed).GetState());
		}
	}

	inline void FTask::ExecuteTask()
	{
		checkSlow(Runnable.IsSet());
		alignas(PLATFORM_CACHE_LINE_SIZE) 
		FTaskDelegate LocalRunnable;
		Runnable.CallAndMove(LocalRunnable);
		checkSlow(!Runnable.IsSet());
		//do not access the task again after this call
		//as by defitition the task can be considered dead
		FPackedData LocalPackedData = PackedData.load(std::memory_order_relaxed);
		PackedData.store(FPackedData(LocalPackedData, ETaskState::Completed), std::memory_order_seq_cst);
	}

	inline ETaskPriority FTask::GetPriority() const 
	{ 
		return PackedData.load(std::memory_order_relaxed).GetPriority(); 
	}

	inline const TCHAR* FTask::GetDebugName() const
	{
		return PackedData.load(std::memory_order_relaxed).GetDebugName(); 
	}
}