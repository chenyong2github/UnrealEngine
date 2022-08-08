// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/TaskGraphInterfaces.h"
#include "Containers/Array.h"
#include "Delegates/Delegate.h"
#include "EntitySystem/MovieSceneEntitySystemTypes.h"
#include "EntitySystem/MovieSceneSequenceInstance.h"
#include "EntitySystem/MovieSceneSequenceInstanceHandle.h"
#include "Evaluation/MovieScenePlayback.h"
#include "HAL/Platform.h"
#include "MovieSceneEntityIDs.h"
#include "MovieSceneSequenceID.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UMovieSceneEntitySystemLinker;

namespace UE::MovieScene
{

class FEntityManager;
struct FInstanceRegistry;

/** Bit-mask enumeration that defines tasks that need running */
enum class ERunnerFlushState
{
	None          = 0,			// Signifies no evaluation is currently underway
	Start         = 1 << 0,		// Sets up initial evaluation flags for external players and listeners
	Import        = 1 << 1,		// Update sequence instances and import entities into the entity manager
	Spawn         = 1 << 2,		// Perform the Spawn phase in the Entity System Graph
	Instantiation = 1 << 3,		// Perform the Instantiation phase in the Entity System Graph
	Evaluation    = 1 << 4,		// Perform the Evaluation phase in the Entity System Graph
	Finalization  = 1 << 5,		// Perform the Finalization phase in the Entity System Graph and trigger any external events
	End           = 1 << 6,		// Counterpart for Start - resets external players and listeners

	// Signifies that, during the Finalization task, there were still outstanding tasks and we need to perform another iteration
	LoopEval      = Import | Spawn | Instantiation | Evaluation | Finalization | End,
	// Initial flush state
	Everything    = Start | LoopEval,
};
ENUM_CLASS_FLAGS(ERunnerFlushState)


} // namespace UE::MovieScene

DECLARE_MULTICAST_DELEGATE(FMovieSceneEntitySystemEventTriggers);

class MOVIESCENE_API FMovieSceneEntitySystemRunner
{
public:

	using FEntityManager = UE::MovieScene::FEntityManager;
	using FInstanceHandle = UE::MovieScene::FInstanceHandle;
	using FInstanceRegistry = UE::MovieScene::FInstanceRegistry;

public:
	/** Creates an unbound runner */
	FMovieSceneEntitySystemRunner();
	/** Destructor */
	~FMovieSceneEntitySystemRunner();

	/** Attach this runner to a linker */
	void AttachToLinker(UMovieSceneEntitySystemLinker* InLinker);
	/** Returns whether this runner is attached to a linker */
	bool IsAttachedToLinker() const;
	/** Detaches this runner from a linker */
	void DetachFromLinker();

	/** Returns whether this runner has any outstanding updates. */
	bool HasQueuedUpdates() const;
	/** Returns whether the given instance is queued for any updates. */
	bool HasQueuedUpdates(FInstanceHandle Instance) const;
	/** Queue the given instance for an update with the given context. */
	void QueueUpdate(const FMovieSceneContext& Context, FInstanceHandle Instance);

	/** Updates the given instance with the given context immediately. This doesn't affect the current update queue. */
	void Update(const FMovieSceneContext& Context, FInstanceHandle Instance);

	/**
	 * Flushes the update queue and applies any outstanding evaluation logic
	 * 
	 * @param BudgetMs      A budget (in milliseconds) to use for evaluation. Evaluation will cease prematurely once this budget is spent
	 *                      and will process the outstanding work on the next call to Flush. A value of 0.0 signifies no budget - the queue
	 *                      will be fully processed without leaving any outstanding work
	 */
	void Flush(double BudgetMs = 0.f);

	/** Finish updating an instance. */
	void FinishInstance(FInstanceHandle InInstanceHandle);

	/** Access this runner's currently executing phase */
	UE::MovieScene::ESystemPhase GetCurrentPhase() const
	{
		return CurrentPhase;
	}

	bool IsCurrentlyEvaluating() const;

public:

