// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MovieSceneTrackEditor.h"

class UCameraAnim;

class FCameraAnimTrackEditorHelper
{
public:
	MOVIESCENETOOLS_API static FKeyPropertyResult AddCameraAnimKey(FMovieSceneTrackEditor& TrackEditor, FFrameNumber KeyTime, const TArray<TWeakObjectPtr<UObject>> Objects, UCameraAnim* CameraAnim);
};

