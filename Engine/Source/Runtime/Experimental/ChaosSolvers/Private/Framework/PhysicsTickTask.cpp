// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/PhysicsTickTask.h"

#include "ChaosSolversModule.h"
#include "Framework/Dispatcher.h"
#include "ChaosStats.h"
#include "PhysicsSolver.h"
#include "PhysicsCoreTypes.h"
#include "ProfilingDebugging/CsvProfiler.h"

FAutoConsoleTaskPriority CPrio_FPhysicsTickTask(
	TEXT("TaskGraph.TaskPriorities.PhysicsTickTask"),
	TEXT("Task and thread priotiry for Chaos physics tick"),
	ENamedThreads::HighThreadPriority, // if we have high priority task threads, then use them...
	ENamedThreads::NormalTaskPriority, // .. at normal task priority
	ENamedThreads::HighTaskPriority // if we don't have hi pri threads, then use normal priority threads at high task priority instead
);

FPhysicsTickTask::FPhysicsTickTask(FGraphEventRef& InCompletionEvent, Chaos::FPhysicsSolver* InPhysicsSolver, float InDt)
	: CompletionEvent(InCompletionEvent)
	, Module(nullptr)
	, Dt(InDt)
{
	check(IsInGameThread());

	Module = FChaosSolversModule::GetModule();

	check(Module);
	
	if(InPhysicsSolver)
	{
		SolverList.Reset(1);
		SolverList.Add(InPhysicsSolver);
	}
	else
	{
		SolverList = Module->GetAllSolvers();
	}

	Module->GetSolverUpdatePrerequisites(SolverTaskPrerequisites);
}

FPhysicsTickTask::FPhysicsTickTask(FGraphEventRef& InCompletionEvent, const TArray<Chaos::FPhysicsSolverBase*>& InSolverList, float InDt)
	: CompletionEvent(InCompletionEvent)
	, Module(nullptr)
	, Dt(InDt)
{
	check(IsInGameThread());

	Module = FChaosSolversModule::GetModule();

	check(Module);
	
	SolverList = InSolverList;
	Module->GetSolverUpdatePrerequisites(SolverTaskPrerequisites);
}

FPhysicsTickTask::FPhysicsTickTask(FGraphEventRef& InCompletionEvent, float InDt)
	: FPhysicsTickTask(InCompletionEvent, nullptr, InDt)
{

}

TStatId FPhysicsTickTask::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FPhysicsTickTask, STATGROUP_TaskGraphTasks);
}

ENamedThreads::Type FPhysicsTickTask::GetDesiredThread()
{
	return CPrio_FPhysicsTickTask.Get();
}

ESubsequentsMode::Type FPhysicsTickTask::GetSubsequentsMode()
{
	return ESubsequentsMode::TrackSubsequents;
}

void FPhysicsTickTask::DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
#if 0
	using namespace Chaos;


	// List of active solvers (assume all are active for single alloc)
	TArray<FPhysicsSolverBase*> ActiveSolvers;
	ActiveSolvers.Reserve(SolverList.Num());

	for(FPhysicsSolverBase* Solver : SolverList)
	{
		Solver->CastHelper([&ActiveSolvers](auto& ConcreteSolver)
		{
			if(ConcreteSolver.HasActiveParticles() || ConcreteSolver.HasPendingCommands())
			{
				ActiveSolvers.Add(&ConcreteSolver);
			}
		});
	}

	const int32 NumActiveSolvers = ActiveSolvers.Num();

	// Prereqs for the final completion task to run (collection of all the solver tasks)
	FGraphEventArray CompletionTaskPrerequisites;

	// For each solver spawn a new solver advance task (which will run the per-solver command buffer and then advance the solver)
	// Record the task reference as a prerequisite for the completion
	for(FPhysicsSolverBase* Solver : ActiveSolvers)
	{
		FGraphEventRef SolverTaskRef = TGraphTask<FPhysicsSolverAdvanceTask>::CreateTask(&SolverTaskPrerequisites).ConstructAndDispatchWhenReady(Solver, Dt);
		CompletionTaskPrerequisites.Add(SolverTaskRef);
	}

	// Finally send the completion task pending on all the solver tasks
	TGraphTask<FPhysicsTickCompleteTask>::CreateTask(&CompletionTaskPrerequisites).ConstructAndDispatchWhenReady(CompletionEvent);

	// Drop our reference as we don't need it anymore - the completion task handles it
	CompletionEvent = nullptr;
#endif
}

//////////////////////////////////////////////////////////////////////////

FPhysicsTickCompleteTask::FPhysicsTickCompleteTask(FGraphEventRef& InCompletionEvent)
	: CompletionEvent(InCompletionEvent)
{

}

TStatId FPhysicsTickCompleteTask::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FPhysicsTickCompleteTask, STATGROUP_TaskGraphTasks);
}

ENamedThreads::Type FPhysicsTickCompleteTask::GetDesiredThread()
{
	return CPrio_FPhysicsTickTask.Get();
}

ESubsequentsMode::Type FPhysicsTickCompleteTask::GetSubsequentsMode()
{
	// No need to track subsequents for this task as it's the last in the chain and shouldn't be a dependency
	return ESubsequentsMode::FireAndForget;
}

void FPhysicsTickCompleteTask::DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	check(CompletionEvent.GetReference()); // Make sure the event still exists

	// Fire off the subsequents on the completion event that we were provided at the beginning of our tick
	TArray<FBaseGraphTask*> NewTasks;
	CompletionEvent->DispatchSubsequents(NewTasks, ENamedThreads::AnyThread);
}

//////////////////////////////////////////////////////////////////////////
