// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class SScrollBar;

////////////////////////////////////////////////////////////////////////////////////////////////////

class FIndexAxisViewport
{
private:
	static constexpr float SLATE_UNITS_TOLERANCE = 0.1f;

public:
	FIndexAxisViewport()
	{
		Reset();
	}

	void Reset()
	{
		Size = 0.0f;

		MinIndex = 0;
		MaxIndex = 0;

		MinPosition = 0.0f;
		MaxPosition = 0.0f;
		Position = 0.0f;

		MinScale = 0.0001f; // 10000 indices / sample; 1 pixel / sample
		MaxScale = 16.0f; // 1 index / sample; 16 pixels / sample
		Scale = MaxScale;
	}

	FString ToDebugString(const TCHAR* Prefix, const TCHAR* Unit) const
	{
		if (Scale >= 1.0f)
		{
			return FString::Printf(TEXT("%sScale = %.3f (%d pixels/%s), Pos = %.2f"),
				Prefix, Scale, FMath::RoundToInt(Scale), Unit, Position);
		}
		else
		{
			return FString::Printf(TEXT("%sScale = %.3f (%d %ss/pixel), Pos = %.2f"),
				Prefix, Scale, FMath::RoundToInt(1.0f / Scale), Unit, Position);
		}
	}

	float GetSize() const { return Size; }

	bool SetSize(const float InSize)
	{
		if (!FMath::IsNearlyEqual(Size, InSize, SLATE_UNITS_TOLERANCE))
		{
			Size = InSize;
			OnSizeChanged();
			return true;
		}
		return false;
	}

	int32 GetMinIndex() const { return MinIndex; }
	int32 GetMaxIndex() const { return MaxIndex; }

	float GetMinPos() const { return MinPosition; }
	float GetMaxPos() const { return MaxPosition; }
	float GetPos() const { return Position; }

	float GetMinScale() const { return MinScale; }
	float GetMaxScale() const { return MaxScale; }
	float GetScale() const { return Scale; }

	float GetSampleSize() const { return FMath::Max(1.0f, FMath::RoundToFloat(Scale)); }
	int32 GetNumIndicesPerSample() const { return FMath::Max(1, FMath::RoundToInt(1.0f / Scale)); }

	void SetMinMaxIndexInterval(const int32 InMinIndex, const int32 InMaxIndex)
	{
		MinIndex = InMinIndex;
		MaxIndex = InMaxIndex;
		UpdateMinMax();
	}

	int32 GetIndexAtPos(const float Pos) const
	{
		return FMath::RoundToInt(Pos / Scale);
	}

	int32 GetIndexAtOffset(const float Offset) const
	{
		return FMath::RoundToInt((Position + Offset) / Scale);
	}

	float GetPosForIndex(const int32 Index) const
	{
		return static_cast<float>(Index) * Scale;
	}

	float GetOffsetForIndex(const int32 Index) const
	{
		return static_cast<float>(Index) * Scale - Position;
	}

	float GetRoundedOffsetForIndex(const int32 Index) const
	{
		return FMath::RoundToFloat(static_cast<float>(Index) * Scale - Position);
	}

	void ScrollAtPos(const float Pos)
	{
		Position = Pos;
		OnPositionChanged();
	}

	void ScrollAtIndex(const int32 Index)
	{
		Position = static_cast<float>(Index) * Scale;
		OnPositionChanged();
	}

	void CenterOnIndex(const int32 Index)
	{
		const float SampleSize = FMath::Max(1.0f, Scale);
		if (SampleSize > Size)
		{
			ScrollAtIndex(Index);
		}
		else
		{
			const float Offset = (Size - SampleSize) / 2.0f;
			Position = static_cast<float>(Index) * Scale - Offset;
			OnPositionChanged();
		}
	}

	void CenterOnIndexInterval(const int32 IntervalStartIndex, const int32 IntervalEndIndex)
	{
		const float SampleIntervalSize = FMath::Max(1.0f, Scale * static_cast<float>(IntervalEndIndex - IntervalStartIndex));
		if (SampleIntervalSize > Size)
		{
			ScrollAtIndex(IntervalStartIndex);
		}
		else
		{
			const float Offset = (Size - SampleIntervalSize) / 2.0f;
			Position = static_cast<float>(IntervalStartIndex) * Scale - Offset;
			OnPositionChanged();
		}
	}

	void ZoomWithFixedOffset(const float NewScale, const float Offset)
	{
		const float LocalNewScale = FMath::Clamp(NewScale, MinScale, MaxScale);
		if (LocalNewScale != Scale)
		{
			// Index at Offset should remain the same. So we resolve equation:
			//   (Position + Offset) / Scale == (NewPosition + Offset) / NewScale
			//   ==> NewPosition = (Position + Offset) / Scale * NewScale - Offset
			Position = (Position + Offset) * (LocalNewScale / Scale) - Offset;
			Scale = LocalNewScale;

			OnPositionChanged();
			OnScaleChanged();
		}
	}

	void RelativeZoomWithFixedOffset(const float Delta, const float Offset);

	bool UpdatePosWithinLimits();

	void OnUserScrolled(TSharedPtr<SScrollBar> ScrollBar, float ScrollOffset);
	void UpdateScrollBar(TSharedPtr<SScrollBar> ScrollBar) const;

private:
	void OnSizeChanged()
	{
	}

	void OnPositionChanged()
	{
	}

	void OnScaleChanged()
	{
		UpdateMinMax();
	}

