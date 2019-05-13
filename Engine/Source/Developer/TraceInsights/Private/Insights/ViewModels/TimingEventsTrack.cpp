// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TimingEventsTrack.h"

#define LOCTEXT_NAMESPACE "TimingEventsTrack"

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTimingEventsTrackLayout
////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingEventsTrackLayout::ForceNormalMode()
{
	bIsCompactMode = false;
	EventH = NormalLayoutEventH;
	EventDY = NormalLayoutEventDY;
	TimelineDY = NormalLayoutTimelineDY;
	MinTimelineH = NormalLayoutMinTimelineH;
	TargetMinTimelineH = NormalLayoutMinTimelineH;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingEventsTrackLayout::ForceCompactMode()
{
	bIsCompactMode = true;
	EventH = CompactLayoutEventH;
	EventDY = CompactLayoutEventDY;
	TimelineDY = CompactLayoutTimelineDY;
	MinTimelineH = CompactLayoutMinTimelineH;
	TargetMinTimelineH = CompactLayoutMinTimelineH;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingEventsTrackLayout::Update()
{
	const float LayoutTransitionSpeed = 0.25f;

	if (bIsCompactMode)
	{
		if (EventH > CompactLayoutEventH)
			EventH -= LayoutTransitionSpeed;
		if (EventDY > CompactLayoutEventDY)
			EventDY -= LayoutTransitionSpeed;
		if (TimelineDY > CompactLayoutTimelineDY)
			TimelineDY -= LayoutTransitionSpeed;
	}
	else
	{
		if (EventH < NormalLayoutEventH)
			EventH += LayoutTransitionSpeed;
		if (EventDY < NormalLayoutEventDY)
			EventDY += LayoutTransitionSpeed;
		if (TimelineDY < NormalLayoutTimelineDY)
			TimelineDY += LayoutTransitionSpeed;
	}

	if (MinTimelineH > TargetMinTimelineH)
		MinTimelineH -= LayoutTransitionSpeed;
	else if (MinTimelineH < TargetMinTimelineH)
		MinTimelineH += LayoutTransitionSpeed;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTimingEventsTrack
////////////////////////////////////////////////////////////////////////////////////////////////////

FTimingEventsTrack::FTimingEventsTrack(uint64 InTrackId)
	: FBaseTimingTrack(InTrackId)
	, Depth(-1)
	, bIsCollapsed(false)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimingEventsTrack::~FTimingEventsTrack()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingEventsTrack::Reset()
{
	FBaseTimingTrack::Reset();

	Depth = -1;
	bIsCollapsed = false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingEventsTrack::UpdateHoveredState(float MX, float MY, const FTimingTrackViewport& Viewport)
{
	if (MY >= Y && MY < Y + H)
	{
		bIsHovered = true;

		if (MX < 100.0f && MY < Y + 14.0f)
		{
			bIsHeaderHovered = true;
		}
		else
		{
			bIsHeaderHovered = false;
		}
	}
	else
	{
		bIsHovered = false;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
