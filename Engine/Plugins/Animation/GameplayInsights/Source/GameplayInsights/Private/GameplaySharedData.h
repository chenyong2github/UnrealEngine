// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Insights/ViewModels/TimingEventsTrack.h"

namespace Trace { class IAnalysisSession; }
namespace Insights { class ITimingViewSession; }
class FObjectEventsTrack;
class FSkeletalMeshPoseTrack;
class FAnimationTickRecordsTrack;
struct FObjectInfo;

class FGameplaySharedData
{
public:
	FGameplaySharedData();

	void OnBeginSession(Insights::ITimingViewSession& InTimingViewSession);
	void OnEndSession(Insights::ITimingViewSession& InTimingViewSession);
	void Tick(Insights::ITimingViewSession& InTimingViewSession, const Trace::IAnalysisSession& InAnalysisSession);
	void ExtendFilterMenu(FMenuBuilder& InMenuBuilder);

	// Helper function. Builds object track hierarchy on-demand and returns a track for the supplied object info.
	TSharedRef<FObjectEventsTrack> GetObjectEventsTrackForId(Insights::ITimingViewSession& InTimingViewSession, const Trace::IAnalysisSession& InAnalysisSession, const FObjectInfo& InObjectInfo);

	// Check whether gameplay tacks are enabled
	bool AreGameplayTracksEnabled() const;

	// Invalidate object trcks order, so they get re-sorted next tick
	void InvalidateObjectTracksOrder() { bObjectTracksDirty = true; }

	// Get the last cached analysis session
	const Trace::IAnalysisSession& GetAnalysisSession() const { return *AnalysisSession; }

	// Enumerate object tracks
	void EnumerateObjectTracks(TFunctionRef<void(const TSharedRef<FObjectEventsTrack>&)> InCallback) const;

	// Get the root tracks
	const TArray<TSharedRef<FBaseTimingTrack>>& GetRootTracks() const { return RootTracks; }

	// Delegate fired when tracks change
	FSimpleMulticastDelegate& OnTracksChanged() { return OnTracksChangedDelegate; }

private:
	// Re-sort tracks if track ordering has changed
	void SortTracks();

	// UI handlers
	void ToggleGameplayTracks();

private:
	// Track for each tracked object, mapped from Object ID -> track
	TMap<uint64, TSharedPtr<FObjectEventsTrack>> ObjectTracks;

	// The root tracks
	TArray<TSharedRef<FBaseTimingTrack>> RootTracks;

	// Cached analysis session, set in Tick()
	const Trace::IAnalysisSession* AnalysisSession;

	// Delegate fired when tracks change
	FSimpleMulticastDelegate OnTracksChangedDelegate;

	// Dirty flag for adding object tracks, used to trigger re-sorting
	bool bObjectTracksDirty;

	// Whether all of our object tracks are enabled
	bool bObjectTracksEnabled;
};