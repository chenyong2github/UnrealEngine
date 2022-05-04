// Copyright Epic Games, Inc. All Rights Reserved.

#include "EntitySystem/MovieSceneEntitySystemRunner.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/MovieSceneEntityMutations.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedCaptureSource.h"
#include "ProfilingDebugging/CountersTrace.h"

DECLARE_CYCLE_STAT(TEXT("ECS System Cost"), 			MovieSceneEval_TotalGTCost, 				STATGROUP_MovieSceneEval);

DECLARE_CYCLE_STAT(TEXT("Spawn Phase"),                 MovieSceneEval_SpawnPhase,              	STATGROUP_MovieSceneECS);
DECLARE_CYCLE_STAT(TEXT("Instantiation Phase"), 		MovieSceneEval_InstantiationPhase, 			STATGROUP_MovieSceneECS);
DECLARE_CYCLE_STAT(TEXT("Instantiation Async Tasks"), 	MovieSceneEval_AsyncInstantiationTasks,		STATGROUP_MovieSceneECS);
DECLARE_CYCLE_STAT(TEXT("Post Instantiation"), 			MovieSceneEval_PostInstantiation, 			STATGROUP_MovieSceneECS);

DECLARE_CYCLE_STAT(TEXT("Evaluation Phase"), 			MovieSceneEval_EvaluationPhase, 			STATGROUP_MovieSceneECS);
DECLARE_CYCLE_STAT(TEXT("Finalization Phase"),          MovieSceneEval_FinalizationPhase,       	STATGROUP_MovieSceneECS);
DECLARE_CYCLE_STAT(TEXT("Post Evaluation Phase"),       MovieSceneEval_PostEvaluationPhase,     	STATGROUP_MovieSceneECS);

TRACE_DECLARE_INT_COUNTER(MovieSceneEntitySystemFlushes, TEXT("MovieScene/ECSFlushes"));
TRACE_DECLARE_INT_COUNTER(MovieSceneEntitySystemEvaluations, TEXT("MovieScene/ECSEvaluations"));

FMovieSceneEntitySystemRunner::FMovieSceneEntitySystemRunner()
	: CompletionTask(nullptr)
	, GameThread(ENamedThreads::GameThread_Local)
	, CurrentPhase(UE::MovieScene::ESystemPhase::None)
{
}

FMovieSceneEntitySystemRunner::~FMovieSceneEntitySystemRunner()
{
	if (IsAttachedToLinker())
	{
		DetachFromLinker();
	}
}

void FMovieSceneEntitySystemRunner::AttachToLinker(UMovieSceneEntitySystemLinker* InLinker)
{
	if (!ensureMsgf(InLinker, TEXT("Can't attach to a null linker!")))
	{
		return;
	}
	if (!ensureMsgf(WeakLinker.IsExplicitlyNull(), TEXT("This runner is already attached to a linker")))
	{
		if (ensureMsgf(WeakLinker.IsValid(), TEXT("Our previous linker isn't valid anymore! We will permit attaching to a new one.")))
		{
			return;
		}
	}

	WeakLinker = InLinker;
	InLinker->Events.AbandonLinker.AddRaw(this, &FMovieSceneEntitySystemRunner::OnLinkerAbandon);
}

bool FMovieSceneEntitySystemRunner::IsAttachedToLinker() const
{
	return !WeakLinker.IsExplicitlyNull();
}

void FMovieSceneEntitySystemRunner::DetachFromLinker()
{
	if (ensureMsgf(!WeakLinker.IsExplicitlyNull(), TEXT("This runner is not attached to any linker")))
	{
		if (ensureMsgf(WeakLinker.IsValid(), TEXT("This runner is attached to an invalid linker!")))
		{
			OnLinkerAbandon(WeakLinker.Get());
		}
		else
		{
			WeakLinker.Reset();
		}
	}
}

UMovieSceneEntitySystemLinker* FMovieSceneEntitySystemRunner::GetLinker() const
{
	return WeakLinker.Get();
}

UE::MovieScene::FEntityManager* FMovieSceneEntitySystemRunner::GetEntityManager() const
{
	if (UMovieSceneEntitySystemLinker* Linker = GetLinker())
	{
		return &Linker->EntityManager;
	}
	return nullptr;
}

UE::MovieScene::FInstanceRegistry* FMovieSceneEntitySystemRunner::GetInstanceRegistry() const
{
	if (UMovieSceneEntitySystemLinker* Linker = GetLinker())
	{
		return Linker->GetInstanceRegistry();
	}
	return nullptr;
}

