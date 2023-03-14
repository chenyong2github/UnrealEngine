// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWaveformViewerOverlay.h"

#include "AudioWidgetsUtils.h"
#include "Widgets/SLeafWidget.h"

void SWaveformViewerOverlay::Construct(const FArguments& InArgs, const TArray<TSharedPtr<SWidget>>& InOverlaidWidgets)
{
	if (InArgs._Style)
	{
		Style = InArgs._Style;
		DesiredWidth = Style->DesiredWidth;
		DesiredHeight = Style->DesiredHeight;
	}
	
	OverlaidWidgets = InOverlaidWidgets;
}

FReply SWaveformViewerOverlay::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return AudioWidgetsUtils::RouteMouseInput(&SWidget::OnMouseButtonDown, MouseEvent, MakeArrayView(OverlaidWidgets.GetData(), OverlaidWidgets.Num()));
}

FReply SWaveformViewerOverlay::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return AudioWidgetsUtils::RouteMouseInput(&SWidget::OnMouseButtonUp, MouseEvent, MakeArrayView(OverlaidWidgets.GetData(), OverlaidWidgets.Num()));
}

FReply SWaveformViewerOverlay::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return AudioWidgetsUtils::RouteMouseInput(&SWidget::OnMouseMove, MouseEvent, MakeArrayView(OverlaidWidgets.GetData(), OverlaidWidgets.Num()));
}

FReply SWaveformViewerOverlay::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply InteractionReply = AudioWidgetsUtils::RouteMouseInput(&SWidget::OnMouseWheel, MouseEvent, MakeArrayView(OverlaidWidgets.GetData(), OverlaidWidgets.Num()));

	if (!InteractionReply.IsEventHandled())
	{
		OnNewMouseDelta.ExecuteIfBound(MouseEvent.GetWheelDelta());
		InteractionReply = FReply::Handled();
	}

	return InteractionReply;

}

FCursorReply SWaveformViewerOverlay::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	return AudioWidgetsUtils::RouteCursorQuery(CursorEvent, MakeArrayView(OverlaidWidgets.GetData(), OverlaidWidgets.Num()));
}

int32 SWaveformViewerOverlay::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	return ++LayerId;
}

FVector2D SWaveformViewerOverlay::ComputeDesiredSize(float) const
{
	return FVector2D(DesiredWidth, DesiredHeight);
}