// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AxisViewportInt32.h"

#include "Widgets/Layout/SScrollBar.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FAxisViewportInt32::SetSize(const float InSize)
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

bool FAxisViewportInt32::SetScale(const float InNewScale)
{
	const float LocalNewScale = FMath::Clamp(InNewScale, MinScale, MaxScale);
	if (LocalNewScale != Scale)
	{
		Scale = LocalNewScale;
		OnScaleChanged();
		return true;
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FAxisViewportInt32::ZoomWithFixedOffset(const float NewScale, const float Offset)
{
	const float LocalNewScale = FMath::Clamp(NewScale, MinScale, MaxScale);
	if (LocalNewScale != Scale)
	{
		// Value at Offset should remain the same. So we resolve equation:
		//   (Position + Offset) / Scale == (NewPosition + Offset) / NewScale
		//   ==> NewPosition = (Position + Offset) / Scale * NewScale - Offset
		Position = (Position + Offset) * (LocalNewScale / Scale) - Offset;
		Scale = LocalNewScale;

		OnPositionChanged();
		OnScaleChanged();
		return true;
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FAxisViewportInt32::RelativeZoomWithFixedOffset(const float Delta, const float Offset)
{
	constexpr float ZoomStep = 0.25f; // as percent

	float NewScale;

	if (Delta > 0)
	{
		NewScale = Scale * FMath::Pow(1.0f + ZoomStep, Delta);
	}
	else
	{
		NewScale = Scale * FMath::Pow(1.0f / (1.0f + ZoomStep), -Delta);
	}

	// Snap to integer value of either: "number of samples per pixel" or "number of pixels per sample".
	if (NewScale < 1.0f)
	{
		// N sample/px; 1 px/sample
		if (Delta > 0)
		{
			NewScale = 1.0f / FMath::FloorToFloat(1.0f / NewScale);
		}
		else
		{
			NewScale = 1.0f / FMath::CeilToFloat(1.0f / NewScale);
		}
	}
	else
	{
		// 1 sample/px; N px/sample
		if (Delta > 0)
		{
			NewScale = FMath::CeilToFloat(NewScale);
		}
		else
		{
			NewScale = FMath::FloorToFloat(NewScale);
		}
	}

	return ZoomWithFixedOffset(NewScale, Offset);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FAxisViewportInt32::UpdatePosWithinLimits()
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
		//ScrollAtPos(Pos);
		ScrollAtValue(GetValueAtPos(Pos)); // snap to sample
		return true;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAxisViewportInt32::OnUserScrolled(TSharedPtr<SScrollBar> ScrollBar, float ScrollOffset)
{
	const float SX = 1.0f / (MaxPosition - MinPosition);
	const float ThumbSizeFraction = FMath::Clamp<float>(Size * SX, 0.0f, 1.0f);
	const float OffsetFraction = FMath::Clamp<float>(ScrollOffset, 0.0f, 1.0f - ThumbSizeFraction);

	const float Pos = MinPosition + OffsetFraction * (MaxPosition - MinPosition);
	ScrollAtPos(Pos);

	ScrollBar->SetState(OffsetFraction, ThumbSizeFraction);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAxisViewportInt32::UpdateScrollBar(TSharedPtr<SScrollBar> ScrollBar) const
{
	const float SX = 1.0f / (MaxPosition - MinPosition);
	const float ThumbSizeFraction = FMath::Clamp<float>(Size * SX, 0.0f, 1.0f);
	const float ScrollOffset = (Position - MinPosition) * SX;
	const float OffsetFraction = FMath::Clamp<float>(ScrollOffset, 0.0f, 1.0f - ThumbSizeFraction);

	ScrollBar->SetState(OffsetFraction, ThumbSizeFraction);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
