// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AxisViewportDouble.h"

#include "Widgets/Layout/SScrollBar.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FAxisViewportDouble::SetSize(const float InSize)
{
	if (!FMath::IsNearlyEqual(Size, InSize, SLATE_UNITS_TOLERANCE))
	{
		Size = InSize;
		OnSizeChanged();
		return true;
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FAxisViewportDouble::SetScale(const double NewScale)
{
	const double LocalNewScale = FMath::Clamp(NewScale, MinScale, MaxScale);
	if (LocalNewScale != Scale)
	{
		Scale = LocalNewScale;
		OnScaleChanged();
		return true;
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FAxisViewportDouble::ZoomWithFixedOffset(const double NewScale, const float Offset)
{
	const double LocalNewScale = FMath::Clamp(NewScale, MinScale, MaxScale);
	if (LocalNewScale != Scale)
	{
		// Value at Offset should remain the same. So we resolve equation:
		//   (Position + Offset) / Scale == (NewPosition + Offset) / NewScale
		//   ==> NewPosition = (Position + Offset) / Scale * NewScale - Offset
		Position = (Position + Offset) * static_cast<float>(LocalNewScale / Scale) - Offset;
		Scale = LocalNewScale;

		OnPositionChanged();
		OnScaleChanged();
		return true;
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FAxisViewportDouble::RelativeZoomWithFixedOffset(const float Delta, const float Offset)
{
	constexpr double ZoomStep = 0.25f; // as percent

	double NewScale;

	if (Delta > 0)
	{
		NewScale = Scale * FMath::Pow(1.0f + ZoomStep, Delta);
	}
	else
	{
		NewScale = Scale * FMath::Pow(1.0f / (1.0f + ZoomStep), -Delta);
	}

	return ZoomWithFixedOffset(NewScale, Offset);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FAxisViewportDouble::UpdatePosWithinLimits()
{
	float MinPos, MaxPos;
	if (MaxPosition - MinPosition < Size)
	{
		MinPos = MaxPosition - Size;
		MaxPos = MinPosition;
	}
	else
	{
		constexpr float ExtraSizeFactor = 0.15f; // allow extra 15% on sides
		MinPos = MinPosition - ExtraSizeFactor * Size;
		MaxPos = MaxPosition - (1.0f - ExtraSizeFactor) * Size;
	}

	constexpr float U = 0.5f; // interpolation factor --> animation speed

	float Pos = Position;
	if (Pos < MinPos)
	{
		Pos = Pos * U + (1.0f - U) * MinPos;
		if (FMath::IsNearlyEqual(Pos, MinPos, 0.5f))
		{
			Pos = MinPos;
		}
	}
	else if (Pos > MaxPos)
	{
		Pos = Pos * U + (1.0f - U) * MaxPos;
		if (FMath::IsNearlyEqual(Pos, MaxPos, 0.5f))
		{
			Pos = MaxPos;
		}

		if (Pos < MinPos)
		{
			Pos = MinPos;
		}
	}

	if (Position != Pos)
	{
		ScrollAtPos(Pos);
		//ScrollAtValue(GetValueAtPos(Pos)); // snap to value
		return true;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAxisViewportDouble::OnUserScrolled(TSharedPtr<SScrollBar> ScrollBar, float ScrollOffset)
{
	const float SX = 1.0f / (MaxPosition - MinPosition);
	const float ThumbSizeFraction = FMath::Clamp<float>(Size * SX, 0.0f, 1.0f);
	const float OffsetFraction = FMath::Clamp<float>(ScrollOffset, 0.0f, 1.0f - ThumbSizeFraction);

	const float Pos = MinPosition + OffsetFraction * (MaxPosition - MinPosition);
	ScrollAtPos(Pos);

	ScrollBar->SetState(OffsetFraction, ThumbSizeFraction);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAxisViewportDouble::UpdateScrollBar(TSharedPtr<SScrollBar> ScrollBar) const
{
	const float SX = 1.0f / (MaxPosition - MinPosition);
	const float ThumbSizeFraction = FMath::Clamp<float>(Size * SX, 0.0f, 1.0f);
	const float ScrollOffset = (Position - MinPosition) * SX;
	const float OffsetFraction = FMath::Clamp<float>(ScrollOffset, 0.0f, 1.0f - ThumbSizeFraction);

	ScrollBar->SetState(OffsetFraction, ThumbSizeFraction);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
