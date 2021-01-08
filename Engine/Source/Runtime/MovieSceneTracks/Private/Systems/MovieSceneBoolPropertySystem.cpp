// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/MovieSceneBoolPropertySystem.h"

#include "MovieSceneTracksComponentTypes.h"


UMovieSceneBoolPropertySystem::UMovieSceneBoolPropertySystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	SystemExclusionContext |= UE::MovieScene::EEntitySystemContext::Interrogation;

	BindToProperty(UE::MovieScene::FMovieSceneTracksComponentTypes::Get()->Bool);
}

void UMovieSceneBoolPropertySystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	Super::OnRun(InPrerequisites, Subsequents);
}

