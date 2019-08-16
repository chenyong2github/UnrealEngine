// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TimingTrackViewport.h"

#include <limits>

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FTimingTrackViewport::ScrollAtTime(const double Time)
{
	const double NewStartTime = AlignTimeToPixel(Time);
	if (NewStartTime != StartTime)
	{
		StartTime = NewStartTime;
		EndTime = SlateUnitsToTime(Width);
		return true;
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FTimingTrackViewport::CenterOnTimeInterval(const double Time, const double Duration)
{
	double NewStartTime = Time;
	const double ViewportDuration = static_cast<double>(Width) / ScaleX;
	if (Duration < ViewportDuration)
	{
		NewStartTime -= (ViewportDuration - Duration) / 2.0;
	}
	return ScrollAtTime(NewStartTime);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FTimingTrackViewport::ZoomOnTimeInterval(const double Time, const double Duration)
{
	const double NewScaleX = FMath::Clamp(static_cast<double>(Width) / Duration, MinScaleX, MaxScaleX);
	double NewStartTime = Time;
	const double NewViewportDuration = static_cast<double>(Width) / NewScaleX;
	if (Duration < NewViewportDuration)
	{
		NewStartTime -= (NewViewportDuration - Duration) / 2;
	}
	NewStartTime = AlignTimeToPixel(NewStartTime, NewScaleX);
	if (NewStartTime != StartTime || NewScaleX != ScaleX)
	{
		StartTime = NewStartTime;
		ScaleX = NewScaleX;
		EndTime = SlateUnitsToTime(Width);
		return true;
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FTimingTrackViewport::ZoomWithFixedX(const double NewScaleX, const float X)
{
	const double LocalNewScaleX = FMath::Clamp(NewScaleX, MinScaleX, MaxScaleX);
	if (LocalNewScaleX != ScaleX)
	{
		// Time at local position X should remain the same. So we resolve equation:
		//   StartTime + X / ScaleX == NewStartTime + X / NewScaleX
		//   ==> NewStartTime = StartTime + X / ScaleX - X / NewScaleX
		//   ==> NewStartTime = StartTime + X * (1 / ScaleX - 1 / NewScaleX)
		StartTime += static_cast<double>(X) * (1.0 / ScaleX - 1.0 / LocalNewScaleX);
		ScaleX = LocalNewScaleX;
		EndTime = SlateUnitsToTime(Width);
		return true;
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

double FTimingTrackViewport::RestrictEndTime(const double InEndTime) const
{
	if (InEndTime == DBL_MAX || InEndTime == std::numeric_limits<double>::infinity())
	{
		return MaxValidTime;
	}
	else
	{
		return InEndTime;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

double FTimingTrackViewport::RestrictDuration(const double InStartTime, const double InEndTime) const
{
	if (InEndTime == DBL_MAX || InEndTime == std::numeric_limits<double>::infinity())
	{
		return MaxValidTime - InStartTime;
	}
	else
	{
		return InEndTime - InStartTime;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingTrackViewport::GetHorizontalScrollLimits(double& OutMinT, double& OutMaxT)
{
	const double ViewportDuration = static_cast<double>(Width) / ScaleX;
	if (MaxValidTime < ViewportDuration)
	{
		OutMinT = MaxValidTime - ViewportDuration;
		OutMaxT = MinValidTime;
	}
	else
	{
		OutMinT = MinValidTime - 0.25 * ViewportDuration;
		OutMaxT = MaxValidTime - 0.75 * ViewportDuration;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FTimingTrackViewport::EnforceHorizontalScrollLimits(const double U)
{
	double MinT, MaxT;
	GetHorizontalScrollLimits(MinT, MaxT);

	double NewStartTime = StartTime;
	if (NewStartTime < MinT)
	{
		NewStartTime = (1.0 - U) * NewStartTime + U * MinT;
		if (FMath::IsNearlyEqual(NewStartTime, MinT, 1.0 / ScaleX))
			NewStartTime = MinT;
	}
	else if (NewStartTime > MaxT)
	{
		NewStartTime = (1.0 - U) * NewStartTime + U * MaxT;
		if (FMath::IsNearlyEqual(NewStartTime, MaxT, 1.0 / ScaleX))
			NewStartTime = MaxT;
		if (NewStartTime < MinT)
			NewStartTime = MinT;
	}

	return ScrollAtTime(NewStartTime);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
