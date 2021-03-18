// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/MovieSceneBoolPropertySystem.h"
#include "Systems/MovieScenePiecewiseBoolBlenderSystem.h"
#include "MovieSceneTracksComponentTypes.h"

UMovieSceneBoolPropertySystem::UMovieSceneBoolPropertySystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	SystemExclusionContext |= UE::MovieScene::EEntitySystemContext::Interrogation;

	BindToProperty(UE::MovieScene::FMovieSceneTracksComponentTypes::Get()->Bool);

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		DefineImplicitPrerequisite(UMovieScenePiecewiseBoolBlenderSystem::StaticClass(), GetClass());

		DefineComponentConsumer(GetClass(), UE::MovieScene::FMovieSceneTracksComponentTypes::Get()->Bool.PropertyTag);
	}
}

void UMovieSceneBoolPropertySystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	Super::OnRun(InPrerequisites, Subsequents);
}

