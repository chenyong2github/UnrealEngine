// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Insights
#include "Insights/ITimingViewExtender.h"
#include "Insights/ViewModels/TimingEventsTrack.h"

class FTimingEventSearchParameters;
class STimingView;

////////////////////////////////////////////////////////////////////////////////////////////////////

class FFileActivitySharedState : public Insights::ITimingViewExtender, public TSharedFromThis<FFileActivitySharedState>
{
	friend class FOverviewFileActivityTimingTrack;
	friend class FDetailedFileActivityTimingTrack;

public:
	struct FIoFileActivity
	{
		uint64 Id;
		const TCHAR* Path;
		double StartTime;
		double EndTime;
		int32 EventCount;
		int32 Depth;
	};

	struct FIoTimingEvent
	{
		double StartTime;
		double EndTime;
		uint32 Depth;
		uint32 Type; // Trace::EFileActivityType + "Failed" flag
		uint64 Offset;
		uint64 Size;
		TSharedPtr<FIoFileActivity> FileActivity;
	};

public:
	explicit FFileActivitySharedState(STimingView* InTimingView) : TimingView(InTimingView) {}
	virtual ~FFileActivitySharedState() = default;

	// ITimingViewExtender
	virtual void OnBeginSession(Insights::ITimingViewSession& InSession) override;
	virtual void OnEndSession(Insights::ITimingViewSession& InSession) override;
	virtual void Tick(Insights::ITimingViewSession& InSession, const Trace::IAnalysisSession& InAnalysisSession) override;
	virtual void ExtendFilterMenu(Insights::ITimingViewSession& InSession, FMenuBuilder& InMenuBuilder) override;

	const TArray<FIoTimingEvent>& GetAllEvents() const { return AllIoEvents; }

	void RequestUpdate() { bForceIoEventsUpdate = true; }
	void ToggleMergeLanes() { bMergeIoLanes = !bMergeIoLanes; }
	void ToggleBackgroundEvents();

	void ShowHideAllIoTracks() { ShowHideAllIoTracks_Execute(); }

private:
	bool ShowHideAllIoTracks_IsChecked() const;
	void ShowHideAllIoTracks_Execute();

private:
	STimingView* TimingView;

	TSharedPtr<FTimingEventsTrack> IoOverviewTrack;
	TSharedPtr<FTimingEventsTrack> IoActivityTrack;

	bool bShowHideAllIoTracks;
	bool bForceIoEventsUpdate;
	bool bMergeIoLanes;
	bool bShowFileActivityBackgroundEvents;

	TArray<TSharedPtr<FIoFileActivity>> FileActivities;
	TMap<uint64, TSharedPtr<FIoFileActivity>> FileActivityMap;

	/** All IO events, cached. */
	TArray<FIoTimingEvent> AllIoEvents;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FFileActivityTimingTrack : public FTimingEventsTrack
{
public:
	explicit FFileActivityTimingTrack(FFileActivitySharedState& InSharedState, const FName SubType, const FString& InName)
		: FTimingEventsTrack(FName(TEXT("FileActivity")), SubType, InName)
		, SharedState(InSharedState)
	{
	}
	virtual ~FFileActivityTimingTrack() {}

	virtual void InitTooltip(FTooltipDrawState& InOutTooltip, const ITimingEvent& InTooltipEvent) const override;

protected:
	bool FindIoTimingEvent(const FTimingEventSearchParameters& InParameters, bool bIgnoreEventDepth, TFunctionRef<void(double, double, uint32, const FFileActivitySharedState::FIoTimingEvent&)> InFoundPredicate) const;

protected:
	FFileActivitySharedState& SharedState;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FOverviewFileActivityTimingTrack : public FFileActivityTimingTrack
{
public:
	explicit FOverviewFileActivityTimingTrack(FFileActivitySharedState& InSharedState)
		: FFileActivityTimingTrack(InSharedState, FName(TEXT("Overview")), TEXT("I/O Overview"))
	{
	}

	virtual void BuildDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context) override;
	virtual const TSharedPtr<const ITimingEvent> SearchEvent(const FTimingEventSearchParameters& InSearchParameters) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FDetailedFileActivityTimingTrack : public FFileActivityTimingTrack
{
public:
	explicit FDetailedFileActivityTimingTrack(FFileActivitySharedState& InSharedState)
		: FFileActivityTimingTrack(InSharedState, FName(TEXT("Detailed")), TEXT("I/O Activity"))
	{
	}

	virtual void BuildDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context) override;
	virtual const TSharedPtr<const ITimingEvent> SearchEvent(const FTimingEventSearchParameters& InSearchParameters) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
