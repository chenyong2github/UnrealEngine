// Copyright Epic Games, Inc. All Rights Reserved.

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
#include "Insights/ITimingViewSession.h"
#include "Insights/ViewModels/TimerNode.h"
#include "Insights/ViewModels/TimingEvent.h"
#include "Insights/ViewModels/TimingEventsTrack.h"
#include "Insights/ViewModels/TimingTrackViewport.h"
#include "Insights/ViewModels/TimingViewDrawHelper.h"
#include "Insights/ViewModels/TooltipDrawState.h"

class FFileActivitySharedState;
class FFrameSharedState;
class FLoadingSharedState;
class FMarkersTimingTrack;
class FMenuBuilder;
class FThreadTimingSharedState;
class FTimeRulerTrack; 
class FTimingGraphTrack;
class FTimingViewDrawHelper;
class SScrollBar;
namespace Insights { class ITimingViewExtender; }

/** A custom widget used to display timing events. */
class STimingView : public SCompoundWidget, public Insights::ITimingViewSession
{
public:
	/** Default constructor. */
	STimingView();

	/** Virtual destructor. */
	virtual ~STimingView();

	SLATE_BEGIN_ARGS(STimingView)
		{
			_Clipping = EWidgetClipping::ClipToBounds;
		}
	SLATE_END_ARGS()

	/**
	 * Construct this widget
	 *
	 * @param	InArgs	The declaration data for this widget
	 */
	void Construct(const FArguments& InArgs);

	TSharedRef<SWidget> MakeTracksFilterMenu();
	void CreateTracksMenu(FMenuBuilder& MenuBuilder);

	bool ShowHideGraphTrack_IsChecked() const;
	void ShowHideGraphTrack_Execute();

	bool IsAutoHideEmptyTracksEnabled() const;
	void ToggleAutoHideEmptyTracks();

	bool ToggleTrackVisibility_IsChecked(uint64 InTrackId) const;
	void ToggleTrackVisibility_Execute(uint64 InTrackId);

	bool IsAssetLoadingModeEnabled() const { return bAssetLoadingMode; }
	void EnableAssetLoadingMode() { bAssetLoadingMode = true; }

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
	// ITimingViewSession interface

	virtual TSharedPtr<FBaseTimingTrack> FindTrack(uint64 InTrackId) override;

	virtual void AddTopDockedTrack(TSharedPtr<FBaseTimingTrack> Track) override;
	virtual void AddBottomDockedTrack(TSharedPtr<FBaseTimingTrack> Track) override;
	virtual void AddScrollableTrack(TSharedPtr<FBaseTimingTrack> Track) override;
	virtual void AddForegroundTrack(TSharedPtr<FBaseTimingTrack> Track) override;

	virtual void PreventThrottling() override;
	virtual void InvalidateScrollableTracksOrder() override;
	//TODO: virtual void InvalidateScrollableTracksVisibility() override;

	virtual Insights::FSelectionChangedDelegate& OnSelectionChanged() override { return OnSelectionChangedDelegate; }
	virtual Insights::FTimeMarkerChangedDelegate& OnTimeMarkerChanged() override { return OnTimeMarkerChangedDelegate; }
	virtual Insights::FHoveredTrackChangedDelegate& OnHoveredTrackChanged() override { return OnHoveredTrackChangedDelegate; }
	virtual Insights::FHoveredEventChangedDelegate& OnHoveredEventChanged() override { return OnHoveredEventChangedDelegate; }
	virtual Insights::FSelectedTrackChangedDelegate& OnSelectedTrackChanged() override { return OnSelectedTrackChangedDelegate; }
	virtual Insights::FSelectedEventChangedDelegate& OnSelectedEventChanged() override { return OnSelectedEventChangedDelegate; }

	////////////////////////////////////////////////////////////////////////////////////////////////////

	void UpdateScrollableTracksOrder();
	void OnTrackVisibilityChanged();

	bool IsGpuTrackVisible() const;
	bool IsCpuTrackVisible(uint32 InThreadId) const;

	////////////////////////////////////////////////////////////////////////////////////////////////////

	const FTimingTrackViewport& GetViewport() const { return Viewport; }

	const FVector2D& GetMousePosition() const { return MousePosition; }