	UMovieSceneEntitySystemLinker* GetLinker() const;
	FEntityManager* GetEntityManager() const;
	FInstanceRegistry* GetInstanceRegistry() const;

public:
	
	// Internal API
	
	void MarkForUpdate(FInstanceHandle InInstanceHandle);
	FMovieSceneEntitySystemEventTriggers& GetQueuedEventTriggers();

private:

	void OnLinkerAbandon(UMovieSceneEntitySystemLinker* Linker);

private:

	/**
	 * Flush the next item in our update loop based off the contents of FlushState
	 *
	 * @param  Linker   The linker we are arrached to
	 * @return True if the loop is allowed to continue, or false if we should not flush any more
	 */
	bool FlushNext(UMovieSceneEntitySystemLinker* Linker);

	/**
	 * Set up initial state before any evaluation runs. Only called once regardless of the number of pending updates we have to process
	 * Primarily used for setting up external 'is evaluating' flags for re-entrancy and async checks.
	 */
	bool StartEvaluation(UMovieSceneEntitySystemLinker* Linker);

	/** Update sequence instances based on currently queued update requests, or outstanding dissected updates */
	bool GameThread_UpdateSequenceInstances(UMovieSceneEntitySystemLinker* Linker);
	/** Execute the spawn phase of the entity system graph, if there is anything to do */
	bool GameThread_SpawnPhase(UMovieSceneEntitySystemLinker* Linker);
	/** Execute the instantiation phase of the entity system graph, if there is anything to do */
	bool GameThread_InstantiationPhase(UMovieSceneEntitySystemLinker* Linker);
	/** Called immediately after instantiation to execute cleanup and bookkeeping tasks. Skipped if instantiation is skipped.*/
	bool GameThread_PostInstantiation(UMovieSceneEntitySystemLinker* Linker);
	/** Main entity-system evaluation phase. Blocks this thread until completion. */
	bool GameThread_EvaluationPhase(UMovieSceneEntitySystemLinker* Linker);
	/** Finalization phase for triggering external events and other behavior. */
	void GameThread_EvaluationFinalizationPhase(UMovieSceneEntitySystemLinker* Linker);
	/** Post-evaluation phase for clean up. */
	void GameThread_PostEvaluationPhase(UMovieSceneEntitySystemLinker* Linker);

	/**
	 * Counterpart for StartEvaluation.
	 * Called only when our UpdateQueue and DissectedUpdates have been fully processed and there is nothing left to do.
	 */
	void EndEvaluation(UMovieSceneEntitySystemLinker* Linker);

private:

	struct FMovieSceneUpdateRequest
	{
		FMovieSceneContext Context;
		FInstanceHandle InstanceHandle;
	};

	struct FDissectedUpdate
	{
		FMovieSceneContext Context;

		FInstanceHandle InstanceHandle;
		int32 Order;
	};

	/** Owner linker */
	TWeakObjectPtr<UMovieSceneEntitySystemLinker> WeakLinker;

	/** Queue of sequence instances to be updated */
	TArray<FMovieSceneUpdateRequest> UpdateQueue;

	/** When an update is running, the list of actual instances being updated */
	TArray<FInstanceHandle> CurrentInstances;
	/** When an update is running, the list of sub-contexts for the requested update */
	TArray<FDissectedUpdate> DissectedUpdates;

	bool bCanQueueEventTriggers;
	FMovieSceneEntitySystemEventTriggers EventTriggers;

	ENamedThreads::Type GameThread;

	UE::MovieScene::ESystemPhase CurrentPhase;

	/**
	 * Defines a bitmask of outstanding tasks that need running.
	 * When a task has completed its bit becomes unset.
	 * Evaluation can trigger tasks to run again by setting previously cleared bits in this mask.
	 * A state of ERunnerFlushState::Everything means an evaluation has not yet started.
	 * A state of ERunnerFlushState::None means an evaluation has just finished.
	 */
	UE::MovieScene::ERunnerFlushState FlushState;
};
