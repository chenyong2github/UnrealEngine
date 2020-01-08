// Copyright Epic Games, Inc. All Rights Reserved.

#include "DragOperations/CurveEditorDragOperation_Marquee.h"
#include "CurveEditor.h"
#include "SCurveEditorView.h"
#include "SCurveEditorPanel.h"
#include "EditorStyleSet.h"
#include "CurveDrawInfo.h"

FCurveEditorDragOperation_Marquee::FCurveEditorDragOperation_Marquee(FCurveEditor* InCurveEditor)
	: LockedToView(nullptr)
{
	CurveEditor = InCurveEditor;
}

FCurveEditorDragOperation_Marquee::FCurveEditorDragOperation_Marquee(FCurveEditor* InCurveEditor, SCurveEditorView* InLockedToView)
	: LockedToView(InLockedToView)
{
	CurveEditor = InCurveEditor;
}

void FCurveEditorDragOperation_Marquee::OnBeginDrag(FVector2D InitialPosition, FVector2D CurrentPosition, const FPointerEvent& MouseEvent)
{
	Marquee = FSlateRect(
		FMath::Min(InitialPosition.X, CurrentPosition.X),
		FMath::Min(InitialPosition.Y, CurrentPosition.Y),
		FMath::Max(InitialPosition.X, CurrentPosition.X),
		FMath::Max(InitialPosition.Y, CurrentPosition.Y)
		);
}

void FCurveEditorDragOperation_Marquee::OnDrag(FVector2D InitialPosition, FVector2D CurrentPosition, const FPointerEvent& MouseEvent)
{
	Marquee = FSlateRect(
		FMath::Min(InitialPosition.X, CurrentPosition.X),
		FMath::Min(InitialPosition.Y, CurrentPosition.Y),
		FMath::Max(InitialPosition.X, CurrentPosition.X),
		FMath::Max(InitialPosition.Y, CurrentPosition.Y)
		);
}

void FCurveEditorDragOperation_Marquee::OnEndDrag(FVector2D InitialPosition, FVector2D CurrentPosition, const FPointerEvent& MouseEvent)
{
	Marquee = FSlateRect(
		FMath::Min(InitialPosition.X, CurrentPosition.X),
		FMath::Min(InitialPosition.Y, CurrentPosition.Y),
		FMath::Max(InitialPosition.X, CurrentPosition.X),
		FMath::Max(InitialPosition.Y, CurrentPosition.Y)
		);

	TArray<FCurvePointHandle> AllPoints;

	if (LockedToView)
	{
		LockedToView->GetPointsWithinWidgetRange(Marquee, &AllPoints);
	}
	else
	{
		TSharedPtr<SCurveEditorPanel> CurveEditorPanel = CurveEditor->GetPanel();

		FGeometry ViewContainerGeometry = CurveEditorPanel->GetViewContainerGeometry();
		FSlateLayoutTransform InverseContainerTransform = ViewContainerGeometry.GetAccumulatedLayoutTransform().Inverse();
		for (TSharedPtr<SCurveEditorView> View : CurveEditorPanel->GetViews())
		{
			const FGeometry& LocalGeometry = View->GetCachedGeometry();
			FSlateLayoutTransform ContainerToView = InverseContainerTransform.Concatenate(LocalGeometry.GetAccumulatedLayoutTransform()).Inverse();

			FSlateRect UnclippedLocalMarquee = FSlateRect(ContainerToView.TransformPoint(Marquee.GetTopLeft()), ContainerToView.TransformPoint(Marquee.GetBottomRight()));
			FSlateRect ClippedLocalMarquee = UnclippedLocalMarquee.IntersectionWith(FSlateRect(FVector2D(0.f,0.f), LocalGeometry.GetLocalSize()));

			if (ClippedLocalMarquee.IsValid() && !ClippedLocalMarquee.IsEmpty())
			{
				View->GetPointsWithinWidgetRange(ClippedLocalMarquee, &AllPoints);
			}
		}
	}

	const bool bIsShiftDown = MouseEvent.IsShiftDown();
	const bool bRemoveFromSelection = MouseEvent.IsAltDown();

	TOptional<ECurvePointType> MatchPointType;

	if (!bIsShiftDown && !bRemoveFromSelection)
	{
		CurveEditor->Selection.Clear();
	}
	else if (CurveEditor->Selection.Count() != 0)
	{
		MatchPointType = CurveEditor->Selection.GetSelectionType();
	}

	// Now that we've gathered the overlapping points, perform the relevant selection
	for (const FCurvePointHandle& Point : AllPoints)
	{
		// If we're able to specify a point type, prefer keys
		if (!MatchPointType.IsSet() && Point.PointType == ECurvePointType::Key)
		{
			// We found a key, so start selecting with keys
			CurveEditor->Selection.Clear();
			MatchPointType.Emplace(ECurvePointType::Key);
		}

		if (MatchPointType.IsSet() && Point.PointType != MatchPointType.GetValue())
		{
			// Point does not match what we want
			continue;
		}
		else if (bRemoveFromSelection)
		{
			CurveEditor->Selection.Remove(Point);
		}
		else
		{
			CurveEditor->Selection.Add(Point);
		}
	}
}

void FCurveEditorDragOperation_Marquee::OnPaint(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 PaintOnLayerId)
{
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		PaintOnLayerId,
		AllottedGeometry.ToPaintGeometry(Marquee.GetTopLeft(), Marquee.GetBottomRight() - Marquee.GetTopLeft()),
		FEditorStyle::GetBrush(TEXT("MarqueeSelection"))
		);
}
