// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SGraphTrack.h"

#include "Containers/ArrayBuilder.h"
#include "Containers/MapBuilder.h"
#include "Fonts/FontMeasure.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Application/MenuStack.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Layout/WidgetPath.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "SlateOptMacros.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

// Insights
#include "Insights/Common/PaintUtils.h"
#include "Insights/Common/TimeUtils.h"
#include "Insights/InsightsStyle.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/ViewModels/DrawHelpers.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "SGraphTrack"

////////////////////////////////////////////////////////////////////////////////////////////////////
// SGraphTrack
////////////////////////////////////////////////////////////////////////////////////////////////////

SGraphTrack::SGraphTrack()
	: TimeRulerTrack(MakeShareable(new FTimeRulerTrack(0)))
	, GraphTrack(MakeShareable(new FRandomGraphTrack(1)))
	, WhiteBrush(FInsightsStyle::Get().GetBrush("WhiteBrush"))
	, MainFont(FCoreStyle::GetDefaultFontStyle("Regular", 8))
{
	Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

SGraphTrack::~SGraphTrack()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SGraphTrack::Reset()
{
	TimeRulerTrack->Reset();
	GraphTrack->Reset();
	StaticCastSharedPtr<FRandomGraphTrack>(GraphTrack)->AddDefaultSeries();

	Viewport.Reset();
	Viewport.SetMaxValidTime(84.0 * 60.0);
	//Viewport.SetScaleX((5 * 20) / 0.1); // 100ms between major tick marks

	bIsViewportDirty = true;
	bIsVerticalViewportDirty = true;

	MousePosition = FVector2D::ZeroVector;

	MousePositionOnButtonDown = FVector2D::ZeroVector;
	ViewportStartTimeOnButtonDown = 0.0;
	ViewportScrollPosYOnButtonDown = 0.0f;

	MousePositionOnButtonUp = FVector2D::ZeroVector;

	bIsLMB_Pressed = false;
	bIsRMB_Pressed = false;

	bIsDragging = false;

	bIsPanning = false;
	PanningMode = EPanningMode::None;

	bIsSelecting = false;
	SelectionStartTime = 0.0;
	SelectionEndTime = 0.0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SGraphTrack::Construct(const FArguments& InArgs)
{
	//TODO: Add toggle buttons for Points/Lines/Bars.
	//TODO: Add "show list of series" expand button + toggle buttons for each available graph series.
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SGraphTrack::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	//ThisGeometry = AllottedGeometry;

	float TrackWidth = AllottedGeometry.GetAbsoluteSize().X;
	float TrackHeight = AllottedGeometry.GetAbsoluteSize().Y;

	if (Viewport.UpdateSize(TrackWidth, TrackHeight) ||
		bIsViewportDirty ||
		bIsVerticalViewportDirty ||
		GraphTrack->GetHeight() != TrackHeight)
	{
		GraphTrack->SetPosY(TimeRulerTrack->GetHeight());
		GraphTrack->SetHeight(Viewport.GetHeight() - TimeRulerTrack->GetHeight());
		GraphTrack->SetDirtyFlag();
	}

	GraphTrack->Update(Viewport);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int32 SGraphTrack::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const bool bEnabled = ShouldBeEnabled(bParentEnabled);
	const ESlateDrawEffect DrawEffects = bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	FDrawContext DrawContext(AllottedGeometry, MyCullingRect, InWidgetStyle, DrawEffects, OutDrawElements, LayerId);

	GraphTrack->Draw(DrawContext, Viewport, MousePosition);

	TimeRulerTrack->Draw(DrawContext, Viewport, MousePosition, bIsSelecting, SelectionStartTime, SelectionEndTime);
	DrawContext.DrawBox(0.0f, TimeRulerTrack->GetHeight(), Viewport.GetWidth(), 1.0f, WhiteBrush, FLinearColor(0.05f, 0.05f, 0.05f, 1.0f));

	FDrawHelpers::DrawTimeRangeSelection(DrawContext, Viewport, SelectionStartTime, SelectionEndTime, WhiteBrush, MainFont);

	GraphTrack->PostDraw(DrawContext, Viewport, MousePosition);

	const bool bShouldDisplayDebugInfo = FInsightsManager::Get()->IsDebugInfoEnabled();
	if (bShouldDisplayDebugInfo)
	{
		const FSlateFontInfo& SummaryFont = MainFont;

		const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const float MaxFontCharHeight = FontMeasureService->Measure(TEXT("!"), SummaryFont).Y;
		const float DbgDY = MaxFontCharHeight;

		const float DbgW = 320.0f;
		const float DbgH = DbgDY * 2 + 3.0f;
		const float DbgX = Viewport.GetWidth() - DbgW - 20.0f;
		float DbgY = Viewport.GetTopOffset() + 10.0f;

		DrawContext.LayerId++;

		DrawContext.DrawBox(DbgX - 2.0f, DbgY - 2.0f, DbgW, DbgH, WhiteBrush, FLinearColor(1.0f, 1.0f, 1.0f, 0.9f));
		DrawContext.LayerId++;

		FLinearColor DbgTextColor(0.0f, 0.0f, 0.0f, 0.9f);

		//////////////////////////////////////////////////
		// Display viewport's horizontal info.

		DrawContext.DrawText
		(
			DbgX, DbgY,
			FString::Printf(TEXT("SX: %g, ST: %g, ET: %s"),
				Viewport.GetScaleX(),
				Viewport.GetStartTime(),
				*TimeUtils::FormatTimeAuto(Viewport.GetMaxValidTime())),
			SummaryFont, DbgTextColor
		);
		DbgY += DbgDY;

		//////////////////////////////////////////////////
		// Display viewport's vertical info.

		DrawContext.DrawText
		(
			DbgX, DbgY,
			FString::Printf(TEXT("Y: %.2f, H: %g, VH: %g"),
				Viewport.GetScrollPosY(),
				Viewport.GetScrollHeight(),
				Viewport.GetHeight()),
			SummaryFont, DbgTextColor
		);
		DbgY += DbgDY;
	}

	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled && IsEnabled());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SGraphTrack::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = FReply::Unhandled();

	MousePositionOnButtonDown = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	MousePosition = MousePositionOnButtonDown;

	bool bStartPanning = false;
	bool bStartSelecting = false;

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (!bIsRMB_Pressed)
		{
			bIsLMB_Pressed = true;

			if ((MousePositionOnButtonDown.Y < TimeRulerTrack->GetHeight() ||
				(MouseEvent.GetModifierKeys().IsControlDown() && MouseEvent.GetModifierKeys().IsShiftDown())))
			{
				bStartSelecting = true;
			}
			else
			{
				bStartPanning = true;
			}

			// Capture mouse, so we can drag outside this widget.
			Reply = FReply::Handled().CaptureMouse(SharedThis(this));
		}
	}
	else if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		if (!bIsLMB_Pressed)
		{
			bIsRMB_Pressed = true;

			if ((MousePositionOnButtonDown.Y < TimeRulerTrack->GetHeight() ||
				(MouseEvent.GetModifierKeys().IsControlDown() && MouseEvent.GetModifierKeys().IsShiftDown())))
			{
				bStartSelecting = true;
			}
			else
			{
				bStartPanning = true;
			}

			// Capture mouse, so we can drag outside this widget.
			Reply = FReply::Handled().CaptureMouse(SharedThis(this));
		}
	}

	if (bStartPanning)
	{
		bIsPanning = true;
		bIsDragging = false;

		ViewportStartTimeOnButtonDown = Viewport.GetStartTime();
		ViewportScrollPosYOnButtonDown = Viewport.GetScrollPosY();

		if (MouseEvent.GetModifierKeys().IsControlDown())
		{
			// Allow panning only horizontally.
			PanningMode = EPanningMode::Horizontal;
		}
		else if (MouseEvent.GetModifierKeys().IsShiftDown())
		{
			// Allow panning only vertically.
			PanningMode = EPanningMode::Vertical;
		}
		else
		{
			// Allow panning both horizontally and vertically.
			PanningMode = EPanningMode::HorizontalAndVertical;
		}
	}
	else if (bStartSelecting)
	{
		bIsSelecting = true;
		bIsDragging = false;

		SelectionStartTime = Viewport.SlateUnitsToTime(MousePositionOnButtonDown.X);
		SelectionEndTime = SelectionStartTime;
		//TODO: SelectionChangingEvent.Broadcast(SelectionStartTime, SelectionEndTime);
	}

	return Reply;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SGraphTrack::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = FReply::Unhandled();

	MousePositionOnButtonUp = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	MousePosition = MousePositionOnButtonUp;

	const bool bIsValidForMouseClick = MousePositionOnButtonUp.Equals(MousePositionOnButtonDown, MOUSE_SNAP_DISTANCE);

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (bIsLMB_Pressed)
		{
			if (bIsPanning)
			{
				PanningMode = EPanningMode::None;

				bIsPanning = false;
			}
			else if (bIsSelecting)
			{
				//TODO: SelectionChangedEvent.Broadcast(SelectionStartTime, SelectionEndTime);

				bIsSelecting = false;
			}

			if (bIsValidForMouseClick)
			{
				// Select single event...

				// When clicking on an empty space...
				//if (!SelectedTimingEvent.IsValid())
				{
					// ...reset selection.
					SelectionEndTime = SelectionStartTime = 0.0;
					//TODO: SelectionChangedEvent.Broadcast(SelectionStartTime, SelectionEndTime);
				}
			}

			bIsDragging = false;

			// Release mouse as we no longer drag.
			Reply = FReply::Handled().ReleaseMouseCapture();

			bIsLMB_Pressed = false;
		}
	}
	else if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		if (bIsRMB_Pressed)
		{
			if (bIsPanning)
			{
				PanningMode = EPanningMode::None;

				bIsPanning = false;
			}
			else if (bIsSelecting)
			{
				//TODO: SelectionChangedEvent.Broadcast(SelectionStartTime, SelectionEndTime);

				bIsSelecting = false;
			}

			if (bIsValidForMouseClick)
			{
				ShowContextMenu(MouseEvent);
			}

			bIsDragging = false;

			// Release mouse as we no longer drag.
			Reply = FReply::Handled().ReleaseMouseCapture();

			bIsRMB_Pressed = false;
		}
	}

	return Reply;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SGraphTrack::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = FReply::Unhandled();

	MousePosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	if (!MouseEvent.GetCursorDelta().IsZero())
	{
		if (bIsPanning)
		{
			if (HasMouseCapture())
			{
				bIsDragging = true;

				if ((int32)PanningMode & (int32)EPanningMode::Horizontal)
				{
					const double StartTime = ViewportStartTimeOnButtonDown + static_cast<double>(MousePositionOnButtonDown.X - MousePosition.X) / Viewport.GetScaleX();
					ScrollAtTime(StartTime);
				}

				if ((int32)PanningMode & (int32)EPanningMode::Vertical)
				{
					const float ScrollPosY = ViewportScrollPosYOnButtonDown + (MousePositionOnButtonDown.Y - MousePosition.Y);
					ScrollAtPosY(ScrollPosY);
				}
			}
		}
		else if (bIsSelecting)
		{
			if (HasMouseCapture())
			{
				bIsDragging = true;

				SelectionStartTime = Viewport.SlateUnitsToTime(MousePositionOnButtonDown.X);
				SelectionEndTime = Viewport.SlateUnitsToTime(MousePosition.X);
				if (SelectionStartTime > SelectionEndTime)
				{
					double Temp = SelectionStartTime;
					SelectionStartTime = SelectionEndTime;
					SelectionEndTime = Temp;
				}
				//TODO: SelectionChangingEvent.Broadcast(SelectionStartTime, SelectionEndTime);
			}
		}
		else
		{
			GraphTrack->UpdateHoveredState(MousePosition.X, MousePosition.Y, Viewport);
		}

		Reply = FReply::Handled();
	}

	return Reply;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SGraphTrack::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SGraphTrack::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	if (!HasMouseCapture())
	{
		// No longer dragging (unless we have mouse capture).
		bIsDragging = false;
		bIsPanning = false;
		bIsSelecting = false;

		bIsLMB_Pressed = false;
		bIsRMB_Pressed = false;
		
		MousePosition = FVector2D::ZeroVector;

		GraphTrack->UpdateHoveredState(MousePosition.X, MousePosition.Y, Viewport);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SGraphTrack::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetModifierKeys().IsShiftDown())
	{
		// Scroll vertically.
		constexpr float ScrollSpeedY = 16.0f * 3;
		const float ScrollPosY = Viewport.GetScrollPosY() - ScrollSpeedY * MouseEvent.GetWheelDelta();
		ScrollAtPosY(ScrollPosY);
	}
	else if (MouseEvent.GetModifierKeys().IsControlDown())
	{
		// Scroll horizontally.
		const double ScrollSpeedX = Viewport.GetDurationForViewportDX(16.0 * 3);
		ScrollAtTime(Viewport.GetStartTime() - ScrollSpeedX * MouseEvent.GetWheelDelta());
	}
	else
	{
		// Zoom in/out horizontally.
		const double Delta = MouseEvent.GetWheelDelta();
		MousePosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		if (Viewport.RelativeZoomWithFixedX(Delta, MousePosition.X))
		{
			//UpdateHorizontalScrollBar();
			bIsViewportDirty = true;
		}
	}

	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SGraphTrack::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	//...

	return FReply::Unhandled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SGraphTrack::OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	SCompoundWidget::OnDragEnter(MyGeometry, DragDropEvent);

	//...
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SGraphTrack::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	SCompoundWidget::OnDragLeave(DragDropEvent);

	//...
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SGraphTrack::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	//...

	return SCompoundWidget::OnDragOver(MyGeometry,DragDropEvent);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SGraphTrack::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	//...

	return SCompoundWidget::OnDrop(MyGeometry,DragDropEvent);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FCursorReply SGraphTrack::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	if (bIsPanning)
	{
		if (bIsDragging)
		{
			//return FCursorReply::Cursor(EMouseCursor::GrabHandClosed);
			return FCursorReply::Cursor(EMouseCursor::GrabHand);
		}
	}
	else if (bIsSelecting)
	{
		if (bIsDragging)
		{
			return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
		}
	}

	return FCursorReply::Unhandled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SGraphTrack::ScrollAtPosY(float ScrollPosY)
{
	if (ScrollPosY != Viewport.GetScrollPosY())
	{
		Viewport.SetScrollPosY(ScrollPosY);

		//UpdateVerticalScrollBar();
		bIsVerticalViewportDirty = true;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SGraphTrack::ScrollAtTime(double StartTime)
{
	if (Viewport.ScrollAtTime(StartTime))
	{
		//UpdateHorizontalScrollBar();
		bIsViewportDirty = true;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SGraphTrack::ShowContextMenu(const FPointerEvent& MouseEvent)
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, NULL);

	GraphTrack->BuildContextMenu(MenuBuilder);

	TSharedRef<SWidget> MenuWidget = MenuBuilder.MakeWidget();

	FWidgetPath EventPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();
	const FVector2D ScreenSpacePosition = MouseEvent.GetScreenSpacePosition();
	FSlateApplication::Get().PushMenu(SharedThis(this), EventPath, MenuWidget, ScreenSpacePosition, FPopupTransitionEffect::ContextMenu);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
