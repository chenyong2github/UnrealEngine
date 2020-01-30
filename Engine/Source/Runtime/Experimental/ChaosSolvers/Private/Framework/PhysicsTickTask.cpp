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
	, PhysicsSolver(InPhysicsSolver)
	, Dt(InDt)
{
	Module = FChaosSolversModule::GetModule();

	check(Module);
	checkSlow(Module->GetDispatcher() && Module->GetDispatcher()->GetMode() == EChaosThreadingMode::TaskGraph);
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
	using namespace Chaos;

	// The command task runs the two global command queues prior to us running the 
	// per-solver commands and the solver advance
	FGraphEventRef CommandsTask = TGraphTask<FPhysicsCommandsTask>::CreateTask().ConstructAndDispatchWhenReady();

	// Otherwise get a full list of solvers from the solvers module.
	const TArray<FPhysicsSolver*>& SolverList
		= (PhysicsSolver == nullptr)
		? Module->GetSolvers()
		: [&]() { TArray<FPhysicsSolver*> Solvers; Solvers.Init(PhysicsSolver, 1); return Solvers; }();


	// List of active solvers (assume all are active for single alloc)
	TArray<FPhysicsSolver*> ActiveSolvers;
	ActiveSolvers.Reserve(SolverList.Num());

	for(FPhysicsSolver* Solver : SolverList)
	{
		if(Solver->HasActiveParticles() || Solver->HasPendingCommands())
		{
			ActiveSolvers.Add(Solver);
		}
	}

	const int32 NumActiveSolvers = ActiveSolvers.Num();

	// Prereqs for the solver task to run
	FGraphEventArray SolverTaskPrerequisites;
	// Prereqs for the final completion task to run (collection of all the solver tasks)
	FGraphEventArray CompletionTaskPrerequisites;
	// Solver tasks have to depend on the global command queues running before them
	SolverTaskPrerequisites.Add(CommandsTask);

	// For each solver spawn a new solver advance task (which will run the per-solver command buffer and then advance the solver)
	// Record the task reference as a prerequisite for the completion
	for(FPhysicsSolver* Solver : ActiveSolvers)
	{
		FGraphEventRef SolverTaskRef = TGraphTask<FPhysicsSolverAdvanceTask>::CreateTask(&SolverTaskPrerequisites).ConstructAndDispatchWhenReady(Solver, Dt);
		CompletionTaskPrerequisites.Add(SolverTaskRef);
	}

	// Finally send the completion task pending on all the solver tasks
	TGraphTask<FPhysicsTickCompleteTask>::CreateTask(&CompletionTaskPrerequisites).ConstructAndDispatchWhenReady(CompletionEvent);

	// Drop our reference as we don't need it anymore - the completion task handles it
	CompletionEvent = nullptr;
}

//////////////////////////////////////////////////////////////////////////

FPhysicsCommandsTask::FPhysicsCommandsTask()
{
	Module = FChaosSolversModule::GetModule();
	check(Module);

	Dispatcher = Module->GetDispatcher();
	check(Dispatcher->GetMode() == EChaosThreadingMode::TaskGraph);
}

TStatId FPhysicsCommandsTask::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FPhysicsCommandsTask, STATGROUP_TaskGraphTasks);
}

ENamedThreads::Type FPhysicsCommandsTask::GetDesiredThread()
{
	return CPrio_FPhysicsTickTask.Get();
}

ESubsequentsMode::Type FPhysicsCommandsTask::GetSubsequentsMode()
{
	return ESubsequentsMode::TrackSubsequents;
}

void FPhysicsCommandsTask::DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	using namespace Chaos;

	ensureAlways(Dispatcher == Module->GetDispatcher());
	check(Dispatcher);

	Dispatcher = Module->GetDispatcher();
	check(Dispatcher->GetMode() == EChaosThreadingMode::TaskGraph);

	//static FCriticalSection DispatcherExecutionSection;
	//DispatcherExecutionSection.Lock();
	Dispatcher->Execute();
	//DispatcherExecutionSection.Unlock();
}

//////////////////////////////////////////////////////////////////////////

FPhysicsSolverAdvanceTask::FPhysicsSolverAdvanceTask(Chaos::FPhysicsSolver* InSolver, float InDt)
	: Solver(InSolver)
	, Dt(InDt)
{

}

TStatId FPhysicsSolverAdvanceTask::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FPhysicsSolverAdvanceTask, STATGROUP_TaskGraphTasks);
}

ENamedThreads::Type FPhysicsSolverAdvanceTask::GetDesiredThread()
{
	return CPrio_FPhysicsTickTask.Get();
}

ESubsequentsMode::Type FPhysicsSolverAdvanceTask::GetSubsequentsMode()
{
	// The completion task relies on the collection of tick tasks in flight
	return ESubsequentsMode::TrackSubsequents;
}

void FPhysicsSolverAdvanceTask::DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	SCOPE_CYCLE_COUNTER(STAT_ChaosTick);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Physics);

	StepSolver(Solver, Dt);
}

void FPhysicsSolverAdvanceTask::StepSolver(Chaos::FPhysicsSolver* InSolver, float InDt)
{
	using namespace Chaos;

	check(InSolver);

	// Handle our solver commands
	{
		SCOPE_CYCLE_COUNTER(STAT_HandleSolverCommands);

		TQueue<TFunction<void(FPhysicsSolver*)>, EQueueMode::Mpsc>& Queue = InSolver->CommandQueue;
		TFunction<void(FPhysicsSolver*)> Command;
		while(Queue.Dequeue(Command))
		{
			Command(InSolver);
		}
	}

	if(InSolver->bEnabled)
	{
		// Only process if we have something to actually simulate
		if(Solver->HasActiveParticles())
		{
			InSolver->AdvanceSolverBy(InDt);
		}
	}
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
