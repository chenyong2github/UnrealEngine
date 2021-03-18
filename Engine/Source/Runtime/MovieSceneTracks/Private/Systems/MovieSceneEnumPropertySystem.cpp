// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/MovieSceneEnumPropertySystem.h"
#include "Systems/ByteChannelEvaluatorSystem.h"
#include "MovieSceneTracksComponentTypes.h"

UMovieSceneEnumPropertySystem::UMovieSceneEnumPropertySystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	SystemExclusionContext |= UE::MovieScene::EEntitySystemContext::Interrogation;

	BindToProperty(UE::MovieScene::FMovieSceneTracksComponentTypes::Get()->Enum);

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		DefineImplicitPrerequisite(UByteChannelEvaluatorSystem::StaticClass(), GetClass());

		DefineComponentConsumer(GetClass(), UE::MovieScene::FMovieSceneTracksComponentTypes::Get()->Enum.PropertyTag);
	}
}

void UMovieSceneEnumPropertySystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	Super::OnRun(InPrerequisites, Subsequents);
}

