// Copyright Epic Games, Inc. All Rights Reserved.

#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/MovieSceneEntitySystemTypes.h"
#include "EntitySystem/MovieSceneEntitySystem.h"
#include "EntitySystem/MovieSceneEntitySystemTask.h"
#include "EntitySystem/MovieSceneEntityMutations.h"
#include "EntitySystem/MovieSceneComponentRegistry.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieScenePreAnimatedStateSystem.h"
#include "EntitySystem/MovieSceneEntitySystemRunner.h"
#include "MovieSceneFwd.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "Templates/SubclassOf.h"
#include "Containers/Ticker.h"
#include "UObject/ObjectKey.h"
#include "Engine/World.h"
#include "Algo/Find.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "ProfilingDebugging/CountersTrace.h"

DECLARE_CYCLE_STAT(TEXT("Link Relevant Systems"),		MovieSceneEval_LinkRelevantSystems,		STATGROUP_MovieSceneECS);

namespace UE
{
namespace MovieScene
{

struct FCustomEventDeleter
{
	void operator()(FEvent* Event)
	{
		FPlatformProcess::ReturnSynchEventToPool(Event);
	}
};

static FComponentRegistry GComponentRegistry;

EEntitySystemLinkerRole RegisterCustomEntitySystemLinkerRole()
{
	static EEntitySystemLinkerRole NextCustom = EEntitySystemLinkerRole::Custom;

	EEntitySystemLinkerRole Result = NextCustom;
	NextCustom = (EEntitySystemLinkerRole)((uint32)NextCustom + 1);
	check((uint32)NextCustom != TNumericLimits<uint32>::Max());
	return Result;
}

} // namespace MovieScene
} // namespace UE


UMovieSceneEntitySystemLinker::UMovieSceneEntitySystemLinker(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	using namespace UE::MovieScene;

	Role = EEntitySystemLinkerRole::Unknown;
	LastSystemLinkVersion = 0;
	LastInstantiationVersion = 0;
	AutoLinkMode = EAutoLinkRelevantSystems::Enabled;
	SystemContext = EEntitySystemContext::Runtime;

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		PreAnimatedState.Initialize(this);

		FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddUObject(this, &UMovieSceneEntitySystemLinker::HandlePreGarbageCollection);
		FCoreUObjectDelegates::GetPostGarbageCollect().AddUObject(this, &UMovieSceneEntitySystemLinker::HandlePostGarbageCollection);

		EntityManager.SetDebugName(GetName() + TEXT("[Entity Manager]"));
		EntityManager.SetComponentRegistry(&GComponentRegistry);

		FWorldDelegates::OnWorldCleanup.AddUObject(this, &UMovieSceneEntitySystemLinker::OnWorldCleanup);

		InstanceRegistry.Reset(new FInstanceRegistry(this));

#if WITH_EDITOR
		FCoreUObjectDelegates::OnObjectsReplaced.AddUObject(this, &UMovieSceneEntitySystemLinker::OnObjectsReplaced);
#endif
	}
}

UE::MovieScene::FEntitySystemLinkerExtensionID UMovieSceneEntitySystemLinker::RegisterExtension()
{
	static int32 StaticID = 0;
	return UE::MovieScene::FEntitySystemLinkerExtensionID{ StaticID++ };
}

void UMovieSceneEntitySystemLinker::Reset()
{
	Events.AbandonLinker.Broadcast(this);

	Events.TagGarbage.Clear();
	Events.CleanTaggedGarbage.Clear();
	Events.AddReferencedObjects.Clear();
	Events.AbandonLinker.Clear();
	Events.CleanUpWorld.Clear();

	SystemGraph.Shutdown();
	EntitySystemsByGlobalGraphID.Reset();

	EntityManager.Destroy();
}

