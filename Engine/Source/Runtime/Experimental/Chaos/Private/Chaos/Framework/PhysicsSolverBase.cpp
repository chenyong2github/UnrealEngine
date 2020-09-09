// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/Framework/PhysicsSolverBase.h"
#include "Chaos/Framework/PhysicsProxyBase.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "ChaosStats.h"
#include "Chaos/PendingSpatialData.h"

namespace Chaos
{	
	void FPhysicsSolverBase::ChangeBufferMode(EMultiBufferMode InBufferMode)
	{
		BufferMode = InBufferMode;
	}

	FDelegateHandle FPhysicsSolverBase::AddPreAdvanceCallback(FSolverPreAdvance::FDelegate InDelegate)
	{
		return EventPreSolve.Add(InDelegate);
	}

	bool FPhysicsSolverBase::RemovePreAdvanceCallback(FDelegateHandle InHandle)
	{
		return EventPreSolve.Remove(InHandle);
	}

	FDelegateHandle FPhysicsSolverBase::AddPreBufferCallback(FSolverPreBuffer::FDelegate InDelegate)
	{
		return EventPreBuffer.Add(InDelegate);
	}

	bool FPhysicsSolverBase::RemovePreBufferCallback(FDelegateHandle InHandle)
	{
		return EventPreBuffer.Remove(InHandle);
	}

	FDelegateHandle FPhysicsSolverBase::AddPostAdvanceCallback(FSolverPostAdvance::FDelegate InDelegate)
	{
		return EventPostSolve.Add(InDelegate);
	}

	bool FPhysicsSolverBase::RemovePostAdvanceCallback(FDelegateHandle InHandle)
	{
		return EventPostSolve.Remove(InHandle);
	}

	FAutoConsoleTaskPriority CPrio_FPhysicsTickTask(
		TEXT("TaskGraph.TaskPriorities.PhysicsTickTask"),
		TEXT("Task and thread priotiry for Chaos physics tick"),
		ENamedThreads::HighThreadPriority, // if we have high priority task threads, then use them...
		ENamedThreads::NormalTaskPriority, // .. at normal task priority
		ENamedThreads::HighTaskPriority // if we don't have hi pri threads, then use normal priority threads at high task priority instead
	);

	FPhysicsSolverAdvanceTask::FPhysicsSolverAdvanceTask(FPhysicsSolverBase& InSolver, TArray<TFunction<void()>>&& InQueue, TArray<FPushPhysicsData*>&& InPushData, FReal InDt)
		: Solver(InSolver)
		, Queue(MoveTemp(InQueue))
		, PushData(MoveTemp(InPushData))
		, Dt(InDt)
	{
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
		AdvanceSolver();
	}

	void FPhysicsSolverAdvanceTask::AdvanceSolver()
	{
		using namespace Chaos;

		LLM_SCOPE(ELLMTag::Chaos);
		SCOPE_CYCLE_COUNTER(STAT_ChaosTick);
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Physics);

		Solver.ProcessPushedData_Internal(PushData);

		// Handle our solver commands
		{
			SCOPE_CYCLE_COUNTER(STAT_HandleSolverCommands);
			for(const auto& Command : Queue)
			{
				Command();
			}
		}

		Solver.AdvanceSolverBy(Dt);
	}

	FPhysicsSolverBase::FPhysicsSolverBase(const EMultiBufferMode BufferingModeIn,const EThreadingModeTemp InThreadingMode,UObject* InOwner,ETraits InTraitIdx)
		: BufferMode(BufferingModeIn)
		, ThreadingMode(InThreadingMode)
		, PendingSpatialOperations_External(MakeUnique<FPendingSpatialDataQueue>())
		, bPaused_External(false)
		, Owner(InOwner)
		, TraitIdx(InTraitIdx)
	{
	}

	FPhysicsSolverBase::~FPhysicsSolverBase() = default;


	void FPhysicsSolverBase::UpdateParticleInAccelerationStructure_External(TGeometryParticle<FReal,3>* Particle,bool bDelete)
	{
		//mark it as pending for async structure being built
		TAccelerationStructureHandle<float,3> AccelerationHandle(Particle);
		FPendingSpatialData& SpatialData = PendingSpatialOperations_External->FindOrAdd(Particle->UniqueIdx());

		//make sure any new operations (i.e not currently being consumed by sim) are not acting on a deleted object
		ensure(SpatialData.SyncTimestamp <= MarshallingManager.GetExternalTimestampConsumed_External() || !SpatialData.bDelete);

		SpatialData.bDelete = bDelete;
		SpatialData.SpatialIdx = Particle->SpatialIdx();
		SpatialData.AccelerationHandle = AccelerationHandle;
		SpatialData.SyncTimestamp = MarshallingManager.GetExternalTimestamp_External();

		if(IPhysicsProxyBase* Proxy = Particle->GetProxy())
		{
			Proxy->SetSyncTimestamp(SpatialData.SyncTimestamp);
		}
	}

	//////////////////////////////////////////////////////////////////////////
}
