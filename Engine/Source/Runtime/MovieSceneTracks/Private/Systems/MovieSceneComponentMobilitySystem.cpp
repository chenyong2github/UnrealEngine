// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/MovieSceneComponentMobilitySystem.h"
#include "MovieSceneTracksComponentTypes.h"
#include "IMovieScenePlayer.h"
#include "MovieSceneExecutionToken.h"

#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieSceneEntitySystemTask.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"

#include "Systems/MovieSceneComponentTransformSystem.h"
#include "Systems/MovieScenePropertyInstantiator.h"

#include "Components/SceneComponent.h"
#include "Evaluation/MovieSceneTemplateCommon.h"

namespace UE
{
namespace MovieScene
{

struct FMobilityCacheHandler
{
	UMovieSceneComponentMobilitySystem* System;

	FMobilityCacheHandler(UMovieSceneComponentMobilitySystem* InSystem)
		: System(InSystem)
	{}

	void InitializeOutput(UObject* Object, TArrayView<const FMovieSceneEntityID> Inputs, EComponentMobility::Type* OutMobility, FEntityOutputAggregate Aggregate)
	{
		if (USceneComponent* SceneComponent = Cast<USceneComponent>(Object))
		{
			*OutMobility = SceneComponent->Mobility;
			SceneComponent->SetMobility(EComponentMobility::Movable);
		}
	}

	static void UpdateOutput(UObject* Object, TArrayView<const FMovieSceneEntityID> Inputs, EComponentMobility::Type* OutMobility, FEntityOutputAggregate Aggregate)
	{
		if (USceneComponent* SceneComponent = Cast<USceneComponent>(Object))
		{
			SceneComponent->SetMobility(EComponentMobility::Movable);
		}
	}

	void DestroyOutput(UObject* Object, EComponentMobility::Type* Output, FEntityOutputAggregate Aggregate)
	{
		if (Aggregate.bNeedsRestoration)
		{
			if (USceneComponent* SceneComponent = Cast<USceneComponent>(Object))
			{
				System->AddPendingRestore(SceneComponent, *Output);
			}
		}
	}
};

/**
 * Gets the flattened depth-first hierarchy of the scene component.
 */
static void GetFlattenedHierarchy(USceneComponent* SceneComponent, TArray<USceneComponent*, TInlineAllocator<4>>& OutFlatHierarchy)
{
	TArray<USceneComponent*, TInlineAllocator<4>> ChildrenStack;
	ChildrenStack.Push(SceneComponent);
	while (ChildrenStack.Num() > 0)
	{
		USceneComponent* Child(ChildrenStack.Pop());

		OutFlatHierarchy.Add(Child);

		const TArray<USceneComponent*>& ChildAttachChildren = Child->GetAttachChildren();
		for (int32 Index = ChildAttachChildren.Num() - 1; Index >= 0; --Index)
		{
			USceneComponent* GrandChild = ChildAttachChildren[Index];
			if (GrandChild)
			{
				ChildrenStack.Add(GrandChild);
			}
		}
	}
}

} // namespace MovieScene
} // namespace UE


UMovieSceneComponentMobilitySystem::UMovieSceneComponentMobilitySystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	using namespace UE::MovieScene;

	SystemExclusionContext |= EEntitySystemContext::Interrogation;

	FBuiltInComponentTypes*          BuiltInComponents = FBuiltInComponentTypes::Get();
	FMovieSceneTracksComponentTypes* TrackComponents   = FMovieSceneTracksComponentTypes::Get();

	// Anything with a component transform or attach component neds to have its mobility pre-set to Moveable
	Filter.Any({ TrackComponents->ComponentTransform.PropertyTag, TrackComponents->AttachParent });

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		DefineImplicitPrerequisite(UMovieSceneCachePreAnimatedStateSystem::StaticClass(), GetClass());
		DefineImplicitPrerequisite(GetClass(), UMovieSceneRestorePreAnimatedStateSystem::StaticClass());
		DefineImplicitPrerequisite(GetClass(), UMovieScenePreAnimatedComponentTransformSystem::StaticClass());

		DefineComponentConsumer(GetClass(), FBuiltInComponentTypes::Get()->SymbolicTags.CreatesEntities);
	}
}

