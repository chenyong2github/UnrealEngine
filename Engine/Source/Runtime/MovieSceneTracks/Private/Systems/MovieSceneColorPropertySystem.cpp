// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/MovieSceneColorPropertySystem.h"
#include "MovieSceneTracksComponentTypes.h"
#include "Systems/FloatChannelEvaluatorSystem.h"
#include "Systems/MovieScenePiecewiseFloatBlenderSystem.h"


UMovieSceneColorPropertySystem::UMovieSceneColorPropertySystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	SystemExclusionContext |= UE::MovieScene::EEntitySystemContext::Interrogation;

	BindToProperty(UE::MovieScene::FMovieSceneTracksComponentTypes::Get()->Color);

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		// We need our floats correctly evaluated and blended, so we are downstream from those systems.
		DefineImplicitPrerequisite(UMovieScenePiecewiseFloatBlenderSystem::StaticClass(), GetClass());
		DefineImplicitPrerequisite(UFloatChannelEvaluatorSystem::StaticClass(), GetClass());
	}
}

void UMovieSceneColorPropertySystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	Super::OnRun(InPrerequisites, Subsequents);
}


