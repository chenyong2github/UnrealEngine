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
	void OnTracksChanged(int32& InOutOrder);

	// Helper function. Builds object track hierarchy on-demand and returns a track for the supplied object info.
	FObjectEventsTrack* GetObjectEventsTrackForId(Insights::ITimingViewSession& InTimingViewSession, const Trace::IAnalysisSession& InAnalysisSession, const FObjectInfo& InObjectInfo);

	// Check whether gameplay tacks are enabled
	bool AreGameplayTracksEnabled() const;

	// Get the last cached analysis session
	const Trace::IAnalysisSession& GetAnalysisSession() const { return *AnalysisSession; }

private:
	// UI handlers
	void ToggleGameplayTracks();

private:
	// Track for each tracked object, mapped from Object ID -> track
	TMap<uint64, FObjectEventsTrack*> ObjectTracks;

	// Cached analysis session, set in Tick()
	const Trace::IAnalysisSession* AnalysisSession;

	// Whether all of our object tracks are enabled
	bool bObjectTracksEnabled;
};