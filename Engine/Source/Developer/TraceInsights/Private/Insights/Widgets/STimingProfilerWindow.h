// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Docking/TabManager.h"
#include "Input/Reply.h"
#include "Layout/Visibility.h"
#include "Misc/Guid.h"
#include "SlateFwd.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SCompoundWidget.h"

// Insights
#include "Insights/InsightsManager.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

class FActiveTimerHandle;
class FMenuBuilder;

class SFrameTrack;
class SLogView;
class SStatsView;
class STimersView;
class STimerTreeView;
class STimingView;
class SVerticalBox;

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FTimingProfilerTabs
{
	// Tab identifiers
	static const FName ToolbarID;
	static const FName FramesTrackID;
	static const FName TimingViewID;
	static const FName TimersID;
	static const FName CallersID;
	static const FName CalleesID;
	static const FName StatsCountersID;
	static const FName LogViewID;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Implements the timing profiler window. */
class STimingProfilerWindow : public SCompoundWidget
{
public:
	/** Default constructor. */
	STimingProfilerWindow();

	/** Virtual destructor. */
	virtual ~STimingProfilerWindow();

	SLATE_BEGIN_ARGS(STimingProfilerWindow) {}
	SLATE_END_ARGS()

	void Reset();

	/** Constructs this widget. */
	void Construct(const FArguments& InArgs, const TSharedRef<SDockTab>& ConstructUnderMajorTab, const TSharedPtr<SWindow>& ConstructUnderWindow);

	void ShowTab(const FName& TabID);
	void HideTab(const FName& TabID);
	void ShowHideTab(const FName& TabID, bool bShow) { bShow ? ShowTab(TabID) : HideTab(TabID); }

	TSharedPtr<FTabManager> GetTabManager() const { return TabManager; }

	TSharedPtr<STimingView> GetTimingView() const { return TimingView; }
	TSharedPtr<STimersView> GetTimersView() const { return TimersView; }
	TSharedPtr<STimerTreeView> GetCallersTreeView() const { return CallersTreeView; }
	TSharedPtr<STimerTreeView> GetCalleesTreeView() const { return CalleesTreeView; }
	TSharedPtr<SStatsView> GetStatsView() const { return StatsView; }
	TSharedPtr<SLogView> GetLogView() const { return LogView; }

private:
	TSharedRef<SDockTab> SpawnTab_Toolbar(const FSpawnTabArgs& Args);
	void OnToolbarTabClosed(TSharedRef<SDockTab> TabBeingClosed);

	TSharedRef<SDockTab> SpawnTab_FramesTrack(const FSpawnTabArgs& Args);
	void OnFramesTrackTabClosed(TSharedRef<SDockTab> TabBeingClosed);

	TSharedRef<SDockTab> SpawnTab_TimingView(const FSpawnTabArgs& Args);
	void OnTimingViewTabClosed(TSharedRef<SDockTab> TabBeingClosed);

	TSharedRef<SDockTab> SpawnTab_Timers(const FSpawnTabArgs& Args);
	void OnTimersTabClosed(TSharedRef<SDockTab> TabBeingClosed);

	TSharedRef<SDockTab> SpawnTab_Callers(const FSpawnTabArgs& Args);
	void OnCallersTabClosed(TSharedRef<SDockTab> TabBeingClosed);

	TSharedRef<SDockTab> SpawnTab_Callees(const FSpawnTabArgs& Args);
	void OnCalleesTabClosed(TSharedRef<SDockTab> TabBeingClosed);

	TSharedRef<SDockTab> SpawnTab_StatsCounters(const FSpawnTabArgs& Args);
	void OnStatsCountersTabClosed(TSharedRef<SDockTab> TabBeingClosed);

	TSharedRef<SDockTab> SpawnTab_LogView(const FSpawnTabArgs& Args);
	void OnLogViewTabClosed(TSharedRef<SDockTab> TabBeingClosed);

	/**
	 * Fill the main menu with menu items.
	 *
	 * @param MenuBuilder The multi-box builder that should be filled with content for this pull-down menu.
	 * @param TabManager A Tab Manager from which to populate tab spawner menu items.
	 */
	static void FillMenu(FMenuBuilder& MenuBuilder, const TSharedPtr<FTabManager> TabManager);

	/** Callback for determining the visibility of the 'Select a session' overlay. */
	EVisibility IsSessionOverlayVisible() const;

	/** Callback for getting the enabled state of the profiler window. */
	bool IsProfilerEnabled() const;

	/** Updates the amount of time the profiler has been active. */
	EActiveTimerReturnType UpdateActiveDuration(double InCurrentTime, float InDeltaTime);

	/**
	 * Ticks this widget. Override in derived classes, but always call the parent implementation.
	 *
	 * @param  AllottedGeometry The space allotted for this widget
	 * @param  InCurrentTime  Current absolute real time
	 * @param  InDeltaTime  Real time passed since last tick
	 */
	//virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/**
	 * The system will use this event to notify a widget that the cursor has entered it. This event is NOT bubbled.
	 *
	 * @param MyGeometry The Geometry of the widget receiving the event
	 * @param MouseEvent Information about the input event
	 */
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	/**
	 * The system will use this event to notify a widget that the cursor has left it. This event is NOT bubbled.
	 *
	 * @param MouseEvent Information about the input event
	 */
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;

	/**
	 * Called after a key is pressed when this widget has focus
	 *
	 * @param MyGeometry The Geometry of the widget receiving the event
	 * @param  InKeyEvent  Key event
	 *
	 * @return  Returns whether the event was handled, along with other possible actions
	 */
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	/**
	 * Called when the user is dropping something onto a widget; terminates drag and drop.
	 *
	 * @param MyGeometry      The geometry of the widget receiving the event.
	 * @param DragDropEvent   The drag and drop event.
	 *
	 * @return A reply that indicated whether this event was handled.
	 */
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;

	/**
	 * Called during drag and drop when the the mouse is being dragged over a widget.
	 *
	 * @param MyGeometry      The geometry of the widget receiving the event.
	 * @param DragDropEvent   The drag and drop event.
	 *
	 * @return A reply that indicated whether this event was handled.
	 */
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)  override;

private:
	/** The Frame track widget */
	TSharedPtr<SFrameTrack> FrameTrack;

	/** The Timing view (multi-track) widget */
	TSharedPtr<STimingView> TimingView;

	/** The Timers view widget */
	TSharedPtr<STimersView> TimersView;

	/** The Callers tree view widget */
	TSharedPtr<STimerTreeView> CallersTreeView;

	/** The Callees tree view widget */
	TSharedPtr<STimerTreeView> CalleesTreeView;

	/** The Stats (Counters) view widget */
	TSharedPtr<SStatsView> StatsView;

	/** The Log view widget */
	TSharedPtr<SLogView> LogView;

	/** Holds the tab manager that manages the front-end's tabs. */
	TSharedPtr<FTabManager> TabManager;

	/** The handle to the active update duration tick */
	TWeakPtr<FActiveTimerHandle> ActiveTimerHandle;

	/** The number of seconds the profiler has been active */
	float DurationActive;
};
