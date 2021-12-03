// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ParllelFor.h: TaskGraph library
=============================================================================*/

#pragma once

#include "CoreTypes.h"
#include "Misc/AssertionMacros.h"
#include "Math/UnrealMathUtility.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"
#include "HAL/ThreadSafeCounter.h"
#include "Stats/Stats.h"
#include "Async/TaskGraphInterfaces.h"
#include "Misc/App.h"
#include "Misc/Fork.h"

#include <atomic>

extern CORE_API int32 GParallelForBackgroundYieldingTimeoutMs;

// Flags controlling the ParallelFor's behavior.
enum class EParallelForFlags
{
	// Default behavior
	None,

	//Mostly used for testing, when used, ParallelFor will run single threaded instead.
	ForceSingleThread = 1,

	//Offers better work distribution among threads at the cost of a little bit more synchronization.
	//This should be used for tasks with highly variable computational time.
	Unbalanced = 2,

	// if running on the rendering thread, make sure the ProcessThread is called when idle
	PumpRenderingThread = 4,

	// tasks should run on background priority threads
	BackgroundPriority = 8,
};

ENUM_CLASS_FLAGS(EParallelForFlags)

namespace ParallelForImpl
{
	// struct to hold the working data; this outlives the ParallelFor call; lifetime is controlled by a shared pointer
	template<typename FunctionType>
	struct TParallelForData
	{
		int32 Num;
		int32 BlockSize;
		int32 LastBlockExtraNum;
		FunctionType Body;
		FEvent* Event;
		FThreadSafeCounter IndexToDo;
		FThreadSafeCounter NumCompleted;
#if DO_CHECK
		std::atomic<bool> bExited{ false };
#endif
		bool bTriggered;
		bool bSaveLastBlockForMaster;
		TParallelForData(int32 InTotalNum, int32 InNumThreads, bool bInSaveLastBlockForMaster, FunctionType InBody, EParallelForFlags Flags)
			: Body(InBody)
			, Event(FPlatformProcess::GetSynchEventFromPool(false))
			, bTriggered(false)
			, bSaveLastBlockForMaster(bInSaveLastBlockForMaster)
		{
			check(InTotalNum >= InNumThreads);

			if ((Flags & EParallelForFlags::Unbalanced) != EParallelForFlags::None)
			{
				BlockSize = 1;
				Num = InTotalNum;
			}
			else
			{
				BlockSize = 0;
				Num = 0;
				for (int32 Div = 6; Div; Div--)
				{
					BlockSize = InTotalNum / (InNumThreads * Div);
					if (BlockSize)
					{
						Num = InTotalNum / BlockSize;
						if (Num >= InNumThreads + !!bSaveLastBlockForMaster)
						{
							break;
						}
					}
				}
			}

			check(BlockSize && Num);
			LastBlockExtraNum = InTotalNum - Num * BlockSize;
			check(LastBlockExtraNum >= 0);
		}
		~TParallelForData()
		{
			check(IndexToDo.GetValue() >= Num);
			check(NumCompleted.GetValue() == Num);
			check(bExited.load(std::memory_order_relaxed));
			FPlatformProcess::ReturnSynchEventToPool(Event);
		}

		template<typename ContextType>
		bool Process(TArray<ContextType>* InContexts, int32 TaskIndex, int32 TasksToSpawn, TSharedRef<TParallelForData, ESPMode::ThreadSafe>& Data, ENamedThreads::Type InDesiredThread, bool bMaster);
		
		inline bool Process(int32 TaskIndex, int32 TasksToSpawn, TSharedRef<TParallelForData, ESPMode::ThreadSafe>& Data, ENamedThreads::Type InDesiredThread, bool bMaster)
		{
			return Process<nullptr_t>(nullptr, TaskIndex, TasksToSpawn, Data, InDesiredThread, bMaster);
		}
	};

