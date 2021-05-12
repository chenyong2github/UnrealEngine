// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/MovieSceneComponentTransformSystem.h"
#include "Systems/FloatChannelEvaluatorSystem.h"
#include "Systems/MovieScenePiecewiseFloatBlenderSystem.h"
#include "Systems/MovieScenePropertyInstantiator.h"

#include "EntitySystem/BuiltInComponentTypes.h"

#include "MovieSceneTracksComponentTypes.h"

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

		DefineComponentConsumer(GetClass(), UE::MovieScene::FMovieSceneTracksComponentTypes::Get()->ComponentTransform.PropertyTag);
	}
}

void UMovieSceneComponentTransformSystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	Super::OnRun(InPrerequisites, Subsequents);
}
