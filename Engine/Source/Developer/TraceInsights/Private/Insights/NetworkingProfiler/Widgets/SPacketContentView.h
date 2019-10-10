// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/CursorReply.h"
#include "Input/Reply.h"
#include "Layout/Geometry.h"
#include "Rendering/RenderingCommon.h"
#include "TraceServices/Model/NetProfiler.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

// Insights
#include "Insights/Common/FixedCircularBuffer.h"
#include "Insights/NetworkingProfiler/ViewModels/PacketContentViewDrawHelper.h"
#include "Insights/NetworkingProfiler/ViewModels/PacketContentViewport.h"
#include "Insights/ViewModels/TooltipDrawState.h"

class SScrollBar;
class SNetworkingProfilerWindow;

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FNetworkPacketEventRef
{
	FNetworkPacketEvent Event;
	bool bIsValid;

	FNetworkPacketEventRef()
		: Event()
		, bIsValid(false)
	{
	}

	FNetworkPacketEventRef(const FNetworkPacketEvent& InEvent)
		: Event(InEvent)
		, bIsValid(true)
	{
	}

	FNetworkPacketEventRef(const FNetworkPacketEventRef& Other)
		: Event(Other.Event)
		, bIsValid(Other.bIsValid)
	{
	}

	FNetworkPacketEventRef& operator=(const FNetworkPacketEventRef& Other)
	{
		Event = Other.Event;
		bIsValid = Other.bIsValid;
		return *this;
	}

	void Reset()
	{
		bIsValid = false;
	}

	bool IsValid() const
	{
		return bIsValid;
	}

	bool Equals(const FNetworkPacketEventRef& Other) const
	{
		return bIsValid == Other.bIsValid && Event.Equals(Other.Event);
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
class SPacketContentView : public SCompoundWidget
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
	SPacketContentView();

	/** Virtual destructor. */
	virtual ~SPacketContentView();

	/** Resets internal widget's data to the default one. */
	void Reset();

	SLATE_BEGIN_ARGS(SPacketContentView) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<SNetworkingProfilerWindow> InProfilerWindow);

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

	void ResetPacket();
	void SetPacket(uint32 InGameInstanceIndex, uint32 InConnectionIndex, Trace::ENetProfilerConnectionMode InConnectionMode, uint32 InPacketIndex, int64 InPacketBitSize);

private:
	//void ShowContextMenu(const FPointerEvent& MouseEvent);

	/** Binds our UI commands to delegates. */
	void BindCommands();

	////////////////////////////////////////////////////////////////////////////////////////////////////

	/**
	 * Called when the user scrolls the horizontal scrollbar.
	 * @param ScrollOffset Scroll offset as a fraction between 0 and 1.
	 */
	void HorizontalScrollBar_OnUserScrolled(float ScrollOffset);
	void UpdateHorizontalScrollBar();

	void ZoomHorizontally(const float Delta, const float X);

	////////////////////////////////////////////////////////////////////////////////////////////////////

	void UpdateState();

	void UpdateHoveredEvent();
	FNetworkPacketEventRef GetEventAtMousePosition(float X, float Y);

	void OnSelectedEventChanged();
	void SelectHoveredEvent();

private:
	TSharedPtr<SNetworkingProfilerWindow> ProfilerWindow;

	/** The track's viewport. Encapsulates info about position and scale. */
	FPacketContentViewport Viewport;
	bool bIsViewportDirty;

	uint32 GameInstanceIndex;
	uint32 ConnectionIndex;
	Trace::ENetProfilerConnectionMode ConnectionMode;
	uint32 PacketIndex;
	int64 PacketBitSize; // total number of bits; [bit]

	/** Cached draw state of the packet content (i.e. all it needs to render). */
	TSharedPtr<FPacketContentViewDrawState> DrawState;
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
	FNetworkPacketEventRef SelectedEvent;

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
