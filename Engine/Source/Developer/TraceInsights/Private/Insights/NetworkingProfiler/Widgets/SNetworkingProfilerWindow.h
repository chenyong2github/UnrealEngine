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

class SDataStreamBreakdownView;
class SPacketBreakdownView;
class SPacketSizesView;

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FNetworkingProfilerTabs
{
	// Tab identifiers
	static const FName ToolbarID;
	static const FName PacketSizesViewID;
	static const FName PacketBreakdownViewID;
	static const FName DataStreamBreakdownViewID;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Implements the timing profiler window. */
class SNetworkingProfilerWindow : public SCompoundWidget
{
public:
	/** Default constructor. */
	SNetworkingProfilerWindow();

	/** Virtual destructor. */
	virtual ~SNetworkingProfilerWindow();

	SLATE_BEGIN_ARGS(SNetworkingProfilerWindow) {}
	SLATE_END_ARGS()

	void Reset();

	/** Constructs this widget. */
	void Construct(const FArguments& InArgs, const TSharedRef<SDockTab>& ConstructUnderMajorTab, const TSharedPtr<SWindow>& ConstructUnderWindow);

	void ShowTab(const FName& TabID);
	void HideTab(const FName& TabID);
	void ShowHideTab(const FName& TabID, bool bShow) { bShow ? ShowTab(TabID) : HideTab(TabID); }

	TSharedPtr<FTabManager> GetTabManager() const { return TabManager; }

	TSharedPtr<SPacketSizesView> GetPacketSizesView() const { return PacketSizesView; }
	TSharedPtr<SPacketBreakdownView> GetPacketBreakdownView() const { return PacketBreakdownView; }
	TSharedPtr<SDataStreamBreakdownView> GetDataStreamBreakdownView() const { return DataStreamBreakdownView; }

private:
	TSharedRef<SDockTab> SpawnTab_Toolbar(const FSpawnTabArgs& Args);
	void OnToolbarTabClosed(TSharedRef<SDockTab> TabBeingClosed);

	TSharedRef<SDockTab> SpawnTab_PacketSizesView(const FSpawnTabArgs& Args);
	void OnPacketSizesViewTabClosed(TSharedRef<SDockTab> TabBeingClosed);

	TSharedRef<SDockTab> SpawnTab_PacketBreakdownView(const FSpawnTabArgs& Args);
	void OnPacketBreakdownViewTabClosed(TSharedRef<SDockTab> TabBeingClosed);

	TSharedRef<SDockTab> SpawnTab_DataStreamBreakdownView(const FSpawnTabArgs& Args);
	void OnDataStreamBreakdownViewTabClosed(TSharedRef<SDockTab> TabBeingClosed);

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
	/** The Packet Sizes widget */
	TSharedPtr<SPacketSizesView> PacketSizesView;

	/** The Packet Breakdown widget */
	TSharedPtr<SPacketBreakdownView> PacketBreakdownView;

	/** The Data Stream Breakdown widget */
	TSharedPtr<SDataStreamBreakdownView> DataStreamBreakdownView;

	/** Holds the tab manager that manages the front-end's tabs. */
	TSharedPtr<FTabManager> TabManager;

	/** The handle to the active update duration tick */
	TWeakPtr<FActiveTimerHandle> ActiveTimerHandle;

	/** The number of seconds the profiler has been active */
	float DurationActive;
};
