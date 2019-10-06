// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Framework/PersistentTask.h"

#include "Modules/ModuleManager.h"

#include "ChaosLog.h"
#include "ChaosStats.h"
#include "ChaosSolversModule.h"

#include "Chaos/Framework/Parallel.h"
#include "Framework/TimeStep.h"
#include "PhysicsSolver.h"
#include "Field/FieldSystem.h"

#include "PhysicsProxy/SkeletalMeshPhysicsProxy.h"
#include "PhysicsProxy/StaticMeshPhysicsProxy.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#include "PhysicsProxy/FieldSystemPhysicsProxy.h"

#ifndef CHAOS_WITH_PAUSABLE_SOLVER
#define CHAOS_WITH_PAUSABLE_SOLVER 1
#endif

namespace Chaos
{
	FPersistentPhysicsTask::FPersistentPhysicsTask(float InTargetDt, bool bInAvoidSpiral, IDispatcher* InDispatcher)
		: TickMode(EChaosSolverTickMode::VariableCappedWithTarget)
		, CommandDispatcher(InDispatcher)
		, Timestep(nullptr)
#if WITH_EDITOR
		, SingleStepCounter(0)
#endif
	{
		ShutdownEvent = FPlatformProcess::GetSynchEventFromPool(true);

		Timestep = new Chaos::FVariableMinimumWithCapTimestep();
	}

	FPersistentPhysicsTask::~FPersistentPhysicsTask()
	{
		if(Timestep)
		{
			delete Timestep;
			Timestep = nullptr;
		}

		FPlatformProcess::ReturnSynchEventToPool(ShutdownEvent);
	}

	void FPersistentPhysicsTask::DoWork()
	{
		// Capture solver states from the module by copying the current state. The module
		// will inject any new solvers with a command.
		FChaosSolversModule& ChaosModule = FModuleManager::Get().GetModuleChecked<FChaosSolversModule>("ChaosSolvers");
		Solvers = ChaosModule.GetSolvers();

#if CHAOS_DEBUG_SUBSTEP
		// Prepare the debug substepping tasks for all existing solvers
		for (FPhysicsSolver* Solver : Solvers)
		{
			DebugSolverTasks.Add(Solver);
		}
#endif

		bRunning = true;
		ShutdownEvent->Reset();

		float Dt = 0.0f;

		// Set up for first frame
		Timestep->Reset();

		// Scratch array for active solver list
		TArray<Chaos::FPhysicsSolver*> ActiveSolverList;

		while(bRunning)
		{
			SCOPE_CYCLE_COUNTER(STAT_PhysicsAdvance);

			CommandDispatcher->Execute();

			Dt = Timestep->GetCalculatedDt();

#if WITH_EDITOR
			const bool bShouldStepSolvers = ChaosModule.ShouldStepSolver(SingleStepCounter);
			if (bShouldStepSolvers)
#endif
			{
				// Go wide if possible on the solvers
				ActiveSolverList.Reset(Solvers.Num());
				for(Chaos::FPhysicsSolver* Solver : Solvers)
				{
					if(Solver->HasActiveParticles())
					{
						ActiveSolverList.Add(Solver);
					}
				}

				const int32 NumSolverEntries = ActiveSolverList.Num();

				PhysicsParallelFor(NumSolverEntries, [&](int32 Index)
				{
					SCOPE_CYCLE_COUNTER(STAT_SolverAdvance);
				
					FPhysicsSolver* Solver = ActiveSolverList[Index];

					// Execute Step function either on this thread or in a pausable side debug thread
					DebugSolverTasks.DebugStep(Solver, [this, Solver, Dt]()
					{
						StepSolver(Solver, Dt);
					});
				});
			}

			Timestep->Update();
			float ActualDt = Timestep->GetActualDt();

#if STATS && CHAOSTHREADSTATS_ENABLED
			// Record our thread statistics
			{
				// Read lock so we can write without anything attempting to flip the buffer
				FRWScopeLock StatsScopeLock(StatsLock, SLT_ReadOnly);
				FPersistentPhysicsTaskStatistics& CurrStats = Stats.GetPhysicsDataForWrite();

				const float TimestepTarget = Timestep->GetTarget();

				CurrStats.AccumulatedTime += ActualDt;
				CurrStats.ActualAccumulatedTime += ActualDt <= TimestepTarget ? TimestepTarget : ActualDt;
				CurrStats.NumUpdates++;
				CurrStats.UpdateTimes.Add(ActualDt);

#if CHAOSTHREADSTATS_PERSOLVER

				const int32 NumSolvers = Solvers.Num();
				if(CurrStats.SolverStats.Num() != NumSolvers)
				{
					CurrStats.SolverStats.Reset();
					CurrStats.SolverStats.AddDefaulted(NumSolvers);
				}

				for(int32 SolverIndex = 0; SolverIndex < NumSolvers; ++SolverIndex)
				{
					FPhysicsSolver* Solver = Solvers[SolverIndex];

					FPersistentPhysicsTaskStatistics::FPerSolverStatistics& SolverStat = CurrStats.SolverStats[SolverIndex];
					FPhysicsSolver::FPBDRigidsEvolution& Evolution = *Solver->MEvolution;

#if TODO_REIMPLEMENT_SOLVER_ENABLING
					if(Solver->Enabled())
					{
#if TODO_REIMPLMEENT_EVOLUTION_ACCESSORS
						SolverStat.NumActiveParticles = Evolution.GetActiveIndices().Num();
						SolverStat.NumActiveConstraints = Evolution.NumConstraints();
						SolverStat.NumAllocatedParticles = Evolution.GetParticles().Size();
						SolverStat.NumParticleIslands = Evolution.NumIslands();
						SolverStat.EvolutionStats = Evolution.GetEvolutionStats();
#endif
					}
					else
					{
						SolverStat.Reset();
					}
#endif
				}
#endif

			}
#endif
		}
		// Shutdown all debug threads if any
		DebugSolverTasks.Shutdown();

		ShutdownEvent->Trigger();
	}