bool UMovieSceneComponentMobilitySystem::IsRelevantImpl(UMovieSceneEntitySystemLinker* InLinker) const
{
	return InLinker->EntityManager.Contains(Filter);
}

void UMovieSceneComponentMobilitySystem::OnLink()
{
	UMovieSceneRestorePreAnimatedStateSystem* RestoreSystem = Linker->LinkSystem<UMovieSceneRestorePreAnimatedStateSystem>();
	Linker->SystemGraph.AddReference(this, RestoreSystem);
	Linker->SystemGraph.AddPrerequisite(this, RestoreSystem);

	Linker->Events.TagGarbage.AddUObject(this, &UMovieSceneComponentMobilitySystem::TagGarbage);
}

void UMovieSceneComponentMobilitySystem::OnUnlink()
{
	using namespace UE::MovieScene;

	// Destroy everything
	MobilityTracker.Destroy(FMobilityCacheHandler(this));
}

void UMovieSceneComponentMobilitySystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	ensureMsgf(PendingMobilitiesToRestore.Num() == 0, TEXT("Pending mobilities were not previously restored when they should have been"));

	// Update the mobility tracker, caching preanimated mobilities and assigning everything as moveable that needs it
	MobilityTracker.Update(Linker, FBuiltInComponentTypes::Get()->BoundObject, Filter);
	MobilityTracker.ProcessInvalidatedOutputs(FMobilityCacheHandler(this));
}

void UMovieSceneComponentMobilitySystem::TagGarbage(UMovieSceneEntitySystemLinker*)
{
	MobilityTracker.CleanupGarbage();
}

void UMovieSceneComponentMobilitySystem::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	CastChecked<UMovieSceneComponentMobilitySystem>(InThis)->MobilityTracker.AddReferencedObjects(Collector);
}

void UMovieSceneComponentMobilitySystem::AddPendingRestore(USceneComponent* SceneComponent, EComponentMobility::Type InMobility)
{
	PendingMobilitiesToRestore.Add(MakeTuple(SceneComponent, InMobility));
}

void UMovieSceneComponentMobilitySystem::SaveGlobalPreAnimatedState(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	FMovieSceneTracksComponentTypes* TrackComponents = FMovieSceneTracksComponentTypes::Get();
	FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();

	static FMovieSceneAnimTypeID AnimType = FMobilityTokenProducer::GetAnimTypeID();

	FMobilityTokenProducer Producer;

	FInstanceRegistry* InstanceRegistry = Linker->GetInstanceRegistry();
	auto IterNewObjects = [Producer, InstanceRegistry](FInstanceHandle InstanceHandle, UObject* InObject)
	{
		IMovieScenePlayer* Player = InstanceRegistry->GetInstance(InstanceHandle).GetPlayer();
		
		if (USceneComponent* SceneComponent = Cast<USceneComponent>(InObject))
		{
			TArray<USceneComponent*, TInlineAllocator<4>> FlatHierarchy;
			GetFlattenedHierarchy(SceneComponent, FlatHierarchy);
			for (USceneComponent* CurrentSceneComponent : FlatHierarchy)
			{
				Player->SaveGlobalPreAnimatedState(*CurrentSceneComponent, AnimType, Producer);
			}
		}
		else
		{
			Player->SaveGlobalPreAnimatedState(*InObject, AnimType, Producer);
		}
	};

	FEntityTaskBuilder()
	.Read(BuiltInComponents->InstanceHandle)
	.Read(BuiltInComponents->BoundObject)
	.FilterAll({ BuiltInComponents->Tags.NeedsLink })
	.FilterAny({ TrackComponents->ComponentTransform.PropertyTag, TrackComponents->AttachParent })
	.Iterate_PerEntity(&Linker->EntityManager, IterNewObjects);
}

void UMovieSceneComponentMobilitySystem::RestorePreAnimatedState(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	for (TTuple<USceneComponent*, EComponentMobility::Type> Pair : PendingMobilitiesToRestore)
	{
		Pair.Key->SetMobility(Pair.Value);
	}
	PendingMobilitiesToRestore.Empty();
}
