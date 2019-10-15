// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FTimingEventsTrack;
namespace Insights { class ITimingViewVisualizer; }
struct FTimingEvent;

namespace Insights
{

enum class ETimeChangedFlags : int32
{
	None,

	// The event fired in response to an interactive change from the user. Will be followed by a non-interactive change finished.
	Interactive = (1 << 0)
};

ENUM_CLASS_FLAGS(ETimeChangedFlags);

/** The delegate to be invoked when the time marker has changed */
DECLARE_MULTICAST_DELEGATE_TwoParams(FTimeMarkerChangedDelegate, ETimeChangedFlags /*InFlags*/, double /*TimeMarker*/);

/** The delegate to be invoked when the selection have been changed. */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FSelectionChangedDelegate, ETimeChangedFlags /*InFlags*/, double /*StartTime*/, double /*EndTime*/);

/** The delegate to be invoked when the timing event being hovered by the mouse has changed. */
DECLARE_MULTICAST_DELEGATE_OneParam(FHoveredEventChangedDelegate, const FTimingEvent& /*InEvent*/);

/** The delegate to be invoked when the selected timing event has changed. */
DECLARE_MULTICAST_DELEGATE_OneParam(FSelectedEventChangedDelegate, const FTimingEvent& /*InEvent*/);

/** Hosts a number of timing view visualizers, represents a session of the timing view */
class ITimingViewSession
{
public:
	virtual ~ITimingViewSession() = default;

	/** Adds a new timing events track. Transfers ownership of the track pointer to the session. */
	virtual void AddTimingEventsTrack(FTimingEventsTrack* InTrack) = 0;

	/** Find a timing events track has been added via AddTimingEventsTrack() */
	virtual FTimingEventsTrack* FindTimingEventsTrack(uint64 InTrackID) = 0;

	/** Get the delegate to be invoked when the time marker has changed */
	virtual FTimeMarkerChangedDelegate& OnTimeMarkerChanged() = 0;

	/** Get the delegate to be invoked when the selection have been changed. */
	virtual FSelectionChangedDelegate& OnSelectionChanged() = 0;

	/** Get the delegate to be invoked when the timing event being hovered by the mouse has changed. */
	virtual FHoveredEventChangedDelegate& OnHoveredEventChanged() = 0;

	/** Get the delegate to be invoked when the selected timing event has changed. */
	virtual FSelectedEventChangedDelegate& OnSelectedEventChanged() = 0;
};

}