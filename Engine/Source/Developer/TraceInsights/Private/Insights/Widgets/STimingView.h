// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"
#include "Input/CursorReply.h"
#include "Input/Reply.h"
#include "Layout/Geometry.h"
#include "TraceServices/AnalysisService.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SWidget.h"

// Insights
#include "Insights/Common/FixedCircularBuffer.h"
#include "Insights/ViewModels/MarkersTimingTrack.h"
#include "Insights/ViewModels/TimerNode.h"
#include "Insights/ViewModels/TimeRulerTrack.h"
#include "Insights/ViewModels/TimingEvent.h"
#include "Insights/ViewModels/TimingEventsTrack.h"
#include "Insights/ViewModels/TimingTrackViewport.h"
#include "Insights/ViewModels/TimingViewDrawHelper.h"
#include "Insights/ViewModels/TooltipDrawState.h"

class FFileActivitySharedState;
class FLoadingSharedState;
class FMenuBuilder;
class FTimingGraphTrack;
class FTimingViewDrawHelper;
class SScrollBar;

/** The delegate to be invoked when the selection have been changed. */
DECLARE_DELEGATE_TwoParams(FSelectionChangedDelegate, double /*StartTime*/, double /*EndTime*/);

/** The delegate to be invoked when the timing event being hovered by the mouse has changed. Returns id of timing event or -1 (if no event is hovered). */
DECLARE_DELEGATE_RetVal(int32, FHoveredEventChangedDelegate);

/** The delegate to be invoked when the selected timing event has changed. Returns id of timing event or -1 (if no event is hovered). */
DECLARE_DELEGATE_RetVal(int32, FSelectedEventChangedDelegate);

/** A custom widget used to display timing events. */
class STimingView : public SCompoundWidget
{
protected:
	struct FAssetLoadingEventAggregationRow
	{
		FString Name;
		int32 Count;
		double Total;
		double Min;
		double Max;
		double Avg;
		double Med;
	};

public:
	/** Default constructor. */
	STimingView();

	/** Virtual destructor. */
	virtual ~STimingView();

	SLATE_BEGIN_ARGS(STimingView)
		: _OnSelectionChanged()
		, _OnHoveredEventChanged()
		, _OnSelectedEventChanged()
		{
			_Clipping = EWidgetClipping::ClipToBounds;
		}

		SLATE_EVENT(FSelectionChangedDelegate, OnSelectionChanged)
		SLATE_EVENT(FHoveredEventChangedDelegate, OnHoveredEventChanged)
		SLATE_EVENT(FSelectedEventChangedDelegate, OnSelectedEventChanged)
	SLATE_END_ARGS()

	/**
	 * Construct this widget
	 *
	 * @param	InArgs	The declaration data for this widget
	 */
	void Construct(const FArguments& InArgs);

	TSharedRef<SWidget> MakeTracksFilterMenu();
	void CreateThreadGroupsMenu(FMenuBuilder& MenuBuilder);
	void CreateTracksMenu(FMenuBuilder& MenuBuilder);

	bool ShowHideGraphTrack_IsChecked() const;
	void ShowHideGraphTrack_Execute();

	bool ShowHideAllCpuTracks_IsChecked() const;
	void ShowHideAllCpuTracks_Execute();

	bool ShowHideAllGpuTracks_IsChecked() const;
	void ShowHideAllGpuTracks_Execute();

	bool ShowHideAllLoadingTracks_IsChecked() const;
	void ShowHideAllLoadingTracks_Execute();

	bool ShowHideAllIoTracks_IsChecked() const;
	void ShowHideAllIoTracks_Execute();

	bool IsAutoHideEmptyTracksEnabled() const;
	void ToggleAutoHideEmptyTracks();

	bool ToggleTrackVisibility_IsChecked(uint64 InTrackId) const;
	void ToggleTrackVisibility_Execute(uint64 InTrackId);

	bool ToggleTrackVisibilityByGroup_IsChecked(const TCHAR* InGroupName) const;
	void ToggleTrackVisibilityByGroup_Execute(const TCHAR* InGroupName);

	void EnableAssetLoadingMode();

	/** Resets internal widget's data to the default one. */
	void Reset();