bool FMovieSceneEntitySystemRunner::HasQueuedUpdates() const
{
	if (UpdateQueue.Num() != 0 || DissectedUpdates.Num() != 0)
	{
		return true;
	}
	if (const UMovieSceneEntitySystemLinker* Linker = GetLinker())
	{
		return Linker->HasStructureChangedSinceLastRun();
	}
	return false;
}

bool FMovieSceneEntitySystemRunner::HasQueuedUpdates(FInstanceHandle InInstanceHandle) const
{
	return Algo::FindBy(UpdateQueue, InInstanceHandle, &FMovieSceneUpdateRequest::InstanceHandle) != nullptr ||
		Algo::FindBy(DissectedUpdates, InInstanceHandle, &FDissectedUpdate::InstanceHandle) != nullptr;
}

void FMovieSceneEntitySystemRunner::QueueUpdate(const FMovieSceneContext& Context, FInstanceHandle InInstanceHandle)
{
	UpdateQueue.Add(FMovieSceneUpdateRequest{ Context, InInstanceHandle });
}

void FMovieSceneEntitySystemRunner::Update(const FMovieSceneContext& Context, FInstanceHandle Instance)
{
	if (UpdateQueue.Num() > 0)
	{
		UE_LOG(LogMovieSceneECS, Warning, TEXT("Updates are already queued! This will run those updates as well, which might not be what's intended."));
	}

	// Queue our one update and flush immediately.
	QueueUpdate(Context, Instance);
	Flush();
}

void FMovieSceneEntitySystemRunner::Flush()
{
	using namespace UE::MovieScene;

	UMovieSceneEntitySystemLinker* Linker = GetLinker();

	// Check that we are attached to a linker that allows starting a new evaluation.
	if (!ensureMsgf(Linker, TEXT("Runner isn't attached to a valid linker")))
	{
		return;
	}

	if (!Linker->StartEvaluation(*this))
	{
		return;
	}

	SCOPE_CYCLE_COUNTER(MovieSceneEval_TotalGTCost);
	TRACE_COUNTER_INCREMENT(MovieSceneEntitySystemFlushes);

	// We need to run the system from the game thread so we know we can fire events and callbacks from here.
	check(IsInGameThread());

	// Our entity manager cannot be locked down for us to continue. Something must have left it locked if 
	// this check fails
	FEntityManager& EntityManager = Linker->EntityManager;
	check(!EntityManager.IsLockedDown());

	EntityManager.SetDispatchThread(ENamedThreads::GameThread_Local);
	EntityManager.SetGatherThread(ENamedThreads::GameThread_Local);

	// We specifically only check whether the entity manager has changed since the last instantation once
	// to ensure that we are not vulnerable to infinite loops where components are added/removed in post-evaluation
	bool bStructureHadChanged = Linker->HasStructureChangedSinceLastRun();

	// Start flushing the update queue... keep flushing as long as we have work to do.
	while (UpdateQueue.Num() > 0 ||
		DissectedUpdates.Num() > 0 ||
		bStructureHadChanged)
	{
		DoFlushUpdateQueueOnce();

		bStructureHadChanged = false;
	}

	Linker->EndEvaluation(*this);
}

void FMovieSceneEntitySystemRunner::DoFlushUpdateQueueOnce()
{
	using namespace UE::MovieScene;

	TRACE_COUNTER_INCREMENT(MovieSceneEntitySystemEvaluations);
	TRACE_CPUPROFILER_EVENT_SCOPE(FMovieSceneEntitySystemRunner::DoFlushUpdateQueueOnce);

	// Setup the completion task that we can wait on.
	CompletionTask = TGraphTask<FNullGraphTask>::CreateTask(nullptr, ENamedThreads::GameThread)
		.ConstructAndHold(TStatId(), ENamedThreads::GameThread_Local);

	// Set the debug vizualizer's entity manager pointer, so all debugging happening here will show
	// relevant information. We need to set it here instead of higher up because we could have, say,
	// a blocking sequence triggering another blocking sequence via an event track. The nested call stack
	// of the second sequence needs to show debug information relevant to its private linker, but when
	// we return back up to the first sequence, which might still have another update round (such as
	// the other side of the dissected update range around the event), we need to set the pointer back
	// again.
	TGuardValue<FEntityManager*> DebugVizGuard(GEntityManagerForDebuggingVisualizers, GetEntityManager());

	// Also reset the capture source scope so that each group of sequences tied to a given linker starts
	// with a clean slate.
	TGuardValue<FScopedPreAnimatedCaptureSource*> CaptureSourceGuard(FScopedPreAnimatedCaptureSource::GetCaptureSourcePtr(), nullptr);

	// Entry point to the whole ECS loop... this will either unroll in the current thread's call stack
	// if there's not much to do, or it will start queuing up tasks on the task graph.
	// We immediately wait for the completion task to be executed.
	{
		GameThread_ProcessQueue();
	}

	FTaskGraphInterface::Get().WaitUntilTaskCompletes(CompletionTask->GetCompletionEvent(), ENamedThreads::GameThread_Local);

	CompletionTask = nullptr;

	// Now run the post-evaluation logic, which contains stuff we don't want to run from inside a task
	// graph call.
	GameThread_EvaluationFinalizationPhase();
	GameThread_PostEvaluationPhase();
}

