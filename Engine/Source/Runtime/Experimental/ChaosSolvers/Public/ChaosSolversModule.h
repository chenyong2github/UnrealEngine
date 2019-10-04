// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Event.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Async/AsyncWork.h"
#include "UObject/ObjectMacros.h"
#include "Framework/Dispatcher.h"
#include "Framework/PersistentTask.h"
#include "Framework/Threading.h"
#include "PhysicsCoreTypes.h"
#include "Chaos/Framework/MultiBufferResource.h"
#include "Chaos/Declares.h"

/** Classes that want to set the solver actor class can implement this. */
class IChaosSolverActorClassProvider
{
public:
	virtual UClass* GetSolverActorClass() const = 0;
};

/** Class that external users can implement and set on the module to provide various settings to the system (see FChaosSolversModule::SetSettingsProvider) */
class IChaosSettingsProvider
{
public:
	virtual ~IChaosSettingsProvider() {}

	virtual Chaos::EThreadingMode GetDefaultThreadingMode() const = 0;
	virtual EChaosSolverTickMode GetDedicatedThreadTickMode() const = 0;
	virtual EChaosBufferMode GetDedicatedThreadBufferMode() const = 0;
}; 

namespace Chaos
{
	class FPersistentPhysicsTask;
	class FPhysicsProxy;
}

// Default settings implementation
namespace Chaos
{
	class FInternalDefaultSettings : public IChaosSettingsProvider
	{
	public:
		virtual Chaos::EThreadingMode GetDefaultThreadingMode() const override;
		virtual EChaosSolverTickMode GetDedicatedThreadTickMode() const override;
		virtual EChaosBufferMode GetDedicatedThreadBufferMode() const override;
	};

	extern CHAOSSOLVERS_API FInternalDefaultSettings GDefaultChaosSettings;
}

struct FChaosConsoleSinks
{
	static void OnCVarsChanged();
};

struct FSolverStateStorage
{
	friend class FChaosSolversModule;

	Chaos::FPhysicsSolver* Solver;
	TArray<Chaos::FPhysicsProxy*> ActiveProxies;
	TArray<Chaos::FPhysicsProxy*> ActiveProxies_GameThread;

private:

	// Private so only the module can actually make these so they can be tracked
	FSolverStateStorage();
	FSolverStateStorage(const FSolverStateStorage& InCopy) = default;
	FSolverStateStorage(FSolverStateStorage&& InSteal) = default;
	FSolverStateStorage& operator =(const FSolverStateStorage& InCopy) = default;
	FSolverStateStorage& operator =(FSolverStateStorage&& InSteal) = default;

};

class CHAOSSOLVERS_API FChaosSolversModule : public IModuleInterface
{
public:

	static FChaosSolversModule* GetModule();

	FChaosSolversModule();

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	void Initialize();
	void Shutdown();

	void OnSettingsChanged();

	void ShutdownThreadingMode();
	void InitializeThreadingMode(Chaos::EThreadingMode InNewMode);
	void ChangeThreadingMode(Chaos::EThreadingMode InNewMode);

	/**
	 * Queries for multithreaded configurations
	 */
	bool IsPersistentTaskEnabled() const;
	bool IsPersistentTaskRunning() const;

	/**
	 * Creates and dispatches the physics thread task
	 */
	void StartPhysicsTask();

	/**
	 * Shuts down the physics thread task
	 * #BG TODO cleanup running task
	 */
	void EndPhysicsTask();

	/**
	 * Get the dispatcher interface currently being used. when running a multi threaded
	 * configuration this will safely marshal commands to the physics thread. in a
	 * single threaded configuration the commands will be called immediately
	 *
	 * Note: This should be queried for every scope that dispatches commands. the game
	 * thread has mechanisms to change the dispatcher implementation (CVar for threadmode)
	 * which means the ptr could be stale
	 * #BGallagher Make this pimpl? Swap out implementation and allow cached dispatcher?
	 */
	Chaos::IDispatcher* GetDispatcher() const;

	/**
	 * Gets the inner physics thread task if it has been spawned. Care must be taken when
	 * using methods and members that the calling context can safely access those fields
	 * as the task will be running on its own thread.
	 */
	Chaos::FPersistentPhysicsTask* GetDedicatedTask() const;

	/**
	 * Called to request a sync between the game thread and the currently running physics task
	 * @param bForceBlockingSync forces this 
	 */
	void SyncTask(bool bForceBlockingSync = false);

