// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Docking/TabManager.h"
#include "Input/Reply.h"
#include "Layout/Visibility.h"
#include "Misc/Guid.h"
#include "SlateFwd.h"
#include "TraceServices/ModuleService.h"
#include "TraceServices/SessionService.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Views/SListView.h"

// Insights
#include "Insights/InsightsManager.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

class FActiveTimerHandle;
class SVerticalBox;
class SEditableTextBox;

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Type definition for shared pointers to instances of SNotificationItem. */
typedef TSharedPtr<class SNotificationItem> SNotificationItemPtr;

/** Type definition for shared references to instances of SNotificationItem. */
typedef TSharedRef<class SNotificationItem> SNotificationItemRef;

/** Type definition for weak references to instances of SNotificationItem. */
typedef TWeakPtr<class SNotificationItem> SNotificationItemWeak;

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FRecorderConnection
{
	Trace::FSessionHandle SessionHandle;
	FText Name;
	FText Uri;

	FRecorderConnection(const Trace::FSessionHandle InSessionHandle, const Trace::FSessionInfo& InSessionInfo)
		: SessionHandle(InSessionHandle)
		, Name(FText::FromString(InSessionInfo.Name))
		, Uri(FText::FromString(InSessionInfo.Uri))
	{}
};

/** Implements the Start Page window. */
class SStartPageWindow : public SCompoundWidget
{
public:
	/** Default constructor. */
	SStartPageWindow();

	/** Virtual destructor. */
	virtual ~SStartPageWindow();

	SLATE_BEGIN_ARGS(SStartPageWindow){}
	SLATE_END_ARGS()

	/** Constructs this widget. */
	void Construct(const FArguments& InArgs);

	void ManageLoadingProgressNotificationState(const FString& Filename, const EInsightsNotificationType NotificatonType, const ELoadingProgressState ProgressState, const float LoadingProgress);

	void OpenSettings();
	void CloseSettings();

private:
	TSharedRef<SWidget> ConstructRecoderPanel();
	TSharedRef<SWidget> ConstructModuleList();

	/** Generate a new row for the Connections list view. */
	TSharedRef<ITableRow> Connections_OnGenerateRow(TSharedPtr<FRecorderConnection> InConnection, const TSharedRef<STableViewBase>& OwnerTable);

	/** Callback for determining the visibility of the 'Please select a trace' overlay. */
	EVisibility IsSessionOverlayVisible() const;

	bool IsSessionValid() const;

	void SendingServiceSideCapture_Cancel(const FString Filename);
	void SendingServiceSideCapture_Load(const FString Filename);

	bool Live_IsEnabled() const;
	bool Last_IsEnabled() const;

	FReply Live_OnClicked();
	FReply Last_OnClicked();
	FReply Load_OnClicked();

	void LoadTraceFile(const TCHAR* TraceFile);
	void LoadSession(Trace::FSessionHandle SessionHandle);

	TSharedRef<SWidget> MakeSessionListMenu();

	FText GetLocalSessionDirectory() const;
	FReply ExploreLocalSessionDirectory_OnClicked();

	FText GetRecorderStatusText() const;
	EVisibility StartTraceRecorder_Visibility() const;
	EVisibility StopTraceRecorder_Visibility() const;
	FReply StartTraceRecorder_OnClicked();
	FReply StopTraceRecorder_OnClicked();

	EVisibility Modules_Visibility() const;
	ECheckBoxState Module_IsChecked(int32 ModuleIndex) const;
	void Module_OnCheckStateChanged(ECheckBoxState NewRadioState, int32 ModuleIndex);

	FReply RefreshConnections_OnClicked();
	FReply Connect_OnClicked();
	void RefreshConnectionList();
	void Connections_OnSelectionChanged(TSharedPtr<FRecorderConnection> Connection, ESelectInfo::Type SelectInfo);
	EVisibility Connections_Visibility() const;

	/** Updates the amount of time the profiler has been active. */
	EActiveTimerReturnType UpdateActiveDuration(double InCurrentTime, float InDeltaTime);

	/**
	 * Ticks this widget.  Override in derived classes, but always call the parent implementation.
	 *
	 * @param  AllottedGeometry The space allotted for this widget
	 * @param  InCurrentTime  Current absolute real time
	 * @param  InDeltaTime  Real time passed since last tick
	 */
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

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

public:
	/** Widget for the non-intrusive notifications. */
	TSharedPtr<SNotificationList> NotificationList;

	/** Holds all active and visible notifications, stored as FGuid -> SNotificationItemWeak. */
	TMap<FString, SNotificationItemWeak> ActiveNotifications;

	/** Overlay slot which contains the profiler settings widget. */
	SOverlay::FOverlaySlot* OverlaySettingsSlot;

	/** The number of seconds the profiler has been active */
	float DurationActive;

private:
	/** The handle to the active update duration tick */
	TWeakPtr<FActiveTimerHandle> ActiveTimerHandle;

	/** Holds all widgets for the profiler window like menu bar, toolbar and tabs. */
	TSharedPtr<SVerticalBox> MainContentPanel;

	bool bIsAnyLiveSessionAvailable;
	Trace::FSessionHandle LastLiveSessionHandle;

	bool bIsAnySessionAvailable;
	Trace::FSessionHandle LastSessionHandle;

	TSharedPtr<SListView<TSharedPtr<FRecorderConnection>>> ConnectionsListView;
	TArray<TSharedPtr<FRecorderConnection>> Connections;
	TSharedPtr<SEditableTextBox> HostTextBox;
	TSharedPtr<FRecorderConnection> SelectedConnection;
	TArray<Trace::FModuleInfo> AvailableModules;
	TArray<bool> AvailableModulesEnabledState;
};
