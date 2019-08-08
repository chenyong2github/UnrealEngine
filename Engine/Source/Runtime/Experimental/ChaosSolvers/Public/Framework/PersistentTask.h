// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#if INCLUDE_CHAOS

#pragma once

#include "Async/AsyncWork.h"
#include "Containers/Queue.h"
#include "Dispatcher.h"
#include "HAL/ThreadSafeBool.h"
#include "Chaos/Framework/BufferedData.h"
#include "Framework/DebugSolverTasks.h"
#include "Chaos/PBDRigidsEvolution.h"
#include "Chaos/Declares.h"

#define CHAOSTHREADSTATS_ENABLED 1
#define CHAOSTHREADSTATS_PERSOLVER 1
#define CHAOSTHREADSTATS_HIERARCHY 1

class FPhysScene_Chaos;
struct FSolverStateStorage;

namespace Chaos
{
	class FPhysicsProxy;
	class ITimeStep;
}

namespace Chaos
{
	/**
	 * Data concerning how the physics thread is updating. Need to bunch all this up and consume it on the game thread
	 * as the physics thread runs at a different rate and doesn't work with normal stats
	 */
	struct FPersistentPhysicsTaskStatistics
	{
		struct FPerSolverStatistics
		{
			FPerSolverStatistics()
			{
				Reset();
			}

			void Reset()
			{
				NumActiveParticles = 0;
				NumActiveConstraints = 0;
				NumParticleIslands = 0;
				NumAllocatedParticles = 0;
				EvolutionStats.Reset();
			}

			FPerSolverStatistics& operator+=(const FPerSolverStatistics& Other)
			{
				NumActiveParticles += Other.NumActiveParticles;
				NumActiveConstraints += Other.NumActiveConstraints;
				NumAllocatedParticles += Other.NumAllocatedParticles;
				NumParticleIslands += Other.NumParticleIslands;
				EvolutionStats += Other.EvolutionStats;
				return *this;
			}

			int32 NumActiveParticles;
			int32 NumActiveConstraints;
			int32 NumAllocatedParticles;
			int32 NumParticleIslands;
			Chaos::FEvolutionStats EvolutionStats;
		};

		FPersistentPhysicsTaskStatistics()
		{
			Reset();
		}

		void Reset()
		{
			AccumulatedTime = 0.0f;
			ActualAccumulatedTime = 0.0f;
			NumUpdates = 0;
			UpdateTimes.Reset();
			SolverStats.Reset();
		}

		FPerSolverStatistics AccumulateSolverStats() const
		{
			FPerSolverStatistics OutStat;
			
			for(const FPerSolverStatistics& Stats : SolverStats)
			{
				OutStat += Stats;
			}

			return OutStat;
		}

		// The total time accumulated by the physics thread, ignoring sleeps used to sync to the desired rate 
		float AccumulatedTime;

		// The total time accumulated by the physics thread, including sleeps used to sync to the desired rate
		float ActualAccumulatedTime;

		// The number of updates the physics thread has performed
		uint32 NumUpdates;

		// The exact times of each update the physics thread has performed
		TArray<float> UpdateTimes;

		// Per-solver stats
		TArray<FPerSolverStatistics> SolverStats;
	};

	class CHAOSSOLVERS_API FPersistentPhysicsTask : public FNonAbandonableTask
	{
		friend class FAsyncTask<FPersistentPhysicsTask>;

	public:
		FPersistentPhysicsTask(float InTargetDt, bool bInAvoidSpiral, IDispatcher* InDispatcher);
		virtual ~FPersistentPhysicsTask();

		/**
		 * Entry point for the physics "thread". This function will never exit and act as a dedicated
		 * physics thread accepting commands from the game thread and running decoupled simulation
		 * iterations.
		 */
		void DoWork();

