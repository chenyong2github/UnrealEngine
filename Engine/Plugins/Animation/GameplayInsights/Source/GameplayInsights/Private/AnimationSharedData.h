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
class ULineBatchComponent;

class FAnimationSharedData
{
public:
	FAnimationSharedData(FGameplaySharedData& InGameplaySharedData);

	void OnBeginSession(Insights::ITimingViewSession& InTimingViewSession);
	void OnEndSession(Insights::ITimingViewSession& InTimingViewSession);
	void Tick(Insights::ITimingViewSession& InTimingViewSession, const Trace::IAnalysisSession& InAnalysisSession);
	void ExtendFilterMenu(FMenuBuilder& InMenuBuilder);

#if WITH_ENGINE
	void DrawPoses(ULineBatchComponent* InLineBatcher);
#endif

	// Check whether animation tracks are enabled
	bool AreAnimationTracksEnabled() const;

	// Get the last cached analysis session
	const Trace::IAnalysisSession& GetAnalysisSession() const { return *AnalysisSession; }

	// Check whether the analysis session is valid
	bool IsAnalysisSessionValid() const { return AnalysisSession != nullptr; }

private:
	// UI handlers
	void ToggleAnimationTracks();
	void OnSelectedEventChanged(const TSharedPtr<const ITimingEvent> InEvent);
	void OnHoveredEventChanged(const TSharedPtr<const ITimingEvent> InEvent);
	void OnTimeMarkerChanged(Insights::ETimeChangedFlags InFlags, double InTimeMarker);
	void OnSelectionChanged(Insights::ETimeChangedFlags InFlags, double InStartTime, double InEndTime);

private:
	// The gameplay shared data we are linked to
	FGameplaySharedData& GameplaySharedData;

	// Cached analysis session, set in Tick()
	const Trace::IAnalysisSession* AnalysisSession;

	// All the tracks we manage
	TArray<TSharedRef<FSkeletalMeshPoseTrack>> SkeletalMeshPoseTracks;
	TArray<TSharedRef<FAnimationTickRecordsTrack>> AnimationTickRecordsTracks;

	// Delegate handles for hooks into the timing view
	FDelegateHandle SelectedEventChangedHandle;
	FDelegateHandle HoveredEventChangedHandle;
	FDelegateHandle TimeMarkerChangedHandle;
	FDelegateHandle SelectionChangedHandle;

	/** Selected/hovered tracks */
	TSharedPtr<const FBaseTimingTrack> SelectedEventTrack;
	TSharedPtr<const FBaseTimingTrack> HoveredEventTrack;

	/** Various times and ranges */
	double SelectedEventStartTime;
	double SelectedEventEndTime;
	double HoveredEventStartTime;
	double HoveredEventEndTime;
	double SelectionStartTime;
	double SelectionEndTime;
	double MarkerTime;

	/** Validity flags for pose times/ranges */
	bool bSelectedEventValid;
	bool bHoveredEventValid;
	bool bSelectionValid;
	bool bTimeMarkerValid;

	// Whether all of our animation tracks are enabled
	bool bAnimationTracksEnabled;
};