// Copyright Epic Games, Inc. All Rights Reserved.

#include "Engine/TimecodeProvider.h"


FTimecode UTimecodeProvider::GetDelayedTimecode() const
{
	FTimecode NewTimecode = GetTimecode();
	FFrameRate NewFrameRate = GetFrameRate();
	if (FrameDelay != 0)
	{
		FFrameNumber TimecodeFrameNumber = NewTimecode.ToFrameNumber(NewFrameRate);
		TimecodeFrameNumber -= FrameDelay;
		NewTimecode = FTimecode::FromFrameNumber(TimecodeFrameNumber, NewFrameRate, NewTimecode.bDropFrameFormat);
	}
	return NewTimecode;
}