UMovieSceneEntitySystemLinker* UMovieSceneEntitySystemLinker::FindOrCreateLinker(UObject* PreferredOuter, UE::MovieScene::EEntitySystemLinkerRole LinkerRole, const TCHAR* Name)
{
	if (!PreferredOuter)
	{
		PreferredOuter = GetTransientPackage();
	}

	UMovieSceneEntitySystemLinker* Existing = FindObject<UMovieSceneEntitySystemLinker>(PreferredOuter, Name);
	if (!Existing)
	{
		Existing = NewObject<UMovieSceneEntitySystemLinker>(PreferredOuter, Name);
		Existing->SetLinkerRole(LinkerRole);
	}
	ensure(Existing->Role == LinkerRole);
	return Existing;
}

UMovieSceneEntitySystemLinker* UMovieSceneEntitySystemLinker::CreateLinker(UObject* PreferredOuter, UE::MovieScene::EEntitySystemLinkerRole LinkerRole)
{
	if (!PreferredOuter)
	{
		PreferredOuter = GetTransientPackage();
	}

	UMovieSceneEntitySystemLinker* NewLinker = NewObject<UMovieSceneEntitySystemLinker>(PreferredOuter);
	NewLinker->Role = LinkerRole;
	return NewLinker;
}

UE::MovieScene::FComponentRegistry* UMovieSceneEntitySystemLinker::GetComponents()
{
	return &UE::MovieScene::GComponentRegistry;
}

void UMovieSceneEntitySystemLinker::InvalidateObjectBinding(const FGuid& ObjectBindingID, FInstanceHandle InInstanceHandle)
{
	if (InstanceRegistry->IsHandleValid(InInstanceHandle))
	{
		InstanceRegistry->InvalidateObjectBinding(ObjectBindingID, InInstanceHandle);
	}
}

void UMovieSceneEntitySystemLinker::SystemLinked(UMovieSceneEntitySystem* InSystem)
{
	const uint16 GlobalID = InSystem->GetGlobalDependencyGraphID();

	EntitySystemsByGlobalGraphID.Insert(GlobalID, InSystem);
}

void UMovieSceneEntitySystemLinker::SystemUnlinked(UMovieSceneEntitySystem* InSystem)
{
	const uint16 GlobalID = InSystem->GetGlobalDependencyGraphID();

	check(EntitySystemsByGlobalGraphID[GlobalID] == InSystem);
	EntitySystemsByGlobalGraphID.RemoveAt(GlobalID);

	Events.TagGarbage.RemoveAll(InSystem);
	Events.CleanTaggedGarbage.RemoveAll(InSystem);
	Events.AddReferencedObjects.RemoveAll(InSystem);
	Events.AbandonLinker.RemoveAll(InSystem);
	Events.CleanUpWorld.RemoveAll(InSystem);

	// Add the system to the recycling pool.
	ensure(EntitySystemsRecyclingPool.Contains(InSystem->GetClass()) == false);
	EntitySystemsRecyclingPool.Add(InSystem->GetClass(), InSystem);
}

bool UMovieSceneEntitySystemLinker::HasLinkedSystem(const uint16 GlobalDependencyGraphID)
{
	return EntitySystemsByGlobalGraphID.IsValidIndex(GlobalDependencyGraphID);
}

void UMovieSceneEntitySystemLinker::BeginDestroy()
{
	Events.AbandonLinker.Broadcast(this);

	SystemGraph.Shutdown();

	FCoreUObjectDelegates::GetPostGarbageCollect().RemoveAll(this);

	Super::BeginDestroy();
}

void UMovieSceneEntitySystemLinker::CleanupInvalidBoundObjects()
{
	TagInvalidBoundObjects();
	Events.TagGarbage.Broadcast(this);
	CleanGarbage();
}

