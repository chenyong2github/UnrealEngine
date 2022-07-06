// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConstraintChannel.h"

bool FMovieSceneConstraintChannel::Evaluate(FFrameTime InTime, bool& OutValue) const
{
	const TArrayView<const FFrameNumber> TimeArray = GetTimes();
	if (TimeArray.IsEmpty())
	{
		return false;
	}

	if (InTime.FrameNumber < TimeArray[0])
	{
		return false;
	}

	return FMovieSceneBoolChannel::Evaluate(InTime, OutValue);
}