void FMovieSceneEntitySystemRunner::GameThread_ProcessQueue()
{
	using namespace UE::MovieScene;

	UMovieSceneEntitySystemLinker* Linker = GetLinker();
	check(Linker);

	FInstanceRegistry* InstanceRegistry = GetInstanceRegistry();

	if (DissectedUpdates.Num() == 0)
	{
		TArray<TRange<FFrameTime>> Dissections;
		TSet<FInstanceHandle, DefaultKeyFuncs<FInstanceHandle>, TInlineSetAllocator<256>> Updates;

		TArray<FMovieSceneUpdateRequest> TempUpdateQueue;
		Swap(TempUpdateQueue, UpdateQueue);
		for (int32 UpdateIndex = 0; UpdateIndex < TempUpdateQueue.Num(); ++UpdateIndex)
		{
			FMovieSceneUpdateRequest Request = TempUpdateQueue[UpdateIndex];
			if (!InstanceRegistry->IsHandleValid(Request.InstanceHandle))
			{
				continue;
			}
			// Already have an update for this 
			else if (Updates.Contains(Request.InstanceHandle))
			{
				UpdateQueue.Add(Request);
				continue;
			}

			Updates.Add(Request.InstanceHandle);

			// Give the instance an opportunity to dissect the range into distinct evaluations
			FSequenceInstance& Instance = InstanceRegistry->MutateInstance(Request.InstanceHandle);
			Instance.DissectContext(Linker, Request.Context, Dissections);

			if (Dissections.Num() != 0)
			{
				for (int32 Index = 0; Index < Dissections.Num() - 1; ++Index)
				{
					FDissectedUpdate Dissection{
						FMovieSceneContext(FMovieSceneEvaluationRange(Dissections[Index], Request.Context.GetFrameRate(), Request.Context.GetDirection()), Request.Context.GetStatus()),
						Request.InstanceHandle,
						Index
					};
					DissectedUpdates.Add(Dissection);
				}

				// Add the last one with MAX_int32 so it gets evaluated with all the others in this flush
				FDissectedUpdate Dissection{
					FMovieSceneContext(FMovieSceneEvaluationRange(Dissections.Last(), Request.Context.GetFrameRate(), Request.Context.GetDirection()), Request.Context.GetStatus()),
					Request.InstanceHandle,
					MAX_int32
				};
				DissectedUpdates.Add(Dissection);

				Dissections.Reset();
			}
			else
			{
				DissectedUpdates.Add(FDissectedUpdate{ Request.Context, Request.InstanceHandle, MAX_int32 });
			}

			MarkForUpdate(Request.InstanceHandle);
		}
	}
	else
	{
		// Look for the next batch of updates, and mark the respective sequence instances as currently updating.
		const int32 PredicateOrder = DissectedUpdates[0].Order;
		for (int32 Index = 0; Index < DissectedUpdates.Num() && DissectedUpdates[Index].Order == PredicateOrder; ++Index)
		{
			const FDissectedUpdate& Update = DissectedUpdates[Index];
			MarkForUpdate(Update.InstanceHandle);
		}
	}

	// If we have no instances marked for update, we are running an evaluation probably because some
	// structural changes have occured in the entity manager (out of date instantiation serial number
	// in the linker). So we mark everything for update, so that PreEvaluation/PostEvaluation callbacks
	// and legacy templates are correctly executed.
	if (CurrentInstances.Num() == 0)
	{
		for (const FSequenceInstance& Instance : InstanceRegistry->GetSparseInstances())
		{
			MarkForUpdate(Instance.GetInstanceHandle());
		}
	}

	// Let sequence instances do any pre-evaluation work.
	for (FInstanceHandle UpdatedInstanceHandle : CurrentInstances)
	{
		InstanceRegistry->MutateInstance(UpdatedInstanceHandle).PreEvaluation(Linker);
	}

	// Process updates
	GameThread_SpawnPhase();
}

