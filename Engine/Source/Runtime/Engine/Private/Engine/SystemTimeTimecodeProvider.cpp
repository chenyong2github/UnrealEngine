// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Engine/SystemTimeTimecodeProvider.h"

#include "HAL/PlatformTime.h"
#include "Misc/CoreMisc.h"
#include "Misc/DateTime.h"


FTimecode USystemTimeTimecodeProvider::GenerateTimecodeFromSystemTime(FFrameRate FrameRate)
{
	const FDateTime DateTime = FDateTime::Now();
	const FTimespan Timespan = DateTime.GetTimeOfDay();
	return FTimecode::FromTimespan(Timespan, FrameRate, false);
}


static double ComputeTimeCodeOffset()
{
	const FDateTime DateTime = FDateTime::Now();
	const double HighPerformanceClock = FPlatformTime::Seconds();
	return DateTime.GetTimeOfDay().GetTotalSeconds() - HighPerformanceClock;
}


FTimecode USystemTimeTimecodeProvider::GenerateTimecodeFromHighPerformanceClock(FFrameRate FrameRate)
{
	constexpr double SecondsPerDay = 24.0 * 60.0 * 60.0;
	static double HighPerformanceClockDelta = ComputeTimeCodeOffset();
	const FTimespan Timespan = FTimespan::FromSeconds(fmod(HighPerformanceClockDelta + FPlatformTime::Seconds(), SecondsPerDay));
	return FTimecode::FromTimespan(Timespan, FrameRate, false);
}


FQualifiedFrameTime USystemTimeTimecodeProvider::GetQualifiedFrameTime() const
{
	// Generate a TC to prevent subframe
	return FQualifiedFrameTime(GenerateTimecodeFromSystemTime(FrameRate), FrameRate);
}
