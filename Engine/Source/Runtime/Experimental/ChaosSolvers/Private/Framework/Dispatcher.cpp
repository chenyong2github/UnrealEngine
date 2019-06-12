// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#if INCLUDE_CHAOS

#include "Framework/Dispatcher.h"

#include "PBDRigidsSolver.h"

namespace Chaos
{
	template<>
	void FDispatcher<EThreadingMode::DedicatedThread>::EnqueueCommand(FPBDRigidsSolver* InSolver, TFunction<void(FPBDRigidsSolver *)> InCommand) const
	{
		check(InSolver && InCommand);
		InSolver->GetCommandQueue().Enqueue(InCommand);
	}

	template<>
	void FDispatcher<EThreadingMode::DedicatedThread>::EnqueueCommand(TFunction<void(FPersistentPhysicsTask*)> InCommand)
	{
		check(Owner);
		TaskCommandQueue.Enqueue(InCommand);
	}

	template<>
	void FDispatcher<EThreadingMode::DedicatedThread>::EnqueueCommand(TFunction<void()> InCommand)
	{
		check(Owner);
		GlobalCommandQueue.Enqueue(InCommand);
	}

	//////////////////////////////////////////////////////////////////////////

	template<>
	void FDispatcher<EThreadingMode::SingleThread>::EnqueueCommand(FPBDRigidsSolver* InSolver, TFunction<void(FPBDRigidsSolver *)> InCommand) const
	{
		check(InSolver && InCommand);
		InCommand(InSolver);
	}

	template<>
	void FDispatcher<EThreadingMode::SingleThread>::EnqueueCommand(TFunction<void(FPersistentPhysicsTask*)> InCommand)
	{
		check(Owner);
		InCommand(nullptr);
	}

	template<>
	void FDispatcher<EThreadingMode::SingleThread>::EnqueueCommand(TFunction<void()> InCommand)
	{
		check(Owner);
		InCommand();
	}

	//////////////////////////////////////////////////////////////////////////

	template<>
	void FDispatcher<EThreadingMode::TaskGraph>::EnqueueCommand(FPBDRigidsSolver* InSolver, TFunction<void(FPBDRigidsSolver *)> InCommand) const
	{
		check(InSolver && InCommand);
		InSolver->GetCommandQueue().Enqueue(InCommand);
	}

	template<>
	void FDispatcher<EThreadingMode::TaskGraph>::EnqueueCommand(TFunction<void(FPersistentPhysicsTask*)> InCommand)
	{
		check(Owner);
		TaskCommandQueue.Enqueue(InCommand);
	}

	template<>
	void FDispatcher<EThreadingMode::TaskGraph>::EnqueueCommand(TFunction<void()> InCommand)
	{
		check(Owner);
		GlobalCommandQueue.Enqueue(InCommand);
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
#endif
