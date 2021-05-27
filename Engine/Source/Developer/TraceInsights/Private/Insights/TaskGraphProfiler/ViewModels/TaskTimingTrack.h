// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Insights
#include "Insights/ITimingViewExtender.h"
#include "Insights/ViewModels/TimingEventsTrack.h"

class FUICommandList;

class FThreadTrackEvent;
class STimingView;

////////////////////////////////////////////////////////////////////////////////////////////////////

namespace Insights
{
class FTaskTimingTrack;

class FTaskTimingSharedState : public Insights::ITimingViewExtender, public TSharedFromThis<FTaskTimingSharedState>
{

public:
	FTaskTimingSharedState(STimingView* InTimingView);
	virtual ~FTaskTimingSharedState() = default;

	TSharedPtr<FTaskTimingTrack> GetTaskTrack() { return TaskTrack; }

	bool IsTaskTrackVisible() const;

	//////////////////////////////////////////////////
	// ITimingViewExtender interface

	virtual void OnBeginSession(Insights::ITimingViewSession& InSession) override;
	virtual void OnEndSession(Insights::ITimingViewSession& InSession) override;
	virtual void Tick(Insights::ITimingViewSession& InSession, const TraceServices::IAnalysisSession& InAnalysisSession) override;
	virtual void ExtendFilterMenu(Insights::ITimingViewSession& InSession, FMenuBuilder& InMenuBuilder) override;
	virtual bool ExtendGlobalContextMenu(ITimingViewSession& InSession, FMenuBuilder& InMenuBuilder) override;

	//////////////////////////////////////////////////

	bool IsTaskTrackToggleOn() const { return bShowHideTaskTrack; }
	void SetTaskTrackToggle(bool bOnOff) { bShowHideTaskTrack = bOnOff; }
	void ShowTaskTrack() { SetTaskTrackToggle(true); }
	void HideTaskTrack() { SetTaskTrackToggle(false); }

	void SetTaskId(uint32 InTaskId);

	void SetResetOnNextTick(bool bInValue) { bResetOnNextTick = bInValue; }

	STimingView* GetTimingView() { return TimingView; }

private:
	void InitCommandList();

	void BuildTasksSubMenu(FMenuBuilder& MenuBuilder);

	void ContextMenu_ShowTaskDependecies_Execute();
	bool ContextMenu_ShowTaskDependecies_CanExecute();
	bool ContextMenu_ShowTaskDependecies_IsChecked();

	void ContextMenu_ShowTaskPrerequisites_Execute();
	bool ContextMenu_ShowTaskPrerequisites_CanExecute();
	bool ContextMenu_ShowTaskPrerequisites_IsChecked();

	void ContextMenu_ShowTaskSubsequents_Execute();
	bool ContextMenu_ShowTaskSubsequents_CanExecute();
	bool ContextMenu_ShowTaskSubsequents_IsChecked();

	void ContextMenu_ShowNestedTasks_Execute();
	bool ContextMenu_ShowNestedTasks_CanExecute();
	bool ContextMenu_ShowNestedTasks_IsChecked();

	void OnTaskSettingsChanged();

private:
	STimingView* TimingView;

	bool bShowHideTaskTrack;
	bool bResetOnNextTick = false;

	TSharedPtr<FTaskTimingTrack> TaskTrack;
	TSharedPtr<FUICommandList> CommandList;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTaskTimingTrack : public FTimingEventsTrack
{
	INSIGHTS_DECLARE_RTTI(FTaskTimingTrack, FTimingEventsTrack)

public:
	static const uint32 InvalidTaskId;

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
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	virtual void PostDraw(const ITimingTrackDrawContext& Context) const override;

	virtual void InitTooltip(FTooltipDrawState& InOutTooltip, const ITimingEvent& InTooltipEvent) const override;

	virtual const TSharedPtr<const ITimingEvent> GetEvent(float InPosX, float InPosY, const FTimingTrackViewport& Viewport) const override;

	void SetTaskId(uint32 InTaskId) { TaskId = InTaskId; SetDirtyFlag(); }
	uint32 GetTaskId() { return TaskId; }
	
	void OnTimingEventSelected(TSharedPtr<const ITimingEvent> InSelectedEvent);
	void GetEventRelations(const FThreadTrackEvent& InSelectedEvent);

private:
	uint32 TimelineIndex;

	FTaskTimingSharedState& SharedState;

	uint32 TaskId = InvalidTaskId;

	FVector2D MousePositionOnButtonDown;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} //namespace Insights