void UMovieSceneEntitySystemLinker::TagInvalidBoundObjects()
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();

	// Tag any bound objects that are now invalid
	TArray<FMovieSceneEntityID> ExpiredBoundObjects;

	auto Iter = [&ExpiredBoundObjects](FMovieSceneEntityID EntityID, UObject* BoundObject)
	{
		if (FBuiltInComponentTypes::IsBoundObjectGarbage(BoundObject))
		{
			ExpiredBoundObjects.Add(EntityID);
		}
	};
	FEntityTaskBuilder()
	.ReadEntityIDs()
	.Read(BuiltInComponents->BoundObject)
	.Iterate_PerEntity(&EntityManager, Iter);

	for (FMovieSceneEntityID Entity : ExpiredBoundObjects)
	{
		EntityManager.AddComponent(Entity, BuiltInComponents->Tags.NeedsUnlink, EEntityRecursion::Full);
	}
}

bool UMovieSceneEntitySystemLinker::HasStructureChangedSinceLastRun() const
{
	return EntityManager.HasStructureChangedSince(LastInstantiationVersion);
}

bool UMovieSceneEntitySystemLinker::StartEvaluation(FMovieSceneEntitySystemRunner& InRunner)
{
	if (ActiveRunners.Num() == 0 || ActiveRunners.Last().bIsReentrancyAllowed)
	{
		// Default to re-entrancy being forbidden. The runner will allow re-entrancy at specific spots
		// in the evaluation loop, via a "re-entrancy window".
		ActiveRunners.Emplace(FActiveRunnerInfo{ &InRunner, false });
		return true;
	}
		
	UE_LOG(LogMovieSceneECS, Warning, TEXT("Can't start a new evaluation: the active runner is not in a re-entrancy window."));
	return false;
}

FMovieSceneEntitySystemRunner* UMovieSceneEntitySystemLinker::GetActiveRunner() const
{
	if (ActiveRunners.Num() > 0)
	{
		return ActiveRunners.Last().Runner;
	}
	return nullptr;
}

void UMovieSceneEntitySystemLinker::PostInstantation(FMovieSceneEntitySystemRunner& InRunner)
{
	LastInstantiationVersion = EntityManager.GetSystemSerial();

	GetInstanceRegistry()->PostInstantation();
}

void UMovieSceneEntitySystemLinker::EndEvaluation(FMovieSceneEntitySystemRunner& InRunner)
{
	if (ensureMsgf((ActiveRunners.Num() > 0 && ActiveRunners.Last().Runner == &InRunner),
				TEXT("Trying end the evaluation of a runner that's not the latest one to run.")))
	{
		ActiveRunners.Pop();
	}
}

void UMovieSceneEntitySystemLinker::HandlePreGarbageCollection()
{
	using namespace UE::MovieScene;

	// @todo: This is currently disabled because it is too indiscriminate
	//         with regards to when garbage collection is run (ie, if it's run _inside_
	//         the instantiation phase, we end up running everything again or if it's run without any
	//         outstanding work, it performs an unnecessary flush.
	//
	//         For now nothing is using the budgeting which this code was written for, so we will revisit in future
	
	// If we have any active runners part-way through an evaluation,
	// they must be flushed before we run a garbage collection
	//for (int32 Index = ActiveRunners.Num()-1; Index >= 0; --Index)
	//{
	//	if (ActiveRunners[Index].Runner->IsAttachedToLinker())
	//	{
	//		ActiveRunners[Index].Runner->Flush();
	//	}
	//}
}

void UMovieSceneEntitySystemLinker::HandlePostGarbageCollection()
{
	using namespace UE::MovieScene;

	// All the instance registry to unlink garbage first
	InstanceRegistry->TagGarbage();

	// Clean any garbage bound objects
	TagInvalidBoundObjects();

	// Allow any other system to tag garbage
	Events.TagGarbage.Broadcast(this);

	auto RouteTagGarbage = [](UMovieSceneEntitySystem* System){ System->TagGarbage(); };
	SystemGraph.IteratePhase(ESystemPhase::Spawn, RouteTagGarbage);
	SystemGraph.IteratePhase(ESystemPhase::Instantiation, RouteTagGarbage);

	CleanGarbage();
}

