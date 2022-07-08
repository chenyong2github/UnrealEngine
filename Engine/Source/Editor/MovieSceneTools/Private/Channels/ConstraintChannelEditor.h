// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SequencerChannelTraits.h"
#include "SequencerChannelInterface.h"

struct FMovieSceneConstraintChannel;

/** Key drawing overrides */
void DrawKeys(
	FMovieSceneConstraintChannel* Channel,
	TArrayView<const FKeyHandle> InKeyHandles,
	const UMovieSceneSection* InOwner,
	TArrayView<FKeyDrawParams> OutKeyDrawParams);

void DrawExtra(
	FMovieSceneConstraintChannel* Channel,
	const UMovieSceneSection* Owner,
	const FGeometry& AllottedGeometry,
	FSequencerSectionPainter& Painter);

/** Overrides for adding or updating a key for non-standard channels */
FKeyHandle AddOrUpdateKey(
	FMovieSceneConstraintChannel* Channel,
	UMovieSceneSection* SectionToKey,
	FFrameNumber Time,
	ISequencer& Sequencer,
	const FGuid& ObjectBindingID,
	FTrackInstancePropertyBindings* PropertyBindings);

/** Key editor overrides */
bool CanCreateKeyEditor(const FMovieSceneConstraintChannel* InChannel);

TSharedRef<SWidget> CreateKeyEditor(
	const TMovieSceneChannelHandle<FMovieSceneConstraintChannel>& InChannel,
	UMovieSceneSection* InSection,
	const FGuid& InObjectBindingID,
	TWeakPtr<FTrackInstancePropertyBindings> PropertyBindings,
	TWeakPtr<ISequencer> Sequencer);