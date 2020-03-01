// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieSceneBinding.h"
#include "MovieSceneTrack.h"


/* FMovieSceneBinding interface
 *****************************************************************************/

void FMovieSceneBinding::AddTrack(UMovieSceneTrack& NewTrack)
{
	Tracks.Add(&NewTrack);
}

bool FMovieSceneBinding::RemoveTrack(UMovieSceneTrack& Track)
{
	return (Tracks.RemoveSingle(&Track) != 0);
}
