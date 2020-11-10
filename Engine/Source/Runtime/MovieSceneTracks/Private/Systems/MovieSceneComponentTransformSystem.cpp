// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/MovieSceneComponentTransformSystem.h"
#include "Systems/FloatChannelEvaluatorSystem.h"
#include "Systems/MovieScenePiecewiseFloatBlenderSystem.h"
#include "Systems/MovieScenePropertyInstantiator.h"

#include "EntitySystem/BuiltInComponentTypes.h"

#include "MovieSceneTracksComponentTypes.h"

namespace UE
{
namespace MovieScene
{

struct FPreAnimatedComponentTransformHandler
{
	UMovieScenePreAnimatedComponentTransformSystem* System;

	FPreAnimatedComponentTransformHandler(UMovieScenePreAnimatedComponentTransformSystem* InSystem)
		: System(InSystem)
	{}

	static void InitializeOutput(UObject* Object, TArrayView<const FMovieSceneEntityID> Inputs, FIntermediate3DTransform* Output, FEntityOutputAggregate Aggregate)
	{
		if (USceneComponent* SceneComponent = Cast<USceneComponent>(Object))
		{
			ConvertOperationalProperty(SceneComponent->GetRelativeTransform(), *Output);
		}
	}

	static void UpdateOutput(UObject* Object, TArrayView<const FMovieSceneEntityID> Inputs, FIntermediate3DTransform* Output, FEntityOutputAggregate Aggregate)
	{
	}

	void DestroyOutput(UObject* Object, FIntermediate3DTransform* Output, FEntityOutputAggregate Aggregate)
	{
		if (Aggregate.bNeedsRestoration)
		{
			System->AddPendingRestoreTransform(Object, *Output);
		}
	}
};

} // namespace MovieScene
} // namespace UE


UMovieScenePreAnimatedComponentTransformSystem::UMovieScenePreAnimatedComponentTransformSystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	using namespace UE::MovieScene;

	SystemExclusionContext |= EEntitySystemContext::Interrogation;

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		DefineComponentConsumer(GetClass(), FBuiltInComponentTypes::Get()->SymbolicTags.CreatesEntities);

		DefineImplicitPrerequisite(UMovieSceneCachePreAnimatedStateSystem::StaticClass(), GetClass());
		DefineImplicitPrerequisite(GetClass(), UMovieSceneRestorePreAnimatedStateSystem::StaticClass());
	}
}

bool UMovieScenePreAnimatedComponentTransformSystem::IsRelevantImpl(UMovieSceneEntitySystemLinker* InLinker) const
{
	using namespace UE::MovieScene;

	return InLinker->ShouldCaptureGlobalState() ||  InLinker->EntityManager.Contains(FEntityComponentFilter().All({
			FMovieSceneTracksComponentTypes::Get()->ComponentTransform.PropertyTag,
			FBuiltInComponentTypes::Get()->Tags.RestoreState,
			FBuiltInComponentTypes::Get()->BoundObject
		})
	);
}

void UMovieScenePreAnimatedComponentTransformSystem::OnLink()
{
	using namespace UE::MovieScene;

	Linker->Events.TagGarbage.AddUObject(this, &UMovieScenePreAnimatedComponentTransformSystem::TagGarbage);
}

void UMovieScenePreAnimatedComponentTransformSystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	ensureMsgf(TransformsToRestore.Num() == 0, TEXT("Pending transforms were not previously restored when they should have been"));

	FEntityComponentFilter ComponentFilter;
	ComponentFilter.All({ FMovieSceneTracksComponentTypes::Get()->ComponentTransform.PropertyTag });

	TrackedTransforms.Update(Linker, FBuiltInComponentTypes::Get()->BoundObject, ComponentFilter);
	TrackedTransforms.ProcessInvalidatedOutputs(FPreAnimatedComponentTransformHandler(this));
}

void UMovieScenePreAnimatedComponentTransformSystem::TagGarbage(UMovieSceneEntitySystemLinker*)
{
	TrackedTransforms.CleanupGarbage();
}

void UMovieScenePreAnimatedComponentTransformSystem::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	CastChecked<UMovieScenePreAnimatedComponentTransformSystem>(InThis)->TrackedTransforms.AddReferencedObjects(Collector);
}

void UMovieScenePreAnimatedComponentTransformSystem::AddPendingRestoreTransform(UObject* Object, const UE::MovieScene::FIntermediate3DTransform& InTransform)
{
	TransformsToRestore.Add(MakeTuple(Object, InTransform));
}

void UMovieScenePreAnimatedComponentTransformSystem::RestorePreAnimatedState(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	for (const TTuple<UObject*, FIntermediate3DTransform>& Pair : TransformsToRestore)
	{
		if (USceneComponent* SceneComponent = Cast<USceneComponent>(Pair.Get<0>()))
		{
			Pair.Get<1>().ApplyTo(SceneComponent);
		}
	}
	TransformsToRestore.Empty();
}

void UMovieScenePreAnimatedComponentTransformSystem::SaveGlobalPreAnimatedState(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();

	FCompositePropertyTypeID PropertyID = FMovieSceneTracksComponentTypes::Get()->ComponentTransform.CompositeID;

	const FPropertyDefinition& Definition = BuiltInComponents->PropertyRegistry.GetDefinition(PropertyID);
	TArrayView<const FPropertyCompositeDefinition> Composites = BuiltInComponents->PropertyRegistry.GetComposites(Definition);

	Definition.Handler->SaveGlobalPreAnimatedState(Definition, Linker);
}

void UMovieScenePreAnimatedComponentTransformSystem::DiscardPreAnimatedStateForObject(UObject& Object)
{
	using namespace UE::MovieScene;

	TrackedTransforms.SetNeedsRestoration(&Object, false);

	TransformsToRestore.RemoveAll([&Object](const TTuple<UObject*, FIntermediate3DTransform>& Pair) -> bool
			{ return Pair.Get<0>() == &Object; });
}


UMovieSceneComponentTransformSystem::UMovieSceneComponentTransformSystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	// This system can be used for interrogation
	SystemExclusionContext = UE::MovieScene::EEntitySystemContext::None;

	BindToProperty(UE::MovieScene::FMovieSceneTracksComponentTypes::Get()->ComponentTransform);

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		DefineImplicitPrerequisite(UMovieScenePiecewiseFloatBlenderSystem::StaticClass(), GetClass());
		DefineImplicitPrerequisite(UFloatChannelEvaluatorSystem::StaticClass(), GetClass());
	}
}

void UMovieSceneComponentTransformSystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	Super::OnRun(InPrerequisites, Subsequents);
}
