// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FTimingTrackViewport
{
public:
	float Width; // width of viewport, in Slate units
	float Height; // height of viewport, in Slate units

	double MinValidTime; // min session time, in seconds
	double MaxValidTime; // max session time, in seconds
	double StartTime; // time of viewport's left side, in seconds
	double EndTime; // time of viewport's right side, in seconds; computed when StartTime or Width changes
	double MinScaleX;
	double MaxScaleX;
	double ScaleX; // scale factor between seconds and pixels (Slate units), in pixels (Slate units) per second

	float TopOffset; // top offset (to allow the time ruller to be visible), in pixels (Slate units)
	float ScrollHeight; // height of the vertical scrollable area, in pixels (Slate units)
	float ScrollPosY; // current vertical scroll position, in pixels (Slate units)

public:
	FTimingTrackViewport()
	{
		Reset();
	}

	void Reset()
	{
		Width = 0.0f;
		Height = 0.0f;

		MinValidTime = 0.0;
		MaxValidTime = 0.0;
		StartTime = 0.0;
		EndTime = 0.0;
		MinScaleX = (5 * 20) / 3600.0; // 1h between major tick marks
		MaxScaleX = 1.0E10; // 10ns between major tick marks
		ScaleX = (5 * 20) / 5.0; // 5s between major tick marks

		TopOffset = 0.0f;
		ScrollHeight = 1.0f;
		ScrollPosY = 0.0f;
	}

	float TimeToSlateUnits(const double Time) const
	{
		return static_cast<float>((Time - StartTime) * ScaleX);
	}

	float TimeToSlateUnitsRounded(const double Time) const
	{
		return static_cast<float>(FMath::RoundToDouble((Time - StartTime) * ScaleX));
	}

	float GetViewportDXForDuration(const double DT) const
	{
		return static_cast<float>(DT * ScaleX);
	}

	double GetDurationForViewportDX(const float DX) const
	{
		return static_cast<double>(DX) / ScaleX;
	}

	double SlateUnitsToTime(const float X) const
	{
		return StartTime + static_cast<double>(X) / ScaleX;
	}

	bool UpdateSize(const float InWidth, const float InHeight)
	{
		if (Width != InWidth || Height != InHeight)
		{
			Width = InWidth;
			Height = InHeight;
			EndTime = SlateUnitsToTime(Width);
			return true;
		}
		return false;
	}

	double AlignTimeToPixel(const double InTime, const double InScaleX) const
	{
		return FMath::RoundToDouble(InTime * InScaleX) / InScaleX;
	}

	double AlignTimeToPixel(const double Time) const
	{
		return AlignTimeToPixel(Time, ScaleX);
	}

	bool ScrollAtTime(const double Time);
	bool CenterOnTimeInterval(const double Time, const double Duration);

	bool ZoomOnTimeInterval(const double Time, const double Duration);
	bool ZoomWithFixedX(const double NewScaleX, const float X);

	double RestrictEndTime(const double InEndTime) const;
	double RestrictDuration(const double InStartTime, const double InEndTime) const;

	void GetHorizontalScrollLimits(double& OutMinT, double& OutMaxT);
	bool EnforceHorizontalScrollLimits(const double U);

	float GetViewportY(const float Y) const { return TopOffset + Y - ScrollPosY; }
};
