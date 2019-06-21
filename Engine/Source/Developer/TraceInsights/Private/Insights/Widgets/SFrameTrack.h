// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/CursorReply.h"
#include "Input/Reply.h"
#include "Layout/Geometry.h"
#include "Rendering/RenderingCommon.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

// Insights
#include "Insights/Common/FixedCircularBuffer.h"
#include "Insights/ViewModels/FrameTrackHelper.h"
#include "Insights/ViewModels/FrameTrackViewport.h"

class SScrollBar;

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FSampleRef
{
	const FFrameTrackTimeline* Timeline;
	const FFrameTrackSample* Sample;

	FSampleRef(const FFrameTrackTimeline* InTimeline, const FFrameTrackSample* InSample)
		: Timeline(InTimeline), Sample(InSample)
	{
	}

	void Reset() { Timeline = nullptr; Sample = nullptr; }
	bool IsValid() const { return Timeline != nullptr && Sample != nullptr; }
};

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Widget used to present frames data in a track.
 */
class SFrameTrack : public SCompoundWidget
{
	enum
	{
		/** Number of pixels. */
		MOUSE_SNAP_DISTANCE = 4,
	};

	enum class EFrameTrackCursor
	{
		Default,
		Arrow,
		Hand,
	};

public:
	/** Default constructor. */
	SFrameTrack();

	/** Virtual destructor. */
	virtual ~SFrameTrack();

	/** Resets internal widget's data to the default one. */
	void Reset();

	SLATE_BEGIN_ARGS(SFrameTrack)
	{
		_Clipping = EWidgetClipping::ClipToBounds;
	}
	SLATE_END_ARGS()

	/**
	 * Construct this widget
	 * @param InArgs The declaration data for this widget
	 */
	void Construct(const FArguments& InArgs);

	/**
	 * Ticks this widget. Override in derived classes, but always call the parent implementation.
	 * @param  AllottedGeometry The space allotted for this widget
	 * @param  InCurrentTime  Current absolute real time
	 * @param  InDeltaTime  Real time passed since last tick
	 */
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

protected:
	bool IsReady() { return true; }

	void UpdateState();

	FSampleRef GetSampleAtMousePosition(float X, float Y);
	void SelectFrameAtMousePosition(float X, float Y);

	void ShowContextMenu(const FVector2D& ScreenSpacePosition);

	/** Binds our UI commands to delegates. */
	void BindCommands();

	/**
	 * Called when the user scrolls the horizontal scrollbar.
	 * @param ScrollOffset Scroll offset as a fraction between 0 and 1.
	 */
	void HorizontalScrollBar_OnUserScrolled(float ScrollOffset);
	void UpdateHorizontalScrollBar();

	void ZoomHorizontally(const float Delta, const float X);

	//////////////////////////////////////////////////
	// SelectionBoxChanged Event

public:
	/** The event to execute when the selection box has been changed. */
	DECLARE_EVENT_TwoParams(SFrameTrack, FSelectionBoxChangedEvent, int32 /*FrameStart*/, int32 /*FrameEnd*/);
	FSelectionBoxChangedEvent& OnSelectionBoxChanged() { return SelectionBoxChangedEvent; }
protected:
	/** The event to execute when the selection box has been changed. */
	FSelectionBoxChangedEvent SelectionBoxChangedEvent;

	//////////////////////////////////////////////////

protected:
	/** The track's viewport. Encapsulates info about position and scale. */
	FFrameTrackViewport Viewport;
	bool bIsViewportDirty;

	/** Cached info for timelines. */
	TMap<uint64, FFrameTrackTimeline> CachedTimelines;
	TArray<int32> TimelinesOrder;

	bool bIsStateDirty;

	uint64 AnalysisSyncNextTimestamp;

	//////////////////////////////////////////////////

	TSharedPtr<SScrollBar> HorizontalScrollBar;

	//////////////////////////////////////////////////
	// Panning, Zooming and Selection behaviors

	/** The current mouse position. */
	FVector2D MousePosition;

	/** Mouse position during the call on mouse button down. */
	FVector2D MousePositionOnButtonDown;
	float ViewportPosXOnButtonDown;
	float ViewportPosYOnButtonDown;

	/** Mouse position during the call on mouse button up. */
	FVector2D MousePositionOnButtonUp;

	bool bIsLMB_Pressed;
	bool bIsRMB_Pressed;

	/** True, if the user is currently interactively scrolling the view by holding the right mouse button and dragging. */
	bool bIsScrolling;

	//////////////////////////////////////////////////

	int32 SelectionStartFrameIndex;
	int32 SelectionEndFrameIndex;

	FSampleRef HoveredSample;

	//////////////////////////////////////////////////
	// Misc

	FGeometry ThisGeometry;

	/** Cursor type. */
	EFrameTrackCursor CursorType;

	// Debug stats
	int32 NumUpdatedFrames;
	TFixedCircularBuffer<uint64, 32> UpdateDurationHistory;
	mutable TFixedCircularBuffer<uint64, 32> DrawDurationHistory;
	mutable TFixedCircularBuffer<uint64, 32> OnPaintDurationHistory;
	mutable uint64 LastOnPaintTime;
};
