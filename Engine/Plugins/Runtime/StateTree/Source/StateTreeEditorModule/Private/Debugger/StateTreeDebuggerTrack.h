// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_STATETREE_DEBUGGER

#include "RewindDebuggerTrack.h"
#include "SStateTreeDebuggerEventTimelineView.h"
#include "StateTreeTypes.h"

struct FStateTreeDebugger;

/**
 * Struct use to store timeline events for a single StateTree instance
 * This is currently not tied to RewindDebugger but we use this base
 * type to facilitate a future integration.
 */
struct FStateTreeDebuggerTrack : RewindDebugger::FRewindDebuggerTrack
{
	FStateTreeDebuggerTrack(const TSharedPtr<FStateTreeDebugger>& InDebugger, const FStateTreeInstanceDebugId InInstanceId, const FText InName, const TRange<double>& InViewRange);
	
	FStateTreeInstanceDebugId GetInstanceId() const { return InstanceId; }
	bool IsStale() const { return bIsStale; }
	void MarkAsStale() { bIsStale = true; }

private:
	virtual bool UpdateInternal() override;
	virtual TSharedPtr<SWidget> GetDetailsViewInternal() override;
	virtual TSharedPtr<SWidget> GetTimelineViewInternal() override;
	virtual FSlateIcon GetIconInternal() override { return Icon; }
	virtual FText GetDisplayNameInternal() const override { return TrackName; }

	TSharedPtr<SStateTreeDebuggerEventTimelineView::FTimelineEventData> EventData;
	
	FSlateIcon Icon;
	FText TrackName;

	TSharedPtr<FStateTreeDebugger> StateTreeDebugger;
	FStateTreeInstanceDebugId InstanceId;
	const TRange<double>& ViewRange;
	bool bIsStale = false;
};

#endif // WITH_STATETREE_DEBUGGER