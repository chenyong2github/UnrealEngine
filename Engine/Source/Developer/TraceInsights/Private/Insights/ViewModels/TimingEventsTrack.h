// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Insights/ViewModels/BaseTimingTrack.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

static const float RealMinTimelineH = 13.0f;

static const float NormalLayoutEventH = 14.0f;
static const float NormalLayoutEventDY = 2.0f;
static const float NormalLayoutTimelineDY = 14.0f;
static const float NormalLayoutMinTimelineH = 0.0f;

static const float CompactLayoutEventH = 2.0f;
static const float CompactLayoutEventDY = 1.0f;
static const float CompactLayoutTimelineDY = 3.0f;
static const float CompactLayoutMinTimelineH = 0.0f;

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FTimingEventsTrackLayout
{
	bool bIsCompactMode;

	float EventH; // height of a timing event, in Slate units
	float EventDY; // vertical distance between two timing event sub-tracks, in Slate units
	float TimelineDY; // space at top and bottom of each timeline, in Slate units
	float MinTimelineH;
	float TargetMinTimelineH;

	float GetLaneY(uint32 Depth) const { return 1.0f + TimelineDY + Depth * (EventDY + EventH); }

	void ForceNormalMode();
	void ForceCompactMode();
	void Update();
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTimingEventsTrack : public FBaseTimingTrack
{
public:
	FTimingEventsTrack(uint64 InTrackId);
	virtual ~FTimingEventsTrack();

	virtual void Reset() override;
	virtual void UpdateHoveredState(float MX, float MY, const FTimingTrackViewport& Viewport);

public:
	int32 Depth; // how many lanes has this track
	bool bIsCollapsed;

	// Cached OnPaint state.
	//TArray<FEventBoxInfo> Boxes;
	//TArray<FEventBoxInfo> MergedBorderBoxes;
	//TArray<FEventBoxInfo> Borders;
	//TArray<FTextInfo> Texts;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
