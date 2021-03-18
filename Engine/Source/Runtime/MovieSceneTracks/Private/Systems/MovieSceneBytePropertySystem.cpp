// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/MovieSceneBytePropertySystem.h"
#include "Systems/ByteChannelEvaluatorSystem.h"
#include "MovieSceneTracksComponentTypes.h"

UMovieSceneBytePropertySystem::UMovieSceneBytePropertySystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	SystemExclusionContext |= UE::MovieScene::EEntitySystemContext::Interrogation;

	BindToProperty(UE::MovieScene::FMovieSceneTracksComponentTypes::Get()->Byte);

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		DefineImplicitPrerequisite(UByteChannelEvaluatorSystem::StaticClass(), GetClass());

		DefineComponentConsumer(GetClass(), UE::MovieScene::FMovieSceneTracksComponentTypes::Get()->Byte.PropertyTag);
	}
}

void UMovieSceneBytePropertySystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	Super::OnRun(InPrerequisites, Subsequents);
}

