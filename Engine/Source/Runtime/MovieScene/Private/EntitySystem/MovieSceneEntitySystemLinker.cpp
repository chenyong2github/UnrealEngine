// Copyright Epic Games, Inc. All Rights Reserved.

#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/MovieSceneEntitySystemTypes.h"
#include "EntitySystem/MovieSceneEntitySystem.h"
#include "EntitySystem/MovieSceneEntitySystemTask.h"
#include "EntitySystem/MovieSceneEntityMutations.h"
#include "EntitySystem/MovieSceneComponentRegistry.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieScenePreAnimatedStateSystem.h"
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

} // namespace MovieScene
} // namespace UE


UMovieSceneEntitySystemLinker::UMovieSceneEntitySystemLinker(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	using namespace UE::MovieScene;

	LastSystemLinkVersion = 0;
	AutoLinkMode = EAutoLinkRelevantSystems::Enabled;
	SystemContext = EEntitySystemContext::Runtime;

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		FCoreUObjectDelegates::GetPostGarbageCollect().AddUObject(this, &UMovieSceneEntitySystemLinker::HandlePostGarbageCollection);

		EntityManager.SetDebugName(GetName() + TEXT("[Entity Manager]"));
		EntityManager.SetComponentRegistry(&GComponentRegistry);

		FWorldDelegates::OnWorldCleanup.AddUObject(this, &UMovieSceneEntitySystemLinker::OnWorldCleanup);

		InstanceRegistry.Reset(new FInstanceRegistry(this));
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

UMovieSceneEntitySystemLinker* UMovieSceneEntitySystemLinker::FindOrCreateLinker(UObject* PreferredOuter, const TCHAR* Name)
{
	if (!PreferredOuter)
	{
		PreferredOuter = GetTransientPackage();
	}

	UMovieSceneEntitySystemLinker* Existing = FindObject<UMovieSceneEntitySystemLinker>(PreferredOuter, Name);
	if (!Existing)
	{
		Existing = NewObject<UMovieSceneEntitySystemLinker>(PreferredOuter, Name);
	}
	return Existing;
}

UMovieSceneEntitySystemLinker* UMovieSceneEntitySystemLinker::CreateLinker(UObject* PreferredOuter)
{
	if (!PreferredOuter)
	{
		PreferredOuter = GetTransientPackage();
	}

	return NewObject<UMovieSceneEntitySystemLinker>(PreferredOuter);
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

bool UMovieSceneEntitySystemLinker::StartEvaluation(FMovieSceneEntitySystemRunner& InRunner)
{
	if (ActiveRunners.Num() == 0 || ActiveRunners.Last().bIsReentrancyAllowed)
	{
		// Default to re-entrancy being forbidden. The runner will allow re-entrancy at specific spots
		// in the evaluation loop, via a "re-entrancy window".
		ActiveRunners.Emplace(FActiveRunnerInfo{ &InRunner, false });
		return true;
	}
		
	UE_LOG(LogMovieScene, Warning, TEXT("Can't start a new evaluation: the active runner is not in a re-entrancy window."));
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

void UMovieSceneEntitySystemLinker::EndEvaluation(FMovieSceneEntitySystemRunner& InRunner)
{
	if (ensureMsgf((ActiveRunners.Num() > 0 && ActiveRunners.Last().Runner == &InRunner),
				TEXT("Trying end the evaluation of a runner that's not the latest one to run.")))
	{
		ActiveRunners.Pop();
	}
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

	FComponentTypeID NeedsUnlink = FBuiltInComponentTypes::Get()->Tags.NeedsUnlink;
	if (!EntityManager.ContainsComponent(NeedsUnlink))
	{
		return;
	}

	// Allow any other system to tag garbage
	Events.CleanTaggedGarbage.Broadcast(this);

	auto RouteCleanTaggedGarbage = [](UMovieSceneEntitySystem* System){ System->CleanTaggedGarbage(); };
	SystemGraph.IteratePhase(ESystemPhase::Spawn, RouteCleanTaggedGarbage);
	SystemGraph.IteratePhase(ESystemPhase::Instantiation, RouteCleanTaggedGarbage);

	// Free the entities
	TSet<FMovieSceneEntityID> FreedEntities;
	EntityManager.FreeEntities(FEntityComponentFilter().All({ NeedsUnlink }), &FreedEntities);

	InstanceRegistry->CleanupLinkerEntities(FreedEntities);
}

void UMovieSceneEntitySystemLinker::OnWorldCleanup(UWorld* InWorld, bool bSessionEnded, bool bCleanupResources)
{
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
}

UMovieSceneEntitySystem* UMovieSceneEntitySystemLinker::LinkSystem(TSubclassOf<UMovieSceneEntitySystem> InClassType)
{
	UMovieSceneEntitySystem* Existing = FindSystem(InClassType);
	if (Existing)
	{
		return Existing;
	}

	// We always create systems with a fixed name (since there should only ever be one of that name)
	// This means we will recycle systems if they previously existed but are no longer used to avoid thrashing the GC
	// Recycling will destruct + memzero + construct the object so we can be sure that previous state doesn't roll over
	UClass* SystemClass = InClassType.Get();
	FName   SystemName  = SystemClass->GetFName();
	UMovieSceneEntitySystem* NewSystem = NewObject<UMovieSceneEntitySystem>(this, SystemClass, SystemName);

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
