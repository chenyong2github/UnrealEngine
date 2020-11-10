// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/Framework/PhysicsSolverBase.h"
#include "Chaos/Framework/PhysicsProxyBase.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "ChaosStats.h"
#include "Chaos/PendingSpatialData.h"
#include "PBDRigidsSolver.h"
#include "Chaos/PullPhysicsDataImp.h"

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

	FPhysicsSolverAdvanceTask::FPhysicsSolverAdvanceTask(FPhysicsSolverBase& InSolver, TArray<TFunction<void()>>&& InQueue, TArray<FPushPhysicsData*>&& InPushData, FReal InDt, int32 InInputDataExternalTimestamp)
		: Solver(InSolver)
		, Queue(MoveTemp(InQueue))
		, PushData(MoveTemp(InPushData))
		, Dt(InDt)
		, InputDataExternalTimestamp(InInputDataExternalTimestamp)
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

		Solver.SetExternalTimestampConsumed_Internal(InputDataExternalTimestamp);
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

	CHAOS_API float DefaultAsyncDt = -1;
	FAutoConsoleVariableRef CVarDefaultAsyncDt(TEXT("p.DefaultAsyncDt"), DefaultAsyncDt,TEXT("Whether to use async results -1 means not async"));

	CHAOS_API int32 UseAsyncInterpolation = 1;
	FAutoConsoleVariableRef CVarUseAsyncInterpolation(TEXT("p.UseAsyncInterpolation"), UseAsyncInterpolation, TEXT("Whether to interpolate when async mode is enabled"));

	CHAOS_API int32 ForceDisableAsyncPhysics = 0;
	FAutoConsoleVariableRef CVarForceDisableAsyncPhysics(TEXT("p.ForceDisableAsyncPhysics"), ForceDisableAsyncPhysics, TEXT("Whether to force async physics off regardless of other settings"));

	FPhysicsSolverBase::FPhysicsSolverBase(const EMultiBufferMode BufferingModeIn,const EThreadingModeTemp InThreadingMode,UObject* InOwner,ETraits InTraitIdx)
		: BufferMode(BufferingModeIn)
		, ThreadingMode(InThreadingMode)
		, PendingSpatialOperations_External(MakeUnique<FPendingSpatialDataQueue>())
		, bPaused_External(false)
		, Owner(InOwner)
		, ExternalDataLock_External(new FPhysicsSceneGuard())
		, TraitIdx(InTraitIdx)
		, AsyncDt(DefaultAsyncDt)
#if !UE_BUILD_SHIPPING
		, bStealAdvanceTasksForTesting(false)
#endif
	{
	}

	FPhysicsSolverBase::~FPhysicsSolverBase() = default;

	void FPhysicsSolverBase::DestroySolver(FPhysicsSolverBase& InSolver)
	{
		// Please read the comments this is a minefield.
				
		const bool bIsSingleThreadEnvironment = FPlatformProcess::SupportsMultithreading() == false;
		if (bIsSingleThreadEnvironment == false)
		{
			// In Multithreaded: DestroySolver should only be called if we are not waiting on async work.
			// This should be called when World/Scene are cleaning up, World implements IsReadyForFinishDestroy() and returns false when async work is still going.
			// This means that garbage collection should not cleanup world and this solver until this async work is complete.
			// We do it this way because it is unsafe for us to block on async task in this function, as it is unsafe to block on a task during GC, as this may schedule
			// another task that may be unsafe during GC, and cause crashes.
			ensure(InSolver.IsPendingTasksComplete());
		}
		else
		{
			// In Singlethreaded: We cannot wait for any tasks in IsReadyForFinishDestroy() (on World) so it always returns true in single threaded.
			// Task will never complete during GC in single theading, as there are no threads to do it.
			// so we have this wait below to allow single threaded to complete pending tasks before solver destroy.

			InSolver.WaitOnPendingTasks_External();
		}

		//make sure any pending commands are executed
		//we don't have a flush function because of dt concerns (don't want people flushing because commands end up in wrong dt)
		//but in this case we just need to ensure all resources are freed
		for(const auto& Command : InSolver.CommandQueue)
		{
			Command();
		}
		
		// GeometryCollection particles do not always remove collision constraints on unregister,
		// explicitly clear constraints so we will not crash when filling collision events in advance.
		InSolver.CastHelper([](auto& Concrete)
		{
			auto* Evolution = Concrete.GetEvolution();
			if (Evolution)
			{
				Evolution->ResetConstraints();
			}
		});

		// Advance in single threaded because we cannot block on an async task here if in multi threaded mode. see above comments.
		InSolver.SetThreadingMode_External(EThreadingModeTemp::SingleThread);
		InSolver.AdvanceAndDispatch_External(0);

		// Ensure callbacks actually get cleaned up, only necessary when solver is disabled.
		InSolver.ApplyCallbacks_Internal(0, 0);
		InSolver.FreeCallbacksData_Internal(0, 0);

		// verify callbacks have been processed and we're not leaking.
		// TODO: why is this still firing in 14.30? (Seems we're still leaking)
		//ensure(InSolver.SimCallbacks.Num() == 0);

		delete &InSolver;
	}

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