	/**
	 * Ticks this widget.  Override in derived classes, but always call the parent implementation.
	 *
	 * @param  AllottedGeometry The space allotted for this widget
	 * @param  InCurrentTime  Current absolute real time
	 * @param  InDeltaTime  Real time passed since last tick
	 */
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	/**
	 * The system calls this method to notify the widget that a mouse button was pressed within it. This event is bubbled.
	 *
	 * @param MyGeometry The Geometry of the widget receiving the event
	 * @param MouseEvent Information about the input event
	 *
	 * @return Whether the event was handled along with possible requests for the system to take action.
	 */
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	/**
	 * The system calls this method to notify the widget that a mouse button was release within it. This event is bubbled.
	 *
	 * @param MyGeometry The Geometry of the widget receiving the event
	 * @param MouseEvent Information about the input event
	 *
	 * @return Whether the event was handled along with possible requests for the system to take action.
	 */
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	/**
	 * Called when a mouse button is double clicked.  Override this in derived classes.
	 *
	 * @param  InMyGeometry  Widget geometry
	 * @param  InMouseEvent  Mouse button event
	 *
	 * @return  Returns whether the event was handled, along with other possible actions
	 */
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	/**
	 * The system calls this method to notify the widget that a mouse moved within it. This event is bubbled.
	 *
	 * @param MyGeometry The Geometry of the widget receiving the event
	 * @param MouseEvent Information about the input event
	 *
	 * @return Whether the event was handled along with possible requests for the system to take action.
	 */
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

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
	 * Called when the mouse wheel is spun. This event is bubbled.
	 *
	 * @param  MouseEvent  Mouse event
	 *
	 * @return  Returns whether the event was handled, along with other possible actions
	 */
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

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
	 * Called during drag and drop when the drag enters a widget.
	 *
	 * Enter/Leave events in slate are meant as lightweight notifications.
	 * So we do not want to capture mouse or set focus in response to these.
	 * However, OnDragEnter must also support external APIs (e.g. OLE Drag/Drop)
	 * Those require that we let them know whether we can handle the content
	 * being dragged OnDragEnter.
	 *
	 * The concession is to return a can_handled/cannot_handle
	 * boolean rather than a full FReply.
	 *
	 * @param MyGeometry      The geometry of the widget receiving the event.
	 * @param DragDropEvent   The drag and drop event.
	 *
	 * @return A reply that indicated whether the contents of the DragDropEvent can potentially be processed by this widget.
	 */
	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;

	/**
	 * Called during drag and drop when the drag leaves a widget.
	 *
	 * @param DragDropEvent   The drag and drop event.
	 */
	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override;

	/**
	 * Called during drag and drop when the the mouse is being dragged over a widget.
	 *
	 * @param MyGeometry      The geometry of the widget receiving the event.
	 * @param DragDropEvent   The drag and drop event.
	 *
	 * @return A reply that indicated whether this event was handled.
	 */
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;

	/**
	 * Called when the system wants to know which cursor to display for this Widget.  This event is bubbled.
	 *
	 * @return  The cursor requested (can be None.)
	 */
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	////////////////////////////////////////////////////////////////////////////////////////////////////

	const FTimingTrackViewport& GetViewport() { return Viewport; }

	double GetSelectionStartTime() const { return SelectionStartTime; }
	double GetSelectionEndTime() const { return SelectionEndTime; }

	bool IsTimeSelected(double Time) const { return Time >= SelectionStartTime && Time < SelectionEndTime; }
	bool IsTimeSelectedInclusive(double Time) const { return Time >= SelectionStartTime && Time <= SelectionEndTime; }

	void ScrollAtPosY(float ScrollPosY);
	void ScrollAtTime(double StartTime);
	void CenterOnTimeInterval(double IntervalStartTime, double IntervalDuration);
	void BringIntoView(double StartTime, double EndTime);
	void SelectTimeInterval(double IntervalStartTime, double IntervalDuration);
	void SetAndCenterOnTimeMarker(double Time);
	void SelectToTimeMarker(double Time);

	bool AreTimeMarkersVisible() { return MarkersTrack.IsVisible(); }
	void SetTimeMarkersVisible(bool bOnOff);
	bool IsDrawOnlyBookmarksEnabled() { return MarkersTrack.IsBookmarksTrack(); }
	void SetDrawOnlyBookmarks(bool bOnOff);

	bool IsGpuTrackVisible() const;
	bool IsCpuTrackVisible(uint32 ThreadId) const;

