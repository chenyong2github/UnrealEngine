// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TimeRulerTrack.h"

#include "Fonts/FontMeasure.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

// Insights
#include "Insights/Common/PaintUtils.h"
#include "Insights/Common/TimeUtils.h"
#include "Insights/InsightsStyle.h"
#include "Insights/ViewModels/DrawHelpers.h"
#include "Insights/ViewModels/TimingTrackViewport.h"

#define LOCTEXT_NAMESPACE "TimeRulerTrack"

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimeRulerTrack::FTimeRulerTrack(uint64 InTrackId)
	: FBaseTimingTrack(InTrackId)
	, WhiteBrush(FInsightsStyle::Get().GetBrush("WhiteBrush"))
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
	FDrawHelpers::DrawBackground(DrawContext, WhiteBrush, Viewport, GetPosY(), GetHeight());
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

	double VX = Viewport.GetStartTime() * Viewport.GetScaleX();
	double MinorN = FMath::FloorToDouble(VX / static_cast<double>(MinorTickMark));
	double MajorN = FMath::FloorToDouble(VX / static_cast<double>(MajorTickMark));
	float MinorOX = static_cast<float>(FMath::RoundToDouble(MinorN * static_cast<double>(MinorTickMark) - VX));
	float MajorOX = static_cast<float>(FMath::RoundToDouble(MajorN * static_cast<double>(MajorTickMark) - VX));

	// Draw the time ruler's background.
	DrawBackground(DrawContext, Viewport);

	// Draw the minor tick marks.
	for (float X = MinorOX; X < Viewport.GetWidth(); X += MinorTickMark)
	{
		const bool bIsTenth = ((int32)(((X - MajorOX) / MinorTickMark) + 0.4f) % 2 == 0);
		const float MinorTickH = bIsTenth ? MinorTickMarkHeight : MinorTickMarkHeight - 1.0f;
		DrawContext.DrawBox(X, GetPosY(), 1.0f, MinorTickH, WhiteBrush,
			bIsTenth ? FLinearColor(0.3f, 0.3f, 0.3f, 1.0f) : FLinearColor(0.25f, 0.25f, 0.25f, 1.0f));
	}
	// Draw the major tick marks.
	for (float X = MajorOX; X < Viewport.GetWidth(); X += MajorTickMark)
	{
		DrawContext.DrawBox(X, GetPosY(), 1.0f, MajorTickMarkHeight, WhiteBrush, FLinearColor(0.4f, 0.4f, 0.4f, 1.0f));
	}
	DrawContext.LayerId++;

	const double DT = static_cast<double>(MajorTickMark) / Viewport.GetScaleX();
	const double Precision = FMath::Max(DT / 10.0, TimeUtils::Nanosecond);

	const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	// Draw the time at major tick marks.
	for (float X = MajorOX; X < Viewport.GetWidth() + MajorTickMark; X += MajorTickMark)
	{
		const double T = Viewport.SlateUnitsToTime(X);
		FString Text = TimeUtils::FormatTime(T, Precision);
		const float TextWidth = FontMeasureService->Measure(Text, Font).X;
		DrawContext.DrawText(X - TextWidth / 2, TextY, Text, Font,
			(T < Viewport.GetMinValidTime() || T >= Viewport.GetMaxValidTime()) ? FLinearColor(0.7f, 0.5f, 0.5f, 1.0f) : FLinearColor(0.8f, 0.8f, 0.8f, 1.0f));
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
			// While selecting, display the current time on either left or right side of the selected time range (i.e. to not overlap the selection arrows).
			float SelectionX1 = Viewport.TimeToSlateUnitsRounded(SelectionStartTime);
			float SelectionX2 = Viewport.TimeToSlateUnitsRounded(SelectionEndTime);
			if (FMath::Abs(X - SelectionX1) > FMath::Abs(SelectionX2 - X))
			{
				X = SelectionX2 + W / 2;
			}
			else
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
			DrawContext.DrawBox(MousePosition.X, 0.0f, 1.0f, Viewport.GetHeight(), WhiteBrush, MousePosLineColor);

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
