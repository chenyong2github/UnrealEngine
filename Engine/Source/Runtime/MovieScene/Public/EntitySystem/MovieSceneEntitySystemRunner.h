// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MovieSceneEntityIDs.h"
#include "MovieSceneSequenceID.h"
#include "Evaluation/MovieScenePlayback.h"
//#include "Misc/FrameTime.h"
//#include "UObject/ObjectKey.h"
#include "Async/TaskGraphInterfaces.h"
#include "EntitySystem/MovieSceneEntitySystemTypes.h"
#include "EntitySystem/MovieSceneSequenceInstance.h"

class UMovieSceneEntitySystemLinker;

namespace UE {
	namespace MovieScene {
		class FEntityManager;
		struct FInstanceRegistry;
	}
}

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
	bool IsAttachedToLinker() const { return Linker != nullptr; }
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

	/** Flushes the update queue, blocking the current thread until all queued updates have run. */
	void Flush();

	/** Finish updating an instance. */
	void FinishInstance(FInstanceHandle InInstanceHandle);

	/** Access this runner's currently executing phase */
	UE::MovieScene::ESystemPhase GetCurrentPhase() const
	{
		return CurrentPhase;
	}

public:

	UMovieSceneEntitySystemLinker* GetLinker() { return Linker; }
	FEntityManager* GetEntityManager();
	FInstanceRegistry* GetInstanceRegistry();

public:
	
	// Internal API
	
	void MarkForUpdate(FInstanceHandle InInstanceHandle);
	FMovieSceneEntitySystemEventTriggers& GetQueuedEventTriggers();

private:

	void OnLinkerGarbageCleaned(UMovieSceneEntitySystemLinker* Linker);
	void OnLinkerAbandon(UMovieSceneEntitySystemLinker* Linker);

private:

	// Update loop
	
	void DoFlushUpdateQueueOnce();
	
	void GameThread_ProcessQueue();
	void GameThread_SpawnPhase();
	void GameThread_InstantiationPhase();
	void GameThread_PostInstantiation();
	void GameThread_EvaluationPhase();
	void GameThread_EvaluationFinalizationPhase();
	void GameThread_PostEvaluationPhase();

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
	UMovieSceneEntitySystemLinker* Linker;

	/** Queue of sequence instances to be updated */
	TArray<FMovieSceneUpdateRequest> UpdateQueue;

	/** When an update is running, the list of actual instances being updated */
	TArray<FInstanceHandle> CurrentInstances;
	/** When an update is running, the list of sub-contexts for the requested update */
	TArray<FDissectedUpdate> DissectedUpdates;

	uint64 LastInstantiationVersion = 0;

	TGraphTask<FNullGraphTask>* CompletionTask;

	bool bCanQueueEventTriggers;
	FMovieSceneEntitySystemEventTriggers EventTriggers;

	ENamedThreads::Type GameThread;

	UE::MovieScene::ESystemPhase CurrentPhase;
};
