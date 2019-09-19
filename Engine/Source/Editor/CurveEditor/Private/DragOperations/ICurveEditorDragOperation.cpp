// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ICurveEditorDragOperation.h"
#include "CurveEditor.h"
#include "Input/Events.h"

void ICurveEditorDragOperation::BeginDrag(FVector2D InitialPosition, FVector2D CurrentPosition, const FPointerEvent& MouseEvent)
{
	OnBeginDrag(InitialPosition, CurrentPosition, MouseEvent);
}

void ICurveEditorDragOperation::Drag(FVector2D InitialPosition, FVector2D CurrentPosition, const FPointerEvent& MouseEvent)
{
	OnDrag(InitialPosition, CurrentPosition, MouseEvent);
}

void ICurveEditorDragOperation::EndDrag(FVector2D InitialPosition, FVector2D CurrentPosition, const FPointerEvent& MouseEvent)
{
	OnEndDrag(InitialPosition, CurrentPosition, MouseEvent);
}

void ICurveEditorDragOperation::Paint(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 PaintOnLayerId)
{
	OnPaint(AllottedGeometry, OutDrawElements, PaintOnLayerId);
}

void ICurveEditorDragOperation::CancelDrag()
{
	OnCancelDrag();
}

void ICurveEditorKeyDragOperation::Initialize(FCurveEditor* InCurveEditor, const TOptional<FCurvePointHandle>& CardinalPoint)
{
	// TODO: maybe cache snap data for all selected curves?
	OnInitialize(InCurveEditor, CardinalPoint);
}