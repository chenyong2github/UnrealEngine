// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FFrameTrackViewport
{
private:
	const float SLATE_UNITS_TOLERANCE = 0.1f;

public:
	FFrameTrackViewport()
	{
		Reset();
	}

	void Reset()
	{
		Width = 0.0f;
		Height = 0.0f;

		MinIndex = 0;
		MaxIndex = 0;

		MinX = 0.0f;
		MaxX = 0.0f;
		PosX = 0.0f;
		MinScaleX = 0.0001f;
		MaxScaleX = 16.0f;
		ScaleX = MaxScaleX;

		MinValue = 0.0;
		MaxValue = 1.0f;

		MinY = 0.0f;
		MaxY = 1.0f;
		PosY = 0.0f;
		MinScaleY = 0.001f;
		MaxScaleY = 1000000.0f;
		ScaleY = 1000.0f;

		LastOnPaintMaxValue = 0.0;
	}

	void SetSize(const float InWidth, const float InHeight)
	{
		if (!FMath::IsNearlyEqual(Width, InWidth, SLATE_UNITS_TOLERANCE) ||
			!FMath::IsNearlyEqual(Height, InHeight, SLATE_UNITS_TOLERANCE))
		{
			Width = InWidth;
			Height = InHeight;
			OnSizeChanged();
		}
	}

	void SetMinMaxIndexInterval(const int32 InMinIndex, const int32 InMaxIndex)
	{
		MinIndex = InMinIndex;
		MaxIndex = InMaxIndex;
		UpdateMinMaxX();
	}

	int32 GetIndexAtPosX(const float X) const
	{
		return FMath::RoundToInt(X / ScaleX);
	}

	int32 GetIndexAtViewportX(const float VX) const
	{
		return FMath::RoundToInt((PosX + VX) / ScaleX);
	}

	float GetPosXForIndex(const int32 Index) const
	{
		return static_cast<float>(Index) * ScaleX;
	}

	float GetViewportXForIndex(const int32 Index) const
	{
		return static_cast<float>(Index) * ScaleX - PosX;
	}

	float GetRoundedViewportXForIndex(const int32 Index) const
	{
		return FMath::RoundToFloat(static_cast<float>(Index) * ScaleX - PosX);
	}

	void ScrollAtPosX(const float InPosX)
	{
		PosX = InPosX;
		OnPosXChanged();
	}

	void ScrollAtIndex(const int32 Index)
	{
		PosX = static_cast<float>(Index) * ScaleX;
		OnPosXChanged();
	}

	void CenterOnIndex(const int32 Index)
	{
		float W = FMath::Max(1.0f, ScaleX);
		if (W > Width)
		{
			ScrollAtIndex(Index);
		}
		else
		{
			float VX = (Width - W) / 2.0f;
			PosX = static_cast<float>(Index) * ScaleX - VX;
			OnPosXChanged();
		}
	}

	void CenterOnIndexInterval(const int32 IntervalStartIndex, const int32 IntervalEndIndex)
	{
		float W = FMath::Max(1.0f, ScaleX * static_cast<float>(IntervalEndIndex - IntervalStartIndex));
		if (W > Width)
		{
			ScrollAtIndex(IntervalStartIndex);
		}
		else
		{
			float VX = (Width - W) / 2.0f;
			PosX = static_cast<float>(IntervalStartIndex) * ScaleX - VX;
			OnPosXChanged();
		}
	}

	void ZoomWithFixedViewportX(const float InNewScaleX, const float VX)
	{
		float LocalNewScaleX = FMath::Clamp(InNewScaleX, MinScaleX, MaxScaleX);
		if (LocalNewScaleX != ScaleX)
		{
			// Index at viewport X should remain the same. So we resolve equation:
			//   (PosX + VX) / ScaleX = (NewPosX + VX) / NewScaleX
			//   ==> NewPosX = (PosX + VX) / ScaleX * NewScaleX - VX
			PosX = (PosX + VX) * (LocalNewScaleX / ScaleX) - VX;
			ScaleX = LocalNewScaleX;

			OnPosXChanged();
			OnScaleXChanged();
		}
	}

	float GetSampleWidth() const { return FMath::Max(1.0f, FMath::RoundToFloat(ScaleX)); }
	int32 GetNumFramesPerSample() const { return FMath::Max(1, FMath::RoundToInt(1.0f / ScaleX)); }

	int32 GetFirstFrameIndex() const { return GetIndexAtViewportX(0.0f); }

	float GetPosYForValue(const double Value) const
	{
		return static_cast<float>(Value) * ScaleY;
	}

	float GetViewportYForValue(const double Value) const
	{
		return static_cast<float>(Value) * ScaleY - PosY;
	}

	void SetScaleY(const float InNewScaleY)
	{
		float LocalNewScaleY = FMath::Clamp(InNewScaleY, MinScaleY, MaxScaleY);
		if (LocalNewScaleY != ScaleY)
		{
			ScaleY = LocalNewScaleY;
			OnScaleYChanged();
		}
	}

private:
	void OnSizeChanged()
	{
	}

	void OnPosXChanged()
	{
	}

	void OnScaleXChanged()
	{
		UpdateMinMaxX();
	}

	void OnScaleYChanged()
	{
	}

	void UpdateMinMaxX()
	{
		MinX = static_cast<float>(MinIndex) * ScaleX;
		MaxX = static_cast<float>(MaxIndex) * ScaleX;

		//float NewPosX = FMath::Clamp(PosX, MinX, MaxX);
		//if (FMath::IsNearlyEqual(NewPosX, PosX, SLATE_UNITS_TOLERANCE))
		//{
		//	PosX = NewPosX;
		//	OnPosXChanged();
		//}
	}

public:
	float Width; // width of viewport, in Slate units
	float Height; // height of viewport, in Slate units

	int32 MinIndex; // minimum index
	int32 MaxIndex; // maximum index (exclusive)

	float MinX; // minimum horizontal position, in Slate units
	float MaxX; // maximum horizontal position, in Slate units
	float PosX;
	float MinScaleX; // minimum horizontal scale factor
	float MaxScaleX; // maximum horizontal scale factor
	float ScaleX; // scale factor between frame index and Slate units: if > 1, it represents number of Slate units for one frame index, otherwise it represents number of frames in one Slate unit

	double MinValue;
	double MaxValue;

	float MinY; // minimum vertical scroll position, in Slate units
	float MaxY; // maximum vertical scroll position, in Slate units
	float PosY; // current vertical scroll position, in Slate units; origin at bottom
	float MinScaleY; // minimum vertical scale factor
	float MaxScaleY; // maximum vertical scale factor
	float ScaleY; // scale factor between Value units and Slate units

	mutable double LastOnPaintMaxValue;
};
