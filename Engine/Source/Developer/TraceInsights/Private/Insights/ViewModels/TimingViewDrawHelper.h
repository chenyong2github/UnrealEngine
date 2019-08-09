// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Math/Color.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FSlateBrush;
struct FDrawContext;

class FTimingEventsTrack;
struct FTimingEventsTrackLayout;
class FTimingTrackViewport;

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FTimingViewTooltip
{
	static constexpr float BorderX = 6.0f;
	static constexpr float BorderY = 3.0f;
	static constexpr float MinWidth = 128.0f;
	static constexpr float MinHeight = 0.0f;

	FTimingViewTooltip() : PosX(0.0f), PosY(0.0f), Width(FTimingViewTooltip::MinWidth), Height(FTimingViewTooltip::MinHeight), Opacity(0.0f) {}

	void Reset()
	{
		PosX = 0.0f;
		PosY = 0.0f;
		Width = MinWidth;
		Height = MinHeight;
		Opacity = 0.0f;
	}

	void Update(const FVector2D& MousePosition, const float DesiredWidth, const float DesiredHeight, const float ViewportWidth, const float ViewportHeight)
	{
		if (Width != DesiredWidth)
		{
			Width = Width * 0.75f + DesiredWidth * 0.25f;
		}
		
		Height = DesiredHeight;

		const float MaxX = FMath::Max(0.0f, ViewportWidth - Width - 12.0f); // -12.0f is to avoid overlapping the vertical scrollbar (one on the right side of the view)
		const float X = FMath::Clamp<float>(MousePosition.X + 12.0f, 0.0f, MaxX);
		PosX = X;

		const float MaxY = FMath::Max(0.0f, ViewportHeight - Height - 12.0f); // -12.0f is to avoid overlapping the horizontal scrollbar (one on the bottom of the view)
		float Y = FMath::Clamp<float>(MousePosition.Y + 15.0f, 0.0f, MaxY);
		PosY = Y;

		const float DesiredOpacity = 1.0f - FMath::Abs(Width - DesiredWidth) / DesiredWidth;
		if (Opacity < DesiredOpacity)
		{
			Opacity = Opacity * 0.9f + DesiredOpacity * 0.1f;
		}
		else
		{
			Opacity = DesiredOpacity;
		}
	}

	float PosX;
	float PosY;
	float Width;
	float Height;
	float Opacity;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTimingViewDrawHelper
{
public:
	enum class EHighlightMode : uint32
	{
		Hovered = 1,
		Selected = 2,
		SelectedAndHovered = 3
	};

private:
	enum class EDrawLayer : int32
	{
		EventBorder,
		EventFill,
		EventText,
		TimelineHeader,
		TimelineText,

		Count,
	};
	static int32 ToInt32(EDrawLayer Layer) { return static_cast<int32>(Layer); }

	struct FBoxData
	{
		float X1;
		float X2;
		uint32 Color;
		FLinearColor LinearColor;

		FBoxData() : X1(0.0f), X2(0.0f), Color(0) {}
		void Reset() { X1 = 0.0f; X2 = 0.0f; Color = 0; }
	};

	//struct FEventBoxInfo
	//{
	//	float X1;
	//	float X2;
	//	int32 Depth;
	//	uint32 Color;
	//}

	//struct FEventTextInfo
	//{
	//	float X;
	//	float Y;
	//	FString Text;
	//	bool bUseDarkTextColor; // true if text needs to be displayed in Black, otherwise will be displayed in White
	//};

	struct FStats
	{
		int32 NumEvents;
		int32 NumDrawBoxes;
		int32 NumMergedBoxes;
		int32 NumDrawBorders;
		int32 NumDrawTexts;
		int32 NumDrawTimeMarkerBoxes;
		int32 NumDrawTimeMarkerTexts;

		FStats()
			: NumEvents(0)
			, NumDrawBoxes(0)
			, NumMergedBoxes(0)
			, NumDrawBorders(0)
			, NumDrawTexts(0)
			, NumDrawTimeMarkerBoxes(0)
			, NumDrawTimeMarkerTexts(0)
		{}
	};

public:
	FTimingViewDrawHelper(const FDrawContext& InDrawContext, const FTimingTrackViewport& InViewport, const FTimingEventsTrackLayout& InLayout);
	~FTimingViewDrawHelper();

	/**
	 * Non-copyable
	 */
	FTimingViewDrawHelper(const FTimingViewDrawHelper&) = delete;
	FTimingViewDrawHelper& operator=(const FTimingViewDrawHelper&) = delete;

	const FDrawContext& GetDrawContext() const { return DrawContext; }
	const FTimingTrackViewport& GetViewport() const { return Viewport; }
	const FTimingEventsTrackLayout& GetLayout() const { return Layout; }

	const FSlateBrush* GetWhiteBrush() const { return WhiteBrush; }
	const FSlateBrush* GetBorderBrush() const { return BorderBrush; }
	const FSlateBrush* GetEventsBorderBrush() const { return EventsBorderBrush; }
	const FSlateFontInfo& GetEventFont() const { return EventFont; }

	int32 GetNumEvents() const              { return Stats.NumEvents; }
	int32 GetNumDrawBoxes() const           { return Stats.NumDrawBoxes; }
	int32 GetNumMergedBoxes() const         { return Stats.NumMergedBoxes; }
	int32 GetNumDrawBorders() const         { return Stats.NumDrawBorders; }
	int32 GetNumDrawTexts() const           { return Stats.NumDrawTexts; }
	int32 GetNumDrawTimeMarkerBoxes() const { return Stats.NumDrawTimeMarkerBoxes; }
	int32 GetNumDrawTimeMarkerTexts() const { return Stats.NumDrawTimeMarkerTexts; }

	void DrawBackground() const;
	void DrawTimingEventHighlight(double StartTime, double EndTime, float Y, EHighlightMode Mode);

	//TODO: move the following in a Builder class
	void BeginTimelines();
	bool BeginTimeline(FTimingEventsTrack& Track);
	void AddEvent(double StartTime, double EndTime, uint32 Depth, const TCHAR* EventName, uint32 Color = 0);
	void EndTimeline(FTimingEventsTrack& Track);
	void EndTimelines();

private:
	void DrawBox(const FBoxData& Box, const float EventY, const float EventH);

private:
	const FDrawContext& DrawContext;
	const FTimingTrackViewport& Viewport;
	const FTimingEventsTrackLayout& Layout;

	const FSlateBrush* WhiteBrush;
	const FSlateBrush* BorderBrush;
	const FSlateBrush* EventsBorderBrush;
	const FSlateBrush* BackgroundAreaBrush;
	const FLinearColor ValidAreaColor;
	const FLinearColor InvalidAreaColor;
	const FLinearColor EdgeColor;
	const FSlateFontInfo EventFont;

	//static const FSlateColorBrush SolidWhiteBrush = FSlateColorBrush(FColorList::White);
	//static const FSlateBrush BorderBrush = FSlateBorderBrush(NAME_None, FMargin(1.0f));

	mutable float ValidAreaX;
	mutable float ValidAreaW;

	//////////////////////////////////////////////////
	// Builder state

	float TimelineTopY;
	float TimelineY;
	int32 MaxDepth;
	int32 TimelineIndex;

	TArray<float> LastEventX2; // X2 value for last event on each depth, for current timeline
	TArray<FBoxData> LastBox;

	//////////////////////////////////////////////////

	/** Debug stats */
	mutable FStats Stats;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
