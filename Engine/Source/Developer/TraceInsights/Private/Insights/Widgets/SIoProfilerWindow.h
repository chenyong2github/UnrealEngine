// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Layout/Visibility.h"
#include "Misc/Guid.h"
#include "SlateFwd.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"

// Insights
#include "Insights/InsightsManager.h"

class FActiveTimerHandle;
class SVerticalBox;
class SFrameTrack;
class SGraphTrack;
class STimersView;
class STimingView;
class SLogView;

/// Type definition for shared pointers to instances of SNotificationItem.
typedef TSharedPtr<class SNotificationItem> SNotificationItemPtr;

/// Type definition for shared references to instances of SNotificationItem.
typedef TSharedRef<class SNotificationItem> SNotificationItemRef;

/// Type definition for weak references to instances of SNotificationItem.
typedef TWeakPtr<class SNotificationItem> SNotificationItemWeak;

/// Implements the timing profiler window.
class SIoProfilerWindow : public SCompoundWidget
{
public:
	/// Default constructor.
	SIoProfilerWindow();

	/// Virtual destructor.
	virtual ~SIoProfilerWindow();

	SLATE_BEGIN_ARGS(SIoProfilerWindow){}
	SLATE_END_ARGS()

	/// Constructs this widget.
	void Construct(const FArguments& InArgs);

	void ManageLoadingProgressNotificationState(const FString& Filename, const EInsightsNotificationType NotificatonType, const ELoadingProgressState ProgressState, const float LoadingProgress);

	void OpenProfilerSettings();
	void CloseProfilerSettings();

protected:
	TSharedRef<SWidget> ConstructMultiTrackView();
	TSharedRef<SWidget> ConstructFramesTrack();
	TSharedRef<SWidget> ConstructGraphTrack();
	TSharedRef<SWidget> ConstructTimingTrack();
	TSharedRef<SWidget> ConstructTimersView();
	TSharedRef<SWidget> ConstructLogView();

	/// Callback for determining the visibility of the Frames track.
	EVisibility IsFramesTrackVisible() const;

	/// Callback for determining the visibility of the Graph track.
	EVisibility IsGraphTrackVisible() const;

	/// Callback for determining the visibility of the Timing track.
	EVisibility IsTimingTrackVisible() const;

	/// Callback for determining the visibility of the Timers View.
	EVisibility IsTimersViewVisible() const;

	/// Callback for determining the visibility of the Log View.
	EVisibility IsLogViewVisible() const;

	/// Callback for determining the visibility of the 'Select a session' overlay.
	EVisibility IsSessionOverlayVisible() const;

	/// Callback for getting the enabled state of the profiler window.
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
	/// Updates the amount of time the profiler has been active
	EActiveTimerReturnType UpdateActiveDuration(double InCurrentTime, float InDeltaTime);

public:
	/// Widget for the frame track
	TSharedPtr<SFrameTrack> FrameTrack;

	/// Widget for the graph track
	TSharedPtr<SGraphTrack> GraphTrack;

	/// Holds all timing tracks (one for each thread).
	TSharedPtr<SVerticalBox> ThreadsPanel;

	/// Widget for the timing track
	TSharedPtr<STimingView> TimingView;

	/// Holds the Timers view widget/slot.
	SSplitter::FSlot* TimersViewSlot;
	TSharedPtr<STimersView> TimersView;

	/// Widget for the log view
	TSharedPtr<SLogView> LogView;

	/// Widget for the non-intrusive notifications.
	TSharedPtr<SNotificationList> NotificationList;

	/// Holds all active and visible notifications, stored as FGuid -> SNotificationItemWeak.
	TMap<FString, SNotificationItemWeak> ActiveNotifications;

	/// Overlay slot which contains the profiler settings widget.
	SOverlay::FOverlaySlot* OverlaySettingsSlot;

	/// The number of seconds the profiler has been active
	float DurationActive;

private:
	/// The handle to the active update duration tick
	TWeakPtr<FActiveTimerHandle> ActiveTimerHandle;

	/// Holds all widgets for the profiler window like menu bar, toolbar and tabs.
	TSharedPtr<SVerticalBox> MainContentPanel;
};
