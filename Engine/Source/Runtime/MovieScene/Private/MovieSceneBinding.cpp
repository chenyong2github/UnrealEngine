// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieSceneBinding.h"
#include "MovieSceneTrack.h"
#include "MovieScene.h"

/* FMovieSceneBinding interface
 *****************************************************************************/

void FMovieSceneBinding::AddTrack(UMovieSceneTrack& NewTrack)
{
#if WITH_EDITOR
	if (!UMovieScene::IsTrackClassAllowed(NewTrack.GetClass()))
	{
		return;
	}
#endif

	Tracks.Add(&NewTrack);
}

bool FMovieSceneBinding::RemoveTrack(UMovieSceneTrack& Track)
{
	return (Tracks.RemoveSingle(&Track) != 0);
}
