// Copyright Epic Games, Inc. All Rights Reserved.

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
#include "Insights/ITimingViewSession.h"
#include "Insights/Widgets/SModulesView.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

class FActiveTimerHandle;
class FMenuBuilder;
class FWorkspaceItem;

class FMemorySharedState;
class SMemInvestigationView;
class SMemTagTreeView;
class STimingView;

namespace Insights
{
	class FTimeMarker;
	class STableTreeView;
	class SMemAllocTableTreeView;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FMemoryProfilerTabs
{
	// Tab identifiers
	static const FName TimingViewID;
	static const FName MemInvestigationViewID;
	static const FName MemTagTreeViewID;
	static const FName MemAllocTableTreeViewID; // base id
	static const FName ModulesViewID;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Implements the timing profiler window. */
class SMemoryProfilerWindow : public SCompoundWidget
{
public:
	/** Default constructor. */
	SMemoryProfilerWindow();

	/** Virtual destructor. */
	virtual ~SMemoryProfilerWindow();

	SLATE_BEGIN_ARGS(SMemoryProfilerWindow) {}
	SLATE_END_ARGS()

	void Reset();

	/** Constructs this widget. */
	void Construct(const FArguments& InArgs, const TSharedRef<SDockTab>& ConstructUnderMajorTab, const TSharedPtr<SWindow>& ConstructUnderWindow);

	void ShowTab(const FName& TabID);
	void HideTab(const FName& TabID);
	void ShowHideTab(const FName& TabID, bool bShow) { bShow ? ShowTab(TabID) : HideTab(TabID); }

	TSharedPtr<Insights::SMemAllocTableTreeView> ShowMemAllocTableTreeViewTab();

	TSharedPtr<FTabManager> GetTabManager() const { return TabManager; }

	TSharedPtr<STimingView> GetTimingView() const { return TimingView; }
	TSharedPtr<SMemInvestigationView> GetMemInvestigationView() const { return MemInvestigationView; }
	TSharedPtr<SMemTagTreeView> GetMemTagTreeView() const { return MemTagTreeView; }

	uint32 GetNumCustomTimeMarkers() const { return (uint32)CustomTimeMarkers.Num(); }
	const TSharedRef<Insights::FTimeMarker>& GetCustomTimeMarker(uint32 Index) const { return CustomTimeMarkers[Index]; }
	const TArray<TSharedRef<Insights::FTimeMarker>>& GetCustomTimeMarkers() const { return CustomTimeMarkers; }

	FMemorySharedState& GetSharedState() { return *SharedState; }
	const FMemorySharedState& GetSharedState() const { return *SharedState; }

	void CloseMemAllocTableTreeTabs();

	void OnMemoryRuleChanged();
	void OnTimeMarkerChanged(Insights::ETimeChangedFlags InFlags, TSharedRef<Insights::ITimeMarker> InTimeMarker);

private:
	TSharedRef<SDockTab> SpawnTab_TimingView(const FSpawnTabArgs& Args);
	void OnTimingViewTabClosed(TSharedRef<SDockTab> TabBeingClosed);

	TSharedRef<SDockTab> SpawnTab_MemInvestigationView(const FSpawnTabArgs& Args);
	void OnMemInvestigationViewTabClosed(TSharedRef<SDockTab> TabBeingClosed);

	TSharedRef<SDockTab> SpawnTab_MemTagTreeView(const FSpawnTabArgs& Args);
	void OnMemTagTreeViewTabClosed(TSharedRef<SDockTab> TabBeingClosed);

	TSharedRef<SDockTab> SpawnTab_MemAllocTableTreeView(const FSpawnTabArgs& Args, int32 TabIndex);
	void OnMemAllocTableTreeViewTabClosed(TSharedRef<SDockTab> TabBeingClosed);

	TSharedRef<SDockTab> SpawnTab_ModulesView(const FSpawnTabArgs& Args);
	void OnModulesViewClosed(TSharedRef<SDockTab> TabBeingClosed);

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

	void OnTimeSelectionChanged(Insights::ETimeChangedFlags InFlags, double InStartTime, double InEndTime);

	void CreateTimingViewMarkers();
	void ResetTimingViewMarkers();
	void UpdateTimingViewMarkers();

private:
	TSharedRef<FMemorySharedState> SharedState;

	/** The Timing view (multi-track) widget */
	TSharedPtr<STimingView> TimingView;

	TArray<TSharedRef<Insights::FTimeMarker>> CustomTimeMarkers;

	/** The Memory Investigation (Allocation Queries) view widget */
	TSharedPtr<SMemInvestigationView> MemInvestigationView;

	/** The "LLM Tags" tree view widget */
	TSharedPtr<SMemTagTreeView> MemTagTreeView;

	/** The list of Allocations table tree view widgets */
	TArray<TSharedPtr<Insights::SMemAllocTableTreeView>> MemAllocTableTreeViews;

	/** The Modules view widget. */
	TSharedPtr<Insights::SModulesView> ModulesView;

	const int32 MaxMemAllocTableTreeViews = 4;
	int32 LastMemAllocTableTreeViewIndex = -1;

	/** Holds the tab manager that manages the front-end's tabs. */
	TSharedPtr<FTabManager> TabManager;

	TSharedPtr<FWorkspaceItem> AppMenuGroup;

	/** The handle to the active update duration tick */
	TWeakPtr<FActiveTimerHandle> ActiveTimerHandle;

	/** The number of seconds the profiler has been active */
	float DurationActive;
};
