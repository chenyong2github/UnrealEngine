// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FDrawContext;
class FTimingTrackViewport;
struct FTimingEventsTrackLayout;
class FTimingEventsTrack;

/** Interface used to draw events into the timing view */
class ITimingViewDrawHelper
{
public:
	virtual const FDrawContext& GetDrawContext() const = 0;
	virtual const FTimingTrackViewport& GetViewport() const = 0;
	virtual const FTimingEventsTrackLayout& GetLayout() const = 0;
	virtual bool BeginTimeline(FTimingEventsTrack& Track) = 0;
	virtual void AddEvent(double StartTime, double EndTime, uint32 Depth, const TCHAR* EventName, uint32 Color = 0) = 0;
	virtual void EndTimeline(FTimingEventsTrack& Track) = 0;
};