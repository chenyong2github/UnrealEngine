// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExtensionLibraries/MovieSceneMaterialTrackExtensions.h"
#include "Tracks/MovieSceneMaterialTrack.h"

void UMovieSceneMaterialTrackExtensions::SetMaterialIndex(UMovieSceneComponentMaterialTrack* Track, const int32 MaterialIndex)
{
	if (!Track)
	{
		FFrame::KismetExecutionMessage(TEXT("Cannot call SetMaterialIndex on a null track"), ELogVerbosity::Error);
		return;
	}

	Track->Modify();
	Track->SetMaterialIndex(MaterialIndex);
}

int32 UMovieSceneMaterialTrackExtensions::GetMaterialIndex(UMovieSceneComponentMaterialTrack* Track)
{
	if (!Track)
	{
		FFrame::KismetExecutionMessage(TEXT("Cannot call GetMaterialIndex on a null track"), ELogVerbosity::Error);
		return -1;
	}

	return Track->GetMaterialIndex();
}


