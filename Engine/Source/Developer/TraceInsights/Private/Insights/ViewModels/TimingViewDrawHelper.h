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

class FTimingViewDrawHelper : public FNoncopyable
{
public:
	enum EHighlightMode
	{
		Hovered,
		Selected,
		SelectedAndHovered
	};

private:
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

public:
	FTimingViewDrawHelper(const FDrawContext& DC, const FTimingTrackViewport& InViewport, const FTimingEventsTrackLayout& Layout);
	~FTimingViewDrawHelper();

	const FTimingTrackViewport& GetViewport() { return Viewport; }

	void Begin();

	void DrawBackground() const;

	//TODO: move the following in a Builder class
	void BeginTimelines();
	bool BeginTimeline(FTimingEventsTrack& Track);
	void AddEvent(double StartTime, double EndTime, uint32 Depth, const TCHAR* EventName, uint32 Color = 0);
	void EndTimeline(FTimingEventsTrack& Track);
	void EndTimelines();

	void DrawTimingEventHighlight(double StartTime, double EndTime, float Y, EHighlightMode Mode);

	void End();

private:
	void DrawBox(const FBoxData& Box, const float EventY, const float EventH);

public:
	const FDrawContext& DC;
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
	int TimelineIndex;

	TArray<float> LastEventX2; // X2 value for last event on each depth, for current timeline
	TArray<FBoxData> LastBox;

	// Debug stats.
	mutable int NumEvents;
	mutable int NumDrawBoxes;
	mutable int NumMergedBoxes;
	mutable int NumDrawBorders;
	mutable int NumDrawTexts;
	mutable int NumDrawTimeMarkerBoxes;
	mutable int NumDrawTimeMarkerTexts;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