	template<typename FunctionType, typename ContextType = nullptr_t>
	class TParallelForTask
	{
		TArray<ContextType>* Contexts;
		TSharedRef<TParallelForData<FunctionType>, ESPMode::ThreadSafe> Data;
		ENamedThreads::Type DesiredThread;
		int32 TaskIndex;
		int32 TasksToSpawn;
	public:
		TParallelForTask(TArray<ContextType>* InContexts, int32 InTaskIndex, TSharedRef<TParallelForData<FunctionType>, ESPMode::ThreadSafe>& InData, ENamedThreads::Type InDesiredThread, int32 InTasksToSpawn = 0)
			: Contexts(InContexts)
			, Data(InData) 
			, DesiredThread(InDesiredThread)
			, TaskIndex(InTaskIndex)
			, TasksToSpawn(InTasksToSpawn)
		{
		}		
		TParallelForTask(int32 InTaskIndex, TSharedRef<TParallelForData<FunctionType>, ESPMode::ThreadSafe>& InData, ENamedThreads::Type InDesiredThread, int32 InTasksToSpawn = 0)
			: Contexts(nullptr)
			, Data(InData) 
			, DesiredThread(InDesiredThread)
			, TaskIndex(InTaskIndex)
			, TasksToSpawn(InTasksToSpawn)
		{
		}
		static FORCEINLINE TStatId GetStatId()
		{
			return GET_STATID(STAT_ParallelForTask);
		}
		
		FORCEINLINE ENamedThreads::Type GetDesiredThread()
		{
			return DesiredThread;
		}

		static FORCEINLINE ESubsequentsMode::Type GetSubsequentsMode() 
		{ 
			return ESubsequentsMode::FireAndForget; 
		}
		void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
		{
			FMemMark Mark(FMemStack::Get());
			if (Data->Process(Contexts, TaskIndex, TasksToSpawn, Data, DesiredThread, false))
			{
				checkSlow(!Data->bTriggered);
				Data->bTriggered = true;
				Data->Event->Trigger();
			}
		}
	};

	// Helper to call body with context reference
	template <typename FunctionType, typename ContextType>
	inline void CallBody(const FunctionType& Body, ContextType* Context, int32 Index)
	{
		Body(*Context, Index);
	}
	
	// Helper specialization for "no context", which changes the assumed body call signature
	template <typename FunctionType>
	inline void CallBody(const FunctionType& Body, nullptr_t*, int32 Index)
	{
		Body(Index);
	}

	template <typename FunctionType, typename ContextType>
	inline void CallBody(const FunctionType& Body, TArray<ContextType>* Contexts, int32 TaskIndex, int32 Index)
	{
		ContextType* Context = Contexts ? &(*Contexts)[TaskIndex] : nullptr;
		CallBody(Body, Context, Index);
	}

