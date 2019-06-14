// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Templates/SharedPointer.h"
#include "ICurveEditorToolExtension.h"
#include "Misc/EnumClassFlags.h"
#include "Framework/DelayedDrag.h"
#include "Curves/KeyHandle.h"
#include "CurveDataAbstraction.h"
#include "ScopedTransaction.h"
#include "CurveEditorSnapMetrics.h"

class FCurveEditor;
struct FPointerEvent;

enum class ECurveEditorAnchorFlags : uint8
{
	None = 0x00,
	Top = 0x01,
	Left = 0x02,
	Right = 0x04,
	Bottom = 0x08,
	Center = 0x10
};

ENUM_CLASS_FLAGS(ECurveEditorAnchorFlags);

struct FCurveEditorTransformWidget
{
	FCurveEditorTransformWidget()
	{
		SelectedAnchorFlags = ECurveEditorAnchorFlags::None;
		Visible = false;
	}

public:
	ECurveEditorAnchorFlags SelectedAnchorFlags;
	
	FGeometry MakeGeometry(const FGeometry& InWidgetGeometry) const
	{
		return InWidgetGeometry.MakeChild(Size, FSlateLayoutTransform(Position));
	}

	ECurveEditorAnchorFlags GetAnchorFlagsForMousePosition(const FGeometry& InWidgetGeometry, const FVector2D& InMouseScreenPosition) const;

	void GetCenterGeometry(const FGeometry& InWidgetGeometry, FGeometry& OutCenter) const;
	void GetSidebarGeometry(const FGeometry& InWidgetGeometry, FGeometry& OutLeft, FGeometry& OutRight, FGeometry& OutTop, FGeometry& OutBottom) const;
	void GetCornerGeometry(const FGeometry& InWidgetGeometry, FGeometry& OutTopLeft, FGeometry& OutTopRight, FGeometry& OutBottomLeft, FGeometry& OutBottomRight) const;

	FVector2D Size;
	FVector2D Position;
	bool Visible;

	FVector2D StartSize;
	FVector2D StartPosition;
};


class FCurveEditorTransformTool : public ICurveEditorToolExtension
{
public:
	FCurveEditorTransformTool(TWeakPtr<FCurveEditor> InCurveEditor)
		: WeakCurveEditor(InCurveEditor)
	{
	}

	// ICurveEditorToolExtension Interface
	virtual void OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 PaintOnLayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual void OnToolActivated() override;
	virtual void OnToolDeactivated() override;
	virtual void BindCommands(TSharedRef<FUICommandList> CommandBindings) override;
	virtual FReply OnMouseButtonDown(TSharedRef<SWidget> OwningWidget, const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(TSharedRef<SWidget> OwningWidget, const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(TSharedRef<SWidget> OwningWidget, const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnFocusLost(const FFocusEvent& InFocusEvent);
	// ~ICurveEditorToolExtension

private:
	void UpdateMarqueeBoundingBox();
	void DrawMarqueeWidget(const FCurveEditorTransformWidget& InTransformWidget, const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 PaintOnLayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const;

	void OnDragStart();
	void OnDrag(const FPointerEvent& MouseEvent);
	void OnDragEnd();
	void StopDragIfPossible();
private:
	/** Weak pointer back to the Curve Editor this belongs to. */
	TWeakPtr<FCurveEditor> WeakCurveEditor;

	/** The currently open transaction (if any) */
	TUniquePtr<class FScopedTransaction> ActiveTransaction;

	/** Cached information about our transform tool such as interaction state, etc. */
	FCurveEditorTransformWidget TransformWidget;

	/** Set when attempting to move a drag handle. This allows us to tell the difference between a click and a click-drag. */
	TOptional<FDelayedDrag> DelayedDrag;

	/** Used to cache selected key data when doing transform operations */
	struct FKeyData
	{
		FKeyData(FCurveModelID InCurveID)
			: CurveID(InCurveID)
		{}

		/** The curve that contains the keys we're dragging */
		FCurveModelID CurveID;
		/** All the handles within a given curve that we are dragging */
		TArray<FKeyHandle> Handles;
		/** The extended key info for each of the above handles */
		TArray<FKeyPosition> StartKeyPositions;
	};

	/** Key dragging data stored per-curve */
	TArray<FKeyData> KeysByCurve;

	FVector2D InitialMousePosition;

	FCurveEditorAxisSnap::FSnapState SnappingState;
};