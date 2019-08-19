// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SPacketBreakdownView.h"

#include "Fonts/FontMeasure.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SScrollBar.h"

// Insights
#include "Insights/Common/PaintUtils.h"
#include "Insights/Common/Stopwatch.h"
#include "Insights/InsightsManager.h"
#include "Insights/InsightsStyle.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "SPacketBreakdownView"

////////////////////////////////////////////////////////////////////////////////////////////////////

SPacketBreakdownView::SPacketBreakdownView()
	: DrawState(MakeShareable(new FPacketBreakdownViewDrawState()))
{
	Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

SPacketBreakdownView::~SPacketBreakdownView()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketBreakdownView::Reset()
{
	Viewport.Reset();
	//FAxisViewportDouble& ViewportX = Viewport.GetHorizontalAxisViewport();
	//ViewportX.SetScaleLimits(0.000001, 100000.0);
	//ViewportX.SetScale(1.0);
	bIsViewportDirty = true;

	PacketFrameIndex = -1;
	PacketSize = 0;
	DrawState->Reset();
	bIsStateDirty = true;

	MousePosition = FVector2D::ZeroVector;

	MousePositionOnButtonDown = FVector2D::ZeroVector;
	ViewportPosXOnButtonDown = 0.0f;

	MousePositionOnButtonUp = FVector2D::ZeroVector;

	bIsLMB_Pressed = false;
	bIsRMB_Pressed = false;

	bIsScrolling = false;

	HoveredEvent.Reset();
	Tooltip.Reset();

	//ThisGeometry

	CursorType = ECursorType::Default;

	UpdateDurationHistory.Reset();
	DrawDurationHistory.Reset();
	OnPaintDurationHistory.Reset();
	LastOnPaintTime = FPlatformTime::Cycles64();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketBreakdownView::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SOverlay)
		.Visibility(EVisibility::SelfHitTestInvisible)

		+ SOverlay::Slot()
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(0, 0, 0, 0))
		[
			SAssignNew(HorizontalScrollBar, SScrollBar)
			.Orientation(Orient_Horizontal)
			.AlwaysShowScrollbar(false)
			.Visibility(EVisibility::Visible)
			.Thickness(FVector2D(5.0f, 5.0f))
			.RenderOpacity(0.75)
			.OnUserScrolled(this, &SPacketBreakdownView::HorizontalScrollBar_OnUserScrolled)
		]
	];

	UpdateHorizontalScrollBar();

	BindCommands();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketBreakdownView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (ThisGeometry != AllottedGeometry || bIsViewportDirty)
	{
		bIsViewportDirty = false;
		const float ViewWidth = AllottedGeometry.GetLocalSize().X;
		const float ViewHeight = AllottedGeometry.GetLocalSize().Y;
		Viewport.SetSize(ViewWidth, ViewHeight);
		bIsStateDirty = true;
	}

	ThisGeometry = AllottedGeometry;

	FAxisViewportDouble& ViewportX = Viewport.GetHorizontalAxisViewport();

	if (!bIsScrolling)
	{
		// Elastic snap to horizontal limits.
		if (ViewportX.UpdatePosWithinLimits())
		{
			bIsStateDirty = true;
		}
	}

	if (bIsStateDirty)
	{
		bIsStateDirty = false;
		UpdateState();
	}

	Tooltip.Update();
	if (!MousePosition.IsZero())
	{
		Tooltip.SetPosition(MousePosition, 0.0f, Viewport.GetWidth(), 0.0f, Viewport.GetHeight() - 12.0f);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketBreakdownView::SetPacket(int32 InFrameIndex, int64 InBitSize)
{
	PacketFrameIndex = InFrameIndex;
	PacketSize = InBitSize;

	FAxisViewportDouble& ViewportX = Viewport.GetHorizontalAxisViewport();
	ViewportX.SetMinMaxValueInterval(0.0f, static_cast<double>(InBitSize));
	ViewportX.CenterOnValueInterval(0.0f, static_cast<double>(InBitSize));
	UpdateHorizontalScrollBar();

	DrawState->Reset();
	bIsStateDirty = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void AddMockEventsRec(FPacketBreakdownViewDrawStateBuilder& Builder, FRandomStream& RandomStream , int32 Depth, int64 Offset, int64 Size, int32 Type)
{
	Builder.AddEvent(Offset, Size, Type, Depth);

	int64 ChildEventOffset = Offset;
	while (ChildEventOffset < Offset + Size)
	{
		int64 ChildEventSize = RandomStream.RandRange(1, Size);
		if (ChildEventOffset + ChildEventSize > Offset + Size)
		{
			ChildEventSize = Offset + Size - ChildEventOffset;
		}

		if (ChildEventOffset != Offset || ChildEventSize != Size)
		{
			int32 ChildEventType = RandomStream.RandRange(1, 1000);
			AddMockEventsRec(Builder, RandomStream, Depth + 1, ChildEventOffset, ChildEventSize, ChildEventType);
		}

		ChildEventOffset += ChildEventSize;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketBreakdownView::UpdateState()
{
	FStopwatch Stopwatch;
	Stopwatch.Start();

	if (PacketFrameIndex >= 0 && PacketSize > 0)
	{
		FPacketBreakdownViewDrawStateBuilder Builder(*DrawState, Viewport);

		// Init packet content with mock data.
		FRandomStream RandomStream(PacketFrameIndex);
		AddMockEventsRec(Builder, RandomStream, 0, 0, PacketSize, 0);

		Builder.Flush();
	}

	Stopwatch.Stop();
	UpdateDurationHistory.AddValue(Stopwatch.AccumulatedTime);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketBreakdownView::UpdateHoveredEvent()
{
	HoveredEvent = GetEventAtMousePosition(MousePosition.X, MousePosition.Y);
	//if (!HoveredEvent.IsValid())
	//{
	//	HoveredEvent = GetEventAtMousePosition(MousePosition.X - 1.0f, MousePosition.Y);
	//}
	//if (!HoveredEvent.IsValid())
	//{
	//	HoveredEvent = GetEventAtMousePosition(MousePosition.X + 1.0f, MousePosition.Y);
	//}

	if (HoveredEvent.IsValid())
	{
		// Init the tooltip's content.
		Tooltip.ResetContent();
		Tooltip.AddTitle(FString::Printf(TEXT("NetObject%d"), HoveredEvent.Event.Type));
		Tooltip.AddNameValueTextLine(TEXT("Offset:"), FString::Format(TEXT("bit {0}"), { FText::AsNumber(HoveredEvent.Event.Offset).ToString() }));
		if (HoveredEvent.Event.Size == 1)
		{
			Tooltip.AddNameValueTextLine(TEXT("Size:"), TEXT("1 bit"));
		}
		else
		{
			Tooltip.AddNameValueTextLine(TEXT("Size:"), FString::Format(TEXT("{0} bits"), { FText::AsNumber(HoveredEvent.Event.Size).ToString() }));
		}
		Tooltip.AddNameValueTextLine(TEXT("Type:"), FText::AsNumber(HoveredEvent.Event.Type).ToString());
		Tooltip.AddNameValueTextLine(TEXT("Depth:"), FText::AsNumber(HoveredEvent.Event.Depth).ToString());
		Tooltip.UpdateLayout();

		Tooltip.SetDesiredOpacity(1.0f);
	}
	else
	{
		Tooltip.SetDesiredOpacity(0.0f);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FNetworkPacketEventRef SPacketBreakdownView::GetEventAtMousePosition(float X, float Y)
{
	if (!bIsStateDirty)
	{
		for (const auto& Event : DrawState->Events)
		{
			const FAxisViewportDouble& ViewportX = Viewport.GetHorizontalAxisViewport();

			const float EventX1 = ViewportX.GetRoundedOffsetForValue(static_cast<double>(Event.Offset));
			const float EventX2 = ViewportX.GetRoundedOffsetForValue(static_cast<double>(Event.Offset + Event.Size));

			constexpr float Y0 = 0.0f;
			constexpr float EventH = 14.0f;
			constexpr float EventDY = 2.0f;
			const float EventY = Y0 + (EventH + EventDY) * Event.Depth;

			constexpr float ToleranceX = 1.0f;

			if (X >= EventX1 - ToleranceX && X <= EventX2 &&
				Y >= EventY - EventDY / 2 && Y < EventY + EventH + EventDY / 2)
			{
				return FNetworkPacketEventRef(Event.Offset, Event.Size, Event.Type, Event.Depth);
			}
		}
	}
	return FNetworkPacketEventRef();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int32 SPacketBreakdownView::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const bool bEnabled = ShouldBeEnabled(bParentEnabled);
	const ESlateDrawEffect DrawEffects = bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	FDrawContext DrawContext(AllottedGeometry, MyCullingRect, InWidgetStyle, DrawEffects, OutDrawElements, LayerId);

	const float ViewWidth = AllottedGeometry.Size.X;
	const float ViewHeight = AllottedGeometry.Size.Y;

	//////////////////////////////////////////////////
	{
		FStopwatch Stopwatch;
		Stopwatch.Start();

		FPacketBreakdownViewDrawHelper Helper(DrawContext, Viewport);

		Helper.DrawBackground();

		// Draw the cached state (the network events of the package breakdown).
		Helper.Draw(*DrawState);

		// Highlight the hovered event.
		if (HoveredEvent.IsValid())
		{
			Helper.DrawEventHighlight(HoveredEvent.Event);
		}

		// Draw tooltip for hovered Event.
		Tooltip.Draw(DrawContext);

		Stopwatch.Stop();
		DrawDurationHistory.AddValue(Stopwatch.AccumulatedTime);
	}
	//////////////////////////////////////////////////

	const bool bShouldDisplayDebugInfo = FInsightsManager::Get()->IsDebugInfoEnabled();
	if (bShouldDisplayDebugInfo)
	{
		const FSlateBrush* WhiteBrush = FInsightsStyle::Get().GetBrush("WhiteBrush");
		FSlateFontInfo SummaryFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);

		const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const float MaxFontCharHeight = FontMeasureService->Measure(TEXT("!"), SummaryFont).Y;
		const float DbgDY = MaxFontCharHeight;

		const float DbgW = 260.0f;
		const float DbgH = DbgDY * 4 + 3.0f;
		const float DbgX = ViewWidth - DbgW - 20.0f;
		float DbgY = 7.0f;

		DrawContext.LayerId++;

		DrawContext.DrawBox(DbgX - 2.0f, DbgY - 2.0f, DbgW, DbgH, WhiteBrush, FLinearColor(1.0, 1.0, 1.0, 0.9));
		DrawContext.LayerId++;

		FLinearColor DbgTextColor(0.0, 0.0, 0.0, 0.9);

		// Time interval since last OnPaint call.
		const uint64 CurrentTime = FPlatformTime::Cycles64();
		const uint64 OnPaintDuration = CurrentTime - LastOnPaintTime;
		LastOnPaintTime = CurrentTime;
		OnPaintDurationHistory.AddValue(OnPaintDuration);
		const uint64 AvgOnPaintDuration = OnPaintDurationHistory.ComputeAverage();
		const uint64 AvgOnPaintDurationMs = FStopwatch::Cycles64ToMilliseconds(AvgOnPaintDuration);
		const double AvgOnPaintFps = AvgOnPaintDurationMs != 0 ? 1.0 / FStopwatch::Cycles64ToSeconds(AvgOnPaintDuration) : 0.0;

		const uint64 AvgUpdateDurationMs = FStopwatch::Cycles64ToMilliseconds(UpdateDurationHistory.ComputeAverage());
		const uint64 AvgDrawDurationMs = FStopwatch::Cycles64ToMilliseconds(DrawDurationHistory.ComputeAverage());

		// Draw performance info.
		DrawContext.DrawText
		(
			DbgX, DbgY,
			FString::Printf(TEXT("U: %llu ms    D: %llu ms + %llu ms = %llu ms (%d fps)"),
				AvgUpdateDurationMs, // average duration of UpdateState calls
				AvgDrawDurationMs, // drawing time
				AvgOnPaintDurationMs - AvgDrawDurationMs, // other overhead to OnPaint calls
				AvgOnPaintDurationMs, // average time between two OnPaint calls
				FMath::RoundToInt(AvgOnPaintFps)), // framerate of OnPaint calls
			SummaryFont, DbgTextColor
		);
		DbgY += DbgDY;

		// Draw "the update stats".
		DrawContext.DrawText
		(
			DbgX, DbgY,
			FString::Printf(TEXT("U: %s events"),
				*FText::AsNumber(DrawState->Events.Num()).ToString()),
			SummaryFont, DbgTextColor
		);
		DbgY += DbgDY;

		// Draw "the draw stats".
		DrawContext.DrawText
		(
			DbgX, DbgY,
			FString::Printf(TEXT("D: %s boxes, %s borders (%s merged), %s texts"),
				*FText::AsNumber(DrawState->Boxes.Num()).ToString(),
				*FText::AsNumber(DrawState->Borders.Num()).ToString(),
				*FText::AsNumber(DrawState->GetNumMergedBoxes()).ToString(),
				*FText::AsNumber(DrawState->Texts.Num()).ToString()),
			SummaryFont, DbgTextColor
		);
		DbgY += DbgDY;

		// Draw viewport's horizontal info.
		DrawContext.DrawText
		(
			DbgX, DbgY,
			Viewport.GetHorizontalAxisViewport().ToDebugString(TEXT("X")),
			SummaryFont, DbgTextColor
		);
		DbgY += DbgDY;
	}

	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled && IsEnabled());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SPacketBreakdownView::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = FReply::Unhandled();

	MousePositionOnButtonDown = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	ViewportPosXOnButtonDown = Viewport.GetHorizontalAxisViewport().GetPos();

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bIsLMB_Pressed = true;

		// Capture mouse.
		Reply = FReply::Handled().CaptureMouse(SharedThis(this));
	}
	else if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		bIsRMB_Pressed = true;

		// Capture mouse, so we can scroll outside this widget.
		Reply = FReply::Handled().CaptureMouse(SharedThis(this));
	}

	return Reply;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SPacketBreakdownView::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = FReply::Unhandled();

	MousePositionOnButtonUp = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	const bool bIsValidForMouseClick = MousePositionOnButtonUp.Equals(MousePositionOnButtonDown, MOUSE_SNAP_DISTANCE);

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (bIsLMB_Pressed)
		{
			if (bIsScrolling)
			{
				bIsScrolling = false;
				CursorType = ECursorType::Default;
			}
			else if (bIsValidForMouseClick)
			{
				//SelectEventAtMousePosition(MousePositionOnButtonUp.X, MousePositionOnButtonUp.Y);
			}

			bIsLMB_Pressed = false;

			// Release the mouse.
			Reply = FReply::Handled().ReleaseMouseCapture();
		}
	}
	else if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		if (bIsRMB_Pressed)
		{
			if (bIsScrolling)
			{
				bIsScrolling = false;
				CursorType = ECursorType::Default;
			}
			else if (bIsValidForMouseClick)
			{
				//ShowContextMenu(MouseEvent);
			}

			bIsRMB_Pressed = false;

			// Release mouse as we no longer scroll.
			Reply = FReply::Handled().ReleaseMouseCapture();
		}
	}

	return Reply;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SPacketBreakdownView::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = FReply::Unhandled();

	MousePosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	if (!MouseEvent.GetCursorDelta().IsZero())
	{
		if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) ||
			MouseEvent.IsMouseButtonDown(EKeys::RightMouseButton))
		{
			if (HasMouseCapture())
			{
				if (!bIsScrolling)
				{
					bIsScrolling = true;
					CursorType = ECursorType::Hand;

					HoveredEvent.Reset();
					Tooltip.SetDesiredOpacity(0.0f);
				}

				FAxisViewportDouble& ViewportX = Viewport.GetHorizontalAxisViewport();
				const float PosX = ViewportPosXOnButtonDown + (MousePositionOnButtonDown.X - MousePosition.X);
				ViewportX.ScrollAtPos(PosX);
				UpdateHorizontalScrollBar();
				bIsStateDirty = true;
			}
		}
		else
		{
			UpdateHoveredEvent();
		}

		Reply = FReply::Handled();
	}

	return Reply;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketBreakdownView::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketBreakdownView::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	if (!HasMouseCapture())
	{
		// No longer scrolling (unless we have mouse capture).
		bIsScrolling = false;

		bIsLMB_Pressed = false;
		bIsRMB_Pressed = false;

		MousePosition = FVector2D::ZeroVector;

		HoveredEvent.Reset();
		Tooltip.SetDesiredOpacity(0.0f);

		CursorType = ECursorType::Default;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SPacketBreakdownView::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	MousePosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	//if (MouseEvent.GetModifierKeys().IsShiftDown())
	//{
	//}
	//else //if (MouseEvent.GetModifierKeys().IsControlDown())
	{
		// Zoom in/out horizontally.
		const float Delta = MouseEvent.GetWheelDelta();
		ZoomHorizontally(Delta, MousePosition.X);
	}

	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SPacketBreakdownView::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return FReply::Unhandled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FCursorReply SPacketBreakdownView::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	FCursorReply CursorReply = FCursorReply::Unhandled();

	if (CursorType == ECursorType::Arrow)
	{
		CursorReply = FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
	}
	else if (CursorType == ECursorType::Hand)
	{
		CursorReply = FCursorReply::Cursor(EMouseCursor::GrabHand);
	}

	return CursorReply;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketBreakdownView::BindCommands()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketBreakdownView::HorizontalScrollBar_OnUserScrolled(float ScrollOffset)
{
	FAxisViewportDouble& ViewportX = Viewport.GetHorizontalAxisViewport();
	ViewportX.OnUserScrolled(HorizontalScrollBar, ScrollOffset);
	bIsStateDirty = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketBreakdownView::UpdateHorizontalScrollBar()
{
	FAxisViewportDouble& ViewportX = Viewport.GetHorizontalAxisViewport();
	ViewportX.UpdateScrollBar(HorizontalScrollBar);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketBreakdownView::ZoomHorizontally(const float Delta, const float X)
{
	FAxisViewportDouble& ViewportX = Viewport.GetHorizontalAxisViewport();
	ViewportX.RelativeZoomWithFixedOffset(Delta, X);
	UpdateHorizontalScrollBar();
	bIsStateDirty = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