	template<typename FunctionType> 
	template<typename ContextType> 
	inline bool TParallelForData<FunctionType>::Process(TArray<ContextType>* Contexts, int32 TaskIndex, int32 TasksToSpawn, TSharedRef<TParallelForData<FunctionType>, ESPMode::ThreadSafe>& Data, ENamedThreads::Type InDesiredThread, bool bMaster)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TParallelForData::Process)

		int32 MaybeTasksLeft = Num - IndexToDo.GetValue();
		if (TasksToSpawn && MaybeTasksLeft > 0)
		{
			TasksToSpawn = FMath::Min<int32>(TasksToSpawn, MaybeTasksLeft);
			TGraphTask<TParallelForTask<FunctionType, ContextType>>::CreateTask().ConstructAndDispatchWhenReady(Contexts, TaskIndex + 1, Data, InDesiredThread, TasksToSpawn - 1);
		}
		int32 LocalBlockSize = BlockSize;
		int32 LocalNum = Num;
		bool bLocalSaveLastBlockForMaster = bSaveLastBlockForMaster;

		auto Now = [] { return FTimespan::FromSeconds(FPlatformTime::Seconds()); };
		FTimespan Start = FTimespan::MinValue();
			
		bool bIsBackgroundPriority = !bMaster && ((InDesiredThread & ENamedThreads::ThreadPriorityMask) == ENamedThreads::BackgroundThreadPriority);

		FTimespan YieldingThreshold;
		if (bIsBackgroundPriority)
		{
			Start = Now();
			YieldingThreshold = FTimespan::FromMilliseconds(FMath::Max(0, GParallelForBackgroundYieldingTimeoutMs));
		}

		while (true)
		{
			int32 MyIndex = IndexToDo.Increment() - 1;
			if (bLocalSaveLastBlockForMaster)
			{
				if (!bMaster && MyIndex >= LocalNum - 1)
				{
					break; // leave the last block for the master, hoping to avoid an event
				}
				else if (bMaster && MyIndex > LocalNum - 1)
				{
					MyIndex = LocalNum - 1; // I am the master, I need to take this block, hoping to avoid an event
				}
			}
			if (MyIndex < LocalNum)
			{
				check(Contexts == nullptr || TaskIndex < Contexts->Num());

				int32 ThisBlockSize = LocalBlockSize;
				if (MyIndex == LocalNum - 1)
				{
					ThisBlockSize += LastBlockExtraNum;
				}
				for (int32 LocalIndex = 0; LocalIndex < ThisBlockSize; LocalIndex++)
				{
					CallBody(Body, Contexts, TaskIndex, MyIndex * LocalBlockSize + LocalIndex);
				}
				checkSlow(!bExited.load(std::memory_order_relaxed));
				int32 LocalNumCompleted = NumCompleted.Increment();
				if (LocalNumCompleted == LocalNum)
				{
					return true;
				}
				checkSlow(LocalNumCompleted < LocalNum);
			}
			if (MyIndex >= LocalNum - 1)
			{
				break;
			}

			if (bIsBackgroundPriority)
			{
				auto PassedTime = [Start, &Now]() { return Now() - Start; };
				if (PassedTime() > YieldingThreshold)
				{
					TGraphTask<TParallelForTask<FunctionType, ContextType>>::CreateTask().ConstructAndDispatchWhenReady(Contexts, TaskIndex, Data, InDesiredThread, 0);
					return false;
				}
			}
		}
		return false;
	}

	inline ENamedThreads::Type GetBestDesiredThread(EParallelForFlags Flags)
	{
		const bool bBackgroundPriority = (Flags & EParallelForFlags::BackgroundPriority) != EParallelForFlags::None;
		if (!bBackgroundPriority)
		{
			// Anything scheduled by the task graph is latency sensitive because it might impact the frame rate. Anything else is not (i.e. Worker / Background threads).
			const ETaskTag LatencySensitiveTasks = 
				ETaskTag::EStaticInit | 
				ETaskTag::EGameThread | 
				ETaskTag::ESlateThread | 
#if !UE_AUDIO_THREAD_AS_PIPE
				ETaskTag::EAudioThread | 
#endif
				ETaskTag::ERenderingThread | 
				ETaskTag::ERhiThread;

			const bool bIsLatencySensitive = (FTaskTagScope::GetCurrentTag() & LatencySensitiveTasks) != ETaskTag::ENone;
			if (bIsLatencySensitive)
			{
				// Keep the legacy behavior in this case
				return ENamedThreads::AnyHiPriThreadHiPriTask;
			}
			// It's coming from a known worker thread, we'll keep the same task and thread priority in this case
			else if (FTaskGraphInterface::Get().IsCurrentThreadKnown())
			{
				const ENamedThreads::Type CurrentThread = FTaskGraphInterface::Get().GetCurrentThreadIfKnown();
				const ENamedThreads::Type CurrentTaskPrio = (ENamedThreads::Type)(CurrentThread & ENamedThreads::Type::TaskPriorityMask);
				const ENamedThreads::Type CurrentThreadPrio = (ENamedThreads::Type)(CurrentThread & ENamedThreads::Type::ThreadPriorityMask);
				return (ENamedThreads::Type)(ENamedThreads::AnyThread | CurrentTaskPrio | CurrentThreadPrio);
			}
		}

		// Either the request comes from a totally unknown thread, or we've specifically been asked to be background
		return ENamedThreads::AnyBackgroundThreadNormalTask;
	}

	inline int32 GetNumberOfThreadTasks(int32 Num, EParallelForFlags Flags)
	{
		int32 NumThreadTasks = 0;
		const bool bIsMultithread = FApp::ShouldUseThreadingForPerformance() || FForkProcessHelper::IsForkedMultithreadInstance();
		if (Num > 1 && (Flags & EParallelForFlags::ForceSingleThread) == EParallelForFlags::None && bIsMultithread)
		{
			NumThreadTasks = FMath::Min<int32>(FTaskGraphInterface::Get().GetNumWorkerThreads(), Num - 1);
		}

		return NumThreadTasks;
	}

	template<typename FunctionType, typename ContextType>
	inline void ParallelForInternal(int32 Num, FunctionType Body, EParallelForFlags Flags, TArray<ContextType>* Contexts)
	{
		SCOPE_CYCLE_COUNTER(STAT_ParallelFor);
		check(Num >= 0);

		int32 AnyThreadTasks = GetNumberOfThreadTasks(Num, Flags);
		if (!AnyThreadTasks)
		{
			// no threads, just do it and return
			for (int32 Index = 0; Index < Num; Index++)
			{				
				CallBody(Body, Contexts, 0, Index);
			}
			return;
		}

		const bool bPumpRenderingThread         = (Flags & EParallelForFlags::PumpRenderingThread) != EParallelForFlags::None;
		const ENamedThreads::Type DesiredThread = GetBestDesiredThread(Flags);

		TParallelForData<FunctionType>* DataPtr = new TParallelForData<FunctionType>(Num, AnyThreadTasks + 1, (Num > AnyThreadTasks + 1) && bPumpRenderingThread, Body, Flags);
		TSharedRef<TParallelForData<FunctionType>, ESPMode::ThreadSafe> Data = MakeShareable(DataPtr);
		TGraphTask<TParallelForTask<FunctionType, ContextType>>::CreateTask().ConstructAndDispatchWhenReady(Contexts, 1, Data, DesiredThread, AnyThreadTasks - 1);
		// this thread can help too and this is important to prevent deadlock on recursion 
		if (!Data->Process(Contexts, 0, 0, Data, DesiredThread, true))
		{
			if (bPumpRenderingThread && IsInActualRenderingThread())
			{
				while (!Data->Event->Wait(1))
				{
					FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GetRenderThread_Local());
				}
			}
			else
			{
				Data->Event->Wait();
			}
			check(Data->bTriggered);
		}
		else
		{
			check(!Data->bTriggered);
		}
		check(Data->NumCompleted.GetValue() == Data->Num);