void FMovieSceneEntitySystemRunner::GameThread_SpawnPhase()
{
	using namespace UE::MovieScene;

	check(GameThread == ENamedThreads::GameThread || GameThread == ENamedThreads::GameThread_Local);

	UMovieSceneEntitySystemLinker* Linker = GetLinker();
	check(Linker);

	Linker->EntityManager.IncrementSystemSerial();

	CurrentPhase = ESystemPhase::Spawn;

	FInstanceRegistry* InstanceRegistry = GetInstanceRegistry();

	// Update all systems
	if (DissectedUpdates.Num() != 0)
	{
		const int32 PredicateOrder = DissectedUpdates[0].Order;
		int32 Index = 0;
		for (; Index < DissectedUpdates.Num() && DissectedUpdates[Index].Order == PredicateOrder; ++Index)
		{
			FDissectedUpdate Update = DissectedUpdates[Index];
			if (ensure(InstanceRegistry->IsHandleValid(Update.InstanceHandle)))
			{
				FSequenceInstance& Instance = InstanceRegistry->MutateInstance(Update.InstanceHandle);

				Instance.Update(Linker, Update.Context);
			}
		}
		DissectedUpdates.RemoveAt(0, Index);
	}

	const bool bInstantiationDirty = Linker->HasStructureChangedSinceLastRun() || InstanceRegistry->HasInvalidatedBindings();

	FGraphEventArray AllTasks;

	Linker->AutoLinkRelevantSystems();

	// --------------------------------------------------------------------------------------------------------------------------------------------
	// Step 1: Run the spawn phase if there were any changes to the current entity instantiations
	if (bInstantiationDirty)
	{
		SCOPE_CYCLE_COUNTER(MovieSceneEval_SpawnPhase);

		// The spawn phase can queue events to trigger from the event tracks.
		bCanQueueEventTriggers = true;
		{
			Linker->SystemGraph.ExecutePhase(ESystemPhase::Spawn, Linker, AllTasks);
		}
		bCanQueueEventTriggers = false;

		// We don't open a re-entrancy window, however, because there's no way we can recursively evaluate things at this point... too many
		// things are in an intermediate state. So events triggered as PreSpawn/PostSpawn can't be wired to something that starts a sequence.
		if (EventTriggers.IsBound())
		{
			EventTriggers.Broadcast();
			EventTriggers.Clear();
		}
	}

	// --------------------------------------------------------------------------------------------------------------------------------------------
	// Step 2: Run the instantiation phase if there is anything to instantiate. This must come after the spawn phase because new intatniations may
	//         be created during the spawn phase
	auto NextStep = [this, bInstantiationDirty, Linker]
	{
		const bool bAnyPending = Linker->EntityManager.ContainsComponent(FBuiltInComponentTypes::Get()->Tags.NeedsLink) || Linker->GetInstanceRegistry()->HasInvalidatedBindings();
		if (bInstantiationDirty || bAnyPending)
		{
			this->GameThread_InstantiationPhase();
		}
		else
		{
			// Go straight to evaluation
			this->GameThread_EvaluationPhase();
		}
	};

	if (AllTasks.Num() != 0)
	{
		TGraphTask<TFunctionGraphTaskImpl<void(), ESubsequentsMode::TrackSubsequents>>::CreateTask(&AllTasks, ENamedThreads::GameThread)
			.ConstructAndDispatchWhenReady(MoveTemp(NextStep), TStatId(), GameThread);
	}
	else
	{
		NextStep();
	}
}

void FMovieSceneEntitySystemRunner::GameThread_InstantiationPhase()
{
	using namespace UE::MovieScene;

	check(GameThread == ENamedThreads::GameThread || GameThread == ENamedThreads::GameThread_Local);

	UMovieSceneEntitySystemLinker* Linker = GetLinker();
	check(Linker);

	CurrentPhase = ESystemPhase::Instantiation;

	FGraphEventArray AllTasks;
	{
		SCOPE_CYCLE_COUNTER(MovieSceneEval_InstantiationPhase);

		Linker->SystemGraph.ExecutePhase(ESystemPhase::Instantiation, Linker, AllTasks);
	}

	if (AllTasks.Num() != 0)
	{
		TGraphTask<TFunctionGraphTaskImpl<void(), ESubsequentsMode::TrackSubsequents>>::CreateTask(&AllTasks, ENamedThreads::GameThread)
			.ConstructAndDispatchWhenReady(
				[this]
				{
					this->GameThread_PostInstantiation();
				}
				, TStatId(), GameThread);
	}
	else
	{
		this->GameThread_PostInstantiation();
	}
}