	TSharedPtr<FTimingGraphTrack> GetMainTimingGraphTrack() { return GraphTrack; }

protected:
	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D(16.0f, 16.0f);
	}

	void AddTimingEventsTrack(FTimingEventsTrack* Track);

	void DrawTimeRangeSelection(FDrawContext& DrawContext) const;

	void ShowContextMenu(const FPointerEvent& MouseEvent);

	/** Binds our UI commands to delegates. */
	void BindCommands();

	////////////////////////////////////////////////////////////////////////////////////////////////////

	/**
	 * Called when the user scrolls the horizontal scrollbar.
	 *
	 * @param ScrollOffset Scroll offset as a fraction between 0 and 1.
	 */
	void HorizontalScrollBar_OnUserScrolled(float ScrollOffset);
	void UpdateHorizontalScrollBar();

	/**
	 * Called when the user scrolls the vertical scrollbar.
	 *
	 * @param ScrollOffset Scroll offset as a fraction between 0 and 1.
	 */
	void VerticalScrollBar_OnUserScrolled(float ScrollOffset);
	void UpdateVerticalScrollBar();

	////////////////////////////////////////////////////////////////////////////////////////////////////

	float DrawAssetLoadingAggregationTable(FDrawContext& DrawContext, float RightX, float BottomY, const TCHAR* TableName, const TArray<FAssetLoadingEventAggregationRow>& Aggregation, int32 TotalRowCount) const;

	void UpdateAggregatedStats();

	void UpdateHoveredTimingEvent(float MX, float MY);

	void OnSelectedTimingEventChanged();
	void SelectHoveredTimingEvent();
	void SelectLeftTimingEvent();
	void SelectRightTimingEvent();
	void SelectUpTimingEvent();
	void SelectDownTimingEvent();

	void FrameSelection();

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// FrameSelectionChanged Event

public:
	/**
	 * The event to execute when the selected frames have been changed.
	 *
	 * @param FrameStartIndex	- The index of the first frame selected.
	 * @param FrameEndIndex		- The index of the last frame selected.
	 *
	 */
	DECLARE_EVENT_TwoParams(STimingView, FFrameSelectionChangedEvent, int32 /*FrameStartIndex*/, int32 /*FrameEndIndex*/);
	FFrameSelectionChangedEvent& OnFrameSelectionChanged()
	{
		return FrameSelectionChangedEvent;
	}

protected:
	/** The event to execute when the selected frames have been changed. */
	FFrameSelectionChangedEvent FrameSelectionChangedEvent;

	////////////////////////////////////////////////////////////////////////////////////////////////////