#if DO_CHECK
		Data->bExited.store(true, std::memory_order_relaxed);
#endif

		// DoneEvent waits here if some other thread finishes the last item
		// Data must live on until all of the tasks are cleared which might be long after this function exits
	}

	template<typename FunctionType>
	inline void ParallelForInternal(int32 Num, FunctionType Body, EParallelForFlags Flags)
	{
		ParallelForInternal<FunctionType, nullptr_t>(Num, Body, Flags, nullptr);
	}
	
	/** 
		*	General purpose parallel for that uses the taskgraph
		*	@param Num; number of calls of Body; Body(0), Body(1)....Body(Num - 1)
		*	@param Body; Function to call from multiple threads
		*	@param CurrentThreadWorkToDoBeforeHelping; The work is performed on the main thread before it starts helping with the ParallelFor proper
		*	@param Flags; Used to customize the behavior of the ParallelFor if needed.
		*	Notes: Please add stats around to calls to parallel for and within your lambda as appropriate. Do not clog the task graph with long running tasks or tasks that block.
	**/
	template<typename FunctionType>
	inline void ParallelForWithPreWorkInternal(int32 Num, FunctionType Body, TFunctionRef<void()> CurrentThreadWorkToDoBeforeHelping, EParallelForFlags Flags = EParallelForFlags::None)
	{
		SCOPE_CYCLE_COUNTER(STAT_ParallelFor);

		int32 AnyThreadTasks = 0;
		const bool bIsMultithread = FApp::ShouldUseThreadingForPerformance() || FForkProcessHelper::IsForkedMultithreadInstance();
		if ((Flags & EParallelForFlags::ForceSingleThread) == EParallelForFlags::None && bIsMultithread)
		{
			AnyThreadTasks = FMath::Min<int32>(FTaskGraphInterface::Get().GetNumWorkerThreads(), Num);
		}
		if (!AnyThreadTasks)
		{
			// do the prework
			CurrentThreadWorkToDoBeforeHelping();
			// no threads, just do it and return
			for (int32 Index = 0; Index < Num; Index++)
			{
				Body(Index);
			}
			return;
		}
		check(Num);

		const ENamedThreads::Type DesiredThread = GetBestDesiredThread(Flags);

		TParallelForData<FunctionType>* DataPtr = new TParallelForData<FunctionType>(Num, AnyThreadTasks, false, Body, Flags);
		TSharedRef<TParallelForData<FunctionType>, ESPMode::ThreadSafe> Data = MakeShareable(DataPtr);
		TGraphTask<TParallelForTask<FunctionType>>::CreateTask().ConstructAndDispatchWhenReady(1, Data, DesiredThread, AnyThreadTasks - 1);
		// do the prework
		CurrentThreadWorkToDoBeforeHelping();
		// this thread can help too and this is important to prevent deadlock on recursion 
		if (!Data->Process(0, 0, Data, DesiredThread, true))
		{
			if ((Flags & EParallelForFlags::PumpRenderingThread) != EParallelForFlags::None && IsInRenderingThread())
			{
				while (!Data->Event->Wait(1))
				{
					FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GetRenderThread_Local());
				}
			}
			else
			{
				Data->Event->Wait();
			}
			check(Data->bTriggered);
		}
		else
		{
			check(!Data->bTriggered);
		}
		check(Data->NumCompleted.GetValue() == Data->Num);