	double GetSelectionStartTime() const { return SelectionStartTime; }
	double GetSelectionEndTime() const { return SelectionEndTime; }

	bool IsPanning() const { return bIsPanning; }
	bool IsSelecting() const { return bIsSelecting; }
	bool IsScrubbing() const { return bIsScrubbing; }

	bool IsTimeSelected(double Time) const { return Time >= SelectionStartTime && Time < SelectionEndTime; }
	bool IsTimeSelectedInclusive(double Time) const { return Time >= SelectionStartTime && Time <= SelectionEndTime; }

	void ScrollAtPosY(float ScrollPosY);
	void ScrollAtTime(double StartTime);
	void CenterOnTimeInterval(double IntervalStartTime, double IntervalDuration);
	void BringIntoView(double StartTime, double EndTime);
	void SelectTimeInterval(double IntervalStartTime, double IntervalDuration);
	void SetAndCenterOnTimeMarker(double Time);
	void SelectToTimeMarker(double Time);

	//bool AreTimeMarkersVisible() { return MarkersTrack->IsVisible(); }
	void SetTimeMarkersVisible(bool bOnOff);
	//bool IsDrawOnlyBookmarksEnabled() { return MarkersTrack->IsBookmarksTrack(); }
	void SetDrawOnlyBookmarks(bool bOnOff);

	TSharedPtr<FTimingGraphTrack> GetMainTimingGraphTrack() { return GraphTrack; }

	const TSharedPtr<FBaseTimingTrack> GetHoveredTrack() const { return HoveredTrack; }
	const TSharedPtr<const ITimingEvent> GetHoveredEvent() const { return HoveredEvent; }

	const TSharedPtr<FBaseTimingTrack> GetSelectedTrack() const { return SelectedTrack; }
	const TSharedPtr<const ITimingEvent> GetSelectedEvent() const { return SelectedEvent; }

	const TSharedPtr<ITimingEventFilter> GetEventFilter() const { return TimingEventFilter; }
	void SetEventFilter(const TSharedPtr<ITimingEventFilter> InEventFilter);

	const TSharedPtr<FBaseTimingTrack> GetTrackAt(float InPosX, float InPosY) const;

protected:
	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D(16.0f, 16.0f);
	}

	void UpdateOtherViews();

	void ShowContextMenu(const FPointerEvent& MouseEvent);

	/** Binds our UI commands to delegates. */
	void BindCommands();

	////////////////////////////////////////////////////////////////////////////////////////////////////

	void UpdatePositionForScrollableTracks();

	double EnforceHorizontalScrollLimits(const double InStartTime);
	float EnforceVerticalScrollLimits(const float InScrollPosY);

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

	void RaiseSelectionChanging();
	void RaiseSelectionChanged();

	void RaiseTimeMarkerChanging();
	void RaiseTimeMarkerChanged();

	void UpdateAggregatedStats();

	void UpdateHoveredTimingEvent(float InMousePosX, float InMousePosY);

	void OnSelectedTimingEventChanged();

	void SelectHoveredTimingTrack();
	void SelectHoveredTimingEvent();

	void SelectLeftTimingEvent();
	void SelectRightTimingEvent();
	void SelectUpTimingEvent();
	void SelectDownTimingEvent();

	void FrameSelection();

	// Get all the plugin extenders we care about
	TArray<Insights::ITimingViewExtender*> GetExtenders() const;

	FReply AllowTracksToProcessOnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);
	FReply AllowTracksToProcessOnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);
	FReply AllowTracksToProcessOnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);

