// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "FrameTrackViewport.h"

#include "Widgets/Layout/SScrollBar.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
// FIndexAxisViewport
////////////////////////////////////////////////////////////////////////////////////////////////////

void FIndexAxisViewport::RelativeZoomWithFixedOffset(const float Delta, const float Offset)
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

	// Snap to integer value of either: "number of indices per sample" or "number of pixels per sample".
	if (NewScale < 1.0f)
	{
		// N indices/sample; 1 pixel/sample
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
		// 1 index/sample; N pixels/sample
		if (Delta > 0)
		{
			NewScale = FMath::CeilToFloat(NewScale);
		}
		else
		{
			NewScale = FMath::FloorToFloat(NewScale);
		}
	}

	ZoomWithFixedOffset(NewScale, Offset);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FIndexAxisViewport::UpdatePosWithinLimits()
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
		ScrollAtIndex(GetIndexAtPos(Pos)); // snap to sample
		return true;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FIndexAxisViewport::OnUserScrolled(TSharedPtr<SScrollBar> ScrollBar, float ScrollOffset)
{
	const float SX = 1.0 / (MaxPosition - MinPosition);
	const float ThumbSizeFraction = FMath::Clamp<float>(Size * SX, 0.0f, 1.0f);
	const float OffsetFraction = FMath::Clamp<float>(ScrollOffset, 0.0f, 1.0f - ThumbSizeFraction);

	const float Pos = MinPosition + OffsetFraction * (MaxPosition - MinPosition);
	ScrollAtPos(Pos);

	ScrollBar->SetState(OffsetFraction, ThumbSizeFraction);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FIndexAxisViewport::UpdateScrollBar(TSharedPtr<SScrollBar> ScrollBar) const
{
	const float SX = 1.0 / (MaxPosition - MinPosition);
	const float ThumbSizeFraction = FMath::Clamp<float>(Size * SX, 0.0f, 1.0f);
	const float ScrollOffset = (Position - MinPosition) * SX;
	const float OffsetFraction = FMath::Clamp<float>(ScrollOffset, 0.0f, 1.0f - ThumbSizeFraction);

	ScrollBar->SetState(OffsetFraction, ThumbSizeFraction);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
