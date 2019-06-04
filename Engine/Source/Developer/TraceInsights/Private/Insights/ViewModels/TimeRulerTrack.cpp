// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TimeRulerTrack.h"

#include "Brushes/SlateColorBrush.h"
#include "Fonts/FontMeasure.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"

// Insights
#include "Insights/Common/PaintUtils.h"
#include "Insights/Common/TimeUtils.h"
#include "Insights/ViewModels/TimingTrackViewport.h"

#define LOCTEXT_NAMESPACE "TimeRulerTrack"

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimeRulerTrack::FTimeRulerTrack(uint64 InTrackId)
	: FBaseTimingTrack(InTrackId)
	, WhiteBrush(FCoreStyle::Get().GetBrush("WhiteBrush"))
	, Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimeRulerTrack::~FTimeRulerTrack()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimeRulerTrack::Reset()
{
	FBaseTimingTrack::Reset();

	constexpr float TimeRulerHeight = 22.0f;
	SetHeight(TimeRulerHeight);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimeRulerTrack::UpdateHoveredState(float MouseX, float MouseY, const FTimingTrackViewport& Viewport)
{
	//constexpr float HeaderWidth = 100.0f;
	//constexpr float HeaderHeight = 14.0f;

	if (MouseY >= GetPosY() && MouseY < GetPosY() + GetHeight())
	{
		SetHoveredState(true);
		//SetHeaderHoveredState(MouseX < HeaderWidth && MouseY < GetPosY() + HeaderHeight);
	}
	else
	{
		SetHoveredState(false);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimeRulerTrack::DrawBackground(FDrawContext& DrawContext, const FTimingTrackViewport& Viewport) const
{
	const float X0 = Viewport.TimeToSlateUnitsRounded(0.0);
	const float X1 = Viewport.TimeToSlateUnitsRounded(Viewport.MaxValidTime);
	const float W = FMath::CeilToFloat(Viewport.Width);
	const float H = GetHeight();

	const FLinearColor InvalidAreaColor(0.08f, 0.07f, 0.07f, 1.0f);
	const FLinearColor ValidAreaColor(0.09f, 0.09f, 0.09f, 1.0f);

	if (X0 >= W || X1 <= 0.0f)
	{
		// Draw invalid area (entire view).
		DrawContext.DrawBox(0.0f, 0.0f, W, H, WhiteBrush, InvalidAreaColor);
	}
	else // X0 < W && X1 > 0
	{
		if (X0 > 0.0f)
		{
			// Draw invalid area (left).
			DrawContext.DrawBox(0.0f, 0.0f, X0, H, WhiteBrush, InvalidAreaColor);
		}

		if (X1 < W)
		{
			// Draw invalid area (right).
			DrawContext.DrawBox(X1, 0.0f, W - X1, H, WhiteBrush, InvalidAreaColor);
		}

		float ValidX0 = FMath::Max(X0, 0.0f);
		float ValidX1 = FMath::Min(X1, W);

		if (ValidX1 > ValidX0)
		{
			// Draw valid area.
			DrawContext.DrawBox(ValidX0, 0.0f, ValidX1 - ValidX0, H, WhiteBrush, ValidAreaColor);
		}
	}

	DrawContext.LayerId++;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimeRulerTrack::Draw(FDrawContext& DrawContext, const FTimingTrackViewport& Viewport, const FVector2D& MousePosition, const bool bIsSelecting, const double SelectionStartTime, const double SelectionEndTime) const
{
	const float MinorTickMark = 5.0f;
	const float MajorTickMark = 20 * MinorTickMark;

	const float MinorTickMarkHeight = 5.0f;
	const float MajorTickMarkHeight = 9.0f;

	const float TextY = GetPosY() + MajorTickMarkHeight;

	double MinorTickMarkTime = Viewport.GetDurationForViewportDX(MinorTickMark);
	double MajorTickMarkTime = Viewport.GetDurationForViewportDX(MajorTickMark);

	double VX = Viewport.StartTime * Viewport.ScaleX;
	double MinorN = FMath::FloorToDouble(VX / static_cast<double>(MinorTickMark));
	double MajorN = FMath::FloorToDouble(VX / static_cast<double>(MajorTickMark));
	float MinorOX = static_cast<float>(FMath::RoundToDouble(MinorN * static_cast<double>(MinorTickMark) - VX));
	float MajorOX = static_cast<float>(FMath::RoundToDouble(MajorN * static_cast<double>(MajorTickMark) - VX));

	// Draw the time ruler's background.
	DrawBackground(DrawContext, Viewport);

	// Draw the minor tick marks.
	for (float X = MinorOX; X < Viewport.Width; X += MinorTickMark)
	{
		const bool bIsTenth = ((int32)(((X - MajorOX) / MinorTickMark) + 0.4f) % 2 == 0);
		const float MinorTickH = bIsTenth ? MinorTickMarkHeight : MinorTickMarkHeight - 1.0f;
		DrawContext.DrawBox(X, GetPosY(), 1.0f, MinorTickH, WhiteBrush,
			bIsTenth ? FLinearColor(0.3f, 0.3f, 0.3f, 1.0f) : FLinearColor(0.25f, 0.25f, 0.25f, 1.0f));
	}
	// Draw the major tick marks.
	for (float X = MajorOX; X < Viewport.Width; X += MajorTickMark)
	{
		DrawContext.DrawBox(X, GetPosY(), 1.0f, MajorTickMarkHeight, WhiteBrush, FLinearColor(0.4f, 0.4f, 0.4f, 1.0f));
	}
	DrawContext.LayerId++;

	const double DT = static_cast<double>(MajorTickMark) / Viewport.ScaleX;
	const double Precision = FMath::Max(DT / 10.0, TimeUtils::Nanosecond);

	const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	// Draw the time at major tick marks.
	for (float X = MajorOX; X < Viewport.Width + MajorTickMark; X += MajorTickMark)
	{
		const double T = Viewport.SlateUnitsToTime(X);
		FString Text = TimeUtils::FormatTime(T, Precision);
		const float TextWidth = FontMeasureService->Measure(Text, Font).X;
		DrawContext.DrawText(X - TextWidth / 2, TextY, Text, Font,
			(T < 0 || T >= Viewport.MaxValidTime) ? FLinearColor(0.7f, 0.5f, 0.5f, 1.0f) : FLinearColor(0.8f, 0.8f, 0.8f, 1.0f));
	}
	DrawContext.LayerId++;

	bool bShowMousePos = !MousePosition.IsZero();
	if (bShowMousePos)
	{
		const FLinearColor MousePosLineColor(0.9f, 0.9f, 0.9f, 0.1f);
		const FLinearColor MousePosTextBackgroundColor(0.9f, 0.9f, 0.9f, 1.0f);
		FLinearColor MousePosTextForegroundColor(0.1f, 0.1f, 0.1f, 1.0f);

		// Time at current mouse position.
		FString MousePosText;

		const double MousePosTime = Viewport.SlateUnitsToTime(MousePosition.X);
		const double MousePosPrecision = FMath::Max(DT / 100.0, TimeUtils::Nanosecond);
		if (MousePosition.Y >= GetPosY() && MousePosition.Y < GetPosY() + GetHeight())
		{
			// If mouse is hovering the time ruller, format time with a better precision (split seconds in ms, us, ns and ps).
			MousePosText = TimeUtils::FormatTimeSplit(MousePosTime, MousePosPrecision);
		}
		else
		{
			// Format current time with one more digit than the time at major tick marks.
			MousePosText = TimeUtils::FormatTime(MousePosTime, MousePosPrecision);
		}

		const float MousePosTextWidth = FMath::RoundToFloat(FontMeasureService->Measure(MousePosText, Font).X);
		static float CrtMousePosTextWidth = 0.0f;

		if (!FMath::IsNearlyEqual(CrtMousePosTextWidth, MousePosTextWidth))
		{
			// Animate the box's width (to avoid flickering).
			CrtMousePosTextWidth = CrtMousePosTextWidth * 0.6f + MousePosTextWidth * 0.4f;
		}

		float X = MousePosition.X;
		float W = CrtMousePosTextWidth + 4.0f;
		if (bIsSelecting && SelectionStartTime < SelectionEndTime)
		{
			float SelectionX1 = Viewport.TimeToSlateUnitsRounded(SelectionStartTime);
			float SelectionX2 = Viewport.TimeToSlateUnitsRounded(SelectionEndTime);
			if (X - SelectionX1 > 1.0f)
			{
				X = SelectionX2 + W / 2;
			}
			if (SelectionX2 - X > 1.0f)
			{
				X = SelectionX1 - W / 2;
			}
			MousePosTextForegroundColor = FLinearColor(FColor(32, 64, 128, 255));
		}
		else
		{
			// Draw horizontal line at mouse position.
			//DrawContext.DrawBox(0.0f, MousePosition.Y, Viewport.Width, 1.0f, WhiteBrush, MousePosLineColor);

			// Draw vertical line at mouse position.
			DrawContext.DrawBox(MousePosition.X, 0.0f, 1.0f, Viewport.Height, WhiteBrush, MousePosLineColor);

			// Stroke the vertical line above current time box.
			DrawContext.DrawBox(MousePosition.X, 0.0f, 1.0f, TextY, WhiteBrush, MousePosTextBackgroundColor);
		}

		// Fill the current time box.
		DrawContext.DrawBox(X - W / 2, TextY, W, 12.0f, WhiteBrush, MousePosTextBackgroundColor);
		DrawContext.LayerId++;

		// Draw current time text.
		DrawContext.DrawText(X - MousePosTextWidth / 2, TextY, MousePosText, Font, MousePosTextForegroundColor);
		DrawContext.LayerId++;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
