// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieScenePossessable.h"
#include "MovieSceneSpawnableAnnotation.h"
#include "MovieSceneSequence.h"
#include "MovieSceneSequenceID.h"
#include "MovieScene.h"
#include "IMovieScenePlayer.h"

bool FMovieScenePossessable::BindSpawnableObject(FMovieSceneSequenceID SequenceID, UObject* Object, IMovieScenePlayer* Player)
{
	TOptional<FMovieSceneSpawnableAnnotation> Spawnable = FMovieSceneSpawnableAnnotation::Find(Object);
	if (Spawnable.IsSet())
	{
		// Check whether the spawnable is underneath the current sequence, if so, we can remap it to a local sequence ID
		SetSpawnableObjectBindingID(UE::MovieScene::FRelativeObjectBindingID(SequenceID, Spawnable->SequenceID, Spawnable->ObjectBindingID, *Player));
		return true;
	}

	return false;
}
