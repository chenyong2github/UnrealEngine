// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/Framework/PhysicsSolverBase.h"
#include "Chaos/Framework/PhysicsProxyBase.h"
#include "ProfilingDebugging/CsvProfiler.h"

namespace Chaos
{	
	void FPhysicsSolverBase::ChangeBufferMode(EMultiBufferMode InBufferMode)
	{
		BufferMode = InBufferMode;
	}

	FAutoConsoleTaskPriority CPrio_FPhysicsTickTask(
		TEXT("TaskGraph.TaskPriorities.PhysicsTickTask"),
		TEXT("Task and thread priotiry for Chaos physics tick"),
		ENamedThreads::HighThreadPriority, // if we have high priority task threads, then use them...
		ENamedThreads::NormalTaskPriority, // .. at normal task priority
		ENamedThreads::HighTaskPriority // if we don't have hi pri threads, then use normal priority threads at high task priority instead
	);

	FPhysicsSolverAdvanceTask::FPhysicsSolverAdvanceTask(FPhysicsSolverBase* InSolver,FReal InDt)
		: Solver(InSolver)
		, Dt(InDt)
	{
		//todo: make this based on proper dt etc...
		Queue = MoveTemp(Solver->CommandQueue);
	}

	TStatId FPhysicsSolverAdvanceTask::GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FPhysicsSolverAdvanceTask,STATGROUP_TaskGraphTasks);
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

	void FPhysicsSolverAdvanceTask::DoTask(ENamedThreads::Type CurrentThread,const FGraphEventRef& MyCompletionGraphEvent)
	{
		using namespace Chaos;

		LLM_SCOPE(ELLMTag::Chaos);
		QUICK_SCOPE_CYCLE_COUNTER(STAT_ChaosTick);
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Physics);

		// Handle our solver commands
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_HandleSolverCommands);
			for(const auto& Command : Queue)
			{
				Command();
			}
		}

		Solver->AdvanceSolverBy(Dt);
	}

	//////////////////////////////////////////////////////////////////////////
}