	/**
	 * Create a new solver state storage object to contain a solver and proxy storage object. Intended
	 * to be used by the physics scene to create a common storage object that can be passed to a dedicated
	 * thread when it is enabled without having to link Engine from Chaos.
	 *
	 * Should be called from the game thread to create a new solver. After creation, non-standalone solvers
	 * are dispatched to the physics thread automatically if it is available
	 *
	 * @param bStandalone Whether the solver is standalone (not sent to physics thread - updating left to caller)
	 */
	Chaos::FPhysicsSolver* CreateSolver(bool bStandalone = false);

	Chaos::EMultiBufferMode GetBufferModeFromThreadingModel(Chaos::EThreadingMode ThreadingMode) const
	{
		Chaos::EMultiBufferMode SolverBufferMode = Chaos::EMultiBufferMode::Single;
		switch (ThreadingMode)
		{
			case Chaos::EThreadingMode::SingleThread:
			{
				SolverBufferMode = Chaos::EMultiBufferMode::Single;
			}
			break;

			case Chaos::EThreadingMode::DedicatedThread:
			case Chaos::EThreadingMode::TaskGraph:
			{
				switch (GetSettingsProvider().GetDedicatedThreadBufferMode())
				{
					case EChaosBufferMode::Triple:
					{
						SolverBufferMode = Chaos::EMultiBufferMode::Triple;
					}
					break;

					case EChaosBufferMode::Double:
					default:
					{
						SolverBufferMode = Chaos::EMultiBufferMode::Double;
					}
				}
			}
			break;

			default:
			{
				checkf(false, TEXT("Unexpected Threading Mode"));
			}
		}
		return SolverBufferMode;
	}

	/**
	 * Sets the actor type which should be AChaosSolverActor::StaticClass() but that is not accessible from engine.
	 */
	void SetSolverActorClass(UClass* InActorClass, UClass* InActorRequiredBaseClass)
	{
		SolverActorClass = InActorClass;
		SolverActorRequiredBaseClass = InActorRequiredBaseClass;
		check(IsValidSolverActorClass(SolverActorClass));
	}

	UClass* GetSolverActorClass() const;

	bool IsValidSolverActorClass(UClass* Class) const;

	/**
	 * Sets the dedicated thread tickmode externally
	 */
	void SetDedicatedThreadTickMode(EChaosSolverTickMode InTickMode);

	/**
	 * Shuts down and destroys a solver state
	 *
	 * Should be called on whichever thread currently owns the solver state
	 */
	void DestroySolver(Chaos::FPhysicsSolver* InState);

	/**
	 * Read access to the current solver-state objects, be aware which thread owns this data when
	 * attempting to use this. Physics thread will query when spinning up to get current world state
	 */
	const TArray<Chaos::FPhysicsSolver*>& GetSolvers() const { return Solvers; }

	/**
	 * Outputs statistics for the solver hierarchies. Currently engine calls into this
	 * from a console command on demand.
	 */
	void DumpHierarchyStats(int32* OutOptMaxCellElements = nullptr);

	/**
	 * Acquires a read lock for physics object results
	 */
	void LockResultsRead();

	/**
	 * Unlocks an acquired physics object result lock
	 */
	void UnlockResultsRead();

#if WITH_EDITOR
	/**
	 * Pause solvers. Thread safe.
	 * This is typically called from a playing editor to pause all solvers.
	 * @note Game pause must use a different per solver mechanics.
	 */
	void PauseSolvers();

	/**
	 * Resume solvers. Thread safe.
	 * This is typically called from a paused editor to resume all solvers.
	 * @note Game resume must use a different per solver mechanics.
	 */
	void ResumeSolvers();

	/**
	 * Single-step advance solvers. Thread safe.
	 * This is typically called from a paused editor to single step all solvers.
	 */
	void SingleStepSolvers();

	/**
	 * Query whether a particular solver should advance and update its single-step counter. Thread safe.
	 */
	bool ShouldStepSolver(int32& InOutSingleStepCounter) const;
#endif  // #if WITH_EDITOR

	void RegisterSolverActorClassProvider(IChaosSolverActorClassProvider* Provider)
	{
		SolverActorClassProvider = Provider;
	};

	void SetSettingsProvider(IChaosSettingsProvider* InProvider)
	{
		SettingsProvider = InProvider;
	}

	void LockSolvers() { SolverLock.Lock(); }
	void UnlockSolvers() { SolverLock.Unlock(); }

	void ChangeBufferMode(Chaos::EMultiBufferMode InBufferMode);

