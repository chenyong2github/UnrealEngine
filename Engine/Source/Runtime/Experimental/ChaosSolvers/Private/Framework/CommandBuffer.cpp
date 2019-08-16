// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#if INCLUDE_CHAOS

#include "Framework/CommandBuffer.h"
#include "ChaosSolversModule.h"
#include "Framework/Dispatcher.h"

bool Chaos::FCommandListData::HasCommands() const
{
	const int32 NumGlobalCommands = GlobalCommands.Num();
	const int32 NumTaskCommands = TaskCommands.Num();
	const int32 NumSolverCommands = SolverCommands.Num();

	return (NumGlobalCommands + NumTaskCommands + NumSolverCommands) > 0;
}

Chaos::FCommandList::FCommandList()
	: Data(nullptr)
{
	AllocData();
}

void Chaos::FCommandList::Flush()
{
	if(!Data || !Data->HasCommands())
	{
		// Nothing to flush here
		return;
	}

	Chaos::IDispatcher* Dispatcher = FChaosSolversModule::GetModule()->GetDispatcher();

	// Send the commands, the dispatcher will steal the pointer
	Dispatcher->SubmitCommandList(MoveTemp(Data));

	// Create new data object for subsequent commands as we've just handed the other
	// data ptr to the dispatcher
	AllocData();
}

void Chaos::FCommandList::Enqueue(IDispatcher::FGlobalCommand&& InCommand)
{
	Data->GlobalCommands.Add(InCommand);
}

void Chaos::FCommandList::Enqueue(FPhysicsSolver* InSolver, IDispatcher::FSolverCommand&& InCommand)
{
	Data->SolverCommands.Add(FCommandListData::FSolverCommandTuple(InSolver, InCommand));
}

void Chaos::FCommandList::Enqueue(IDispatcher::FTaskCommand&& InCommand)
{
	Data->TaskCommands.Add(InCommand);
}

void Chaos::FCommandList::AllocData()
{
	// Enable construction for command list data for the make call below
	class FCommandList_Constructor : public FCommandListData
	{};

	Data = MakeUnique<FCommandList_Constructor>();
}

#endif