		/**
		 * Adds a solver to the internal list of solvers to run on the async task.
		 * Once the solver has been added to this task the game thread should never
		 * touch the internal state again unless performing a sync of the data
		 */
		void AddSolver(FPhysicsSolver* InSolver);

		/**
		 * Removes a solver from the internal list of solvers to run on the async task
		 */
		void RemoveSolver(FPhysicsSolver* InSolver);

		/**
		 * Synchronize proxies to their most recent gamethread readable results
		 * @param bFullSync Whether or not the physics thread has stalled. If it has then we can read from it here and
		 * perform some extra processing for removed objects
		 */
		void SyncProxiesFromCache(bool bFullSync = false);

		/**
		 * Request a shutdown of the current task. This will not happen immediately.
		 * Wait on the shutdown event (see GetShutdownEvent) to guarantee shutdown.
		 * Thread-safe, can be called from any thread to shut down the physics task
		 */
		void RequestShutdown();

		/**
		 * Get the shutdown event, which this task will trigger when the main
		 * running loop in DoWork is broken
		 */
		FEvent* GetShutdownEvent();

		/**
		 * Below functions alter the running task state and should be called using commands
		 * once the task is actually running
		 */

		 /**
		  * Sets the target per-tick Dt. Each physics update is always this length when running
		  * in fixed mode. The thread will stall after simulating if simulation takes less than
		  * this time. If it takes more than Dt seconds to do the simulation a warning is fired
		  * but the simulation will be running behind real-time
		  */
		void SetTargetDt(float InNewDt);

		/**
		 * Sets the tickmode for the thread, this controls how timesteps are calculated
		 */
		void SetTickMode(EChaosSolverTickMode InTickMode);

		/**
		 * Lock for handling caching for proxies. Read and write to either side of a double buffer counts
		 * as a read on this lock. It should only be write locked for flipping (happens after physics
		 * finishes a simulation)
		 */
		FRWLock CacheLock;

		/**
		 * Get a copy of the thread stats for the physics task. This will consume the statistics, flipping
		 * the current buffer so the physics thread will begin accumulating results for the next time it
		 * is consumed
		 */
		FPersistentPhysicsTaskStatistics GetNextThreadStatistics_GameThread();

		/** 
		 * Read/Write lock for thread stats, as these are flipped seperately to the rest of the
		 * physics data
		 */
		FRWLock StatsLock;

	private:

		TStatId GetStatId() const
		{
			RETURN_QUICK_DECLARE_CYCLE_STAT(FPersistentPhysicsTask, STATGROUP_ThreadPoolAsyncTasks);
		}

		void StepSolver(FPhysicsSolver* InSolver, float InDt);
		void HandleSolverCommands(FPhysicsSolver* InSolver);
		void AdvanceSolver(FPhysicsSolver* InSolver, float InDt);

		// List of solvers we'll advance in this task
		TArray<FPhysicsSolver*> Solvers;

		// Debug threads used to debug substep solver advance.
		FDebugSolverTasks DebugSolverTasks;

		// List of proxies that have been requested to be removed. Cached until the next
		// gamethread sync for final data handoff before being destroyed.
		TArray<FPhysicsProxy*> RemovedProxies;

		// Mode enum set externally to control which ITimestep implementation we use
		EChaosSolverTickMode TickMode;

		// Whether the main physics loop is running in DoWork;
		FThreadSafeBool bRunning;

		// The dispatcher made by the Chaos module to enable the gamethread to communicate with this one.
		IDispatcher* CommandDispatcher;

		// Event to fire after we've broken from the running physics loop as the thread shuts down
		FEvent* ShutdownEvent;
		
		// Double buffered data from the physics thread regarding thread statistics (FPS etc.)
		Chaos::TBufferedData<FPersistentPhysicsTaskStatistics> Stats;

		Chaos::ITimeStep* Timestep;

#if WITH_EDITOR
		// Counter used to check a match with the single step status.
		int32 SingleStepCounter;
#endif
	};
}

#endif
