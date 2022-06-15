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
