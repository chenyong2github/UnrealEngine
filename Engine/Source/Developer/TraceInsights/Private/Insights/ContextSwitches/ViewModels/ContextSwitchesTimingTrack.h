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

	//////////////////////////////////////////////////
	// ITimingViewExtender interface

	virtual void OnBeginSession(Insights::ITimingViewSession& InSession) override;
	virtual void OnEndSession(Insights::ITimingViewSession& InSession) override;
	virtual void Tick(Insights::ITimingViewSession& InSession, const TraceServices::IAnalysisSession& InAnalysisSession) override;
	virtual void ExtendFilterMenu(Insights::ITimingViewSession& InSession, FMenuBuilder& InMenuBuilder) override;

	//////////////////////////////////////////////////

	STimingView* GetTimingView() { return TimingView; }

	bool AreContextSwitchesAvailable() const;

	bool AreCoreTracksVisible() const { return bAreCoreTracksVisible; }
	void ShowCoreTracks() { SetCoreTracksVisible(true); }
	void HideCoreTracks() { SetCoreTracksVisible(false); }
	void ToggleCoreTracks() { SetCoreTracksVisible(!bAreCoreTracksVisible); }
	void SetCoreTracksVisible(bool bOnOff);

	bool AreContextSwitchesVisible() const { return bAreContextSwitchesVisible; }
	void ShowContextSwitches() { SetContextSwitchesVisible(true); }
	void HideContextSwitches() { SetContextSwitchesVisible(false); }
	void ToggleContextSwitches() { SetContextSwitchesVisible(!bAreContextSwitchesVisible); }
	void SetContextSwitchesVisible(bool bOnOff);

	bool AreOverlaysVisible() const { return bAreOverlaysVisible; }
	void ShowOverlays() { SetOverlaysVisible(true); }
	void HideOverlays() { SetOverlaysVisible(false); }
	void ToggleOverlays() { SetOverlaysVisible(!bAreOverlaysVisible); }
	void SetOverlaysVisible(bool bOnOff);

	bool AreExtendedLinesVisible() const { return bAreExtendedLinesVisible; }
	void ShowExtendedLines() { SetExtendedLinesVisible(true); }
	void HideExtendedLines() { SetExtendedLinesVisible(false); }
	void ToggleExtendedLines() { SetExtendedLinesVisible(!bAreExtendedLinesVisible); }
	void SetExtendedLinesVisible(bool bOnOff);

	void AddCommands();

private:
	void AddCoreTracks();
	void RemoveCoreTracks();

	void AddContextSwitchesChildTracks();
	void RemoveContextSwitchesChildTracks();

	void BuildSubMenu(FMenuBuilder& InMenuBuilder);

	void ContextMenu_ShowCoreTracks_Execute() { ToggleCoreTracks(); }
	bool ContextMenu_ShowCoreTracks_CanExecute() const { return AreContextSwitchesAvailable(); }
	bool ContextMenu_ShowCoreTracks_IsChecked() const { return AreCoreTracksVisible(); }

	void ContextMenu_ShowContextSwitches_Execute() { ToggleContextSwitches(); }
	bool ContextMenu_ShowContextSwitches_CanExecute() const { return AreContextSwitchesAvailable(); }
	bool ContextMenu_ShowContextSwitches_IsChecked() const { return AreContextSwitchesVisible(); }

	void ContextMenu_ShowOverlays_Execute() { ToggleOverlays(); }
	bool ContextMenu_ShowOverlays_CanExecute() const { return AreContextSwitchesAvailable() && AreContextSwitchesVisible(); }
	bool ContextMenu_ShowOverlays_IsChecked() const { return AreOverlaysVisible(); }

	void ContextMenu_ShowExtendedLines_Execute() { ToggleExtendedLines(); }
	bool ContextMenu_ShowExtendedLines_CanExecute() const { return AreContextSwitchesAvailable() && AreContextSwitchesVisible() && AreOverlaysVisible(); }
	bool ContextMenu_ShowExtendedLines_IsChecked() const { return AreExtendedLinesVisible(); }

private:
	STimingView* TimingView;

	bool bAreContextSwitchesVisible;
	bool bAreCoreTracksVisible;
	bool bAreOverlaysVisible;
	bool bAreExtendedLinesVisible;

	bool bSyncWithProviders;
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
