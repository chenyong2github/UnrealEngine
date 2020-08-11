// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sections/MovieSceneCameraShakeSourceShakeSection.h"
#include "Tracks/MovieSceneCameraShakeSourceShakeTrack.h"

UMovieSceneCameraShakeSourceShakeSection::UMovieSceneCameraShakeSourceShakeSection(const FObjectInitializer& ObjectInitializer)
	: Super( ObjectInitializer )
{
	EvalOptions.EnableAndSetCompletionMode(EMovieSceneCompletionMode::ProjectDefault);
}
