// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SGraphTrack.h"

#include "Brushes/SlateBorderBrush.h"
#include "Brushes/SlateBoxBrush.h"
#include "Brushes/SlateColorBrush.h"
#include "Containers/ArrayBuilder.h"
#include "Containers/MapBuilder.h"
#include "EditorStyleSet.h"
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
#include "Insights/TimingProfilerManager.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "SGraphTrack"

////////////////////////////////////////////////////////////////////////////////////////////////////
// SGraphTrack
////////////////////////////////////////////////////////////////////////////////////////////////////

SGraphTrack::SGraphTrack()
	: TimeRulerTrack(0)
	, GraphTrack(1)
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
	TimeRulerTrack.Reset();
	GraphTrack.Reset();
	Viewport.ScaleX = (5 * 20) / 0.1; // 100ms between major tick marks
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

	bool bIsGraphDirty = false;

	if (Viewport.UpdateSize(TrackWidth, TrackHeight))
	{
		bIsGraphDirty = true;
	}

	if (GraphTrack.GetHeight() != TrackHeight)
	{
		bIsGraphDirty = true;
	}

	if (bIsGraphDirty)
	{
		bIsGraphDirty = false;

		GraphTrack.SetPosY(TimeRulerTrack.GetHeight());
		GraphTrack.SetHeight(Viewport.Height - TimeRulerTrack.GetHeight());
		GraphTrack.Update(Viewport);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int32 SGraphTrack::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const bool bEnabled = ShouldBeEnabled(bParentEnabled);
	const ESlateDrawEffect DrawEffects = bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	FDrawContext DrawContext(AllottedGeometry, MyCullingRect, InWidgetStyle, DrawEffects, OutDrawElements, LayerId);

	GraphTrack.Draw(DrawContext, Viewport);
	TimeRulerTrack.Draw(DrawContext, Viewport);

	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled && IsEnabled());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SGraphTrack::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	//...

	return FReply::Unhandled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SGraphTrack::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	//...

	return FReply::Unhandled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SGraphTrack::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	//...

	return FReply::Unhandled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SGraphTrack::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	//...
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SGraphTrack::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	//...
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SGraphTrack::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	//...

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
	//...

	return FCursorReply::Unhandled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
