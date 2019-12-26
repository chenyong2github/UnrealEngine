// Copyright Epic Games, Inc. All Rights Reserved.

#include "Engine/SystemTimeTimecodeProvider.h"

#include "Misc/CoreMisc.h"
#include "Misc/DateTime.h"

static double ComputeTimeCodeOffset()
{
	const FDateTime DateTime = FDateTime::Now();
	double HighPerformanceClock = FPlatformTime::Seconds();
	const FTimespan Timespan = DateTime.GetTimeOfDay();
	double Delta = Timespan.GetTotalSeconds() - HighPerformanceClock;
	return Delta;
}


FTimecode USystemTimeTimecodeProvider::GetTimecode() const
{
	static double HighPerformanceClockDelta = ComputeTimeCodeOffset();
	static double SecondsPerDay = 24.0 * 60.0 * 60.0;
	FTimespan Timespan;
	Timespan = FTimespan::FromSeconds(fmod(HighPerformanceClockDelta + FPlatformTime::Seconds(), SecondsPerDay));

	FTimecode Result =  FTimecode::FromTimespan(Timespan, FrameRate, FTimecode::IsDropFormatTimecodeSupported(FrameRate), false);
	return Result;
}