void UMovieSceneEntitySystemLinker::CleanGarbage()
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();
	FComponentTypeID NeedsUnlink = BuiltInComponents->Tags.NeedsUnlink;
	if (!EntityManager.ContainsComponent(NeedsUnlink))
	{
		return;
	}

	// Clear the instantiation serial to indicate that we probably need to re-run the instantiation systems
	// the next time a runner gets flushed
	LastInstantiationVersion = 0;

	// Allow any other system to tag garbage
	Events.CleanTaggedGarbage.Broadcast(this);

	auto RouteCleanTaggedGarbage = [](UMovieSceneEntitySystem* System){ System->CleanTaggedGarbage(); };
	SystemGraph.IteratePhase(ESystemPhase::Spawn, RouteCleanTaggedGarbage);
	SystemGraph.IteratePhase(ESystemPhase::Instantiation, RouteCleanTaggedGarbage);

	TArray<FMovieSceneEntityID> UnresolvedEntities;

	FEntityTaskBuilder()
	.Read(BuiltInComponents->BoundObject)
	.Read(BuiltInComponents->ParentEntity)
	.FilterNone({ BuiltInComponents->Tags.NeedsUnlink, BuiltInComponents->Tags.Ignored, BuiltInComponents->Tags.Finished })
	.Iterate_PerEntity(&EntityManager, [&UnresolvedEntities](UObject* Object, FMovieSceneEntityID ParentEntityID)
	{
		if (!Object)
		{
			UnresolvedEntities.Add(ParentEntityID);
		}
	});

	for (FMovieSceneEntityID EntityID : UnresolvedEntities)
	{
		EntityManager.AddComponent(EntityID, BuiltInComponents->Tags.HasUnresolvedBinding);
	}

	// Free the entities
	TSet<FMovieSceneEntityID> FreedEntities;
	EntityManager.FreeEntities(FEntityComponentFilter().All({ NeedsUnlink }), &FreedEntities);

	InstanceRegistry->CleanupLinkerEntities(FreedEntities);
}

void UMovieSceneEntitySystemLinker::OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap)
{
#if WITH_EDITOR
	using namespace UE::MovieScene;

	FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();

	FEntityTaskBuilder()
	.Write(BuiltInComponents->BoundObject)
	.Iterate_PerEntity(&EntityManager, [&ReplacementMap](UObject*& Object)
	{
		if (UObject* Replacement = ReplacementMap.FindRef(Object))
		{
			Object = Replacement;
		}
	});
#endif
}

void UMovieSceneEntitySystemLinker::OnWorldCleanup(UWorld* InWorld, bool bSessionEnded, bool bCleanupResources)
{
	// If we have any active runners part-way through an evaluation,
	// they must be flushed before we cleanup
	for (int32 Index = ActiveRunners.Num()-1; Index >= 0; --Index)
	{
		if (ActiveRunners[Index].Runner->IsAttachedToLinker())
		{
			ActiveRunners[Index].Runner->Flush();
		}
	}

	Events.CleanUpWorld.Broadcast(this, InWorld);
	InstanceRegistry->WorldCleanup(InWorld);

	HandlePostGarbageCollection();
}

void UMovieSceneEntitySystemLinker::AddReferencedObjects(UObject* Object, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Object, Collector);

	UMovieSceneEntitySystemLinker* This = CastChecked<UMovieSceneEntitySystemLinker>(Object);

	This->EntityManager.AddReferencedObjects(Collector);
	This->Events.AddReferencedObjects.Broadcast(This, Collector);
	Collector.AddReferencedObjects(This->EntitySystemsRecyclingPool);
}

