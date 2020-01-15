// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreTypes.h"
#include "Math/Vector2D.h"
#include "Containers/Map.h"
#include "UObject/NameTypes.h"
#include "ICurveEditorDragOperation.h"

class FCurveEditor;
class SCurveEditorView;
class SCurveEditorPanel;

class FCurveEditorDragOperation_Marquee : public ICurveEditorDragOperation
{
public:

	FCurveEditorDragOperation_Marquee(FCurveEditor* InCurveEditor);
	FCurveEditorDragOperation_Marquee(FCurveEditor* InCurveEditor, SCurveEditorView*  InLockedToView);

	virtual void OnBeginDrag(FVector2D InitialPosition, FVector2D CurrentPosition, const FPointerEvent& MouseEvent) override;
	virtual void OnDrag(FVector2D InitialPosition, FVector2D CurrentPosition, const FPointerEvent& MouseEvent) override;
	virtual void OnEndDrag(FVector2D InitialPosition, FVector2D CurrentPosition, const FPointerEvent& MouseEvent) override;
	virtual void OnPaint(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 PaintOnLayerId) override;

private:

	/** The current marquee rectangle */
	FSlateRect Marquee;
	/** Ptr back to the curve editor */
	FCurveEditor* CurveEditor;
	/** When valid, marquee selection should only occur inside this view; all geometries are in local space */
	SCurveEditorView* LockedToView;
};