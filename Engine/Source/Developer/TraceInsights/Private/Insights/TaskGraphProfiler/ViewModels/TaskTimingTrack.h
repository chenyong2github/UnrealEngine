// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Insights
#include "Insights/ITimingViewExtender.h"
#include "Insights/ViewModels/TimingEventsTrack.h"

class STimingView;

////////////////////////////////////////////////////////////////////////////////////////////////////

namespace Insights
{
class FTaskTimingTrack;

class FTaskTimingSharedState : public Insights::ITimingViewExtender, public TSharedFromThis<FTaskTimingSharedState>
{

public:
	explicit FTaskTimingSharedState(STimingView* InTimingView) : TimingView(InTimingView) {}
	virtual ~FTaskTimingSharedState() = default;

	TSharedPtr<FTaskTimingTrack> GetTaskTrack() { return TaskTrack; }

	bool IsTaskTrackVisible() const;

	//////////////////////////////////////////////////
	// ITimingViewExtender interface

	virtual void OnBeginSession(Insights::ITimingViewSession& InSession) override;
	virtual void OnEndSession(Insights::ITimingViewSession& InSession) override;
	virtual void Tick(Insights::ITimingViewSession& InSession, const TraceServices::IAnalysisSession& InAnalysisSession) override;
	virtual void ExtendFilterMenu(Insights::ITimingViewSession& InSession, FMenuBuilder& InMenuBuilder) override;

	//////////////////////////////////////////////////

	bool IsTaskTrackToggleOn() const { return bShowHideTaskTrack; }
	void SetTaskTrackToggle(bool bOnOff) { bShowHideTaskTrack = bOnOff; }
	void ShowTaskTrack() { SetTaskTrackToggle(true); }
	void HideTaskTrack() { SetTaskTrackToggle(false); }

	void SetTaskId(uint32 InTaskId);

private:
	STimingView* TimingView;

	bool bShowHideTaskTrack;

	TSharedPtr<FTaskTimingTrack> TaskTrack;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTaskTimingTrack : public FTimingEventsTrack
{
	INSIGHTS_DECLARE_RTTI(FTaskTimingTrack, FTimingEventsTrack)

public:

	struct FPendingEventInfo
	{
		double StartTime;
		double EndTime;
		uint32 Depth;
		uint32 TimerIndex;
	};

	explicit FTaskTimingTrack(FTaskTimingSharedState& InSharedState, const FString& InName, uint32 InTimelineIndex)
		: FTimingEventsTrack(InName)
		, TimelineIndex(InTimelineIndex)
		, SharedState(InSharedState)
	{
	}

	virtual ~FTaskTimingTrack() {}

	uint32 GetTimelineIndex() const { return TimelineIndex; }

	virtual void BuildDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context) override;
	virtual void BuildFilteredDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context) override;

	virtual void PostDraw(const ITimingTrackDrawContext& Context) const override;

	virtual void InitTooltip(FTooltipDrawState& InOutTooltip, const ITimingEvent& InTooltipEvent) const override;

	virtual const TSharedPtr<const ITimingEvent> GetEvent(float InPosX, float InPosY, const FTimingTrackViewport& Viewport) const override;

	void SetTaskId(uint32 InTaskId) { TaskId = InTaskId; SetDirtyFlag(); }
	uint32 GetTaskId() { return TaskId; }
	
	void OnTimingEventSelected(TSharedPtr<const ITimingEvent> InSelectedEvent);

private:
	static const uint32 InvalidTaskId;
	uint32 TimelineIndex;

	FTaskTimingSharedState& SharedState;

	uint32 TaskId = InvalidTaskId;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} //namespace Insights
