// Copyright 1998-2020 Epic Games, Inc. All Rights Reserved.

#include "Engine/TimecodeProvider.h"


FQualifiedFrameTime UTimecodeProvider::GetDelayedQualifiedFrameTime() const
{
	FQualifiedFrameTime NewFrameTime = GetQualifiedFrameTime();
	NewFrameTime.Time -= FFrameTime::FromDecimal(FrameDelay);
	return NewFrameTime;
}


FTimecode UTimecodeProvider::GetTimecode() const
{
	const FQualifiedFrameTime NewFrameTime = GetQualifiedFrameTime();
	return FTimecode::FromFrameNumber(NewFrameTime.Time.GetFrame(), NewFrameTime.Rate);
}


FTimecode UTimecodeProvider::GetDelayedTimecode() const
{
	const FQualifiedFrameTime NewFrameTime = GetDelayedQualifiedFrameTime();
	return FTimecode::FromFrameNumber(NewFrameTime.Time.GetFrame(), NewFrameTime.Rate);
}
