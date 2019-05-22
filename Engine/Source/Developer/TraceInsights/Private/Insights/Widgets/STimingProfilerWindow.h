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
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"

// Insights
#include "Insights/InsightsManager.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

class FActiveTimerHandle;
class SVerticalBox;
class SFrameTrack;
class SGraphTrack;
class SStatsView;
class STimersView;
class STimingView;
class SLogView;

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Type definition for shared pointers to instances of SNotificationItem. */
typedef TSharedPtr<class SNotificationItem> SNotificationItemPtr;

/** Type definition for shared references to instances of SNotificationItem. */
typedef TSharedRef<class SNotificationItem> SNotificationItemRef;

/** Type definition for weak references to instances of SNotificationItem. */
typedef TWeakPtr<class SNotificationItem> SNotificationItemWeak;

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FTimingProfilerTabs
{
	// Tab identifiers
	static const FName ToolbarID;
	static const FName FramesTrackID;
	static const FName GraphTrackID;
	static const FName TimingViewID;
	static const FName TimersID;
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

	SLATE_BEGIN_ARGS(STimingProfilerWindow){}
	SLATE_END_ARGS()

	/** Constructs this widget. */
	void Construct(const FArguments& InArgs, const TSharedRef<SDockTab>& ConstructUnderMajorTab, const TSharedPtr<SWindow>& ConstructUnderWindow);

	void ManageLoadingProgressNotificationState(const FString& Filename, const EInsightsNotificationType NotificatonType, const ELoadingProgressState ProgressState, const float LoadingProgress);

	void OpenProfilerSettings();
	void CloseProfilerSettings();

	void ShowHideTab(const FName& TabID, bool bIsVisible);

protected:
	TSharedRef<SDockTab> SpawnTab_Toolbar(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_FramesTrack(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_GraphTrack(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_TimingView(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Timers(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_StatsCounters(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_LogView(const FSpawnTabArgs& Args);

	//void RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager);
	//void UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager);

	/**
	 * Fill the main menu with menu items.
	 *
	 * @param MenuBuilder The multi-box builder that should be filled with content for this pull-down menu.
	 * @param TabManager A Tab Manager from which to populate tab spawner menu items.
	 */
	static void FillMenu(FMenuBuilder& MenuBuilder, const TSharedPtr<FTabManager> TabManager);

	/** Callback for determining the visibility of the Frames track. */
	EVisibility IsFramesTrackVisible() const;

	/** Callback for determining the visibility of the Graph track. */
	EVisibility IsGraphTrackVisible() const;

	/** Callback for determining the visibility of the Timing view. */
	EVisibility IsTimingViewVisible() const;

	/** Callback for determining the visibility of the Timers View. */
	EVisibility IsTimersViewVisible() const;

	/** Callback for determining the visibility of the Stats Counters View. */
	EVisibility IsStatsCountersVisible() const;

	/** Callback for determining the visibility of the Log View. */
	EVisibility IsLogViewVisible() const;

	/** Callback for determining the visibility of the 'Select a session' overlay. */
	EVisibility IsSessionOverlayVisible() const;

	/** Callback for getting the enabled state of the profiler window. */
	bool IsProfilerEnabled() const;

	void SendingServiceSideCapture_Cancel(const FString Filename);

	void SendingServiceSideCapture_Load(const FString Filename);

private:

	/**
	 * Ticks this widget.  Override in derived classes, but always call the parent implementation.
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
	/** Updates the amount of time the profiler has been active. */
	EActiveTimerReturnType UpdateActiveDuration(double InCurrentTime, float InDeltaTime);

public:
	/** Widget for the frame track */
	TSharedPtr<SFrameTrack> FrameTrack;

	/** Widget for the graph track */
	TSharedPtr<SGraphTrack> GraphTrack;

	/** Widget for the timing track */
	TSharedPtr<STimingView> TimingView;

	/** Holds the Timers view widget. */
	TSharedPtr<STimersView> TimersView;

	/** Holds the Stats (Counters) view widget. */
	TSharedPtr<SStatsView> StatsView;

	/** Widget for the log view */
	TSharedPtr<SLogView> LogView;

	/** Widget for the non-intrusive notifications. */
	TSharedPtr<SNotificationList> NotificationList;

	/** Holds all active and visible notifications, stored as FGuid -> SNotificationItemWeak. */
	TMap<FString, SNotificationItemWeak> ActiveNotifications;

	/** Overlay slot which contains the profiler settings widget. */
	SOverlay::FOverlaySlot* OverlaySettingsSlot;

	/** The number of seconds the profiler has been active */
	float DurationActive;

private:
	/** Holds the tab manager that manages the front-end's tabs. */
	TSharedPtr<FTabManager> TabManager;

	/** The handle to the active update duration tick */
	TWeakPtr<FActiveTimerHandle> ActiveTimerHandle;

	/** Holds all widgets for the profiler window like menu bar, toolbar and tabs. */
	TSharedPtr<SVerticalBox> MainContentPanel;
};
