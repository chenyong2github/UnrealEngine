// Copyright Epic Games, Inc. All Rights Reserved.

#include "ITimedDataInput.h"

FFrameRate ITimedDataInput::UnknowedFrameRate = FFrameRate(-1, -1);


double ITimedDataInput::ConvertSecondOffsetInFrameOffset(double Seconds, FFrameRate Rate)
{
	return Rate.AsFrameTime(Seconds).AsDecimal();
}


double ITimedDataInput::ConvertFrameOffsetInSecondOffset(double Frames, FFrameRate Rate)
{
	return Rate.AsSeconds(FFrameTime::FromDecimal(Frames));
}
