// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConstraintChannel.h"

bool FMovieSceneConstraintChannel::Evaluate(FFrameTime InTime, bool& OutValue) const
{
	if (Times.IsEmpty())
	{
		return false;
	}

	if (InTime.FrameNumber < Times[0])
	{
		return false;
	}

	return FMovieSceneBoolChannel::Evaluate(InTime, OutValue);
}