UMovieSceneEntitySystem* UMovieSceneEntitySystemLinker::LinkSystem(TSubclassOf<UMovieSceneEntitySystem> InClassType)
{
	UMovieSceneEntitySystem* Existing = FindSystem(InClassType);
	if (Existing)
	{
		return Existing;
	}

	// We always create systems with a fixed name (since there should only ever be one of that name)
	// This means we can do our own recycling within the scope of this linker, to save on the cost of re-creating
	// systems when the first instantiation phase kicks in after a period without any sequence playing.
	UMovieSceneEntitySystem* NewSystem = nullptr;
	UMovieSceneEntitySystem** Recycled = EntitySystemsRecyclingPool.Find(InClassType);
	if (Recycled)
	{
		// Revive a recycled system.
		NewSystem = *Recycled;
		check(NewSystem);
		EntitySystemsRecyclingPool.Remove(InClassType);
		UE_LOG(LogMovieSceneECS, Verbose, TEXT("Recycling system: "), *InClassType->GetName());
	}
	else
	{
		// Unique names also mean we will recycle systems if they previously existed but are no longer used to avoid thrashing the GC
		// Recycling will destruct + memzero + construct the object so we can be sure that previous state doesn't roll over
		UClass* SystemClass = InClassType.Get();
		FName   SystemName  = SystemClass->GetFName();
		NewSystem = NewObject<UMovieSceneEntitySystem>(this, SystemClass, SystemName);
	}

	// If a system implements a hard depdency on another (through direct use of LinkSystem<>), we can't break the client code by returning null, but we can still warn that it should have checked whether it can call LinkSystem first
	ensureMsgf(!EnumHasAnyFlags(NewSystem->GetExclusionContext(), SystemContext), TEXT("Attempting to link a system that should have been excluded - this is probably an explicit call to Link a system that should have been excluded."));

	SystemGraph.AddSystem(NewSystem);
	NewSystem->Link(this);
	return NewSystem;
}

UMovieSceneEntitySystem* UMovieSceneEntitySystemLinker::FindSystem(TSubclassOf<UMovieSceneEntitySystem> InClassType) const
{
	UClass* Class = InClassType.Get();
	UMovieSceneEntitySystem* SystemCDO = Class ? Cast<UMovieSceneEntitySystem>(Class->GetDefaultObject()) : nullptr;
	if (SystemCDO)
	{
		const uint16 GlobalID = SystemCDO->GetGlobalDependencyGraphID();
		if (EntitySystemsByGlobalGraphID.IsValidIndex(GlobalID))
		{
			return EntitySystemsByGlobalGraphID[GlobalID];
		}
	}

	return nullptr;
}

void UMovieSceneEntitySystemLinker::LinkRelevantSystems()
{
	MOVIESCENE_DETAILED_SCOPE_CYCLE_COUNTER(MovieSceneEval_LinkRelevantSystems);

	// If the structure has not changed there's no way that there are any other relevant systems still
	if (EntityManager.HasStructureChangedSince(LastSystemLinkVersion))
	{
		UMovieSceneEntitySystem::LinkRelevantSystems(this);

		LastSystemLinkVersion = EntityManager.GetSystemSerial();
	}
}

void UMovieSceneEntitySystemLinker::AutoLinkRelevantSystems()
{
	if (AutoLinkMode == UE::MovieScene::EAutoLinkRelevantSystems::Enabled)
	{
		LinkRelevantSystems();
	}
}

FMovieSceneEntitySystemEvaluationReentrancyWindow::FMovieSceneEntitySystemEvaluationReentrancyWindow(UMovieSceneEntitySystemLinker& InLinker)
	: Linker(InLinker)
{
	CurrentLevel = Linker.ActiveRunners.Num() - 1;
	Linker.ActiveRunners[CurrentLevel].bIsReentrancyAllowed = true;
}

FMovieSceneEntitySystemEvaluationReentrancyWindow::~FMovieSceneEntitySystemEvaluationReentrancyWindow()
{
	if (ensure(Linker.ActiveRunners.IsValidIndex(CurrentLevel)))
	{
		Linker.ActiveRunners[CurrentLevel].bIsReentrancyAllowed = false;
	}
}
