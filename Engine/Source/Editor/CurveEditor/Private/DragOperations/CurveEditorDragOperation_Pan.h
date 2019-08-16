// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ICurveEditorDragOperation.h"

class FCurveEditor;
class SCurveEditorView;

class FCurveEditorDragOperation_PanView : public ICurveEditorDragOperation
{
public:
	FCurveEditorDragOperation_PanView(FCurveEditor* CurveEditor, TSharedPtr<SCurveEditorView> InView);

	virtual void OnBeginDrag(FVector2D InitialPosition, FVector2D CurrentPosition, const FPointerEvent& MouseEvent) override;
	virtual void OnDrag(FVector2D InitialPosition, FVector2D CurrentPosition, const FPointerEvent& MouseEvent) override;

private:

	FCurveEditor* CurveEditor;
	TSharedPtr<SCurveEditorView> View;

	double InitialInputMin, InitialInputMax;
	double InitialOutputMin, InitialOutputMax;
	FCurveEditorAxisSnap::FSnapState SnappingState;
};

class FCurveEditorDragOperation_PanInput : public ICurveEditorDragOperation
{
public:
	FCurveEditorDragOperation_PanInput(FCurveEditor* CurveEditor);

	virtual void OnBeginDrag(FVector2D InitialPosition, FVector2D CurrentPosition, const FPointerEvent& MouseEvent) override;
	virtual void OnDrag(FVector2D InitialPosition, FVector2D CurrentPosition, const FPointerEvent& MouseEvent) override;

private:

	FCurveEditor* CurveEditor;
	double InitialInputMin, InitialInputMax;
	FCurveEditorAxisSnap::FSnapState SnappingState;
};