void FMovieSceneEntitySystemRunner::GameThread_PostInstantiation()
{
	using namespace UE::MovieScene;

	{
		SCOPE_CYCLE_COUNTER(MovieSceneEval_PostInstantiation);

		check(GameThread == ENamedThreads::GameThread || GameThread == ENamedThreads::GameThread_Local);

		UMovieSceneEntitySystemLinker* Linker = GetLinker();
		check(Linker);

		Linker->PostInstantation(*this);

		FEntityManager& EntityManager = Linker->EntityManager;
		FBuiltInComponentTypes* BuiltInComponentTypes = FBuiltInComponentTypes::Get();

		// Nothing needs linking, caching or restoring any more
		FRemoveMultipleMutation Mutation;
		Mutation.RemoveComponent(BuiltInComponentTypes->Tags.NeedsLink);

		FEntityComponentFilter Filter = FEntityComponentFilter().Any({ BuiltInComponentTypes->Tags.NeedsLink });
		EntityManager.MutateAll(Filter, Mutation);

		// Free anything that has been unlinked
		EntityManager.FreeEntities(FEntityComponentFilter().All({ BuiltInComponentTypes->Tags.NeedsUnlink }));

		Linker->SystemGraph.RemoveIrrelevantSystems(Linker);

		EntityManager.Compact();
	}

	GameThread_EvaluationPhase();
}

void FMovieSceneEntitySystemRunner::GameThread_EvaluationPhase()
{
	using namespace UE::MovieScene;

	UMovieSceneEntitySystemLinker* Linker = GetLinker();
	check(Linker);

	CurrentPhase = ESystemPhase::Evaluation;

	FGraphEventArray AllTasks;
	{
		SCOPE_CYCLE_COUNTER(MovieSceneEval_EvaluationPhase);

		// --------------------------------------------------------------------------------------------------------------------------------------------
		// Step 2: Run the evaluation phase. The entity manager is locked down for this phase, meaning no changes to entity-component structure is allowed
		//         This vastly simplifies the concurrent handling of entity component allocations
		Linker->EntityManager.LockDown();

		checkf(!Linker->EntityManager.ContainsComponent(FBuiltInComponentTypes::Get()->Tags.NeedsUnlink), TEXT("Stale entities remain in the entity manager during evaluation - these should have been destroyed during the instantiation phase. Did it run?"));

		Linker->SystemGraph.ExecutePhase(ESystemPhase::Evaluation, Linker, AllTasks);
	}

	auto Finish = [this]
	{
		// We are now done with the current update batch. Let's unlock the completion task to unblock
		// the main thread, which is waiting on it inside Flush().
		check(this->CompletionTask != nullptr);
		this->CompletionTask->Unlock();
	};

	if (AllTasks.Num() != 0)
	{
		TGraphTask<TFunctionGraphTaskImpl<void(), ESubsequentsMode::TrackSubsequents>>::CreateTask(&AllTasks, ENamedThreads::GameThread)
			.ConstructAndDispatchWhenReady(MoveTemp(Finish), TStatId(), GameThread);
	}
	else
	{
		Finish();
	}
}

void FMovieSceneEntitySystemRunner::GameThread_EvaluationFinalizationPhase()
{
	using namespace UE::MovieScene;

	check(GameThread == ENamedThreads::GameThread || GameThread == ENamedThreads::GameThread_Local);

	UMovieSceneEntitySystemLinker* Linker = GetLinker();
	check(Linker);

	Linker->EntityManager.ReleaseLockDown();

	CurrentPhase = ESystemPhase::Finalization;

	// Post-eval events can be queued during the finalization phase so let's open that up.
	// The events are actually executed a bit later, in GameThread_PostEvaluationPhase.
	bCanQueueEventTriggers = true;
	{
		SCOPE_CYCLE_COUNTER(MovieSceneEval_FinalizationPhase);

		FInstanceRegistry* InstanceRegistry = GetInstanceRegistry();

		// Iterate on a copy of our current instances, since LegacyEvaluator->Evaluate() could change the instance handle, which would affect PostEvaluationPhase
		TArray<FInstanceHandle> CurrentInstancesCopy(CurrentInstances);
		for (FInstanceHandle UpdatedInstanceHandle : CurrentInstancesCopy)
		{
			if (InstanceRegistry->IsHandleValid(UpdatedInstanceHandle))
			{
				FSequenceInstance& Instance = InstanceRegistry->MutateInstance(UpdatedInstanceHandle);
				if (Instance.IsRootSequence())
				{
					Instance.RunLegacyTrackTemplates();
				}
			}
		}

		FGraphEventArray Tasks;
		Linker->SystemGraph.ExecutePhase(ESystemPhase::Finalization, Linker, Tasks);
		checkf(Tasks.Num() == 0, TEXT("Cannot dispatch new tasks during finalization"));
	}
	bCanQueueEventTriggers = false;

	CurrentPhase = ESystemPhase::None;
}