protected:
	/** The track's viewport. Encapsulates info about position and scale. */
	FTimingTrackViewport Viewport;

	////////////////////////////////////////////////////////////

	/** All created tracks.
	  * Maps track id to track pointer.
	  */
	TMap<uint64, TSharedPtr<FBaseTimingTrack>> AllTracks;

	TArray<TSharedPtr<FBaseTimingTrack>> TopDockedTracks; /**< tracks docked on top, in the order to be displayed (top to bottom) */
	TArray<TSharedPtr<FBaseTimingTrack>> BottomDockedTracks; /**< tracks docked on bottom, in the order to be displayed (top to bottom) */
	TArray<TSharedPtr<FBaseTimingTrack>> ScrollableTracks; /**< tracks in scrollable area, in the order to be displayed (top to bottom) */
	TArray<TSharedPtr<FBaseTimingTrack>> ForegroundTracks; /**< tracks to draw over top/scrollable/bottom tracks (can use entire area), in the order to be displayed (back to front) */

	/** Whether the order of scrollable tracks is dirty and list need to be re-sorted */
	bool bScrollableTracksOrderIsDirty;

	////////////////////////////////////////////////////////////

	// Shared state for Frame Thread tracks
	TSharedPtr<FFrameSharedState> FrameSharedState;

	// Shared state for Cpu/Gpu Thread tracks
	TSharedPtr<FThreadTimingSharedState> ThreadTimingSharedState;

	// Shared state for Asset Loading tracks
	TSharedPtr<FLoadingSharedState> LoadingSharedState;
	bool bAssetLoadingMode;

	// Shared state for File Activity (I/O) tracks
	TSharedPtr<FFileActivitySharedState> FileActivitySharedState;

	////////////////////////////////////////////////////////////

	/** The time ruler track. */
	TSharedPtr<FTimeRulerTrack> TimeRulerTrack;

	/** The time markers track. */
	TSharedPtr<FMarkersTimingTrack> MarkersTrack;

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

	float LastScrollPosY;

	bool bIsLMB_Pressed;
	bool bIsRMB_Pressed;

	bool bIsSpaceBarKeyPressed;
	bool bIsDragging;

	////////////////////////////////////////////////////////////
	// Panning

	/** True, if the user is currently interactively panning the view (horizontally and/or vertically). */
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

	float OverscrollLeft;
	float OverscrollRight;
	float OverscrollTop;
	float OverscrollBottom;

	////////////////////////////////////////////////////////////
	// Selection

	/** True, if the user is currently changing the selection. */
	bool bIsSelecting;

	double SelectionStartTime;
	double SelectionEndTime;

	TSharedPtr<FBaseTimingTrack> HoveredTrack;
	TSharedPtr<const ITimingEvent> HoveredEvent;

	TSharedPtr<FBaseTimingTrack> SelectedTrack;
	TSharedPtr<const ITimingEvent> SelectedEvent;

	TSharedPtr<ITimingEventFilter> TimingEventFilter;

	FTooltipDrawState Tooltip;

	enum class ESelectionType
	{
		None,
		TimeRange,
		TimingEvent
	};
	ESelectionType LastSelectionType;

	double TimeMarker;

	/** Throttle flag, allowing tracks to control whether Slate throttle should take place */
	bool bPreventThrottling;

	/** True of the user is currently dragging the time marker */
	bool bIsScrubbing;

	////////////////////////////////////////////////////////////
	// Misc

	FGeometry ThisGeometry;

	const FSlateBrush* WhiteBrush;
	const FSlateFontInfo MainFont;

	// Debug stats
	int32 NumUpdatedEvents;
	TFixedCircularBuffer<uint64, 32> PreUpdateTracksDurationHistory;
	TFixedCircularBuffer<uint64, 32> UpdateTracksDurationHistory;
	TFixedCircularBuffer<uint64, 32> PostUpdateTracksDurationHistory;
	TFixedCircularBuffer<uint64, 32> TickDurationHistory;
	mutable TFixedCircularBuffer<uint64, 32> PreDrawTracksDurationHistory;
	mutable TFixedCircularBuffer<uint64, 32> DrawTracksDurationHistory;
	mutable TFixedCircularBuffer<uint64, 32> PostDrawTracksDurationHistory;
	mutable TFixedCircularBuffer<uint64, 32> TotalDrawDurationHistory;
	mutable TFixedCircularBuffer<uint64, 32> OnPaintDeltaTimeHistory;
	mutable uint64 LastOnPaintTime;

	////////////////////////////////////////////////////////////
	// Delegates

	Insights::FSelectionChangedDelegate OnSelectionChangedDelegate;
	Insights::FTimeMarkerChangedDelegate OnTimeMarkerChangedDelegate;
	Insights::FHoveredTrackChangedDelegate OnHoveredTrackChangedDelegate;
	Insights::FHoveredEventChangedDelegate OnHoveredEventChangedDelegate;
	Insights::FSelectedTrackChangedDelegate OnSelectedTrackChangedDelegate;
	Insights::FSelectedEventChangedDelegate OnSelectedEventChangedDelegate;
};
