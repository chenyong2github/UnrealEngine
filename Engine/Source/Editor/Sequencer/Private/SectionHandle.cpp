// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SectionHandle.h"
#include "ISequencerSection.h"
#include "DisplayNodes/SequencerTrackNode.h"

TSharedRef<ISequencerSection> FSectionHandle::GetSectionInterface() const
{
	return TrackNode->GetSections()[SectionIndex];
}

UMovieSceneSection* FSectionHandle::GetSectionObject() const
{
	return GetSectionInterface()->GetSectionObject();
}