#if DO_CHECK
		Data->bExited.store(true, std::memory_order_relaxed);
#endif

		// Data must live on until all of the tasks are cleared which might be long after this function exits
	}
}

/** 
	*	General purpose parallel for that uses the taskgraph
	*	@param Num; number of calls of Body; Body(0), Body(1)....Body(Num - 1)
	*	@param Body; Function to call from multiple threads
	*	@param bForceSingleThread; Mostly used for testing, if true, run single threaded instead.
	*	Notes: Please add stats around to calls to parallel for and within your lambda as appropriate. Do not clog the task graph with long running tasks or tasks that block.
**/
inline void ParallelFor(int32 Num, TFunctionRef<void(int32)> Body, bool bForceSingleThread, bool bPumpRenderingThread=false)
{
	ParallelForImpl::ParallelForInternal(Num, Body,
		(bForceSingleThread ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None) | 
		(bPumpRenderingThread ? EParallelForFlags::PumpRenderingThread : EParallelForFlags::None));
}

/**
	*	General purpose parallel for that uses the taskgraph
	*	@param Num; number of calls of Body; Body(0), Body(1)....Body(Num - 1)
	*	@param Body; Function to call from multiple threads
	*	@param bForceSingleThread; Mostly used for testing, if true, run single threaded instead.
	*	Notes: Please add stats around to calls to parallel for and within your lambda as appropriate. Do not clog the task graph with long running tasks or tasks that block.
**/
template<typename FunctionType>
inline void ParallelForTemplate(int32 Num, const FunctionType& Body, EParallelForFlags Flags = EParallelForFlags::None)
{
	ParallelForImpl::ParallelForInternal(Num, Body, Flags);
}
/** 
	*	General purpose parallel for that uses the taskgraph for unbalanced tasks
	*	Offers better work distribution among threads at the cost of a little bit more synchronization.
	*	This should be used for tasks with highly variable computational time.
	*
	*	@param Num; number of calls of Body; Body(0), Body(1)....Body(Num - 1)
	*	@param Body; Function to call from multiple threads
	*	@param Flags; Used to customize the behavior of the ParallelFor if needed.
	*	Notes: Please add stats around to calls to parallel for and within your lambda as appropriate. Do not clog the task graph with long running tasks or tasks that block.
**/
inline void ParallelFor(int32 Num, TFunctionRef<void(int32)> Body, EParallelForFlags Flags = EParallelForFlags::None)
{
	ParallelForImpl::ParallelForInternal(Num, Body, Flags);
}

/** 
	*	General purpose parallel for that uses the taskgraph
	*	@param Num; number of calls of Body; Body(0), Body(1)....Body(Num - 1)
	*	@param Body; Function to call from multiple threads
	*	@param CurrentThreadWorkToDoBeforeHelping; The work is performed on the main thread before it starts helping with the ParallelFor proper
	*	@param bForceSingleThread; Mostly used for testing, if true, run single threaded instead.
	*	Notes: Please add stats around to calls to parallel for and within your lambda as appropriate. Do not clog the task graph with long running tasks or tasks that block.
**/
inline void ParallelForWithPreWork(int32 Num, TFunctionRef<void(int32)> Body, TFunctionRef<void()> CurrentThreadWorkToDoBeforeHelping, bool bForceSingleThread, bool bPumpRenderingThread = false)
{
	ParallelForImpl::ParallelForWithPreWorkInternal(Num, Body, CurrentThreadWorkToDoBeforeHelping,
		(bForceSingleThread ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None) |
		(bPumpRenderingThread ? EParallelForFlags::PumpRenderingThread : EParallelForFlags::None));
}

