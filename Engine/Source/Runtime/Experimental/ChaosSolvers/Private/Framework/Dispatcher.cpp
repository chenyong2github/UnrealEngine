// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#if INCLUDE_CHAOS

#include "Framework/Dispatcher.h"
#include "DispatcherImpl.h"

#include "PhysicsSolver.h"
#include "Framework/CommandBuffer.h"
#include "ChaosSolversModule.h"
#include "ChaosStats.h"
#include "PhysicsCoreTypes.h"

namespace Chaos
{
	template<>
	void FDispatcher<EThreadingMode::DedicatedThread>::EnqueueCommandImmediate(FPhysicsSolver* InSolver, FSolverCommand InCommand)
	{
		check(InSolver && InCommand);
		InSolver->GetCommandQueue().Enqueue(InCommand);
	}

	template<>
	void FDispatcher<EThreadingMode::DedicatedThread>::EnqueueCommandImmediate(FTaskCommand InCommand)
	{
		check(Owner);
		TaskCommandQueue.Enqueue(InCommand);
	}

	template<>
	void FDispatcher<EThreadingMode::DedicatedThread>::EnqueueCommandImmediate(FGlobalCommand InCommand)
	{
		check(Owner);
		GlobalCommandQueue.Enqueue(InCommand);
	}

	template<>
	void FDispatcher<EThreadingMode::DedicatedThread>::SubmitCommandList(TUniquePtr<FCommandListData>&& InCommandData)
	{
		CommandLists.Enqueue(MoveTemp(InCommandData));
	}

	template<>
	void FDispatcher<EThreadingMode::DedicatedThread>::Execute()
	{
		check(!IsInGameThread());

		TUniquePtr<FCommandListData> Data = nullptr;
		FPersistentPhysicsTask* Task = Owner->GetDedicatedTask();

		// Enqueue all pending command lists
		while(CommandLists.Dequeue(Data))
		{
			for(FGlobalCommand& C : Data->GlobalCommands)
			{
				EnqueueCommandImmediate(C);
			}

			for(FTaskCommand& C : Data->TaskCommands)
			{
				EnqueueCommandImmediate(C);
			}

			for(TTuple<FPhysicsSolver*, FSolverCommand> Pair : Data->SolverCommands)
			{
				EnqueueCommandImmediate(Pair.Get<0>(), Pair.Get<1>());
			}
		}

		// Execute global and task commands.
		{
			SCOPE_CYCLE_COUNTER(STAT_PhysCommands);
			TFunction<void()> GlobalCommand;
			while(GlobalCommandQueue.Dequeue(GlobalCommand))
			{
				GlobalCommand();
			}
		}

		{
			SCOPE_CYCLE_COUNTER(STAT_TaskCommands);
			TFunction<void(FPersistentPhysicsTask*)> TaskCommand;
			while(TaskCommandQueue.Dequeue(TaskCommand))
			{
				// No dedicated thread task in this threading mode.
				TaskCommand(Task);
			}
		}
	}

	//////////////////////////////////////////////////////////////////////////

	template<>
	void FDispatcher<EThreadingMode::SingleThread>::EnqueueCommandImmediate(FPhysicsSolver* InSolver, FSolverCommand InCommand)
	{
		check(InSolver && InCommand);
		InCommand(InSolver);
	}

	template<>
	void FDispatcher<EThreadingMode::SingleThread>::EnqueueCommandImmediate(FTaskCommand InCommand)
	{
		check(Owner);
		InCommand(nullptr);
	}

	template<>
	void FDispatcher<EThreadingMode::SingleThread>::EnqueueCommandImmediate(FGlobalCommand InCommand)
	{
		check(Owner);
		InCommand();
	}

	template<>
	void FDispatcher<EThreadingMode::SingleThread>::SubmitCommandList(TUniquePtr<FCommandListData>&& InCommandData)
	{
		// Steal the ptr from the external caller still to emulate the same experience under all dispatchers
		TUniquePtr<FCommandListData> Data = MoveTemp(InCommandData);

		// Just pass to enqueue
		for(FGlobalCommand& C : Data->GlobalCommands)
		{
			EnqueueCommandImmediate(C);
		}

		for(FTaskCommand& C : Data->TaskCommands)
		{
			EnqueueCommandImmediate(C);
		}

		for(const TTuple<FPhysicsSolver*, FSolverCommand>& Pair : Data->SolverCommands)
		{
			EnqueueCommandImmediate(Pair.Get<0>(), Pair.Get<1>());
		}
	}

