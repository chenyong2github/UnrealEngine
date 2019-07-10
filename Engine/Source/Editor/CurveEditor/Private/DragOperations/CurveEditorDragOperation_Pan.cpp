// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CurveEditorDragOperation_Pan.h"
#include "CurveEditorScreenSpace.h"
#include "CurveEditor.h"
#include "SCurveEditorView.h"
#include "SCurveEditorPanel.h"

FCurveEditorDragOperation_PanView::FCurveEditorDragOperation_PanView(FCurveEditor* InCurveEditor, TSharedPtr<SCurveEditorView> InView)
	: CurveEditor(InCurveEditor)
	, View(InView)
{}

void FCurveEditorDragOperation_PanView::OnBeginDrag(FVector2D InitialPosition, FVector2D CurrentPosition, const FPointerEvent& MouseEvent)
{
	FCurveEditorScreenSpace ViewSpace = View->GetViewSpace();

	InitialInputMin = ViewSpace.GetInputMin();
	InitialInputMax = ViewSpace.GetInputMax();
	InitialOutputMin = ViewSpace.GetOutputMin();
	InitialOutputMax = ViewSpace.GetOutputMax();
	SnappingState.Reset();
}

void FCurveEditorDragOperation_PanView::OnDrag(FVector2D InitialPosition, FVector2D CurrentPosition, const FPointerEvent& MouseEvent)
{
	FVector2D PixelDelta = CurveEditor->GetAxisSnap().GetSnappedPosition(InitialPosition, CurrentPosition, MouseEvent, SnappingState, true) - InitialPosition;

	FCurveEditorScreenSpace ViewSpace = View->GetViewSpace();

	double InputMin = InitialInputMin - PixelDelta.X / ViewSpace.PixelsPerInput();
	double InputMax = InitialInputMax - PixelDelta.X / ViewSpace.PixelsPerInput();

	double OutputMin = InitialOutputMin + PixelDelta.Y / ViewSpace.PixelsPerOutput();
	double OutputMax = InitialOutputMax + PixelDelta.Y / ViewSpace.PixelsPerOutput();

	CurveEditor->GetBounds().SetInputBounds(InputMin, InputMax);
	View->SetOutputBounds(OutputMin, OutputMax);
}

FCurveEditorDragOperation_PanInput::FCurveEditorDragOperation_PanInput(FCurveEditor* InCurveEditor)
	: CurveEditor(InCurveEditor)
{}

void FCurveEditorDragOperation_PanInput::OnBeginDrag(FVector2D InitialPosition, FVector2D CurrentPosition, const FPointerEvent& MouseEvent)
{
	FCurveEditorScreenSpaceH InputSpace = CurveEditor->GetPanelInputSpace();
	InitialInputMin = InputSpace.GetInputMin();
	InitialInputMax = InputSpace.GetInputMax();
	SnappingState.Reset();
}

void FCurveEditorDragOperation_PanInput::OnDrag(FVector2D InitialPosition, FVector2D CurrentPosition, const FPointerEvent& MouseEvent)
{
	FVector2D PixelDelta = CurveEditor->GetAxisSnap().GetSnappedPosition(InitialPosition, CurrentPosition, MouseEvent, SnappingState, true) - InitialPosition;

	FCurveEditorScreenSpaceH InputSpace = CurveEditor->GetPanelInputSpace();

	double InputMin = InitialInputMin - PixelDelta.X / InputSpace.PixelsPerInput();
	double InputMax = InitialInputMax - PixelDelta.X / InputSpace.PixelsPerInput();

	CurveEditor->GetBounds().SetInputBounds(InputMin, InputMax);

	CurveEditor->GetPanel()->ScrollBy(-MouseEvent.GetCursorDelta().Y);
}