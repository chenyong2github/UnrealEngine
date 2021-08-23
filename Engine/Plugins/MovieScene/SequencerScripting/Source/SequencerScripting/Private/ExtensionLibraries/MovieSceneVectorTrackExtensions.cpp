// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExtensionLibraries/MovieSceneVectorTrackExtensions.h"
#include "Tracks/MovieSceneVectorTrack.h"

void UMovieSceneFloatVectorTrackExtensions::SetNumChannelsUsed(UMovieSceneFloatVectorTrack* Track, int32 InNumChannelsUsed)
{
	Track->Modify();

	Track->SetNumChannelsUsed(InNumChannelsUsed);
}


int32 UMovieSceneFloatVectorTrackExtensions::GetNumChannelsUsed(UMovieSceneFloatVectorTrack* Track)
{
	return Track->GetNumChannelsUsed();
}

void UMovieSceneDoubleVectorTrackExtensions::SetNumChannelsUsed(UMovieSceneDoubleVectorTrack* Track, int32 InNumChannelsUsed)
{
	Track->Modify();

	Track->SetNumChannelsUsed(InNumChannelsUsed);
}


int32 UMovieSceneDoubleVectorTrackExtensions::GetNumChannelsUsed(UMovieSceneDoubleVectorTrack* Track)
{
	return Track->GetNumChannelsUsed();
}

