// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/MovieSceneIntegerPropertySystem.h"
#include "Systems/IntegerChannelEvaluatorSystem.h"
#include "Systems/MovieScenePiecewiseIntegerBlenderSystem.h"
#include "MovieSceneTracksComponentTypes.h"

UMovieSceneIntegerPropertySystem::UMovieSceneIntegerPropertySystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	SystemExclusionContext |= UE::MovieScene::EEntitySystemContext::Interrogation;

	BindToProperty(UE::MovieScene::FMovieSceneTracksComponentTypes::Get()->Integer);

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		DefineImplicitPrerequisite(UMovieScenePiecewiseIntegerBlenderSystem::StaticClass(), GetClass());
		DefineImplicitPrerequisite(UIntegerChannelEvaluatorSystem::StaticClass(), GetClass());

		DefineComponentConsumer(GetClass(), UE::MovieScene::FMovieSceneTracksComponentTypes::Get()->Integer.PropertyTag);
	}
}

void UMovieSceneIntegerPropertySystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	Super::OnRun(InPrerequisites, Subsequents);
}

