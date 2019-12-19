// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TimingViewDrawHelper.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "TraceServices/AnalysisService.h"

// Insights
#include "Insights/Common/PaintUtils.h"
#include "Insights/Common/TimeUtils.h"
#include "Insights/InsightsStyle.h"
#include "Insights/ViewModels/DrawHelpers.h"
#include "Insights/ViewModels/MarkersTimingTrack.h"
#include "Insights/ViewModels/TimingEvent.h"
#include "Insights/ViewModels/TimingEventsTrack.h"
#include "Insights/ViewModels/TimingTrackViewport.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTimingEventsTrackDrawStateBuilder
////////////////////////////////////////////////////////////////////////////////////////////////////

FTimingEventsTrackDrawStateBuilder::FTimingEventsTrackDrawStateBuilder(FTimingEventsTrackDrawState& InState, const FTimingTrackViewport& InViewport)
	: DrawState(InState)
	, Viewport(InViewport)
	, MaxDepth(-1)
	, LastEventX2()
	, LastBox()
	, EventFont(FCoreStyle::GetDefaultFontStyle("Regular", 8))
{
	DrawState.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingEventsTrackDrawStateBuilder::AddEvent(double EventStartTime, double EventEndTime, uint32 EventDepth, const TCHAR* EventName, uint64 TypeId, uint32 Color)
{
	DrawState.NumEvents++;

	float EventX1 = Viewport.TimeToSlateUnitsRounded(EventStartTime);
	if (EventX1 > Viewport.GetWidth())
	{
		return;
	}

	const double RestrictedEndTime = Viewport.RestrictEndTime(EventEndTime);
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
	const float MaxX = Viewport.GetWidth() + 2.0f; // +2 allows event border to remain outside screen
	if (EventX2 > MaxX)
	{
		EventX2 = MaxX;
	}

	const float EventW = EventX2 - EventX1;

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

	// Save X2, for current depth.
	LastEventX2[Depth] = EventX2;

	if (EventW > 2.0f)
	{
		FBoxData& Box = LastBox[Depth];
		if (Box.X1 < Box.X2)
		{
			FlushBox(Box, Depth);
			Box.Reset();
		}

		// Fill inside of the timing event box.
		DrawState.InsideBoxes.AddUninitialized();
		FTimingEventsTrackDrawState::FBoxPrimitive& InsideBox = DrawState.InsideBoxes.Last();
		InsideBox.Depth = Depth;
		InsideBox.X = EventX1 + 1.0f;
		InsideBox.W = EventW - 2.0f;
		InsideBox.Color = EventColorFill;

		// Add border around the timing event box.
		DrawState.Borders.AddUninitialized();
		FTimingEventsTrackDrawState::FBoxPrimitive& BorderBox = DrawState.Borders.Last();
		BorderBox.Depth = Depth;
		BorderBox.X = EventX1;
		BorderBox.W = EventW;
		BorderBox.Color = FLinearColor(EventColorFill.R * BorderColorFactor, EventColorFill.G * BorderColorFactor, EventColorFill.B * BorderColorFactor, EventColorFill.A);
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
			DrawState.NumMergedBoxes++;
		}
		else
		{
			// Flush previous box, if any.
			if (Box.X1 < Box.X2)
			{
				FlushBox(Box, Depth);
			}

			// Start new "merge box".
			Box.X1 = EventX1;
			Box.X2 = EventX2;
			Box.Color = Color;
			Box.LinearColor = FLinearColor(EventColorFill.R * BorderColorFactor, EventColorFill.G * BorderColorFactor, EventColorFill.B * BorderColorFactor, EventColorFill.A);
		}
	}

	// Draw the name of the timing event.
	if (EventW > 8.0f)
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

			DrawState.Texts.AddDefaulted();
			FTimingEventsTrackDrawState::FTextPrimitive& DrawText = DrawState.Texts.Last();
			DrawText.Depth = Depth;
			DrawText.X = EventX1 + 2.0f;
			DrawText.Text = Name.Left(LastWholeCharacterIndex + 1);
			DrawText.bWhite = bIsDarkColor;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingEventsTrackDrawStateBuilder::FlushBox(const FBoxData& Box, const int32 Depth)
{
	DrawState.Boxes.AddUninitialized();
	FTimingEventsTrackDrawState::FBoxPrimitive& DrawBox = DrawState.Boxes.Last();
	DrawBox.Depth = Depth;
	DrawBox.X = Box.X1;
	DrawBox.W = Box.X2 - Box.X1;
	DrawBox.Color = Box.LinearColor;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingEventsTrackDrawStateBuilder::Flush()
{
	// Flush merged boxes.
	for (int32 Depth = 0; Depth <= MaxDepth; ++Depth)
	{
		const FBoxData& Box = LastBox[Depth];
		if (Box.X1 < Box.X2)
		{
			FlushBox(Box, Depth);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTimingViewDrawHelper
////////////////////////////////////////////////////////////////////////////////////////////////////

FTimingViewDrawHelper::FTimingViewDrawHelper(const FDrawContext& InDrawContext, const FTimingTrackViewport& InViewport)
	: DrawContext(InDrawContext)
	, Viewport(InViewport)
	, WhiteBrush(FInsightsStyle::Get().GetBrush("WhiteBrush"))
	, EventBorderBrush(FInsightsStyle::Get().GetBrush("EventBorder"))
	, HoveredEventBorderBrush(FInsightsStyle::Get().GetBrush("HoveredEventBorder"))
	, SelectedEventBorderBrush(FInsightsStyle::Get().GetBrush("SelectedEventBorder"))
	, BackgroundAreaBrush(WhiteBrush)
	, ValidAreaColor(0.07f, 0.07f, 0.07f, 1.0f)
	, InvalidAreaColor(0.1f, 0.07f, 0.07f, 1.0f)
	, EdgeColor(0.05f, 0.05f, 0.05f, 1.0f)
	, EventFont(FCoreStyle::GetDefaultFontStyle("Regular", 8))
	, ValidAreaX(0.0f)
	, ValidAreaW(0.0f)
	, NumEvents(0)
	, NumMergedBoxes(0)
	, NumDrawBoxes(0)
	, NumDrawBorders(0)
	, NumDrawTexts(0)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimingViewDrawHelper::~FTimingViewDrawHelper()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingViewDrawHelper::DrawBackground() const
{
	const float Y = 0.0f;
	const float H = FMath::CeilToFloat(Viewport.GetHeight());
	FDrawHelpers::DrawBackground(DrawContext, BackgroundAreaBrush, Viewport, Y, H, ValidAreaX, ValidAreaW); // also computes ValidAreaX and ValidAreaW
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingViewDrawHelper::DrawEvents(const FTimingEventsTrackDrawState& DrawState, const FTimingEventsTrack& Track, const float OffsetY) const
{
	const float TrackY = Track.GetPosY();
	const float TrackH = Track.GetHeight();

	if (TrackH > 0.0f &&
		TrackY + TrackH > Viewport.GetTopOffset() &&
		TrackY < Viewport.GetHeight() - Viewport.GetBottomOffset())
	{
		const FTimingViewLayout& Layout = Viewport.GetLayout();

		NumEvents += DrawState.GetNumEvents();
		NumMergedBoxes += DrawState.GetNumMergedBoxes();

		const float TopLaneY = TrackY + OffsetY + Layout.TimelineDY;

		// Draw filled boxes (merged borders).
		//if (Layout.EventH > 0.0f)
		{
			const int32 EventFillLayerId = ReservedLayerId + ToInt32(EDrawLayer::EventFill);
			const float EventFillH = Layout.EventH;
			for (const FTimingEventsTrackDrawState::FBoxPrimitive& Box : DrawState.Boxes)
			{
				const float Y = TopLaneY + (Layout.EventH + Layout.EventDY) * Box.Depth;
				DrawContext.DrawBox(EventFillLayerId, Box.X, Y, Box.W, EventFillH, WhiteBrush, Box.Color);
			}
			NumDrawBoxes += DrawState.Boxes.Num();
		}

		// Draw filled boxes (event inside area).
		if (Layout.EventH > 2.0f)
		{
			const int32 EventFillLayerId = ReservedLayerId + ToInt32(EDrawLayer::EventFill);
			const float EventFillH = Layout.EventH - 2.0f;
			for (const FTimingEventsTrackDrawState::FBoxPrimitive& Box : DrawState.InsideBoxes)
			{
				const float Y = TopLaneY + (Layout.EventH + Layout.EventDY) * Box.Depth + 1.0f;
				DrawContext.DrawBox(EventFillLayerId, Box.X, Y, Box.W, EventFillH, WhiteBrush, Box.Color);
			}
			NumDrawBoxes += DrawState.InsideBoxes.Num();
		}

		// Draw borders.
		//if (Layout.EventH > 0.0f)
		{
			const int32 EventBorderLayerId = ReservedLayerId + ToInt32(EDrawLayer::EventBorder);
			const float EventBorderH = Layout.EventH;
			for (const FTimingEventsTrackDrawState::FBoxPrimitive& Box : DrawState.Borders)
			{
				const float Y = TopLaneY + (Layout.EventH + Layout.EventDY) * Box.Depth;
				DrawContext.DrawBox(EventBorderLayerId, Box.X, Y, Box.W, EventBorderH, EventBorderBrush, Box.Color);
			}
			NumDrawBorders += DrawState.Borders.Num();
		}

		// Draw texts.
		if (Layout.EventH > 10.0f)
		{
			const int32 EventTextLayerId = ReservedLayerId + ToInt32(EDrawLayer::EventText);
			for (const FTimingEventsTrackDrawState::FTextPrimitive& Text : DrawState.Texts)
			{
				const float Y = TopLaneY + (Layout.EventH + Layout.EventDY) * Text.Depth + 1.0f;
				DrawContext.DrawText(EventTextLayerId, Text.X, Y, Text.Text, EventFont, Text.bWhite ? FLinearColor::White : FLinearColor::Black);
			}
			NumDrawTexts += DrawState.Texts.Num();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingViewDrawHelper::DrawFadedEvents(const FTimingEventsTrackDrawState& DrawState, const FTimingEventsTrack& Track, const float OffsetY, const float Opacity) const
{
	const float TrackY = Track.GetPosY();
	const float TrackH = Track.GetHeight();

	if (TrackH > 0.0f &&
		TrackY + TrackH > Viewport.GetTopOffset() &&
		TrackY < Viewport.GetHeight() - Viewport.GetBottomOffset())
	{
		const FTimingViewLayout& Layout = Viewport.GetLayout();

		NumEvents += DrawState.GetNumEvents();
		NumMergedBoxes += DrawState.GetNumMergedBoxes();

		const float TopLaneY = TrackY + OffsetY + Layout.TimelineDY;

		// Draw filled boxes (merged borders).
		//if (Layout.EventH > 0.0f)
		{
			const int32 EventFillLayerId = ReservedLayerId + ToInt32(EDrawLayer::EventFill);
			const float EventFillH = Layout.EventH;
			for (const FTimingEventsTrackDrawState::FBoxPrimitive& Box : DrawState.Boxes)
			{
				const float Y = TopLaneY + (Layout.EventH + Layout.EventDY) * Box.Depth;
				DrawContext.DrawBox(EventFillLayerId, Box.X, Y, Box.W, EventFillH, WhiteBrush, Box.Color.CopyWithNewOpacity(Opacity));
			}
			NumDrawBoxes += DrawState.Boxes.Num();
		}

		// Draw filled boxes (event inside area).
		if (Layout.EventH > 2.0f)
		{
			const int32 EventFillLayerId = ReservedLayerId + ToInt32(EDrawLayer::EventFill);
			const float EventFillH = Layout.EventH - 2.0f;
			for (const FTimingEventsTrackDrawState::FBoxPrimitive& Box : DrawState.InsideBoxes)
			{
				const float Y = TopLaneY + (Layout.EventH + Layout.EventDY) * Box.Depth + 1.0f;
				DrawContext.DrawBox(EventFillLayerId, Box.X, Y, Box.W, EventFillH, WhiteBrush, Box.Color.CopyWithNewOpacity(Opacity));
			}
			NumDrawBoxes += DrawState.InsideBoxes.Num();
		}

		// Draw borders.
		//if (Layout.EventH > 0.0f)
		{
			const int32 EventBorderLayerId = ReservedLayerId + ToInt32(EDrawLayer::EventBorder);
			const float EventBorderH = Layout.EventH;
			for (const FTimingEventsTrackDrawState::FBoxPrimitive& Box : DrawState.Borders)
			{
				const float Y = TopLaneY + (Layout.EventH + Layout.EventDY) * Box.Depth;
				DrawContext.DrawBox(EventBorderLayerId, Box.X, Y, Box.W, EventBorderH, EventBorderBrush, Box.Color.CopyWithNewOpacity(Opacity));
			}
			NumDrawBorders += DrawState.Borders.Num();
		}

		// Draw texts.
		if (Layout.EventH > 10.0f)
		{
			const FLinearColor WhiteColor(1.0f, 1.0f, 1.0f, Opacity);
			const FLinearColor BlackColor(0.0f, 0.0f, 0.0f, Opacity);

			const int32 EventTextLayerId = ReservedLayerId + ToInt32(EDrawLayer::EventText);
			for (const FTimingEventsTrackDrawState::FTextPrimitive& Text : DrawState.Texts)
			{
				const float Y = TopLaneY + (Layout.EventH + Layout.EventDY) * Text.Depth + 1.0f;
				DrawContext.DrawText(EventTextLayerId, Text.X, Y, Text.Text, EventFont, Text.bWhite ? WhiteColor : BlackColor);
			}
			NumDrawTexts += DrawState.Texts.Num();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingViewDrawHelper::DrawMarkers(const FTimingEventsTrackDrawState& DrawState, float LineY, float LineH, float Opacity) const
{
	if (LineH > 0.0f)
	{
		const FTimingViewLayout& Layout = Viewport.GetLayout();

		// Draw markers from filled boxes (merged borders).
		for (const FTimingEventsTrackDrawState::FBoxPrimitive& Box : DrawState.Boxes)
		{
			DrawContext.DrawBox(Box.X, LineY, Box.W, LineH, WhiteBrush, Box.Color.CopyWithNewOpacity(Opacity));
		}

		// Draw markers from borders.
		for (const FTimingEventsTrackDrawState::FBoxPrimitive& Box : DrawState.Borders)
		{
			DrawContext.DrawBox(Box.X, LineY, 1.0f, LineH, WhiteBrush, Box.Color.CopyWithNewOpacity(Opacity));
			if (Box.W > 1.0f)
			{
				DrawContext.DrawBox(Box.X + Box.W - 1.0f, LineY, 1.0f, LineH, WhiteBrush, Box.Color.CopyWithNewOpacity(Opacity));
			}
		}

		DrawContext.LayerId++;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingViewDrawHelper::DrawTrackHeader(const FTimingEventsTrack& Track) const
{
	const float TrackY = Track.GetPosY();
	const float TrackH = Track.GetHeight();

	if (TrackH > 0.0f &&
		TrackY + TrackH > Viewport.GetTopOffset() &&
		TrackY < Viewport.GetHeight() - Viewport.GetBottomOffset())
	{
		// Draw a horizontal line between timelines (top line of a track).
		const int32 HeaderLayerId = ReservedLayerId + ToInt32(EDrawLayer::HeaderBackground);
		DrawContext.DrawBox(HeaderLayerId, 0.0f, TrackY, Viewport.GetWidth(), 1.0f, WhiteBrush, EdgeColor);

		// Draw header with name of timeline.
		if (TrackH > 4.0f)
		{
			const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			float TextWidth = FontMeasureService->Measure(Track.GetName(), EventFont).X;

			constexpr float PinWidth = 8.0f;
			if (Track.IsSelected())
			{
				TextWidth += PinWidth;
			}

			const float HeaderX = 0.0f;
			const float HeaderW = TextWidth + 4.0f;

			const float HeaderY = TrackY + 1.0f;
			const float HeaderH = FMath::Min(12.0f, Track.GetHeight() - 1.0f);

			if (HeaderH > 0)
			{
				DrawContext.DrawBox(HeaderLayerId, HeaderX, HeaderY, HeaderW, HeaderH, WhiteBrush, EdgeColor);

				const FLinearColor TextColor = GetTrackNameTextColor(Track);

				float TextX = HeaderX + 2.0f;
				const float TextY = HeaderY + HeaderH / 2.0f - 7.0f;
				const int32 HeaderTextLayerId = ReservedLayerId + ToInt32(EDrawLayer::HeaderText);

				if (Track.IsSelected())
				{
					// TODO: Use a "pin" image brush instead.
					DrawContext.DrawText(HeaderTextLayerId, TextX, TextY, TEXT(">"), EventFont, TextColor);
					TextX += PinWidth;
				}

				DrawContext.DrawText(HeaderTextLayerId, TextX, TextY, Track.GetName(), EventFont, TextColor);
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingViewDrawHelper::BeginDrawTracks() const
{
	// Reserve layers.
	ReservedLayerId = DrawContext.LayerId;
	DrawContext.LayerId += ToInt32(EDrawLayer::Count);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingViewDrawHelper::EndDrawTracks() const
{
	if (Viewport.GetWidth() > 0.0f)
	{
		// Y position of the first pixel below the last track.
		const float Y = Viewport.GetTopOffset() + Viewport.GetScrollHeight() - Viewport.GetScrollPosY();

		const float TopY = Viewport.GetTopOffset();
		const float BottomY = Viewport.GetHeight() - Viewport.GetBottomOffset();

		if (Y >= Viewport.GetTopOffset() && Y < BottomY)
		{
			// Draw a last horizontal line.
			DrawContext.DrawBox(ReservedLayerId + ToInt32(EDrawLayer::HeaderBackground), 0.0f, Y, Viewport.GetWidth(), 1.0f, WhiteBrush, EdgeColor);
		}

		// Note: ValidAreaX and ValidAreaW are computed in DrawBackground.
		if (ValidAreaW > 0.0f)
		{
			const float TopInvalidAreaH = FMath::Min(0.0f - Viewport.GetScrollPosY(), Viewport.GetScrollableAreaHeight());
			if (TopInvalidAreaH > 0.0f)
			{
				// Draw invalid area (top).
				DrawContext.DrawBox(ReservedLayerId + ToInt32(EDrawLayer::HeaderBackground), ValidAreaX, TopY, ValidAreaW, TopInvalidAreaH, BackgroundAreaBrush, InvalidAreaColor);
			}

			const float BottomInvalidAreaH = FMath::Min(BottomY - Y - 1.0f, Viewport.GetScrollableAreaHeight());
			if (BottomInvalidAreaH > 0.0f)
			{
				// Draw invalid area (bottom).
				DrawContext.DrawBox(ReservedLayerId + ToInt32(EDrawLayer::HeaderBackground), ValidAreaX, BottomY - BottomInvalidAreaH, ValidAreaW, BottomInvalidAreaH, BackgroundAreaBrush, InvalidAreaColor);
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingViewDrawHelper::DrawTimingEventHighlight(double StartTime, double EndTime, float Y, EDrawEventMode Mode) const
{
	float EventX1 = Viewport.TimeToSlateUnitsRounded(StartTime);
	if (EventX1 > Viewport.GetWidth())
	{
		return;
	}

	EndTime = Viewport.RestrictEndTime(EndTime);
	float EventX2 = Viewport.TimeToSlateUnitsRounded(EndTime);
	if (EventX2 < 0)
	{
		return;
	}

	// Timing events are displayed with minimum 1px (including empty ones).
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
	const float MaxX = Viewport.GetWidth() + 2.0f; // +2 allows event border to remain outside screen
	if (EventX2 > MaxX)
	{
		EventX2 = MaxX;
	}

	const float EventW = EventX2 - EventX1;

	const FTimingViewLayout& Layout = Viewport.GetLayout();

	const int32 LayerId = ReservedLayerId + ToInt32(EDrawLayer::EventHighlight);

	if (Mode == EDrawEventMode::Hovered)
	{
		const FLinearColor Color(1.0f, 1.0f, 0.0f, 1.0f); // yellow

		// Draw border around the timing event box.
		DrawContext.DrawBox(LayerId, EventX1 - 2.0f, Y - 2.0f, EventW + 4.0f, Layout.EventH + 4.0f, HoveredEventBorderBrush, Color);
	}
	else // EDrawEventMode::Selected or EDrawEventMode::SelectedAndHovered
	{
		// Animate color from white (if selected and hovered) or yellow (if only selected) to black, using a squared sine function.
		const double Time = static_cast<double>(FPlatformTime::Cycles64()) * FPlatformTime::GetSecondsPerCycle64();
		float S = FMath::Sin(2.0 * Time);
		S = S * S; // squared, to ensure only positive [0 - 1] values
		const float Blue = (Mode == EDrawEventMode::SelectedAndHovered) ? 0.0f : S;
		const FLinearColor Color(S, S, Blue, 1.0f);

		// Draw border around the timing event box.
		DrawContext.DrawBox(LayerId, EventX1 - 2.0f, Y - 2.0f, EventW + 4.0f, Layout.EventH + 4.0f, SelectedEventBorderBrush, Color);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FLinearColor FTimingViewDrawHelper::GetTrackNameTextColor(const FBaseTimingTrack& Track) const
{
	return  Track.IsHovered() ?  FLinearColor(1.0f, 1.0f, 0.0f, 1.0f) :
			Track.IsSelected() ? FLinearColor(1.0f, 1.0f, 0.5f, 1.0f) :
			                     FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