protected:
	bool bShowHideAllGpuTracks;
	bool bShowHideAllCpuTracks;
	bool bShowHideAllLoadingTracks;
	bool bShowHideAllIoTracks;

	////////////////////////////////////////////////////////////

	/** The track's viewport. Encapsulates info about position and scale. */
	FTimingTrackViewport Viewport;

	bool bIsViewportDirty;
	bool bIsVerticalViewportDirty;

	////////////////////////////////////////////////////////////

	/** All created tracks.
	  * Maps track id to track pointer.
	  */
	TMap<uint64, FTimingEventsTrack*> CachedTimelines;

	//TODO: TArray<FBaseTimingTrack*> TopTracks; /**< tracks docked on top, in order to be displayed (top to bottom) */
	//TODO: TArray<FBaseTimingTrack*> ScrollableTracks; /**< tracks in scrollable area, in order to be displayed (top to bottom) */
	//TODO: TArray<FBaseTimingTrack*> BottomTracks; /**< tracks docked on bottom, in order to be displayed (top to bottom) */
	//TODO: TArray<FBaseTimingTrack*> ForegorundTracks; /**< tracks to draw over top/scrollable/bottom tracks (can use entire area), in order to be displayed (back to front) */

	////////////////////////////////////////////////////////////

	TArray<FTimingEventsTrack*> TimingEventsTracks; /**< all timing events tracks in order to be displayed */

	bool bAreTimingEventsTracksDirty;

	////////////////////////////////////////////////////////////
	// Cpu/Gpu

	FTimingEventsTrack* GpuTrack;
	TMap<uint32, FTimingEventsTrack*> CpuTracks; /**< maps thread id to track pointer */

	struct FThreadGroup
	{
		const TCHAR* Name; /**< The thread group name; pointer to string owned by ThreadProvider. */
		bool bIsVisible;  /**< Toggle to show/hide all thread timelines associated with this group at once. Used also as default for new thread timelines. */
		uint32 NumTimelines; /**< Number of thread timelines associated with this group. */
		int32 Order; //**< Order index used for sorting. Inherited from last thread timeline associated with this group. **/

		int32 GetOrder() const { return Order; }
	};

	TMap<const TCHAR*, FThreadGroup> ThreadGroups; /**< maps thread group name to thread group info */

	////////////////////////////////////////////////////////////
	// Asset Loading

	TSharedPtr<FLoadingSharedState> LoadingSharedState;
	FTimingEventsTrack* LoadingMainThreadTrack;
	FTimingEventsTrack* LoadingAsyncThreadTrack;

	uint32 LoadingMainThreadId;
	uint32 LoadingAsyncThreadId;

	bool bAssetLoadingMode;

	int32 EventAggregationTotalCount;
	TArray<FAssetLoadingEventAggregationRow> EventAggregation;

	int32 ObjectTypeAggregationTotalCount;
	TArray<FAssetLoadingEventAggregationRow> ObjectTypeAggregation;

	////////////////////////////////////////////////////////////
	// File activity (I/O)

	TSharedPtr<FFileActivitySharedState> FileActivitySharedState;
	FTimingEventsTrack* IoOverviewTrack;
	FTimingEventsTrack* IoActivityTrack;

	////////////////////////////////////////////////////////////

	/** The time ruler track. */
	FTimeRulerTrack TimeRulerTrack;

	/** The time markers track. */
	FMarkersTimingTrack MarkersTrack;

	/** A graph track for frame times. */
	TSharedPtr<FTimingGraphTrack> GraphTrack;

	////////////////////////////////////////////////////////////

	/** Horizontal scroll bar, used for scrolling timing events' viewport. */
	TSharedPtr<SScrollBar> HorizontalScrollBar;

	/** Vertical scroll bar, used for scrolling timing events' viewport. */
	TSharedPtr<SScrollBar> VerticalScrollBar;

	////////////////////////////////////////////////////////////

	/** The current mouse position. */
	FVector2D MousePosition;

	/** Mouse position during the call on mouse button down. */
	FVector2D MousePositionOnButtonDown;
	double ViewportStartTimeOnButtonDown;
	float ViewportScrollPosYOnButtonDown;

	/** Mouse position during the call on mouse button up. */
	FVector2D MousePositionOnButtonUp;

	bool bIsLMB_Pressed;
	bool bIsRMB_Pressed;

	bool bIsSpaceBarKeyPressed;
	bool bIsDragging;

	////////////////////////////////////////////////////////////
	// Panning

	/**
	 * True, if the user is currently interactively panning the view (horizontally and/or vertically),
	 * either by holding the right mouse button and dragging
	 * or by holding spacebar pressed and dragging with left mouse button.
	 */
	bool bIsPanning;

	/** How to pan. */
	enum class EPanningMode : uint8
	{
		None = 0,
		Horizontal = 0x01,
		Vertical = 0x02,
		HorizontalAndVertical = Horizontal | Vertical,
	};
	EPanningMode PanningMode;

	////////////////////////////////////////////////////////////
	// Selection

	/** True, if the user is currently changing the selection (by holding the left mouse button and dragging). */
	bool bIsSelecting;

	double SelectionStartTime;
	double SelectionEndTime;

	FTimingEvent HoveredTimingEvent;
	FTimingEvent SelectedTimingEvent;

	FTooltipDrawState Tooltip;

	enum class ESelectionType
	{
		None,
		TimeRange,
		TimingEvent
	};
	ESelectionType LastSelectionType;

	double TimeMarker;

	////////////////////////////////////////////////////////////
	// Misc

	FGeometry ThisGeometry;

	const FSlateBrush* WhiteBrush;
	const FSlateFontInfo MainFont;

	FTimingEventsTrackLayout Layout;

	// Debug stats
	int32 NumUpdatedEvents;
	TFixedCircularBuffer<uint64, 32> TimelineCacheUpdateDurationHistory;
	TFixedCircularBuffer<uint64, 32> TimeMarkerCacheUpdateDurationHistory;
	mutable TFixedCircularBuffer<uint64, 32> DrawDurationHistory;
	mutable TFixedCircularBuffer<uint64, 32> OnPaintDurationHistory;
	mutable uint64 LastOnPaintTime;

	////////////////////////////////////////////////////////////
	// Delegates

	FSelectionChangedDelegate OnSelectionChanged;
	FHoveredEventChangedDelegate OnHoveredEventChanged;
	FSelectedEventChangedDelegate OnSelectedEventChanged;
};
