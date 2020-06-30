// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieSceneTestObjects.h"
#include "Compilation/MovieSceneCompilerRules.h"
#include "Compilation/MovieSceneSegmentCompiler.h"

FMovieSceneEvalTemplatePtr UTestMovieSceneTrack::CreateTemplateForSection(const UMovieSceneSection& InSection) const
{
	return FTestMovieSceneEvalTemplate();
}
