// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Insights/ViewModels/TimingEventsTrack.h"

class FGameplaySharedData;
namespace Trace { class IAnalysisSession; }
namespace Insights { class ITimingViewSession; }
namespace Insights { enum class ETimeChangedFlags : int32; }
class FSkeletalMeshPoseTrack;
class FAnimationTickRecordsTrack;
class FMenuBuilder;
class UWorld;

class FAnimationSharedData
{
public:
	FAnimationSharedData(FGameplaySharedData& InGameplaySharedData);

	void OnBeginSession(Insights::ITimingViewSession& InTimingViewSession);
	void OnEndSession(Insights::ITimingViewSession& InTimingViewSession);
	void Tick(Insights::ITimingViewSession& InTimingViewSession, const Trace::IAnalysisSession& InAnalysisSession);
	void ExtendFilterMenu(FMenuBuilder& InMenuBuilder);

#if WITH_ENGINE
	void DrawPoses(UWorld* InWorld);
#endif

	// Check whether animation tracks are enabled
	bool AreAnimationTracksEnabled() const;

	// Get the last cached analysis session
	const Trace::IAnalysisSession& GetAnalysisSession() const { return *AnalysisSession; }

	// Check whether the analysis session is valid
	bool IsAnalysisSessionValid() const { return AnalysisSession != nullptr; }

	// Get the gameplay shared data we are linked to
	const FGameplaySharedData& GetGameplaySharedData() const { return GameplaySharedData; }

	// Enumerate skeletal mesh pose tracks
	void EnumerateSkeletalMeshPoseTracks(TFunctionRef<void(const TSharedRef<FSkeletalMeshPoseTrack>&)> InCallback) const;

private:
	// UI handlers
	void ToggleAnimationTracks();
	void OnTimeMarkerChanged(Insights::ETimeChangedFlags InFlags, double InTimeMarker);
	void ToggleAnimationParentTracks();
	bool AreAnimationParentTracksEnabled() const;

private:
	// The gameplay shared data we are linked to
	FGameplaySharedData& GameplaySharedData;

	// Cached analysis session, set in Tick()
	const Trace::IAnalysisSession* AnalysisSession;

	// All the tracks we manage
	TArray<TSharedRef<FSkeletalMeshPoseTrack>> SkeletalMeshPoseTracks;
	TArray<TSharedRef<FAnimationTickRecordsTrack>> AnimationTickRecordsTracks;

	// Delegate handles for hooks into the timing view
	FDelegateHandle TimeMarkerChangedHandle;

	/** Various times and ranges */
	double MarkerTime;

	/** Validity flags for pose times/ranges */
	bool bTimeMarkerValid;

	// Whether all of our animation tracks are enabled
	bool bAnimationTracksEnabled;
};