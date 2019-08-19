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
#include "Insights/NetworkingProfiler/ViewModels/PacketBreakdownViewHelper.h"
#include "Insights/NetworkingProfiler/ViewModels/PacketBreakdownViewport.h"
#include "Insights/ViewModels/TooltipDrawState.h"

class SScrollBar;

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FNetworkPacketEventRef
{
	FNetworkPacketEvent Event;

	FNetworkPacketEventRef()
	: Event(0, 0, -1, -1)
	{
	}

	FNetworkPacketEventRef(int64 InOffset, int64 InSize, int32 InType, int32 InDepth)
		: Event(InOffset, InSize, InType, InDepth)
	{
	}

	FNetworkPacketEventRef(const FNetworkPacketEventRef& Other)
		: Event(Other.Event)
	{
	}

	FNetworkPacketEventRef& operator=(const FNetworkPacketEventRef& Other)
	{
		Event = Other.Event;
		return *this;
	}

	void Reset()
	{
		Event.Type = -1;
	}

	bool IsValid() const
	{
		return Event.Type >= 0;
	}

	bool Equals(const FNetworkPacketEventRef& Other) const
	{
		return Event.Equals(Other.Event);
	}

	static bool AreEquals(const FNetworkPacketEventRef& A, const FNetworkPacketEventRef& B)
	{
		return A.Equals(B);
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Widget used to present content of a network packet.
 */
class SPacketBreakdownView : public SCompoundWidget
{
public:
	/** Number of pixels. */
	static constexpr float MOUSE_SNAP_DISTANCE = 2.0f;

	enum class ECursorType
	{
		Default,
		Arrow,
		Hand,
	};

public:
	/** Default constructor. */
	SPacketBreakdownView();

	/** Virtual destructor. */
	virtual ~SPacketBreakdownView();

	/** Resets internal widget's data to the default one. */
	void Reset();

	SLATE_BEGIN_ARGS(SPacketBreakdownView) {}
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

	void SetPacket(int32 InPacketFrameIndex, int64 InPacketBitSize);

private:
	void UpdateState();
	void UpdateHoveredEvent();
	FNetworkPacketEventRef GetEventAtMousePosition(float X, float Y);

	//void ShowContextMenu(const FPointerEvent& MouseEvent);

	/** Binds our UI commands to delegates. */
	void BindCommands();

	/**
	 * Called when the user scrolls the horizontal scrollbar.
	 * @param ScrollOffset Scroll offset as a fraction between 0 and 1.
	 */
	void HorizontalScrollBar_OnUserScrolled(float ScrollOffset);
	void UpdateHorizontalScrollBar();

	void ZoomHorizontally(const float Delta, const float X);

private:
	/** The track's viewport. Encapsulates info about position and scale. */
	FPacketBreakdownViewport Viewport;
	bool bIsViewportDirty;

	int32 PacketFrameIndex;
	int64 PacketSize; // total number of bits; [bit]

	/** Cached draw state of the packet content (i.e. all it needs to render). */
	TSharedPtr<FPacketBreakdownViewDrawState> DrawState;
	bool bIsStateDirty;
	//////////////////////////////////////////////////

	TSharedPtr<SScrollBar> HorizontalScrollBar;

	//////////////////////////////////////////////////
	// Panning and Zooming behaviors

	/** The current mouse position. */
	FVector2D MousePosition;

	/** Mouse position during the call on mouse button down. */
	FVector2D MousePositionOnButtonDown;
	float ViewportPosXOnButtonDown;

	/** Mouse position during the call on mouse button up. */
	FVector2D MousePositionOnButtonUp;

	bool bIsLMB_Pressed;
	bool bIsRMB_Pressed;

	/** True, if the user is currently interactively scrolling the view (ex.: by holding the left mouse button and dragging). */
	bool bIsScrolling;

	//////////////////////////////////////////////////
	// Selection

	FNetworkPacketEventRef HoveredEvent;
	FTooltipDrawState Tooltip;

	//////////////////////////////////////////////////
	// Misc

	FGeometry ThisGeometry;

	/** Cursor type. */
	ECursorType CursorType;

	// Debug stats
	TFixedCircularBuffer<uint64, 32> UpdateDurationHistory;
	mutable TFixedCircularBuffer<uint64, 32> DrawDurationHistory;
	mutable TFixedCircularBuffer<uint64, 32> OnPaintDurationHistory;
	mutable uint64 LastOnPaintTime;
};
