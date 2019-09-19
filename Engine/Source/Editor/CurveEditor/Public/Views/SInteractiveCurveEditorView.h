// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "CurveEditorTypes.h"
#include "SCurveEditorView.h"
#include "ICurveEditorDragOperation.h"
#include "ICurveEditorToolExtension.h"
#include "CurveDrawInfo.h"

struct FCurveModelID;
struct FCurveEditorScreenSpace;
struct FOptionalSize;
struct FCurveEditorDelayedDrag;
class IMenu;

namespace CurveViewConstants
{
	/** The default offset from the top-right corner of curve views for curve labels to be drawn. */
	constexpr float CurveLabelOffsetX = 15.f;
	constexpr float CurveLabelOffsetY = 10.f;

	constexpr FLinearColor BufferedCurveColor = FLinearColor(.4f, .4f, .4f);

	/**
	 * Pre-defined layer offsets for specific curve view elements. Fixed values are used to decouple draw order and layering
	 * Some elements deliberately leave some spare layers as a buffer for slight tweaks to layering within that element
	 */
	namespace ELayerOffset
	{
		enum
		{
			Background     = 0,
			GridLines      = 1,
			GridOverlays   = 2,
			GridLabels     = 3,
			Curves         = 10,
			HoveredCurves  = 15,
			Keys           = 20,
			SelectedKeys   = 30,
			Tools          = 35,
			DragOperations = 40,
			Labels         = 45,
			WidgetContent  = 50,
			Last = Labels
		};
	}
}

/**
 */
class CURVEEDITOR_API SInteractiveCurveEditorView : public SCurveEditorView
{
public:

	SLATE_BEGIN_ARGS(SInteractiveCurveEditorView)
		: _BackgroundTint(FLinearColor::White)
		, _MaximumCapacity(0)
		, _AutoSize(true)
	{}

		SLATE_ARGUMENT(FLinearColor, BackgroundTint)

		SLATE_ARGUMENT(int32, MaximumCapacity)

		SLATE_ATTRIBUTE(float, FixedHeight)

		SLATE_ARGUMENT(bool, AutoSize)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TWeakPtr<FCurveEditor> InCurveEditor);

	virtual void GetGridLinesX(TSharedRef<const FCurveEditor> CurveEditor, TArray<float>& MajorGridLines, TArray<float>& MinorGridLines, TArray<FText>* MajorGridLabels = nullptr) const override;
	virtual void GetGridLinesY(TSharedRef<const FCurveEditor> CurveEditor, TArray<float>& MajorGridLines, TArray<float>& MinorGridLines, TArray<FText>* MajorGridLabels = nullptr) const override;

protected:

	// ~SCurveEditorView Interface
	virtual void GetPointsWithinWidgetRange(const FSlateRect& WidgetRectangle, TArray<FCurvePointHandle>* OutPoints) const override;

	virtual void PaintView(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 BaseLayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const;

protected:

	// SWidget Interface
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 BaseLayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	// ~SWidget Interface

protected:

	void DrawBackground(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 BaseLayerId, ESlateDrawEffect DrawEffects) const;
	void DrawGridLines(TSharedRef<FCurveEditor> CurveEditor, const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 BaseLayerId, ESlateDrawEffect DrawEffects) const;
	void DrawCurves(TSharedRef<FCurveEditor> CurveEditor, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 BaseLayerId, const FWidgetStyle& InWidgetStyle, ESlateDrawEffect DrawEffects) const;
	void DrawBufferedCurves(TSharedRef<FCurveEditor> CurveEditor, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 BaseLayerId, const FWidgetStyle& InWidgetStyle, ESlateDrawEffect DrawEffects) const;

	FSlateColor GetCurveCaptionColor() const;
	FText GetCurveCaption() const;

private:

	/** Gets info about the curves being drawn. Converts actual curves into an abstract series of lines/points/handles/etc. */
	void GetCurveDrawParams(TArray<FCurveDrawParams>& OutDrawParams) const;

	void CreateContextMenu(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);

	/** Updates our distance to all of the curves we represent. */
	void UpdateCurveProximities(FVector2D MousePixel);

	TOptional<FCurveModelID> GetHoveredCurve() const;
	TOptional<FCurvePointHandle> HitPoint(FVector2D MousePixel) const;

	bool IsToolTipEnabled() const;
	FText GetToolTipCurveName() const;
	FText GetToolTipTimeText() const;
	FText GetToolTipValueText() const;

	/*~ Command binding callbacks */
	void AddKeyAtScrubTime(TSet<FCurveModelID> ForCurves);
	void AddKeyAtMousePosition(TSet<FCurveModelID> ForCurves);
	void AddKeyAtTime(const TSet<FCurveModelID>& ToCurves, double InTime);

	void OnCurveEditorToolChanged(FCurveEditorToolID InToolId);

	/**
	 * Rebind contextual command mappings that rely on the mouse position
	 */
	void RebindContextualActions(FVector2D InMousePosition);

	/** Copy the curves from this view and set them as the Curve Editor's buffered curve support. */
	void BufferVisibleCurves();
	/** Copy the curves from this view and set them as the Curve Editor's buffered curve support. */
	void BufferCurve(const FCurveModelID CurveID);
	/** Attempt to apply the previously buffered curves to the currently visible curves. */
	void ApplyBufferCurves(TOptional<FCurveModelID> DestinationCurve);
	/** Check if it's legal to apply any of the buffered curves to our currently visible curves. */
	bool CanApplyBufferedCurves(TOptional<FCurveModelID> DestinationCurve) const;

protected:

	/** Background tint for this widget */
	FLinearColor BackgroundTint;

private:

	/** Curve draw parameters that are re-generated on tick. We generate them once and then they're used in multiple places per frame. */
	TArray<FCurveDrawParams> CachedDrawParams;

	/** (Optional) the current drag operation */
	TOptional<FCurveEditorDelayedDrag> DragOperation;

	struct FCachedToolTipData
	{
		FCachedToolTipData() {}

		FText Text;
		FText EvaluatedValue;
		FText EvaluatedTime;
	};

	TOptional<FCachedToolTipData> CachedToolTipData;

	/** Array of curve proximities in slate units that's updated on mouse move */
	TArray<TTuple<FCurveModelID, float>> CurveProximities;

	/** Track if we have a context menu active. Used to supress hover updates as it causes flickers in the CanExecute bindings. */
	TWeakPtr<IMenu> ActiveContextMenu;

	/** Cached location of the mouse relative to this widget each tick. This is so that command bindings related to the mouse cursor can create them at the right time. */
	FVector2D CachedMousePosition;
};