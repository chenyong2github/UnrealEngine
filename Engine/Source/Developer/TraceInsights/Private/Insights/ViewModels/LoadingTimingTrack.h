// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TraceServices/AnalysisService.h"

// Insights
#include "Insights/ITimingViewExtender.h"
#include "Insights/ViewModels/TimingEventsTrack.h"

class FTimingEventSearchParameters;
class STimingView;
class FLoadingTimingTrack;

/** Defines FLoadingTrackGetEventNameDelegate delegate interface. Returns the name for a timing event in a Loading track. */
DECLARE_DELEGATE_RetVal_TwoParams(const TCHAR*, FLoadingTrackGetEventNameDelegate, uint32 /*Depth*/, const Trace::FLoadTimeProfilerCpuEvent& /*Event*/);

////////////////////////////////////////////////////////////////////////////////////////////////////

class FLoadingSharedState : public Insights::ITimingViewExtender, public TSharedFromThis<FLoadingSharedState>
{
public:
	explicit FLoadingSharedState(STimingView* InTimingView) : TimingView(InTimingView) {}
	virtual ~FLoadingSharedState() = default;

	// ITimingViewExtender
	virtual void OnBeginSession(Insights::ITimingViewSession& InSession) override;
	virtual void OnEndSession(Insights::ITimingViewSession& InSession) override;
	virtual void Tick(Insights::ITimingViewSession& InSession, const Trace::IAnalysisSession& InAnalysisSession) override;
	virtual void ExtendFilterMenu(Insights::ITimingViewSession& InSession, FMenuBuilder& InMenuBuilder) override;

	const TCHAR* GetEventName(uint32 Depth, const Trace::FLoadTimeProfilerCpuEvent& Event) const;
	void SetColorSchema(int32 Schema);

	TSharedPtr<FLoadingTimingTrack> GetLoadingTrack(uint32 InThreadId)
	{
		TSharedPtr<FLoadingTimingTrack>* TrackPtrPtr = LoadingTracks.Find(InThreadId);
		return TrackPtrPtr ? *TrackPtrPtr : nullptr;
	}

	bool IsAllLoadingTracksToggleOn() const { return bShowHideAllLoadingTracks; }
	void SetAllLoadingTracksToggle(bool bOnOff);
	void ShowAllLoadingTracks() { SetAllLoadingTracksToggle(true); }
	void HideAllLoadingTracks() { SetAllLoadingTracksToggle(false); }
	void ShowHideAllLoadingTracks() { SetAllLoadingTracksToggle(!IsAllLoadingTracksToggleOn()); }

private:
	const TCHAR* GetEventNameByEventType(uint32 Depth, const Trace::FLoadTimeProfilerCpuEvent& Event) const;
	const TCHAR* GetEventNameByPackageName(uint32 Depth, const Trace::FLoadTimeProfilerCpuEvent& Event) const;
	const TCHAR* GetEventNameByExportClassName(uint32 Depth, const Trace::FLoadTimeProfilerCpuEvent& Event) const;
	const TCHAR* GetEventNameByPackageAndExportClassName(uint32 Depth, const Trace::FLoadTimeProfilerCpuEvent& Event) const;

private:
	STimingView* TimingView;

	bool bShowHideAllLoadingTracks;

	/** Maps thread id to track pointer. */
	TMap<uint32, TSharedPtr<FLoadingTimingTrack>> LoadingTracks;

	uint64 LoadTimeProfilerTimelineCount;

	FLoadingTrackGetEventNameDelegate GetEventNameDelegate;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FLoadingTimingTrack : public FTimingEventsTrack
{
	INSIGHTS_DECLARE_RTTI(FLoadingTimingTrack, FTimingEventsTrack)

public:
	explicit FLoadingTimingTrack(FLoadingSharedState& InSharedState, uint32 InTimelineIndex, const FString& InName)
		: FTimingEventsTrack(InName)
		, SharedState(InSharedState)
		, TimelineIndex(InTimelineIndex)
	{
	}
	virtual ~FLoadingTimingTrack() {}

	virtual void BuildDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context) override;

	virtual void InitTooltip(FTooltipDrawState& InOutTooltip, const ITimingEvent& InTooltipEvent) const override;

	virtual const TSharedPtr<const ITimingEvent> SearchEvent(const FTimingEventSearchParameters& InSearchParameters) const override;

protected:
	// Helper function to find an event given search parameters
	bool FindLoadTimeProfilerCpuEvent(const FTimingEventSearchParameters& InParameters, TFunctionRef<void(double, double, uint32, const Trace::FLoadTimeProfilerCpuEvent&)> InFoundPredicate) const;

protected:
	FLoadingSharedState& SharedState;
	uint32 TimelineIndex;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
