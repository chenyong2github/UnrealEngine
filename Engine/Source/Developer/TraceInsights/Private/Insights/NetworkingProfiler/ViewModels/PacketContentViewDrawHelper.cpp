// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PacketContentViewDrawHelper.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "TraceServices/AnalysisService.h"

// Insights
#include "Insights/Common/PaintUtils.h"
#include "Insights/InsightsStyle.h"
#include "Insights/NetworkingProfiler/ViewModels/PacketContentViewport.h"
#include "Insights/ViewModels/DrawHelpers.h"

#include <limits>

////////////////////////////////////////////////////////////////////////////////////////////////////
// FPacketContentViewDrawStateBuilder
////////////////////////////////////////////////////////////////////////////////////////////////////

FPacketContentViewDrawStateBuilder::FPacketContentViewDrawStateBuilder(FPacketContentViewDrawState& InDrawState, const FPacketContentViewport& InViewport)
	: DrawState(InDrawState)
	, Viewport(InViewport)
	, MaxDepth(-1)
	, LastEventX2()
	, LastBox()
	, EventFont(FCoreStyle::GetDefaultFontStyle("Regular", 8))
{
	DrawState.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FPacketContentViewDrawStateBuilder::AddEvent(const Trace::FNetProfilerContentEvent& Event, const TCHAR* EventName, uint32 NetId)
{
	DrawState.Events.AddUninitialized();
	FNetworkPacketEvent& PacketEvent = DrawState.Events.Last();
	PacketEvent.EventTypeIndex = Event.EventTypeIndex;
	PacketEvent.ObjectInstanceIndex = Event.ObjectInstanceIndex;
	PacketEvent.NetId = NetId;
	PacketEvent.BitOffset = Event.StartPos;
	PacketEvent.BitSize = Event.EndPos - Event.StartPos;
	PacketEvent.Level = Event.Level;

	const FAxisViewportDouble& ViewportX = Viewport.GetHorizontalAxisViewport();

	float EventX1 = ViewportX.GetRoundedOffsetForValue(static_cast<double>(Event.StartPos));
	if (EventX1 > Viewport.GetWidth())
	{
		return;
	}

	//float EventX2 = EventSize == 0 ?
	//	EventX1 + 1.0f :
	//	ViewportX.GetScale() < 1.0f ?
	//	ViewportX.GetRoundedOffsetForValue(static_cast<double>(Event.EndPos - 1)) + 1.0f : // x2 = pos of last inclusive bit + 1px
	//	ViewportX.GetRoundedOffsetForValue(static_cast<double>(Event.EndPos));             // x2 = pos of first exclusive bit
	float EventX2 = ViewportX.GetRoundedOffsetForValue(static_cast<double>(Event.EndPos));
	if (EventX2 < 0)
	{
		return;
	}

	// Events are displayed with minimum 1px (including empty ones).
	if (EventX1 == EventX2)
	{
		EventX2 = EventX1 + 1.0f;
	}

	const int32 Depth = static_cast<int32>(Event.Level);
	if (Depth > MaxDepth)
	{
		MaxDepth = Depth;
	}

	// Ensure we have enough slots in array. See LastBox[Depth] usage.
	if (LastBox.Num() <= Depth)
	{
		LastBox.AddDefaulted(Depth + 1 - LastBox.Num());
	}

	constexpr float Y0 = 0.0f;
	constexpr float EventH = 14.0f;
	constexpr float EventDY = 2.0f;

	const float EventY = Y0 + (EventH + EventDY) * Depth;
	if (EventY < -EventH || EventY > Viewport.GetHeight())
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

	const uint32 Color = (Event.NameIndex + 0x2c2c57ed) * 0x2c2c57ed;
	const FLinearColor EventColorFill(((Color >> 16) & 0xFF) / 255.0f, ((Color >> 8) & 0xFF) / 255.0f, (Color & 0xFF) / 255.0f, 1.0f);
	constexpr float BorderColorFactor = 1.25f; // brighter border

	//////////////////////////////////////////////////

	if (EventW > 2.0f && EventH > 2.0f)
	{
		// Fill inside of the event box.
		DrawState.Boxes.AddUninitialized();
		FPacketContentViewDrawState::FBox& DrawBox = DrawState.Boxes.Last();
		DrawBox.X = EventX1 + 1.0f;
		DrawBox.Y = EventY + 1.0f;
		DrawBox.W = EventW - 2.0f;
		DrawBox.H = EventH - 2.0f;
		DrawBox.Color = EventColorFill;
	}

	// Save X2, for current depth.
	LastEventX2[Depth] = EventX2;

	// Add border around the event box.
	if (EventW > 2.0f)
	{
		FBoxData& Box = LastBox[Depth];
		if (Box.X1 < Box.X2)
		{
			FlushBox(Box, EventY, EventH);
			Box.Reset();
		}

		DrawState.Borders.AddUninitialized();
		FPacketContentViewDrawState::FBox& DrawBox = DrawState.Borders.Last();
		DrawBox.X = EventX1;
		DrawBox.Y = EventY;
		DrawBox.W = EventW;
		DrawBox.H = EventH;
		DrawBox.Color.R = EventColorFill.R * BorderColorFactor;
		DrawBox.Color.G = EventColorFill.G * BorderColorFactor;
		DrawBox.Color.B = EventColorFill.B * BorderColorFactor;
		DrawBox.Color.A = 1.0f;
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
				FlushBox(Box, EventY, EventH);
			}

			// Start new "merge box".
			Box.X1 = EventX1;
			Box.X2 = EventX2;
			Box.Color = Color;
			Box.LinearColor = FLinearColor(EventColorFill.R * BorderColorFactor, EventColorFill.G * BorderColorFactor, EventColorFill.B * BorderColorFactor, 1.0f);
		}
	}

	// Draw the name of the event.
	if (EventW > 8.0f && EventH > 10.0f)
	{
		FString Name = EventName ? EventName : TEXT("?");
		if (EventW > Name.Len() * 2.0f + 48.0f)
		{
			if (Event.ObjectInstanceIndex != 0)
			{
				Name += TEXT(" (NetId:");
				Name += FText::AsNumber(NetId).ToString();
				Name += TEXT(", ");
			}
			else
			{
				Name += TEXT(" (");
			}
			const uint32 EventSize = Event.EndPos - Event.StartPos;
			if (EventSize == 1)
			{
				Name += TEXT("1 bit)");
			}
			else
			{
				Name += FText::AsNumber(EventSize).ToString();
				Name += TEXT(" bits)");
			}
		}

		const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const int32 LastWholeCharacterIndex = FontMeasureService->FindLastWholeCharacterIndexBeforeOffset(Name, EventFont, FMath::RoundToInt(EventW - 2.0f));

		if (LastWholeCharacterIndex >= 0)
		{
			// Grey threshold is shifted toward black (0.4 instead of 0.5 in test below) due to "area rule":
			// a large gray surface (background of an event in this case) is perceived lighter than a smaller area (text pixels).
			// Ref: https://books.google.ro/books?id=0pVr7dhmdWYC
			const bool bIsDarkColor = (EventColorFill.ComputeLuminance() < 0.4f);

			DrawState.Texts.AddDefaulted();
			FPacketContentViewDrawState::FText& DrawText = DrawState.Texts.Last();
			DrawText.X = EventX1 + 2.0f;
			DrawText.Y = EventY + 1.0f;
			DrawText.Text = Name.Left(LastWholeCharacterIndex + 1);
			DrawText.bWhite = bIsDarkColor;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FPacketContentViewDrawStateBuilder::FlushBox(const FBoxData& Box, const float EventY, const float EventH)
{
	DrawState.Boxes.AddUninitialized();
	FPacketContentViewDrawState::FBox& DrawBox = DrawState.Boxes.Last();
	DrawBox.X = Box.X1;
	DrawBox.W = Box.X2 - Box.X1;
	DrawBox.Y = EventY;
	DrawBox.H = EventH;
	DrawBox.Color = Box.LinearColor;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FPacketContentViewDrawStateBuilder::Flush()
{
	constexpr float Y0 = 0.0f;
	constexpr float EventH = 14.0f;
	constexpr float EventDY = 2.0f;

	// Flush merged boxes.
	for (int32 Depth = 0; Depth <= MaxDepth; ++Depth)
	{
		const FBoxData& Box = LastBox[Depth];
		if (Box.X1 < Box.X2)
		{
			const float EventY = Y0 + (EventH + EventDY) * Depth;
			FlushBox(Box, EventY, EventH);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FPacketContentViewDrawHelper
////////////////////////////////////////////////////////////////////////////////////////////////////

FPacketContentViewDrawHelper::FPacketContentViewDrawHelper(const FDrawContext& InDrawContext, const FPacketContentViewport& InViewport)
	: DrawContext(InDrawContext)
	, Viewport(InViewport)
	, WhiteBrush(FInsightsStyle::Get().GetBrush("WhiteBrush"))
	, EventBorderBrush(FInsightsStyle::Get().GetBrush("EventBorder"))
	, HoveredEventBorderBrush(FInsightsStyle::Get().GetBrush("HoveredEventBorder"))
	, SelectedEventBorderBrush(FInsightsStyle::Get().GetBrush("SelectedEventBorder"))
	, EventFont(FCoreStyle::GetDefaultFontStyle("Regular", 8))
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FPacketContentViewDrawHelper::DrawBackground() const
{
	const FAxisViewportDouble& ViewportX = Viewport.GetHorizontalAxisViewport();

	const float X0 = 0.0f;
	const float X1 = ViewportX.GetMinPos() - ViewportX.GetPos();
	const float X2 = ViewportX.GetMaxPos() - ViewportX.GetPos();
	const float X3 = FMath::CeilToFloat(Viewport.GetWidth());

	const float Y = 0.0f;
	const float H = FMath::CeilToFloat(Viewport.GetHeight());

	FDrawHelpers::DrawBackground(DrawContext, WhiteBrush, X0, X1, X2, X3, Y, H);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FLinearColor FPacketContentViewDrawHelper::GetColorByType(int32 Type)
{
	constexpr float Alpha = 1.0f;
	switch (Type)
	{
	case 0:
		return FLinearColor(0.75f, 0.25f, 0.25f, Alpha);

	case 1:
		return FLinearColor(0.25f, 0.75f, 0.25f, Alpha);

	case 2:
		return FLinearColor(0.25f, 0.25f, 0.75f, Alpha);

	case 3:
		return FLinearColor(0.75f, 0.75f, 0.25f, Alpha);

	case 4:
		return FLinearColor(0.25f, 0.75f, 0.75f, Alpha);

	case 5:
		return FLinearColor(0.75f, 0.25f, 0.75f, Alpha);

	//default:
	//	return FLinearColor(0.75f, 0.75f, 0.75f, Alpha);
	}

	uint32 Hash = (static_cast<uint32>(Type) + 0x2c2c57ed) * 0x2c2c57ed;
	return FLinearColor(((Hash >> 16) & 0xFF) / 255.0f,
						((Hash >>  8) & 0xFF) / 255.0f,
						((Hash      ) & 0xFF) / 255.0f,
						1.0f);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FPacketContentViewDrawHelper::Draw(const FPacketContentViewDrawState& DrawState) const
{
	// Draw filled boxes.
	for (const FPacketContentViewDrawState::FBox& Box : DrawState.Boxes)
	{
		DrawContext.DrawBox(Box.X, Box.Y, Box.W, Box.H, WhiteBrush, Box.Color);
	}
	DrawContext.LayerId++;

	// Draw borders.
	for (const FPacketContentViewDrawState::FBox& Box : DrawState.Borders)
	{
		DrawContext.DrawBox(Box.X, Box.Y, Box.W, Box.H, EventBorderBrush, Box.Color);
	}
	DrawContext.LayerId++;

	// Draw texts.
	for (const FPacketContentViewDrawState::FText& Text : DrawState.Texts)
	{
		DrawContext.DrawText(Text.X, Text.Y, Text.Text, EventFont, Text.bWhite ? FLinearColor::White : FLinearColor::Black);
	}
	DrawContext.LayerId++;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FPacketContentViewDrawHelper::DrawEventHighlight(const FNetworkPacketEvent& Event, EHighlightMode Mode) const
{
	const FAxisViewportDouble& ViewportX = Viewport.GetHorizontalAxisViewport();

	float EventX1 = ViewportX.GetRoundedOffsetForValue(static_cast<double>(Event.BitOffset));
	if (EventX1 > Viewport.GetWidth())
	{
		return;
	}

	float EventX2 = ViewportX.GetRoundedOffsetForValue(static_cast<double>(Event.BitOffset + Event.BitSize));
	if (EventX2 < 0)
	{
		return;
	}

	// Events are displayed with minimum 1px (including empty ones).
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

	constexpr float Y0 = 0.0f;
	constexpr float EventH = 14.0f;
	constexpr float EventDY = 2.0f;

	const float EventY = Y0 + (EventH + EventDY) * Event.Level;

	if (Mode == EHighlightMode::Hovered)
	{
		const FLinearColor Color(1.0f, 1.0f, 0.0f, 1.0f); // yellow

		// Draw border around the timing event box.
		DrawContext.DrawBox(EventX1 - 2.0f, EventY - 2.0f, EventW + 4.0f, EventH + 4.0f, HoveredEventBorderBrush, Color);
	}
	else // EHighlightMode::Selected or EHighlightMode::SelectedAndHovered
	{
		// Animate color from white (if selected and hovered) or yellow (if only selected) to black, using a squared sine function.
		const double Time = static_cast<double>(FPlatformTime::Cycles64()) * FPlatformTime::GetSecondsPerCycle64();
		float S = FMath::Sin(2.0 * Time);
		S = S * S; // squared, to ensure only positive [0 - 1] values
		const float Blue = (Mode == EHighlightMode::SelectedAndHovered) ? 0.0f : S;
		const FLinearColor Color(S, S, Blue, 1.0f);

		// Draw border around the timing event box.
		DrawContext.DrawBox(EventX1 - 2.0f, EventY - 2.0f, EventW + 4.0f, EventH + 4.0f, SelectedEventBorderBrush, Color);
	}
	DrawContext.LayerId++;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
