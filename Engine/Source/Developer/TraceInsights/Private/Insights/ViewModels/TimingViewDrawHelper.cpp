// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TimingViewDrawHelper.h"

#include "Brushes/SlateBorderBrush.h"
#include "Brushes/SlateColorBrush.h"
#include "EditorStyleSet.h"
#include "Fonts/FontMeasure.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "TraceServices/AnalysisService.h"

// Insights
#include "Insights/Common/PaintUtils.h"
#include "Insights/Common/TimeUtils.h"
#include "Insights/ViewModels/MarkersTimingTrack.h"
#include "Insights/ViewModels/TimingEventsTrack.h"
#include "Insights/ViewModels/TimingTrackViewport.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimingViewDrawHelper::FTimingViewDrawHelper(const FDrawContext& InDC, const FTimingTrackViewport& InViewport, const FTimingEventsTrackLayout& InLayout)
	: DrawContext(InDC)
	, Viewport(InViewport)
	, Layout(InLayout)
	, WhiteBrush(FCoreStyle::Get().GetBrush("WhiteBrush"))
	, BorderBrush(FEditorStyle::GetBrush("PlainBorder"))
	, EventsBorderBrush(new FSlateBorderBrush(NAME_None, FMargin(1.0f)))
	, BackgroundAreaBrush(WhiteBrush)
	, ValidAreaColor(0.07f, 0.07f, 0.07f, 1.0f)
	, InvalidAreaColor(0.1f, 0.07f, 0.07f, 1.0f)
	, EventFont(FCoreStyle::GetDefaultFontStyle("Regular", 8))
	, Stats()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimingViewDrawHelper::~FTimingViewDrawHelper()
{
	delete EventsBorderBrush; //im: is it safe to delete brush (i.e in OnPaint, imediately after use)?
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingViewDrawHelper::DrawBackground() const
{
	const float X0 = Viewport.TimeToSlateUnitsRounded(0.0);
	const float X1 = Viewport.TimeToSlateUnitsRounded(Viewport.MaxValidTime);
	const float W = FMath::CeilToFloat(Viewport.Width);
	const float H = FMath::CeilToFloat(Viewport.Height);

	if (X0 >= W || X1 <= 0.0f)
	{
		ValidX0 = 0.0f;
		ValidX1 = W;

		// Draw invalid area (entire view).
		DrawContext.DrawBox(0.0f, 0.0f, W, H, BackgroundAreaBrush, InvalidAreaColor);
	}
	else // X0 < W && X1 > 0
	{
		if (X0 > 0.0f)
		{
			// Draw invalid area (left).
			DrawContext.DrawBox(0.0f, 0.0f, X0, H, BackgroundAreaBrush, InvalidAreaColor);
		}

		if (X1 < W)
		{
			// Draw invalid area (right).
			DrawContext.DrawBox(X1, 0.0f, W - X1, H, BackgroundAreaBrush, InvalidAreaColor);
		}

		ValidX0 = FMath::Max(X0, 0.0f);
		ValidX1 = FMath::Min(X1, W);

		if (ValidX1 > ValidX0)
		{
			// Draw valid area.
			DrawContext.DrawBox(ValidX0, 0.0f, ValidX1 - ValidX0, H, BackgroundAreaBrush, ValidAreaColor);
		}
	}

	DrawContext.LayerId++;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingViewDrawHelper::BeginTimelines()
{
	TimelineIndex = -1;
	TimelineTopY = -Viewport.ScrollPosY;

	float Y = TimelineTopY + Viewport.TopOffset;

	if (Y > 0.0f && ValidX1 > ValidX0)
	{
		Y = FMath::Min(Y, Viewport.Height);

		// Draw invalid area (top).
		DrawContext.DrawBox(DrawContext.LayerId + ToInt32(EDrawLayer::TimelineHeader), ValidX0, 0.0f, ValidX1 - ValidX0, Y, BackgroundAreaBrush, InvalidAreaColor);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FTimingViewDrawHelper::BeginTimeline(FTimingEventsTrack& Track)
{
	TimelineIndex++;

	Track.SetPosY(TimelineTopY + Viewport.ScrollPosY);

	if (Track.GetHeight() < Layout.MinTimelineH)
		Track.SetHeight(Layout.MinTimelineH);

	if (TimelineTopY + Track.GetHeight() < -1.0f)
	{
		TimelineTopY += Track.GetHeight();
		return false;
	}

	if (TimelineTopY > Viewport.Height - Viewport.TopOffset)
	{
		return false;
	}

	MaxDepth = -1;

	// +1.0f is for horizontal line between timelines
	TimelineY = Viewport.TopOffset + TimelineTopY + 1.0f + Layout.TimelineDY;

	// Reset "last event X2" for all depths.
	LastEventX2.Reset();

	// Reset "last box" for all depths.
	LastBox.Reset();

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingViewDrawHelper::EndTimeline(FTimingEventsTrack& Track)
{
	// Flush merged boxes.
	for (int32 Depth = 0; Depth <= MaxDepth; ++Depth)
	{
		const FBoxData& Box = LastBox[Depth];
		if (Box.X1 < Box.X2)
		{
			const float EventY = TimelineY + (Layout.EventH + Layout.EventDY) * Depth;
			DrawBox(Box, EventY, Layout.EventH);
		}
	}

	Track.Depth = MaxDepth;

	float NewH;
	if (MaxDepth < 0)
	{
		NewH = Layout.MinTimelineH;
	}
	else //if (MaxDepth >= 0)
	{
		// 1.0f is for horizontal line between timelines
		NewH = 1.0f + Layout.TimelineDY + Layout.EventH * (MaxDepth + 1) + Layout.EventDY * MaxDepth + Layout.TimelineDY;

		if (NewH < FTimingEventsTrackLayout::RealMinTimelineH)
		{
			NewH = FTimingEventsTrackLayout::RealMinTimelineH;
		}
	}

	if (Track.GetHeight() < NewH)
	{
		float NewTrackHeight;
		if (Layout.bIsCompactMode)
		{
			NewTrackHeight = FMath::CeilToFloat(Track.GetHeight() * 0.5f + NewH * 0.5f);
		}
		else
		{
			NewTrackHeight = FMath::CeilToFloat(Track.GetHeight() * 0.9f + NewH * 0.1f);
		}
		Track.SetHeight(NewTrackHeight);
	}
	else if (Track.GetHeight() > NewH)
	{
		float NewTrackHeight;
		if (Layout.bIsCompactMode)
		{
			NewTrackHeight = FMath::FloorToFloat(Track.GetHeight() * 0.5f + NewH * 0.5f);
		}
		else
		{
			NewTrackHeight = FMath::FloorToFloat(Track.GetHeight() * 0.9f + NewH * 0.1f);
		}
		Track.SetHeight(NewTrackHeight);
	}

	if (Track.GetHeight() > 0)
	{
		const float Y = Viewport.TopOffset + TimelineTopY;

		FLinearColor Color(0.05f, 0.05f, 0.05f, 1.0f);
		FLinearColor TextColor(1.0f, 1.0f, 1.0f, 1.0f);

		if (Track.IsSelected())
		{
			if (Track.IsHovered())
			{
				TextColor = FLinearColor(1.0f, 1.0f, 0.0f, 1.0f);
			}
			else
			{
				TextColor = FLinearColor(1.0f, 1.0f, 0.5f, 1.0f);
			}
		}
		else if (Track.IsHovered())
		{
			TextColor = FLinearColor(1.0f, 1.0f, 0.0f, 1.0f);
		}

		// Draw a horizontal line between timelines.
		DrawContext.DrawBox(DrawContext.LayerId + ToInt32(EDrawLayer::TimelineHeader), 0, Y, Viewport.Width, 1.0f, WhiteBrush, Color);

		// Draw name of timeline.
		//const FString Name = FString::Printf(TEXT("%d Y: %g, H: %g, VO: %g, VY: %g"), TimelineIndex, Track.Y, Track.H, Viewport.TopOffset, Viewport.ScrollPosY); //debug
		const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		float NameWidth = FontMeasureService->Measure(Track.GetName(), EventFont).X;
		DrawContext.DrawBox(DrawContext.LayerId + ToInt32(EDrawLayer::TimelineHeader), 0.0f, Y + 1.0f, NameWidth + 4.0f, 12.0f, WhiteBrush, Color);
		DrawContext.DrawText(DrawContext.LayerId + ToInt32(EDrawLayer::TimelineText), 2.0f, Y, Track.GetName(), EventFont, TextColor);
	}

	TimelineTopY += Track.GetHeight();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingViewDrawHelper::AddEvent(double EventStartTime, double EventEndTime, uint32 EventDepth, const TCHAR* EventName, uint32 Color)
{
	Stats.NumEvents++;

	float EventX1 = Viewport.TimeToSlateUnitsRounded(EventStartTime);
	if (EventX1 > Viewport.Width)
	{
		return;
	}

	double RestrictedEndTime = Viewport.RestrictEndTime(EventEndTime);
	float EventX2 = Viewport.TimeToSlateUnitsRounded(RestrictedEndTime);
	if (EventX2 < 0)
	{
		return;
	}

	// Timing events are displayed with minimum 1px (including empty ones).
	if (EventX1 == EventX2)
	{
		EventX2 = EventX1 + 1.0f;
	}

	const int32 Depth = static_cast<int32>(EventDepth);
	if (Depth > MaxDepth)
	{
		MaxDepth = Depth;
	}

	// Ensure we have enough slots in array. See LastBox[Depth] usage.
	if (LastBox.Num() <= Depth)
	{
		LastBox.AddDefaulted(Depth + 1 - LastBox.Num());
	}

	float EventY = TimelineY + (Layout.EventH + Layout.EventDY) * Depth;
	if (EventY < -Layout.EventH || EventY > Viewport.Height)
	{
		return;
	}

	// Ensure we have enough slots in array. See LastEventX2[Depth] usage.
	while (LastEventX2.Num() <= Depth)
	{
		LastEventX2.Add(-1.0f);
	}

	// Limit event width on the viewport's left side.
	// This also makes the text to be displayed in viewport, for very long events.
	const float MinX = -2.0f; // -2 allows event border to remain outside screen
	if (EventX1 < MinX)
	{
		EventX1 = MinX;
	}

	// Limit event width on the viewport's right side.
	const float MaxX = Viewport.Width + 2.0f; // +2 allows event border to remain outside screen
	if (EventX2 > MaxX)
	{
		EventX2 = MaxX;
	}

	float EventW = EventX2 - EventX1;

	// Optimization...
	if (EventW == 1.0f && EventX1 == LastEventX2[Depth] - 1.0f)
	{
		// Do no draw 1 pixel event if the last event was ended on that pixel.
		return;
	}

	//////////////////////////////////////////////////
	// Coloring

	FLinearColor EventColorFill;

	//im:TODO: EventColorFill = GetEventColorFn(Event);
	if (Color == 0)
	{
		uint32 NameHash = 0;
		for (const TCHAR* c = EventName; *c; ++c)
		{
			NameHash = (NameHash + *c) * 0x2c2c57ed;
		}

		Color = NameHash | 0xFF000000;

		// Increase brightness.
		//Color = ((NameHash | ((NameHash & 0x00808080) >> 1) | ((NameHash & 0x00808080) >> 2) | ((NameHash & 0x00808080) >> 3)) << 1) | 0xFF000000;

		// Increase brightness.
		//EventColorFill.R = ((NameHash >> 16) & 0xFF) / 128.0f;
		//EventColorFill.G = ((NameHash >> 8) & 0xFF) / 128.0f;
		//EventColorFill.B = (NameHash & 0xFF) / 128.0f;
		//EventColorFill.A = 1.0f;
	}
	//else
	{
		EventColorFill.R = ((Color >> 16) & 0xFF) / 255.0f;
		EventColorFill.G = ((Color >>  8) & 0xFF) / 255.0f;
		EventColorFill.B = ((Color      ) & 0xFF) / 255.0f;
		EventColorFill.A = ((Color >> 24) & 0xFF) / 255.0f;
	}

	// Maps hash to color directly.
	//EventColorFill.R = ((Color >> 16) & 0xFF) / 255.0f;
	//EventColorFill.G = ((Color >> 8) & 0xFF) / 255.0f;
	//EventColorFill.B = (Color & 0xFF) / 255.0f;
	//EventColorFill.A = 1.0f;

	// Maps hash to color (sRGB to linear).
	//const FColor ColorFill(Color);
	//EventColorFill = ColorFill;

	// Invereted color.
	//EventColorFill.R = 1.0f - ((Color >> 16) & 0xFF) / 255.0f;
	//EventColorFill.G = 1.0f - ((Color >> 8) & 0xFF) / 255.0f;
	//EventColorFill.B = 1.0f - (Color & 0xFF) / 255.0f;
	//EventColorFill.A = 1.0f;

	// Bright color.
	//EventColorFill.R = ((Color >> 16) & 0xFF) / 128.0f;
	//EventColorFill.G = ((Color >> 8) & 0xFF) / 128.0f;
	//EventColorFill.B = (Color & 0xFF) / 128.0f;
	//EventColorFill.A = 1.0f;

	// Bright color (adjusted R, G, B).
	//const float BrightnessFactor = 0.59f / 255.0f; // [0 .. 0.59f / 255.0f]
	//EventColorFill.R = ((Color >> 16) & 0xFF) * (BrightnessFactor / 0.30f);
	//EventColorFill.G = ((Color >>  8) & 0xFF) * (BrightnessFactor / 0.59f);
	//EventColorFill.B = ((Color      ) & 0xFF) * (BrightnessFactor / 0.11f);
	//EventColorFill.A = 1.0f;

	// Bright desaturated color.
	//const float MinL = 0.3f;
	//const float MulL = (1.0f - MinL) / 255.0f;
	//EventColorFill.R = MinL + ((Color >> 16) & 0xFF) * MulL;
	//EventColorFill.G = MinL + ((Color >> 8) & 0xFF) * MulL;
	//EventColorFill.B = MinL + (Color & 0xFF) * MulL;
	//EventColorFill.A = 1.0f;

	// Dark desaturated color.
	//const float MaxL = 0.5f;
	//const float MulL = MaxL / 255.0f;
	//EventColorFill.R = (((Color >> 16) & 0xFF) * MulL;
	//EventColorFill.G = ((Color >> 8) & 0xFF) * MulL;
	//EventColorFill.B = (Color & 0xFF) * MulL;
	//EventColorFill.A = 1.0f;

	//constexpr float BorderColorFactor = 0.75f; // darker border
	constexpr float BorderColorFactor = 1.25f; // brighter border

	//////////////////////////////////////////////////

	if (EventW > 2.0f && Layout.EventH > 2.0f)
	{
		// Fill inside of the timing event box.
		DrawContext.DrawBox(DrawContext.LayerId + ToInt32(EDrawLayer::EventFill), EventX1 + 1.0f, EventY + 1.0f, EventW - 2.0f, Layout.EventH - 2.0f, WhiteBrush, EventColorFill);
		Stats.NumDrawBoxes++;
	}

	// Save X2, for current depth.
	LastEventX2[Depth] = EventX2;

	// Draw border around the timing event box.
	if (EventW > 2.0f)
	{
		FBoxData& Box = LastBox[Depth];
		if (Box.X1 < Box.X2)
		{
			DrawBox(Box, EventY, Layout.EventH);
			Box.Reset();
		}

		const FLinearColor EventColorBorder(EventColorFill.R * BorderColorFactor, EventColorFill.G * BorderColorFactor, EventColorFill.B * BorderColorFactor, 1.0f);
		DrawContext.DrawBox(DrawContext.LayerId + ToInt32(EDrawLayer::EventBorder), EventX1, EventY, EventW, Layout.EventH, EventsBorderBrush, EventColorBorder);
		Stats.NumDrawBorders++;
	}
	else // 1px or 2px boxes
	{
		FBoxData& Box = LastBox[Depth];

		// Check if we can merge this box with previous one, if any.
		// Note: We are assuming events are processed in sorted order by X1.
		if (Color == Box.Color && // same color
			EventX1 <= Box.X2) // overlapping or adjacent
		{
			// Merge it with previous box.
			Box.X2 = EventX2;
			Stats.NumMergedBoxes++;
		}
		else
		{
			// Flush previous box, if any.
			if (Box.X1 < Box.X2)
			{
				DrawBox(Box, EventY, Layout.EventH);
			}

			// Start new "merge box".
			Box.X1 = EventX1;
			Box.X2 = EventX2;
			Box.Color = Color;
			Box.LinearColor = FLinearColor(EventColorFill.R * BorderColorFactor, EventColorFill.G * BorderColorFactor, EventColorFill.B * BorderColorFactor, 1.0f);
		}
	}

	// Draw the name of the timing event.
	if (EventW > 8.0f && Layout.EventH > 10.0f)
	{
		FString Name = EventName;// +TEXT(" [") + FText::AsNumber(Event.Type->Id).ToString() + TEXT("]");
		if (EventW > Name.Len() * 2.0f + 48.0f)
		{
			const double Duration = EventEndTime - EventStartTime;
			Name += TEXT(" (");
			Name += TimeUtils::FormatTimeAuto(Duration);
			Name += TEXT(")");
		}

		const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const int32 LastWholeCharacterIndex = FontMeasureService->FindLastWholeCharacterIndexBeforeOffset(Name, EventFont, FMath::RoundToInt(EventW - 2.0f));

		if (LastWholeCharacterIndex >= 0)
		{
			// Grey threshold is shifted toward black (0.4 instead of 0.5 in test below) due to "area rule":
			// a large gray surface (background of a timing event in this case) is perceived lighter than a smaller area (text pixels).
			// Ref: https://books.google.ro/books?id=0pVr7dhmdWYC
			const bool bIsDarkColor = (EventColorFill.ComputeLuminance() < 0.4f);

			DrawContext.DrawText(DrawContext.LayerId + ToInt32(EDrawLayer::EventText), EventX1 + 2.0f, EventY + 1.0f, Name, 0, LastWholeCharacterIndex + 1, EventFont, bIsDarkColor ? FLinearColor::White : FLinearColor::Black);
			Stats.NumDrawTexts++;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingViewDrawHelper::DrawBox(const FBoxData& Box, const float EventY, const float EventH)
{
	DrawContext.DrawBox(DrawContext.LayerId + ToInt32(EDrawLayer::EventBorder), Box.X1, EventY, Box.X2 - Box.X1, EventH, WhiteBrush, Box.LinearColor);
	Stats.NumDrawBoxes++;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingViewDrawHelper::EndTimelines()
{
	float Y = Viewport.TopOffset + TimelineTopY;

	// Draw a last horizontal line.
	DrawContext.DrawBox(DrawContext.LayerId + ToInt32(EDrawLayer::TimelineHeader), 0, Y, Viewport.Width, 1.0f, WhiteBrush, FLinearColor(0.05f, 0.05f, 0.05f, 1.0f));
	Y += 1.0f;

	if (Y < Viewport.Height && ValidX1 > ValidX0)
	{
		Y = FMath::Max(Y, 0.0f);

		// Draw invalid area (bottom).
		DrawContext.DrawBox(DrawContext.LayerId + ToInt32(EDrawLayer::TimelineHeader), ValidX0, Y, ValidX1 - ValidX0, Viewport.Height - Y, BackgroundAreaBrush, InvalidAreaColor);
	}

	DrawContext.LayerId += ToInt32(EDrawLayer::Count);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingViewDrawHelper::DrawTimingEventHighlight(double StartTime, double EndTime, float Y, EHighlightMode Mode)
{
	float EventX1 = Viewport.TimeToSlateUnitsRounded(StartTime);
	if (EventX1 > Viewport.Width)
	{
		return;
	}

	EndTime = Viewport.RestrictEndTime(EndTime);
	float EventX2 = Viewport.TimeToSlateUnitsRounded(EndTime);
	if (EventX2 < 0)
	{
		return;
	}

	if (EventX1 == EventX2)
	{
		EventX2 = EventX1 + 1.0f;
	}

	// Limit event width on the viewport's left side.
	const float MinX = -2.0f; // -2 allows event border to remain outside screen
	if (EventX1 < MinX)
	{
		EventX1 = MinX;
	}

	// Limit event width on the viewport's right side.
	const float MaxX = Viewport.Width + 2.0f; // +2 allows event border to remain outside screen
	if (EventX2 > MaxX)
	{
		EventX2 = MaxX;
	}

	float EventW = EventX2 - EventX1;

	if (Mode == EHighlightMode::Hovered)
	{
		const FSlateBrush* HighlightBrush = BorderBrush;

		const FLinearColor Color(1.0f, 1.0f, 0.0f, 1.0f); // yellow

		// Draw border around the timing event box.
		DrawContext.DrawBox(EventX1 - 2.0f, Y - 2.0f, EventW + 4.0f, Layout.EventH + 4.0f, HighlightBrush, Color);
	}
	else // EHighlightMode::Selected or EHighlightMode::SelectedAndHovered
	{
		const FSlateBrush* SelectedBrush = BorderBrush;

		// Animate color from white (if selected and hovered) or yellow (if only selected) to black, using a squared sine function.
		const double Time = static_cast<double>(FPlatformTime::Cycles64()) * FPlatformTime::GetSecondsPerCycle64();
		float S = FMath::Sin(2.0 * Time);
		S = S * S; // squared, to ensure only positive [0 - 1] values
		const float Blue = (Mode == EHighlightMode::SelectedAndHovered) ? 0.0f : S;
		const FLinearColor Color(S, S, Blue, 1.0f);

		// Draw border around the timing event box.
		DrawContext.DrawBox(EventX1 - 2.0f, Y - 2.0f, EventW + 4.0f, Layout.EventH + 4.0f, SelectedBrush, Color);
	}
	DrawContext.LayerId++;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
