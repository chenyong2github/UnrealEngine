// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/CycleTime.h"

#include "HAL/PlatformTime.h"
#include "Misc/Timespan.h"

namespace UE
{

FCycleTimeSpan::FCycleTimeSpan(const FTimespan Span)
	: Cycles(int64(Span.GetTotalSeconds() / FPlatformTime::GetSecondsPerCycle64()))
{
}

double FCycleTimeSpan::ToSeconds() const
{
	return FPlatformTime::GetSecondsPerCycle64() * double(Cycles);
}

double FCycleTimeSpan::ToMilliseconds() const
{
	return FPlatformTime::GetSecondsPerCycle64() * 1000.0 * double(Cycles);
}

FCycleTimeSpan FCycleTimeSpan::FromSeconds(const double Seconds)
{
	return FromCycles(int64(Seconds / FPlatformTime::GetSecondsPerCycle64()));
}

FCycleTimeSpan FCycleTimeSpan::FromMilliseconds(const double Milliseconds)
{
	return FromCycles(int64(Milliseconds / 1000.0 / FPlatformTime::GetSecondsPerCycle64()));
}

FCycleTimePoint FCycleTimePoint::Now()
{
	return FromCycles(FPlatformTime::Cycles64());
}

} // namespace UE
