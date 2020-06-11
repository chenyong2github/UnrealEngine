// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/Dispatcher.h"
#include "DispatcherImpl.h"

#include "PhysicsSolver.h"
#include "ChaosSolversModule.h"
#include "ChaosStats.h"
#include "PhysicsCoreTypes.h"

namespace Chaos
{
	template<>
	void FDispatcher<EThreadingMode::DedicatedThread>::EnqueueCommandImmediate(FTaskCommand InCommand)
	{
		check(Owner);
		TaskCommandQueue.Enqueue(InCommand);
	}

	template<>
	void FDispatcher<EThreadingMode::DedicatedThread>::Execute()
	{
#if 0
		check(!IsInGameThread());

		TUniquePtr<FCommandListData> Data = nullptr;
		FPersistentPhysicsTask* Task = Owner->GetDedicatedTask();

		// Enqueue all pending command lists
		while(CommandLists.Dequeue(Data))
		{
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
#endif
	}

	//////////////////////////////////////////////////////////////////////////

	
	template<>
	void FDispatcher<EThreadingMode::SingleThread>::EnqueueCommandImmediate(FTaskCommand InCommand)
	{
		check(Owner);
		InCommand(nullptr);
	}

	template<>
	void FDispatcher<EThreadingMode::SingleThread>::Execute()
	{
		ensureMsgf(false, TEXT("Single threaded dispatcher should never be executed as commands are processed immediately."));
	}
	//////////////////////////////////////////////////////////////////////////

	
	template<>
	void FDispatcher<EThreadingMode::TaskGraph>::EnqueueCommandImmediate(FTaskCommand InCommand)
	{
		check(Owner);
		TaskCommandQueue.Enqueue(InCommand);
	}



	template<>
	void FDispatcher<EThreadingMode::TaskGraph>::Execute()
	{
		
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
