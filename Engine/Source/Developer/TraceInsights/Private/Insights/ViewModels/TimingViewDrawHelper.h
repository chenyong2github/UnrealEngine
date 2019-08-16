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

class FTimingViewDrawHelper
{
public:
	enum class EHighlightMode
	{
		Hovered,
		Selected,
		SelectedAndHovered
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
	const FSlateFontInfo EventFont;

	//static const FSlateColorBrush SolidWhiteBrush = FSlateColorBrush(FColorList::White);
	//static const FSlateBrush BorderBrush = FSlateBorderBrush(NAME_None, FMargin(1.0f));

	mutable float ValidX0;
	mutable float ValidX1;

	float TimelineTopY;
	float TimelineY;
	int32 MaxDepth;
	int32 TimelineIndex;

	TArray<float> LastEventX2; // X2 value for last event on each depth, for current timeline
	TArray<FBoxData> LastBox;

	/** Debug stats */
	mutable FStats Stats;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