	Chaos::EThreadingMode GetDesiredThreadingMode() const;
	Chaos::EMultiBufferMode GetDesiredBufferingMode() const;

private:

	/** Safe method for always getting a settings provider (from the external caller or an internal default) */
	const IChaosSettingsProvider& GetSettingsProvider() const;

	/** Object that contains implementation of GetSolverActorClass() */
	IChaosSolverActorClassProvider* SolverActorClassProvider;

	/** Settings provider from external user */
	IChaosSettingsProvider* SettingsProvider;

	// Whether we actually spawned a physics task (distinct from whether we _should_ spawn it)
	bool bPersistentTaskSpawned;

	// The actually running tasks if running in a multi threaded configuration.
	FAsyncTask<Chaos::FPersistentPhysicsTask>* PhysicsAsyncTask;
	Chaos::FPersistentPhysicsTask* PhysicsInnerTask;

	// Current command dispatcher
	Chaos::IDispatcher* Dispatcher;

	// Core delegate signaling app shutdown, clean up and spin down threads before exit.
	FDelegateHandle PreExitHandle;

	// Allocated storage for solvers and proxies. Existing on the module makes it easier for hand off in multi threaded mode.
	// To actually use a solver, call CreateSolver to receive one of these and use it to hold the solver. In the event
	// of switching to multi threaded mode these will be handed over to the other thread.
	//
	// Where these objects are valid for interaction depends on the current threading mode. Use IsPersistentTaskRunning to
	// check whether the physics thread owns these before manipulating. When adding/removing solver or proxy items in
	// multi threaded mode the physics thread must also be notified of the change.
	TArray<Chaos::FPhysicsSolver*> Solvers;

	// Lock for the above list to ensure we don't delete solvers out from underneath other threads
	// or mess up the solvers array during use.
	mutable FCriticalSection SolverLock;

	// Called from the sync point to retrieve stats from the physics thread and push them to profilers or the stats system
	void UpdateStats();

	/** Store the ChaosSolverActor type */
	UClass* SolverActorClass;

	/** SolverActorClass is require to be this class or a child thereof */
	UClass* SolverActorRequiredBaseClass;

#if STATS
	// Stored stats from the physics thread
	float AverageUpdateTime;
	float TotalAverageUpdateTime;
	float Fps;
	float EffectiveFps;

	Chaos::FPersistentPhysicsTaskStatistics::FPerSolverStatistics PerSolverStats;
#endif

#if WITH_EDITOR
	// Pause/Resume/Single-step thread safe booleans.
	FThreadSafeBool bPauseSolvers;
	FThreadSafeCounter SingleStepCounter;  // Counter that increments its value each time a single step is instructed.
#endif  // #if WITH_EDITOR

	// Whether we're initialized, gates work in Initialize() and Shutdown()
	bool bModuleInitialized;
};

/**
 * Scoped locking object for physics thread. Currently this will stall out the persistent
 * physics task if it is running. Use this in situations where another thread absolutely
 * must read or write.
 *
 * Will block on construction until the physics thread confirms it has stalled, then
 * the constructor returns. Will let the physics thread continue post-destruction
 *
 * Does a runtime check on the type of the dispatcher and will do nothing if we're
 * not running the dedicated thread mode.
 */
class CHAOSSOLVERS_API FChaosScopedPhysicsThreadLock
{
public:

	FChaosScopedPhysicsThreadLock();
	explicit FChaosScopedPhysicsThreadLock(uint32 InMsToWait);
	~FChaosScopedPhysicsThreadLock();

	bool DidGetLock() const;

private:

	// Only construction through the above constructor is valid
	FChaosScopedPhysicsThreadLock(const FChaosScopedPhysicsThreadLock& InCopy) = default;
	FChaosScopedPhysicsThreadLock(FChaosScopedPhysicsThreadLock&& InSteal) = default;
	FChaosScopedPhysicsThreadLock& operator=(const FChaosScopedPhysicsThreadLock& InCopy) = default;
	FChaosScopedPhysicsThreadLock& operator=(FChaosScopedPhysicsThreadLock&& InSteal) = default;

	FEvent* CompleteEvent;
	FEvent* PTStallEvent;
	FChaosSolversModule* Module;
	bool bGotLock;
};

struct CHAOSSOLVERS_API FChaosScopeSolverLock
{
	FChaosScopeSolverLock()
	{
		FChaosSolversModule::GetModule()->LockSolvers();
	}

	~FChaosScopeSolverLock()
	{
		FChaosSolversModule::GetModule()->UnlockSolvers();
	}
};
