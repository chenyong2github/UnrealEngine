// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Math/Color.h"
#include "Insights/ITimingViewDrawHelper.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FSlateBrush;
struct FDrawContext;

class FTimingEventsTrack;
struct FTimingEventsTrackLayout;
class FTimingTrackViewport;

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTimingViewDrawHelper final : public ITimingViewDrawHelper
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

		FStats()
			: NumEvents(0)
			, NumDrawBoxes(0)
			, NumMergedBoxes(0)
			, NumDrawBorders(0)
			, NumDrawTexts(0)
		{}
	};

public:
	explicit FTimingViewDrawHelper(const FDrawContext& InDrawContext, const FTimingTrackViewport& InViewport, const FTimingEventsTrackLayout& InLayout);
	~FTimingViewDrawHelper();

	/**
	 * Non-copyable
	 */
	FTimingViewDrawHelper(const FTimingViewDrawHelper&) = delete;
	FTimingViewDrawHelper& operator=(const FTimingViewDrawHelper&) = delete;

	// ITimingViewDrawHelper interface
	virtual const FDrawContext& GetDrawContext() const override { return DrawContext; }
	virtual const FTimingTrackViewport& GetViewport() const override { return Viewport; }
	virtual const FTimingEventsTrackLayout& GetLayout() const override { return Layout; }
	virtual bool BeginTimeline(FTimingEventsTrack& Track) override;
	virtual void AddEvent(double StartTime, double EndTime, uint32 Depth, const TCHAR* EventName, uint32 Color = 0) override;
	virtual void EndTimeline(FTimingEventsTrack& Track) override;

	const FSlateBrush* GetWhiteBrush() const { return WhiteBrush; }
	const FSlateFontInfo& GetEventFont() const { return EventFont; }

	int32 GetNumEvents() const      { return Stats.NumEvents; }
	int32 GetNumDrawBoxes() const   { return Stats.NumDrawBoxes; }
	int32 GetNumMergedBoxes() const { return Stats.NumMergedBoxes; }
	int32 GetNumDrawBorders() const { return Stats.NumDrawBorders; }
	int32 GetNumDrawTexts() const   { return Stats.NumDrawTexts; }

	void DrawBackground() const;
	void DrawTimingEventHighlight(double StartTime, double EndTime, float Y, EHighlightMode Mode);

	//TODO: move the following in a Builder class
	void BeginTimelines();
	void EndTimelines();

private:
	void DrawBox(const FBoxData& Box, const float EventY, const float EventH);

private:
	const FDrawContext& DrawContext;
	const FTimingTrackViewport& Viewport;
	const FTimingEventsTrackLayout& Layout;

	const FSlateBrush* WhiteBrush;
	const FSlateBrush* EventBorderBrush;
	const FSlateBrush* HoveredEventBorderBrush;
	const FSlateBrush* SelectedEventBorderBrush;
	const FSlateBrush* BackgroundAreaBrush;
	const FLinearColor ValidAreaColor;
	const FLinearColor InvalidAreaColor;
	const FLinearColor EdgeColor;
	const FSlateFontInfo EventFont;

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