void FMovieSceneEntitySystemRunner::GameThread_PostEvaluationPhase()
{
	using namespace UE::MovieScene;

	SCOPE_CYCLE_COUNTER(MovieSceneEval_PostEvaluationPhase);

	UMovieSceneEntitySystemLinker* Linker = GetLinker();
	check(Linker);

	// Execute any queued events from the evaluation finalization phase.
	if (EventTriggers.IsBound())
	{
		// Let's allow re-entrant evaluation at this point.
		FMovieSceneEntitySystemEvaluationReentrancyWindow Window(*Linker);

		EventTriggers.Broadcast();
		EventTriggers.Clear();
	}

	// Now run the post-evaluation logic so that we can safely handle broadcast events (like OnFinished)
	// that trigger some new evaluations (such as connecting it to another sequence's Play in Blueprint).
	//
	// If we are the global linker (and not a "private" linker, as is the case with "blocking" sequences),
	// we may find ourselves in a re-entrant call, which means we need to save our state here and restore
	// it afterwards. We also:
	//   - iterate on a copy of our current instances, since a re-entrant call would modify that array.
	//   - store our pending update result in a separate variable before assigning it to ourselves, for
	//     the same reasons as above.
	//
	FInstanceRegistry* InstanceRegistry = GetInstanceRegistry();
	TArray<FInstanceHandle> CurrentInstancesCopy(CurrentInstances);
	CurrentInstances.Empty();

	{
		FMovieSceneEntitySystemEvaluationReentrancyWindow Window(*Linker);

		for (FInstanceHandle UpdatedInstanceHandle : CurrentInstancesCopy)
		{
			// We must check for validity here because the cache handles may have become invalid
			// during this iteration (since there is a re-entrancy window open)
			if (InstanceRegistry->IsHandleValid(UpdatedInstanceHandle))
			{
				InstanceRegistry->MutateInstance(UpdatedInstanceHandle).PostEvaluation(Linker);
			}
		}
	}
}

void FMovieSceneEntitySystemRunner::FinishInstance(FInstanceHandle InInstanceHandle)
{
	using namespace UE::MovieScene;

	UMovieSceneEntitySystemLinker* Linker = GetLinker();
	check(Linker);

	// If we've already got queued updates for this instance we need to flush the linker first so that those updates are reflected correctly
	FInstanceRegistry* InstanceRegistry = GetInstanceRegistry();
	if (InstanceRegistry->GetInstance(InInstanceHandle).HasEverUpdated() && HasQueuedUpdates(InInstanceHandle))
	{
		Flush();
	}

	InstanceRegistry->MutateInstance(InInstanceHandle).Finish(Linker);
	if (Linker->HasStructureChangedSinceLastRun())
	{
		MarkForUpdate(InInstanceHandle);
		Flush();
	}
	else
	{
		InstanceRegistry->MutateInstance(InInstanceHandle).PostEvaluation(Linker);
	}
}

void FMovieSceneEntitySystemRunner::MarkForUpdate(FInstanceHandle InInstanceHandle)
{
	CurrentInstances.AddUnique(InInstanceHandle);
}

void FMovieSceneEntitySystemRunner::OnLinkerAbandon(UMovieSceneEntitySystemLinker* InLinker)
{
	if (ensure(InLinker))
	{
		InLinker->Events.AbandonLinker.RemoveAll(this);
	}
	WeakLinker.Reset();
}

FMovieSceneEntitySystemEventTriggers& FMovieSceneEntitySystemRunner::GetQueuedEventTriggers()
{
	checkf(bCanQueueEventTriggers, TEXT("Can't queue event triggers at this point in the update loop."));
	return EventTriggers;
}

