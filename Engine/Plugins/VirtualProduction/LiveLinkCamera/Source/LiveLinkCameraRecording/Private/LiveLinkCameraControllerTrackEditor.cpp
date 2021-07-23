// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkCameraControllerTrackEditor.h"

#include "LevelSequence.h"


TSharedRef<ISequencerTrackEditor> FLiveLinkCameraControllerTrackEditor::CreateTrackEditor(TSharedRef<ISequencer> InSequencer)
{
	return MakeShared<FLiveLinkCameraControllerTrackEditor>(InSequencer);
}

bool FLiveLinkCameraControllerTrackEditor::SupportsSequence(UMovieSceneSequence* InSequence) const
{
	return (InSequence && InSequence->IsA(ULevelSequence::StaticClass()));
}

bool FLiveLinkCameraControllerTrackEditor::SupportsType(TSubclassOf<UMovieSceneTrack> Type) const
{
	return (Type == UMovieSceneLiveLinkCameraControllerTrack::StaticClass());
}