	void FPersistentPhysicsTask::StepSolver(FPhysicsSolver* InSolver, float Dt)
	{
		HandleSolverCommands(InSolver);

		// Check whether this solver is paused (changes in pause states usually happens during HandleSolverCommands)
#if CHAOS_WITH_PAUSABLE_SOLVER
#if TODO_REIMPLEMENT_SOLVER_PAUSING
		if (!InSolver->Paused())
#endif
#endif
		{
			if (InSolver->bEnabled)
			{
				// Only process if we have something to actually simulate
				if(InSolver->HasActiveParticles())
				{
					AdvanceSolver(InSolver, Dt);

					{
						SCOPE_CYCLE_COUNTER(STAT_BufferPhysicsResults);
						CacheLock.ReadLock();

						InSolver->ForEachPhysicsProxyParallel([](auto* Object)
						{
							Object->BufferPhysicsResults();
						});

						CacheLock.ReadUnlock();
					}

					{
						SCOPE_CYCLE_COUNTER(STAT_FlipResults);
						CacheLock.WriteLock();

						InSolver->ForEachPhysicsProxy([](auto* Object)
						{
							Object->FlipBuffer();
						});

						CacheLock.WriteUnlock();
					}
				}
			}
		}
	}

	void FPersistentPhysicsTask::AddSolver(FPhysicsSolver* InSolver)
	{
		Solvers.Add(InSolver);
		DebugSolverTasks.Add(InSolver);
	}

	void FPersistentPhysicsTask::RemoveSolver(FPhysicsSolver* InSolver)
	{
		DebugSolverTasks.Remove(InSolver);
		Solvers.Remove(InSolver);
	}