	template<>
	void FDispatcher<EThreadingMode::SingleThread>::Execute()
	{
		ensureMsgf(false, TEXT("Single threaded dispatcher should never be executed as commands are processed immediately."));
	}

	//////////////////////////////////////////////////////////////////////////

	template<>
	void FDispatcher<EThreadingMode::TaskGraph>::EnqueueCommandImmediate(FPhysicsSolver* InSolver, FSolverCommand InCommand)
	{
		check(InSolver && InCommand);
		InSolver->GetCommandQueue().Enqueue(InCommand);
	}

	template<>
	void FDispatcher<EThreadingMode::TaskGraph>::EnqueueCommandImmediate(FTaskCommand InCommand)
	{
		check(Owner);
		TaskCommandQueue.Enqueue(InCommand);
	}

	template<>
	void FDispatcher<EThreadingMode::TaskGraph>::EnqueueCommandImmediate(FGlobalCommand InCommand)
	{
		check(Owner);
		GlobalCommandQueue.Enqueue(InCommand);
	}

	template<>
	void FDispatcher<EThreadingMode::TaskGraph>::SubmitCommandList(TUniquePtr<FCommandListData>&& InCommandData)
	{
		CommandLists.Enqueue(MoveTemp(InCommandData));
	}

	template<>
	void FDispatcher<EThreadingMode::TaskGraph>::Execute()
	{
		TUniquePtr<FCommandListData> Data = nullptr;

		// Enqueue all pending command lists
		while(CommandLists.Dequeue(Data))
		{
			for(FGlobalCommand& C : Data->GlobalCommands)
			{
				EnqueueCommandImmediate(C);
			}

			for(FTaskCommand& C : Data->TaskCommands)
			{
				EnqueueCommandImmediate(C);
			}

			for(TTuple<FPhysicsSolver*, FSolverCommand> Pair : Data->SolverCommands)
			{
				EnqueueCommandImmediate(Pair.Get<0>(), Pair.Get<1>());
			}
		}

		// Execute global and task commands.
		{
			SCOPE_CYCLE_COUNTER(STAT_PhysCommands);
			TFunction<void()> GlobalCommand;
			while(GlobalCommandQueue.Dequeue(GlobalCommand))
			{
				GlobalCommand();
			}
		}

		{
			SCOPE_CYCLE_COUNTER(STAT_TaskCommands);
			TFunction<void(FPersistentPhysicsTask*)> TaskCommand;
			while(TaskCommandQueue.Dequeue(TaskCommand))
			{
				// No dedicated thread task in this threading mode.
				TaskCommand(nullptr);
			}
		}
	}
}

void LexFromString(Chaos::EThreadingMode& OutValue, const TCHAR* InString) 
{
	OutValue = Chaos::EThreadingMode::Invalid;

	if(FCString::Stricmp(InString, TEXT("DedicatedThread")) == 0)
	{
		OutValue = Chaos::EThreadingMode::DedicatedThread;
	}
	else if(FCString::Stricmp(InString, TEXT("TaskGraph")) == 0) 
	{
		OutValue = Chaos::EThreadingMode::TaskGraph;
	}
	else if(FCString::Stricmp(InString, TEXT("SingleThread")) == 0)
	{
		OutValue = Chaos::EThreadingMode::SingleThread;
	}
}

FString LexToString(const Chaos::EThreadingMode InValue)
{
	switch(InValue)
	{
	case Chaos::EThreadingMode::DedicatedThread:
		return TEXT("DedicatedThread");
	case Chaos::EThreadingMode::TaskGraph:
		return TEXT("TaskGraph");
	case Chaos::EThreadingMode::SingleThread:
		return TEXT("SingleThread");
	default:
		break;
	}

	return TEXT("");
}

template class Chaos::FDispatcher<EChaosThreadingMode::DedicatedThread>;
template class Chaos::FDispatcher<EChaosThreadingMode::SingleThread>;
template class Chaos::FDispatcher<EChaosThreadingMode::TaskGraph>;

#endif