/** 
	*	General purpose parallel for that uses the taskgraph
	*	@param Num; number of calls of Body; Body(0), Body(1)....Body(Num - 1)
	*	@param Body; Function to call from multiple threads
	*	@param CurrentThreadWorkToDoBeforeHelping; The work is performed on the main thread before it starts helping with the ParallelFor proper
	*	@param Flags; Used to customize the behavior of the ParallelFor if needed.
	*	Notes: Please add stats around to calls to parallel for and within your lambda as appropriate. Do not clog the task graph with long running tasks or tasks that block.
**/
inline void ParallelForWithPreWork(int32 Num, TFunctionRef<void(int32)> Body, TFunctionRef<void()> CurrentThreadWorkToDoBeforeHelping, EParallelForFlags Flags = EParallelForFlags::None)
{
	ParallelForImpl::ParallelForWithPreWorkInternal(Num, Body, CurrentThreadWorkToDoBeforeHelping, Flags);
}

/** 
	*	General purpose parallel for that uses the taskgraph. This variant constructs for the caller a user-defined context
	* 	object for each task that may get spawned to do work, and passes it on to the loop body to give it a task-local
	*   "workspace" that can be mutated without need for synchronization primitives. For this variant, the user provides a
	* 	callable to construct each context element.
	*	@param OutContexts; Array that will hold the user-defined, task-level context objects (allocated per parallel task)
	*	@param Num; number of calls of Body; Body(0), Body(1)....Body(Num - 1)
	* 	@param ContextConstructor; Function to call to initialize each task context allocated for the operation
	*	@param Body; Function to call from multiple threads
	*	@param Flags; Used to customize the behavior of the ParallelFor if needed.
	*	Notes: Please add stats around to calls to parallel for and within your lambda as appropriate. Do not clog the task graph with long running tasks or tasks that block.
**/
template <typename ContextType, typename ContextAllocatorType, typename ContextConstructorType, typename FunctionType>
inline void ParallelForWithTaskContext(TArray<ContextType, ContextAllocatorType>& OutContexts, int32 Num, const ContextConstructorType& ContextConstructor, const FunctionType& Body, EParallelForFlags Flags = EParallelForFlags::None)
{
	if (Num > 0)
	{
		const int32 NumContexts = ParallelForImpl::GetNumberOfThreadTasks(Num, Flags) + 1;
		OutContexts.Reset();
		OutContexts.AddUninitialized(NumContexts);
		for (int32 ContextIndex = 0; ContextIndex < NumContexts; ++ContextIndex)
		{
			new(&OutContexts[ContextIndex]) ContextType(ContextConstructor(ContextIndex, NumContexts));
		}
		ParallelForImpl::ParallelForInternal(Num, Body, Flags, &OutContexts);
	}
}

/** 
	*	General purpose parallel for that uses the taskgraph. This variant constructs for the caller a user-defined context
	* 	object for each task that may get spawned to do work, and passes it on to the loop body to give it a task-local
	*   "workspace" that can be mutated without need for synchronization primitives.
	*	@param OutContexts; Array that will hold the user-defined, task-level context objects (allocated per parallel task)
	*	@param Num; number of calls of Body; Body(0), Body(1)....Body(Num - 1)
	*	@param Body; Function to call from multiple threads
	*	@param Flags; Used to customize the behavior of the ParallelFor if needed.
	*	Notes: Please add stats around to calls to parallel for and within your lambda as appropriate. Do not clog the task graph with long running tasks or tasks that block.
**/
template <typename ContextType, typename ContextAllocatorType, typename FunctionType>
inline void ParallelForWithTaskContext(TArray<ContextType, ContextAllocatorType>& OutContexts, int32 Num, const FunctionType& Body, EParallelForFlags Flags = EParallelForFlags::None)
{
	if (Num > 0)
	{
		const int32 NumContexts = ParallelForImpl::GetNumberOfThreadTasks(Num, Flags) + 1;
		OutContexts.Reset();
		OutContexts.AddDefaulted(NumContexts);
		ParallelForImpl::ParallelForInternal(Num, Body, Flags, &OutContexts);
	}
}