// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#if INCLUDE_CHAOS

#include "Framework/PersistentTask.h"

#include "Modules/ModuleManager.h"

#include "ChaosLog.h"
#include "ChaosStats.h"
#include "ChaosSolversModule.h"

#include "Chaos/Framework/Parallel.h"
#include "Framework/TimeStep.h"
#include "PBDRigidsSolver.h"
#include "Field/FieldSystem.h"

#include "SolverObjects/SkeletalMeshPhysicsObject.h"
#include "SolverObjects/StaticMeshPhysicsObject.h"
#include "SolverObjects/BodyInstancePhysicsObject.h"
#include "SolverObjects/GeometryCollectionPhysicsObject.h"
#include "SolverObjects/FieldSystemPhysicsObject.h"

#ifndef CHAOS_WITH_PAUSABLE_SOLVER
#define CHAOS_WITH_PAUSABLE_SOLVER 1
#endif

namespace Chaos
{
	FPersistentPhysicsTask::FPersistentPhysicsTask(float InTargetDt, bool bInAvoidSpiral, FDispatcher<EThreadingMode::DedicatedThread>* InDispatcher)
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
		for (FPBDRigidsSolver* Solver : Solvers)
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
		TArray<Chaos::FPBDRigidsSolver*> ActiveSolverList;

		while(bRunning)
		{
			SCOPE_CYCLE_COUNTER(STAT_PhysicsAdvance);

			// Run global and task commands
			{
				SCOPE_CYCLE_COUNTER(STAT_PhysCommands);
				TFunction<void()> GlobalCommand;
				while(CommandDispatcher->GlobalCommandQueue.Dequeue(GlobalCommand))
				{
					GlobalCommand();
				}
			}

			{
				SCOPE_CYCLE_COUNTER(STAT_TaskCommands);
				TFunction<void(FPersistentPhysicsTask*)> TaskCommand;
				while(CommandDispatcher->TaskCommandQueue.Dequeue(TaskCommand))
				{
					TaskCommand(this);
				}
			}

			Dt = Timestep->GetCalculatedDt();

#if WITH_EDITOR
			const bool bShouldStepSolvers = ChaosModule.ShouldStepSolver(SingleStepCounter);
			if (bShouldStepSolvers)
#endif
			{
				// Go wide if possible on the solvers
				ActiveSolverList.Reset(Solvers.Num());
				for(Chaos::FPBDRigidsSolver* Solver : Solvers)
				{
					if(Solver->HasActiveObjects())
					{
						ActiveSolverList.Add(Solver);
					}
				}

				const int32 NumSolverEntries = ActiveSolverList.Num();

				PhysicsParallelFor(NumSolverEntries, [&](int32 Index)
				{
					SCOPE_CYCLE_COUNTER(STAT_SolverAdvance);
				
					FPBDRigidsSolver* Solver = ActiveSolverList[Index];

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
					FPBDRigidsSolver* Solver = Solvers[SolverIndex];

					FPersistentPhysicsTaskStatistics::FPerSolverStatistics& SolverStat = CurrStats.SolverStats[SolverIndex];
					FPBDRigidsEvolution& Evolution = *Solver->MEvolution;

					if(Solver->Enabled())
					{
						SolverStat.NumActiveParticles = Evolution.GetActiveIndices().Num();
						SolverStat.NumActiveConstraints = Evolution.NumConstraints();
						SolverStat.NumAllocatedParticles = Evolution.GetParticles().Size();
						SolverStat.NumParticleIslands = Evolution.NumIslands();
						SolverStat.EvolutionStats = Evolution.GetEvolutionStats();
					}
					else
					{
						SolverStat.Reset();
					}
				}
#endif

			}
#endif
		}
		// Shutdown all debug threads if any
		DebugSolverTasks.Shutdown();

		ShutdownEvent->Trigger();
	}

	void FPersistentPhysicsTask::StepSolver(FPBDRigidsSolver* InSolver, float Dt)
	{
		HandleSolverCommands(InSolver);

		// Check whether this solver is paused (changes in pause states usually happens during HandleSolverCommands)
#if CHAOS_WITH_PAUSABLE_SOLVER
		if (!InSolver->Paused())
#endif
		{
			if (InSolver->bEnabled)
			{
				FSolverObjectStorage& Objects = InSolver->GetObjectStorage();
					
				// Only process if we have something to actually simulate
				if(Objects.GetNumObjects() > 0)
				{
					AdvanceSolver(InSolver, Dt);

					{
						SCOPE_CYCLE_COUNTER(STAT_CacheResults);
						CacheLock.ReadLock();

						Objects.ForEachSolverObjectParallel([](auto* Object)
						{
							Object->CacheResults();
						});

						CacheLock.ReadUnlock();
					}

					{
						SCOPE_CYCLE_COUNTER(STAT_FlipResults);
						CacheLock.WriteLock();

						Objects.ForEachSolverObject([](auto* Object)
						{
							Object->FlipCache();
						});

						CacheLock.WriteUnlock();
					}
				}
			}
		}
	}

	void FPersistentPhysicsTask::AddSolver(FPBDRigidsSolver* InSolver)
	{
		Solvers.Add(InSolver);
		DebugSolverTasks.Add(InSolver);
	}

	void FPersistentPhysicsTask::RemoveSolver(FPBDRigidsSolver* InSolver)
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
			TArray<FFieldSystemPhysicsObject*> FieldsToDelete;

			for(FPBDRigidsSolver* Solver : Solvers)
			{
				FSolverObjectStorage& Objects = Solver->GetObjectStorage();
				FSolverObjectStorage& RemovedObjects = Solver->GetRemovedObjectStorage();
				
				Objects.ForEachSolverObject([](auto* Object)
				{
					Object->SyncToCache();
				});

				RemovedObjects.ForEachSolverObject([](auto* Object)
				{
					if (ensure(Object))
					{
						Object->SyncBeforeDestroy();
						delete Object;
					}
				});

				RemovedObjects.ForEachFieldSolverObject([Solver, &FieldsToDelete](auto* Object)
				{
					if(Object->GetSolver() == Solver)
					{
						FieldsToDelete.Add(Object);
					}
				});

				RemovedObjects.Reset();
			}

			for(FFieldSystemPhysicsObject* FieldObj : FieldsToDelete)
			{
				delete FieldObj;
			}

			for (FPBDRigidsSolver* Solver : Solvers)
			{
				Solver->SyncEvents_GameThread();
			}
		}
		else
		{ 
			for(FPBDRigidsSolver* Solver : Solvers)
			{
				FSolverObjectStorage& Objects_GameThread = Solver->GetObjectStorage_GameThread();

				Objects_GameThread.ForEachSolverObject([](auto* Object)
				{
					Object->SyncToCache();
				});
			}

			for (FPBDRigidsSolver* Solver : Solvers)
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

	void FPersistentPhysicsTask::HandleSolverCommands(FPBDRigidsSolver* InSolver)
	{
		SCOPE_CYCLE_COUNTER(STAT_HandleSolverCommands);

		check(InSolver);
		TQueue<TFunction<void(FPBDRigidsSolver*)>, EQueueMode::Mpsc>& Queue = InSolver->CommandQueue;
		TFunction<void(FPBDRigidsSolver*)> Command;
		while(Queue.Dequeue(Command))
		{
			Command(InSolver);
		}
	}

	void FPersistentPhysicsTask::AdvanceSolver(FPBDRigidsSolver* InSolver, float InDt)
	{
		SCOPE_CYCLE_COUNTER(STAT_IntegrateSolver);

		check(InSolver);
		InSolver->AdvanceSolverBy(InDt);
	}
}

#endif
