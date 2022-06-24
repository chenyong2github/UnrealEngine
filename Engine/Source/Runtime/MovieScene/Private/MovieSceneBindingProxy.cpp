// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieSceneBindingProxy.h"
#include "MovieSceneSequence.h"

UMovieScene* FMovieSceneBindingProxy::GetMovieScene() const
{
	return Sequence ? Sequence->GetMovieScene() : nullptr;
}