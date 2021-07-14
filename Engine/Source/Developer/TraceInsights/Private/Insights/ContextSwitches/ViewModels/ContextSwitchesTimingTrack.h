// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Insights
#include "Insights/ITimingViewExtender.h"
#include "Insights/ViewModels/TimingEventsTrack.h"

class FThreadTrackEvent;
class STimingView;

////////////////////////////////////////////////////////////////////////////////////////////////////

namespace Insights
{
class FContextSwitchesTimingTrack;

class FContextSwitchesSharedState : public Insights::ITimingViewExtender, public TSharedFromThis<FContextSwitchesSharedState>
{

public:
	FContextSwitchesSharedState(STimingView* InTimingView);
	virtual ~FContextSwitchesSharedState() = default;

	bool IsContextSwitchesVisible() const;

	//////////////////////////////////////////////////
	// ITimingViewExtender interface

	virtual void OnBeginSession(Insights::ITimingViewSession& InSession) override;
	virtual void OnEndSession(Insights::ITimingViewSession& InSession) override;
	virtual void Tick(Insights::ITimingViewSession& InSession, const TraceServices::IAnalysisSession& InAnalysisSession) override;
	virtual void ExtendFilterMenu(Insights::ITimingViewSession& InSession, FMenuBuilder& InMenuBuilder) override;
	virtual bool ExtendGlobalContextMenu(ITimingViewSession& InSession, FMenuBuilder& InMenuBuilder) override;

	//////////////////////////////////////////////////

	bool IsContextSwitchesToggleOn() const { return bShowContextSwitchesTrack; }
	void SetContextSwitchesToggle(bool bOnOff) { bShowContextSwitchesTrack = bOnOff; }
	void ShowContextSwitches() { SetContextSwitchesToggle(true); }
	void HideContextSwitches() { SetContextSwitchesToggle(false); }

	STimingView* GetTimingView() { return TimingView; }
	void AddCommands();

private:
	void ContextMenu_ShowContextSwitches_Execute();
	bool ContextMenu_ShowContextSwitches_CanExecute();
	bool ContextMenu_ShowContextSwitches_IsChecked();

	void AddContextSwitchesChildTracks();
	void RemoveContextSwitchesChildTracks();


private:
	STimingView* TimingView;

	bool bShowContextSwitchesTrack = true;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FContextSwitchesTimingTrack : public FTimingEventsTrack
{
	INSIGHTS_DECLARE_RTTI(FContextSwitchesTimingTrack, FTimingEventsTrack)

public:
	explicit FContextSwitchesTimingTrack(FContextSwitchesSharedState& InSharedState, const FString& InName, uint32 InTimelineIndex, uint32 ThreadId)
		: FTimingEventsTrack(InName)
		, TimelineIndex(InTimelineIndex)
		, ThreadId(ThreadId)
		, SharedState(InSharedState)
	{
	}

	virtual ~FContextSwitchesTimingTrack() {}

	uint32 GetTimelineIndex() const { return TimelineIndex; }

	virtual void BuildDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context) override;
	virtual void BuildFilteredDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context) override;

	virtual void Draw(const ITimingTrackDrawContext& Context) const override;
	virtual void PostDraw(const ITimingTrackDrawContext& Context) const override;

	virtual void InitTooltip(FTooltipDrawState& InOutTooltip, const ITimingEvent& InTooltipEvent) const override;

	virtual const TSharedPtr<const ITimingEvent> GetEvent(float InPosX, float InPosY, const FTimingTrackViewport& Viewport) const override;

protected:
	void DrawLineEvents(const ITimingTrackDrawContext& Context, const float OffsetY = 1.0f) const;

private:
	uint32 TimelineIndex;
	uint32 ThreadId;

	FContextSwitchesSharedState& SharedState;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} //namespace Insights