#if !UE_BUILD_SHIPPING
	void FPhysicsSolverBase::SetStealAdvanceTasks_ForTesting(bool bInStealAdvanceTasksForTesting)
	{
		bStealAdvanceTasksForTesting = bInStealAdvanceTasksForTesting;
	}

	void FPhysicsSolverBase::PopAndExecuteStolenAdvanceTask_ForTesting()
	{
		ensure(ThreadingMode == EThreadingModeTemp::SingleThread);
		if (ensure(StolenSolverAdvanceTasks.Num() > 0))
		{
			StolenSolverAdvanceTasks[0].AdvanceSolver();
			StolenSolverAdvanceTasks.RemoveAt(0);
		}
	}
#endif

	void FPhysicsSolverBase::TrackGTParticle_External(TGeometryParticle<FReal, 3>& Particle)
	{
		const int32 Idx = Particle.UniqueIdx().Idx;
		const int32 SlotsNeeded = Idx + 1 - UniqueIdxToGTParticles.Num();
		if (SlotsNeeded > 0)
		{
			UniqueIdxToGTParticles.AddZeroed(SlotsNeeded);
		}

		UniqueIdxToGTParticles[Idx] = &Particle;
	}

	void FPhysicsSolverBase::ClearGTParticle_External(TGeometryParticle<FReal, 3>& Particle)
	{
		const int32 Idx = Particle.UniqueIdx().Idx;
		if (ensure(Idx < UniqueIdxToGTParticles.Num()))
		{
			UniqueIdxToGTParticles[Idx] = nullptr;
		}
	}


	FChaosPullPhysicsResults FChaosResultsManager::PullPhysicsResults_External(FChaosMarshallingManager& MarshallingManager, const FReal ResultsTime, const bool bUseAsync)
	{
		if (bUseAsync)
		{
			//in async mode we must interpolate between prev and next, where ResultsTime is in the inclusive interval [Prev, Next]

			if (Results.Next == nullptr)
			{
				ensure(Results.Prev == nullptr);	//if next is null, prev must also be null since we haven't consumed any results yet
				Results.Next = MarshallingManager.PopPullData_External();
			}

			while (Results.Next && Results.Next->ExternalStartTime < ResultsTime)
			{
				if (Results.Prev)
				{
					MarshallingManager.FreePullData_External(Results.Prev);
				}

				Results.Prev = Results.Next;
				Results.Next = MarshallingManager.PopPullData_External();
			}

			if (Results.Prev != nullptr && Results.Next == nullptr)
			{
				//we are lagging and can't interpolate
				//UE_LOG(LogChaos, Warning, TEXT("Physics interpolation failed: Is the delay too small?"));

				Results.Next = Results.Prev;	//no interpolation, so results are only in next and nothing in prev
				Results.Prev = nullptr;

				//question: is it better to put it in prev results since time has exceeded?
			}
		}
		else
		{
			//sync mode doesn't use prev results, but if we were async previously we need to clean it up
			if (Results.Prev)
			{
				MarshallingManager.FreePullData_External(Results.Prev);
				Results.Prev = nullptr;
			}

			//if we switched from async to sync we may have multiple pending results, so discard them all except latest
			FPullPhysicsData* PullDataLatest = nullptr;
			do
			{
				//free any old results
				if (Results.Next)
				{
					MarshallingManager.FreePullData_External(Results.Next);
				}

				Results.Next = PullDataLatest;
				PullDataLatest = MarshallingManager.PopPullData_External();

			} while (PullDataLatest);
		}

		return Results;
	}

	//////////////////////////////////////////////////////////////////////////
}
