// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

#include "ICurveEditorDragOperation.h"
#include "CurveEditor.h"
#include "CurveEditorTypes.h"

class SScrollBox;
class SCurveEditorView;
class ITimeSliderController;

/**
 * Curve editor widget that reflects the state of an FCurveEditor
 */
class CURVEEDITOR_API SCurveEditorViewContainer : public SVerticalBox
{
	SLATE_BEGIN_ARGS(SCurveEditorViewContainer)
	{}

		/** Optional Time Slider Controller which allows us to synchronize with an externally controlled Time Slider */
		SLATE_ARGUMENT(TSharedPtr<ITimeSliderController>, ExternalTimeSliderController)

	SLATE_END_ARGS()

	/**
	 * Construct a new curve editor panel widget
	 */
	void Construct(const FArguments& InArgs, TSharedRef<FCurveEditor> InCurveEditor);

	TArrayView<const TSharedPtr<SCurveEditorView>> GetViews() const { return Views; }

	void AddView(TSharedRef<SCurveEditorView> ViewToAdd);

	void Clear();

private:

	// SWidget Interface
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FVector2D ComputeDesiredSize(float) const;
	virtual bool ComputeVolatility() const override;
	// ~SWidget Interface

	/*~ Mouse interaction */
	virtual FReply OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void OnFocusLost(const FFocusEvent& InFocusEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:

	FMargin GetSlotPadding(int32 SlotIndex) const;

	void OnCurveEditorToolChanged(FCurveEditorToolID InToolId);

	void ExpandInputBounds(float NewWidth);

private:

	/** The curve editor pointer */
	TSharedPtr<FCurveEditor> CurveEditor;

	/** Optional time slider controller */
	TSharedPtr<ITimeSliderController> TimeSliderController;

	/** (Optional) the current drag operation */
	TOptional<FCurveEditorDelayedDrag> DragOperation;

	/** Array of views that may need their height updating on tick. */
	TArray<TSharedPtr<SCurveEditorView>> Views;

	/** 
	 * Whether or not this widget caught an OnMouseDown notification 
	 * Used to check if the selection should be cleared
	 */
	bool bCaughtMouseDown;
};