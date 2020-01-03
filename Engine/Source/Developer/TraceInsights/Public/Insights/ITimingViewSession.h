// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FBaseTimingTrack;
class FTimingEventsTrack;
class ITimingEvent;

namespace Insights
{

enum class ETimeChangedFlags : int32
{
	None,

	// The event fired in response to an interactive change from the user. Will be followed by a non-interactive change finished.
	Interactive = (1 << 0)
};
ENUM_CLASS_FLAGS(ETimeChangedFlags);

/** The delegate to be invoked when the selection have been changed */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FSelectionChangedDelegate, ETimeChangedFlags /*InFlags*/, double /*StartTime*/, double /*EndTime*/);

/** The delegate to be invoked when the time marker has changed */
DECLARE_MULTICAST_DELEGATE_TwoParams(FTimeMarkerChangedDelegate, ETimeChangedFlags /*InFlags*/, double /*TimeMarker*/);

/** The delegate to be invoked when the timing track being hovered by the mouse has changed */
DECLARE_MULTICAST_DELEGATE_OneParam(FHoveredTrackChangedDelegate, const TSharedPtr<FBaseTimingTrack> /*InTrack*/);

/** The delegate to be invoked when the timing event being hovered by the mouse has changed */
DECLARE_MULTICAST_DELEGATE_OneParam(FHoveredEventChangedDelegate, const TSharedPtr<const ITimingEvent> /*InEvent*/);

/** The delegate to be invoked when the selected timing track has changed */
DECLARE_MULTICAST_DELEGATE_OneParam(FSelectedTrackChangedDelegate, const TSharedPtr<FBaseTimingTrack> /*InTrack*/);

/** The delegate to be invoked when the selected timing event has changed */
DECLARE_MULTICAST_DELEGATE_OneParam(FSelectedEventChangedDelegate, const TSharedPtr<const ITimingEvent> /*InEvent*/);

/** Hosts a number of timing view visualizers, represents a session of the timing view. */
class TRACEINSIGHTS_API ITimingViewSession
{
public:
	virtual ~ITimingViewSession() = default;

	/** Adds a new top docked track. */
	virtual void AddTopDockedTrack(TSharedPtr<FBaseTimingTrack> Track) = 0;
	/** Adds a new bottom docked track. */
	virtual void AddBottomDockedTrack(TSharedPtr<FBaseTimingTrack> Track) = 0;
	/** Adds a new scrollable track. */
	virtual void AddScrollableTrack(TSharedPtr<FBaseTimingTrack> Track) = 0;
	/** Adds a new foreground track. */
	virtual void AddForegroundTrack(TSharedPtr<FBaseTimingTrack> Track) = 0;

	/** Prevents mouse movements from throttling application updates */
	virtual void PreventThrottling() = 0;

	/** Marks the scrollable tracks as not being in the correct order so they will be re-sorted */
	virtual void InvalidateScrollableTracksOrder() = 0;

	/** Finds a track has been added via Add*Track(). */
	virtual TSharedPtr<FBaseTimingTrack> FindTrack(uint64 InTrackId) = 0;

	/** Gets the delegate to be invoked when the selection have been changed. */
	virtual FSelectionChangedDelegate& OnSelectionChanged() = 0;

	/** Gets the delegate to be invoked when the time marker has changed. */
	virtual FTimeMarkerChangedDelegate& OnTimeMarkerChanged() = 0;

	/** Gets the delegate to be invoked when the timing track being hovered by the mouse has changed. */
	virtual FHoveredTrackChangedDelegate& OnHoveredTrackChanged() = 0;

	/** Gets the delegate to be invoked when the timing event being hovered by the mouse has changed. */
	virtual FHoveredEventChangedDelegate& OnHoveredEventChanged() = 0;

	/** Gets the delegate to be invoked when the selected timing track has changed. */
	virtual FSelectedTrackChangedDelegate& OnSelectedTrackChanged() = 0;

	/** Gets the delegate to be invoked when the selected timing event has changed. */
	virtual FSelectedEventChangedDelegate& OnSelectedEventChanged() = 0;
};

} // namespace Insights