	void FPersistentPhysicsTask::SyncProxiesFromCache(bool bFullSync /*= false*/)
	{
		check(IsInGameThread());

		// "Read" lock the cachelock here. Write is for flipping. Acquiring read here prevents a flip happening
		// on the physics thread (Sync called from game thread).

		CacheLock.ReadLock();


		if(bFullSync)
		{
			TArray<FFieldSystemPhysicsProxy*> FieldsToDelete;

			for(FPhysicsSolver* Solver : Solvers)
			{
				Solver->ForEachPhysicsProxy([](auto* Object)
				{
					Object->PullFromPhysicsState();
				});

#if TODO_REIMPLEMENT_REMOVED_PROXY_STORAGE
				FPhysicsProxyStorage& RemovedObjects = Solver->GetRemovedObjectStorage();

				RemovedObjects.ForEachPhysicsProxy([](auto* Object)
				{
					if (ensure(Object))
					{
						Object->SyncBeforeDestroy();
						delete Object;
					}
				});

				RemovedObjects.ForEachFieldPhysicsProxy([Solver, &FieldsToDelete](auto* Object)
				{
					if(Object->GetSolver() == Solver)
					{
						FieldsToDelete.Add(Object);
					}
				});

				RemovedObjects.Reset();
#endif
			}

			// @todo(question) : Why is there a separate delete here for fields. [brice]
			for(FFieldSystemPhysicsProxy* FieldObj : FieldsToDelete)
			{
				delete FieldObj;
			}

			for (FPhysicsSolver* Solver : Solvers)
			{
				Solver->SyncEvents_GameThread();
			}
		}
		else
		{ 
			for(FPhysicsSolver* Solver : Solvers)
			{
				Solver->ForEachPhysicsProxy([](auto* Object)
				{
					Object->PullFromPhysicsState();
				});
			}

			for (FPhysicsSolver* Solver : Solvers)
			{
				Solver->SyncEvents_GameThread();
			}
		}

		CacheLock.ReadUnlock();
	}

	void FPersistentPhysicsTask::RequestShutdown()
	{
		bRunning = false;
	}

	FEvent* FPersistentPhysicsTask::GetShutdownEvent()
	{
		return ShutdownEvent;
	}

	void FPersistentPhysicsTask::SetTargetDt(float InNewDt)
	{
		Timestep->SetTarget(InNewDt);
	}

	void FPersistentPhysicsTask::SetTickMode(EChaosSolverTickMode InTickMode)
	{
		if(TickMode != InTickMode)
		{
			TickMode = InTickMode;
			delete Timestep;

			switch(TickMode)
			{
			case EChaosSolverTickMode::Fixed:
			{
				Timestep = new FFixedTimeStep;
				break;
			}
			case EChaosSolverTickMode::Variable:
			{
				Timestep = new FVariableTimeStep;
				break;
			}
			case EChaosSolverTickMode::VariableCapped:
			{
				Timestep = new FVariableWithCapTimestep;
				break;
			}
			case EChaosSolverTickMode::VariableCappedWithTarget:
			{
				Timestep = new FVariableMinimumWithCapTimestep;
				break;
			}
			default:
			{
				// Unable to create a tickmode, this is fatal as we need one to calculate deltas
				check(false);
				break;
			}
			}
		}
	}

	Chaos::FPersistentPhysicsTaskStatistics FPersistentPhysicsTask::GetNextThreadStatistics_GameThread()
	{
		FRWScopeLock StatsScopeLock(StatsLock, SLT_Write);

		// Get the data that the physics thread has been writing into the gamethread buffer
		Stats.Flip();

		// Reset the data that's now on the physics thread
		Stats.GetPhysicsDataForWrite().Reset();

		// Return a copy of the current data
		return Stats.GetGameDataForRead();
	}

	void FPersistentPhysicsTask::HandleSolverCommands(FPhysicsSolver* InSolver)
	{
		SCOPE_CYCLE_COUNTER(STAT_HandleSolverCommands);

		check(InSolver);
		TQueue<TFunction<void(FPhysicsSolver*)>, EQueueMode::Mpsc>& Queue = InSolver->CommandQueue;
		TFunction<void(FPhysicsSolver*)> Command;
		while(Queue.Dequeue(Command))
		{
			Command(InSolver);
		}
	}

	void FPersistentPhysicsTask::AdvanceSolver(FPhysicsSolver* InSolver, float InDt)
	{
		SCOPE_CYCLE_COUNTER(STAT_IntegrateSolver);

		check(InSolver);
		InSolver->AdvanceSolverBy(InDt);
	}
}