	void UpdateMinMax()
	{
		MinPosition = static_cast<float>(MinIndex) * Scale;
		MaxPosition = static_cast<float>(MaxIndex) * Scale;

		//float NewPosition = FMath::Clamp(Position, MinPosition, MaxPosition);
		//if (FMath::IsNearlyEqual(NewPosition, Position, SLATE_UNITS_TOLERANCE))
		//{
		//	Position = NewPosition;
		//	OnPositionChanged();
		//}
	}

private:
	float Size; // size of viewport (ex.: the viewport's width if this is a horizontal axis), in Slate units

	int32 MinIndex; // minimum index
	int32 MaxIndex; // maximum index (exclusive)

	float MinPosition; // minimum position (corresponding to MinIndex), in Slate units
	float MaxPosition; // maximum position (corresponding to MaxIndex), in Slate units
	float Position; // current position of the viewport, in Slate units

	float MinScale; // minimum scale factor
	float MaxScale; // maximum scale factor
	float Scale; // current scale factor between viewport index units and Slate units: if > 1, it represents number of pixels (Slate units) for one viewport index, otherwise it represents number of viewport indices in one pixel (Slate unit)
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FValueAxisViewport
{
private:
	static constexpr float SLATE_UNITS_TOLERANCE = 0.1f;

public:
	FValueAxisViewport()
	{
		Reset();
	}

	void Reset()
	{
		Size = 0.0f;

		MinValue = 0.0;
		MaxValue = 0.0;

		MinPosition = 0.0f;
		MaxPosition = 0.0f;
		Position = 0.0f;

		MinScale = 0.001;
		MaxScale = 1000000.0;
		Scale = 1500.0;
	}

	FString ToDebugString(const TCHAR* Prefix) const
	{
		return FString::Printf(TEXT("%sScale = %g, Pos = %.2f"), Prefix, Scale, Position);
	}

	float GetSize() const { return Size; }

	bool SetSize(const float InSize)
	{
		if (!FMath::IsNearlyEqual(Size, InSize, SLATE_UNITS_TOLERANCE))
		{
			Size = InSize;
			OnSizeChanged();
			return true;
		}
		return false;
	}

	float GetMinPos() const { return MinPosition; }
	float GetMaxPos() const { return MaxPosition; }
	float GetPos() const { return Position; }

	double GetMinScale() const { return MinScale; }
	double GetMaxScale() const { return MaxScale; }
	double GetScale() const { return Scale; }

	float GetPosForValue(const double Value) const
	{
		return static_cast<float>(Value * Scale);
	}

	float GetOffsetForValue(const double Value) const
	{
		return static_cast<float>(Value * Scale) - Position;
	}

	double GetValueForOffset(const float Offset) const
	{
		return static_cast<double>(Position + Offset) / Scale;
	}

	void SetScaleLimits(const double InMinScale, const double InMaxScale)
	{
		MinScale = InMinScale;
		MaxScale = InMaxScale;
	}

	bool SetScale(const double InNewScale)
	{
		float LocalNewScale = FMath::Clamp(InNewScale, MinScale, MaxScale);
		if (LocalNewScale != Scale)
		{
			Scale = LocalNewScale;
			OnScaleChanged();
			return true;
		}
		return false;
	}

private:
	void OnSizeChanged()
	{
	}

	void OnPositionChanged()
	{
	}

	void OnScaleChanged()
	{
		//UpdateMinMaxX();
	}

private:
	float Size; // size of viewport (ex.: the viewport's height if this is a vertical axis), in Slate units

	double MinValue;
	double MaxValue;

	float MinPosition; // minimum scroll position (i.e. position corresponding to MinValue), in Slate units
	float MaxPosition; // maximum scroll position (i.e. position corresponding to MaxValue), in Slate units
	float Position; // current position of the viewport, in Slate units

	double MinScale; // minimum scale factor
	double MaxScale; // maximum scale factor
	double Scale; // scale factor between Value units and Slate units
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FFrameTrackViewport
{
private:
	static constexpr float SLATE_UNITS_TOLERANCE = 0.1f;

public:
	FFrameTrackViewport()
	{
		Reset();
	}

	void Reset()
	{
		HorizontalAxisViewport.Reset();
		VerticalAxisViewport.Reset();
	}

	const FIndexAxisViewport& GetHorizontalAxisViewport() const { return HorizontalAxisViewport; }
	FIndexAxisViewport& GetHorizontalAxisViewport() { return HorizontalAxisViewport; }

	const FValueAxisViewport& GetVerticalAxisViewport() const { return VerticalAxisViewport; }
	FValueAxisViewport& GetVerticalAxisViewport() { return VerticalAxisViewport; }

	float GetWidth() const { return HorizontalAxisViewport.GetSize(); }
	float GetHeight() const { return VerticalAxisViewport.GetSize(); }

	bool SetSize(const float InWidth, const float InHeight)
	{
		const bool bWidthChanged = HorizontalAxisViewport.SetSize(InWidth);
		const bool bHeightChanged = VerticalAxisViewport.SetSize(InHeight);
		if (bWidthChanged || bHeightChanged)
		{
			OnSizeChanged();
			return true;
		}
		return false;
	}

	float GetSampleWidth() const { return HorizontalAxisViewport.GetSampleSize(); }
	int32 GetNumFramesPerSample() const { return HorizontalAxisViewport.GetNumIndicesPerSample(); }
	int32 GetFirstFrameIndex() const { return HorizontalAxisViewport.GetIndexAtOffset(0.0f); }

private:
	void OnSizeChanged()
	{
	}

private:
	FIndexAxisViewport HorizontalAxisViewport;
	FValueAxisViewport VerticalAxisViewport;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
