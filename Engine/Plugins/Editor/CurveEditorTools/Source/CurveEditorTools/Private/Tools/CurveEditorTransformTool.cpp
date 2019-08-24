// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CurveEditorTransformTool.h"
#include "CurveEditorToolCommands.h"
#include "CurveEditor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Commands/UIAction.h"
#include "Rendering/DrawElements.h"
#include "Framework/DelayedDrag.h"
#include "SCurveEditorView.h"
#include "ScopedTransaction.h"
#include "CurveEditorSelection.h"
#include "SCurveEditorPanel.h"
#include "CurveModel.h"
#include "CurveEditorScreenSpace.h"
#include "Containers/ArrayView.h"
#include "CurveDataAbstraction.h"
#include "CurveEditorSnapMetrics.h"

#define LOCTEXT_NAMESPACE "CurveEditorToolCommands"

namespace CurveEditorTransformTool
{
	constexpr float EdgeAnchorWidth = 13.f;
	constexpr float EdgeHighlightAlpha = 0.15f;
}

void FCurveEditorTransformWidget::GetSidebarGeometry(const FGeometry& InWidgetGeometry, FGeometry& OutLeft, FGeometry& OutRight, FGeometry& OutTop, FGeometry& OutBottom) const
{
	FVector2D SidebarSize = FVector2D(CurveEditorTransformTool::EdgeAnchorWidth, InWidgetGeometry.GetLocalSize().Y - CurveEditorTransformTool::EdgeAnchorWidth);
	FVector2D SidebarSizeOffset = FVector2D(CurveEditorTransformTool::EdgeAnchorWidth / 2.f, 0.f);
	FVector2D TopbarSize = FVector2D(InWidgetGeometry.GetLocalSize().X - CurveEditorTransformTool::EdgeAnchorWidth, CurveEditorTransformTool::EdgeAnchorWidth);
	FVector2D TopbarSizeOffset = FVector2D(0.f, CurveEditorTransformTool::EdgeAnchorWidth / 2.f);

	OutLeft = InWidgetGeometry.MakeChild(SidebarSize, FSlateLayoutTransform(FVector2D(0, CurveEditorTransformTool::EdgeAnchorWidth / 2.f) - SidebarSizeOffset));
	OutRight = InWidgetGeometry.MakeChild(SidebarSize, FSlateLayoutTransform(FVector2D(InWidgetGeometry.GetLocalSize().X, CurveEditorTransformTool::EdgeAnchorWidth / 2.f) - SidebarSizeOffset));
	OutTop = InWidgetGeometry.MakeChild(TopbarSize, FSlateLayoutTransform(FVector2D(CurveEditorTransformTool::EdgeAnchorWidth / 2.f, 0) - TopbarSizeOffset));
	OutBottom = InWidgetGeometry.MakeChild(TopbarSize, FSlateLayoutTransform(FVector2D(CurveEditorTransformTool::EdgeAnchorWidth / 2.f, InWidgetGeometry.GetLocalSize().Y) - TopbarSizeOffset));
}

void FCurveEditorTransformWidget::GetCornerGeometry(const FGeometry& InWidgetGeometry, FGeometry& OutTopLeft, FGeometry& OutTopRight, FGeometry& OutBottomLeft, FGeometry& OutBottomRight) const
{
	FVector2D CornerSize = FVector2D(CurveEditorTransformTool::EdgeAnchorWidth, CurveEditorTransformTool::EdgeAnchorWidth);
	FVector2D HalfSizeOffset = FVector2D(CornerSize / 2.f);

	FSlateLayoutTransform TopLeftPosition = FSlateLayoutTransform(FVector2D(0, 0) - HalfSizeOffset);
	FSlateLayoutTransform TopRightPosition = FSlateLayoutTransform(FVector2D(InWidgetGeometry.GetLocalSize().X, 0) - HalfSizeOffset);
	FSlateLayoutTransform BottomLeftPosition = FSlateLayoutTransform(FVector2D(0, InWidgetGeometry.GetLocalSize().Y) - HalfSizeOffset);
	FSlateLayoutTransform BottomRightPosition = FSlateLayoutTransform(FVector2D(InWidgetGeometry.GetLocalSize()) - HalfSizeOffset);

	OutTopLeft = InWidgetGeometry.MakeChild(CornerSize, TopLeftPosition);
	OutTopRight = InWidgetGeometry.MakeChild(CornerSize, TopRightPosition);
	OutBottomLeft = InWidgetGeometry.MakeChild(CornerSize, BottomLeftPosition);
	OutBottomRight = InWidgetGeometry.MakeChild(CornerSize, BottomRightPosition);
}

void FCurveEditorTransformWidget::GetCenterGeometry(const FGeometry& InWidgetGeometry, FGeometry& OutCenter) const
{
	FVector2D CenterSize = InWidgetGeometry.GetLocalSize() - FVector2D(CurveEditorTransformTool::EdgeAnchorWidth, CurveEditorTransformTool::EdgeAnchorWidth);
	FVector2D CenterOffset = FVector2D(CurveEditorTransformTool::EdgeAnchorWidth / 2.f, CurveEditorTransformTool::EdgeAnchorWidth / 2.f);

	OutCenter = InWidgetGeometry.MakeChild(CenterSize, FSlateLayoutTransform(CenterOffset));
}

ECurveEditorAnchorFlags FCurveEditorTransformWidget::GetAnchorFlagsForMousePosition(const FGeometry& InWidgetGeometry, const FVector2D& InMouseScreenPosition) const
{
	// We store a geometry to represent each different region, updated on Tick. We check if the mouse
	// overlaps a region and update the selection anchors depending on which region you're hovering in.
	ECurveEditorAnchorFlags  OutFlags = ECurveEditorAnchorFlags::None;

	FGeometry LeftSidebarGeometry, RightSidebarGeometry, TopSidebarGeometry, BottomSidebarGeometry;
	FGeometry TopLeftCornerGeometry, TopRightCornerGeometry, BottomLeftCornerGeometry, BottomRightCornerGeometry;

	GetSidebarGeometry(InWidgetGeometry, LeftSidebarGeometry, RightSidebarGeometry, TopSidebarGeometry, BottomSidebarGeometry);
	GetCornerGeometry(InWidgetGeometry, TopLeftCornerGeometry, TopRightCornerGeometry, BottomLeftCornerGeometry, BottomRightCornerGeometry);

	// Deflate the supplied widget by the size of our sidebars so they don't overlap before doing the center check.
	{
		FGeometry CenterGeometry;
		GetCenterGeometry(InWidgetGeometry, CenterGeometry);

		if (CenterGeometry.IsUnderLocation(InMouseScreenPosition))
		{
			OutFlags |= ECurveEditorAnchorFlags::Center;
		}
	}

	if (LeftSidebarGeometry.IsUnderLocation(InMouseScreenPosition))
	{
		OutFlags |= ECurveEditorAnchorFlags::Left;
	}
	if (RightSidebarGeometry.IsUnderLocation(InMouseScreenPosition))
	{
		OutFlags |= ECurveEditorAnchorFlags::Right;
	}
	if (TopSidebarGeometry.IsUnderLocation(InMouseScreenPosition))
	{
		OutFlags |= ECurveEditorAnchorFlags::Top;
	}
	if (BottomSidebarGeometry.IsUnderLocation(InMouseScreenPosition))
	{
		OutFlags |= ECurveEditorAnchorFlags::Bottom;
	}

	if (TopLeftCornerGeometry.IsUnderLocation(InMouseScreenPosition))
	{
		OutFlags |= ECurveEditorAnchorFlags::Top | ECurveEditorAnchorFlags::Left;
	}
	if (TopRightCornerGeometry.IsUnderLocation(InMouseScreenPosition))
	{
		OutFlags |= ECurveEditorAnchorFlags::Top | ECurveEditorAnchorFlags::Right;
	}
	if (BottomLeftCornerGeometry.IsUnderLocation(InMouseScreenPosition))
	{
		OutFlags |= ECurveEditorAnchorFlags::Bottom | ECurveEditorAnchorFlags::Left;
	}
	if (BottomRightCornerGeometry.IsUnderLocation(InMouseScreenPosition))
	{
		OutFlags |= ECurveEditorAnchorFlags::Bottom | ECurveEditorAnchorFlags::Right;
	}

	return OutFlags;
}


void FCurveEditorTransformTool::OnToolActivated()
{
	// UE_LOG(LogTemp, Log, TEXT("TransformTool Activated."));
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (CurveEditor.IsValid())
	{
		// CurveEditor->GetSelection().SelectionChangedDelegate += OnSelectionChanged;
	}

	// Manually fire an OnSelectionChanged event so we go look up the selection and build our bounding box
	// OnSelectionChanged();
}

void FCurveEditorTransformTool::OnToolDeactivated()
{
	// UE_LOG(LogTemp, Log, TEXT("TransformTool Deactivated."));

	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (CurveEditor.IsValid())
	{
		// CurveEditor->GetSelection().SelectionChangedDelegate -= OnSelectionChanged;
	}
}

void FCurveEditorTransformTool::UpdateMarqueeBoundingBox()
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (!CurveEditor.IsValid())
	{
		return;
	}
	// We need to look at all selected keys and get their positions relative to the widget view. This lets us put the bounding box around the
	// current selection, even if it goes off-screen (which it may).

	TOptional<FVector2D> MinValue;
	TOptional<FVector2D> MaxValue;

	FSlateLayoutTransform AbsoluteToContainer = CurveEditor->GetPanel()->GetViewContainerGeometry().GetAccumulatedLayoutTransform();

	const TMap<FCurveModelID, FKeyHandleSet>& SelectedKeySet = CurveEditor->GetSelection().GetAll();
	for (const TPair<FCurveModelID, FKeyHandleSet> Pair : SelectedKeySet)
	{
		const SCurveEditorView* View = CurveEditor->FindFirstInteractiveView(Pair.Key);
		if (!View)
		{
			continue;
		}

		// A newly created view may have a zero-size until the next tick which is a problem if
		// we ask the View for it's curve space, so we skip over it until it has a size.
		if(View->GetCachedGeometry().GetLocalSize() == FVector2D::ZeroVector)
		{
			continue;
		}

		FCurveModel* CurveModel = CurveEditor->FindCurve(Pair.Key);
		check(CurveModel);

		TArrayView<const FKeyHandle> KeyHandles = Pair.Value.AsArray();

		TArray<FKeyPosition> KeyPositions;
		KeyPositions.SetNumUninitialized(KeyHandles.Num());
		CurveModel->GetKeyPositions(KeyHandles, KeyPositions);

		FCurveEditorScreenSpace CurveSpace = View->GetCurveSpace(Pair.Key);
		FSlateLayoutTransform InnerToOuterTransform = Concatenate(View->GetCachedGeometry().GetAccumulatedLayoutTransform(), AbsoluteToContainer.Inverse());

		for (int32 i = 0; i < KeyPositions.Num(); i++)
		{
			FVector2D ViewSpaceLocation = FVector2D(CurveSpace.SecondsToScreen(KeyPositions[i].InputValue), CurveSpace.ValueToScreen(KeyPositions[i].OutputValue));
			FVector2D PanelSpaceLocation = InnerToOuterTransform.TransformPoint(ViewSpaceLocation);

			if (!MinValue.IsSet())
			{
				MinValue = PanelSpaceLocation;
			}

			if (!MaxValue.IsSet())
			{
				MaxValue = PanelSpaceLocation;
			}

			MinValue = FVector2D::Min(MinValue.GetValue(), PanelSpaceLocation);
			MaxValue = FVector2D::Max(MaxValue.GetValue(), PanelSpaceLocation);
		}
	}

	if (MinValue.IsSet() && MaxValue.IsSet())
	{
		FVector2D MarqueeSize = MaxValue.GetValue() - MinValue.GetValue();
		FVector2D Offset = FVector2D::ZeroVector;
		
		// Enforce a minimum size for single time/value selections.
		if (MarqueeSize.X < 8.f)
		{
			MarqueeSize.X = 30;
			Offset.X = MarqueeSize.X / 2.f;
		}
		if (MarqueeSize.Y < 8.f)
		{
			MarqueeSize.Y = 30;
			Offset.Y = MarqueeSize.Y / 2.f;
		}

		TransformWidget.Visible = true;
		TransformWidget.Size = MarqueeSize;
		TransformWidget.Position = MinValue.GetValue() - Offset;
	}
	else
	{

		// No selection, no bounding box.
		TransformWidget.Visible = false;
		TransformWidget.Size = FVector2D::ZeroVector;
		TransformWidget.Position = FVector2D::ZeroVector;
	}

	// UE_LOG(LogTemp, Log, TEXT("Size: %s Position: %s"), *TransformWidget.Size.ToString(), *TransformWidget.Position.ToString());
}

FReply FCurveEditorTransformTool::OnMouseButtonDown(TSharedRef<SWidget> OwningWidget, const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// UE_LOG(LogTemp, Log, TEXT("FCurveEditorTransformTool::OnMouseButtonDown"));
	DelayedDrag.Reset();
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		FGeometry WidgetGeo = TransformWidget.MakeGeometry(MyGeometry);
		ECurveEditorAnchorFlags HitWidgetFlags = TransformWidget.GetAnchorFlagsForMousePosition(WidgetGeo, MouseEvent.GetScreenSpacePosition());
		if (HitWidgetFlags != ECurveEditorAnchorFlags::None)
		{
			// TransformWidget.SelectedAnchorFlags = HitWidgetFlags;
			
			// Start a Delayed Drag
			DelayedDrag = FDelayedDrag(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()), MouseEvent.GetEffectingButton());
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

FReply FCurveEditorTransformTool::OnMouseMove(TSharedRef<SWidget> OwningWidget, const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Update the Hover State of the widget.
	if (!DelayedDrag.IsSet())
	{
		FGeometry WidgetGeo = TransformWidget.MakeGeometry(MyGeometry);
		ECurveEditorAnchorFlags HitWidgetFlags = TransformWidget.GetAnchorFlagsForMousePosition(WidgetGeo, MouseEvent.GetScreenSpacePosition());
		TransformWidget.SelectedAnchorFlags = HitWidgetFlags;
	}

	if (DelayedDrag.IsSet())
	{
		FReply Reply = FReply::Handled();

		if (DelayedDrag->IsDragging())
		{
			OnDrag(MouseEvent);
		}
		else if (DelayedDrag->AttemptDragStart(MouseEvent))
		{
			InitialMousePosition = MouseEvent.GetScreenSpacePosition();
			OnDragStart();

			// Steal the capture, as we're now the authoritative widget in charge of a mouse-drag operation
			Reply.CaptureMouse(OwningWidget);
		}

		return Reply;
	}

	return FReply::Unhandled();
}

FReply FCurveEditorTransformTool::OnMouseButtonUp(TSharedRef<SWidget> OwningWidget, const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// UE_LOG(LogTemp, Log, TEXT("FCurveEditorTransformTool::OnMouseButtonUp"));
	FReply Reply = FReply::Handled();
	if (DelayedDrag.IsSet())
	{
		if (DelayedDrag->IsDragging())
		{
			OnDragEnd();

			// Only return handled if we actually started a drag
			Reply.ReleaseMouseCapture();
		}

		DelayedDrag.Reset();
		return Reply;
	}

	return FReply::Unhandled();
}

void FCurveEditorTransformTool::OnFocusLost(const FFocusEvent& InFocusEvent)
{
	// UE_LOG(LogTemp, Log, TEXT("FCurveEditorTimeTool::OnFocusLost."));
	// We need to end our drag if we lose Window focus to close the transaction, otherwise alt-tabbing while dragging
	// can cause a transaction to get stuck open.
	StopDragIfPossible();
}


void FCurveEditorTransformTool::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 PaintOnLayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// This Geometry represents the Marquee size, but also the offset into the window
	FGeometry WidgetGeo = TransformWidget.MakeGeometry(AllottedGeometry);
	DrawMarqueeWidget(TransformWidget, Args, WidgetGeo, MyCullingRect, OutDrawElements, PaintOnLayerId, InWidgetStyle, bParentEnabled);
}

void FCurveEditorTransformTool::DrawMarqueeWidget(const FCurveEditorTransformWidget& InTransformWidget, const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 PaintOnLayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (!InTransformWidget.Visible)
	{
		return;
	}

	// Draw the inner marquee dotted rectangle line and the highlight
	{
		FLinearColor CenterHighlightColor = (InTransformWidget.SelectedAnchorFlags == ECurveEditorAnchorFlags::Center) ? FLinearColor::White.CopyWithNewOpacity(CurveEditorTransformTool::EdgeHighlightAlpha) : FLinearColor::Transparent;
		FGeometry CenterGeometry;
		InTransformWidget.GetCenterGeometry(AllottedGeometry, CenterGeometry);

		FSlateDrawElement::MakeBox(OutDrawElements, PaintOnLayerId, CenterGeometry.ToPaintGeometry(), FEditorStyle::GetBrush(TEXT("WhiteBrush")), ESlateDrawEffect::None, CenterHighlightColor);
		FSlateDrawElement::MakeBox(OutDrawElements, PaintOnLayerId, AllottedGeometry.ToPaintGeometry(), FEditorStyle::GetBrush(TEXT("MarqueeSelection")));
	}

	// Draw the thing that darkens the background
	{
		// This is comprised of four boxes that exclude the Marquee dimensions in the middle.
	}

	// Draw inner gradient which indicates tool falloff


	// Draw edge highlight regions on mouse hover
	{
		FLinearColor LeftEdgeHighlightColor =	(InTransformWidget.SelectedAnchorFlags == ECurveEditorAnchorFlags::Left)	? FLinearColor::White.CopyWithNewOpacity(CurveEditorTransformTool::EdgeHighlightAlpha) : FLinearColor::Transparent;
		FLinearColor RightEdgeHighlightColor =	(InTransformWidget.SelectedAnchorFlags == ECurveEditorAnchorFlags::Right)	? FLinearColor::White.CopyWithNewOpacity(CurveEditorTransformTool::EdgeHighlightAlpha) : FLinearColor::Transparent;
		FLinearColor TopEdgeHighlightColor =	(InTransformWidget.SelectedAnchorFlags == ECurveEditorAnchorFlags::Top)		? FLinearColor::White.CopyWithNewOpacity(CurveEditorTransformTool::EdgeHighlightAlpha) : FLinearColor::Transparent;
		FLinearColor BottomEdgeHighlightColor =	(InTransformWidget.SelectedAnchorFlags == ECurveEditorAnchorFlags::Bottom)	? FLinearColor::White.CopyWithNewOpacity(CurveEditorTransformTool::EdgeHighlightAlpha) : FLinearColor::Transparent;

		FGeometry LeftSidebarGeometry, RightSidebarGeometry, TopSidebarGeometry, BottomSidebarGeometry;
		InTransformWidget.GetSidebarGeometry(AllottedGeometry, LeftSidebarGeometry, RightSidebarGeometry, TopSidebarGeometry, BottomSidebarGeometry);

		// Left Edge
		FSlateDrawElement::MakeBox(OutDrawElements, PaintOnLayerId, LeftSidebarGeometry.ToPaintGeometry(), FEditorStyle::GetBrush(TEXT("WhiteBrush")), ESlateDrawEffect::None, LeftEdgeHighlightColor);
		// Right Edge
		FSlateDrawElement::MakeBox(OutDrawElements, PaintOnLayerId, RightSidebarGeometry.ToPaintGeometry(), FEditorStyle::GetBrush(TEXT("WhiteBrush")), ESlateDrawEffect::None, RightEdgeHighlightColor);
		// Top Edge
		FSlateDrawElement::MakeBox(OutDrawElements, PaintOnLayerId, TopSidebarGeometry.ToPaintGeometry(), FEditorStyle::GetBrush(TEXT("WhiteBrush")), ESlateDrawEffect::None, TopEdgeHighlightColor);
		// Bottom Edge
		FSlateDrawElement::MakeBox(OutDrawElements, PaintOnLayerId, BottomSidebarGeometry.ToPaintGeometry(), FEditorStyle::GetBrush(TEXT("WhiteBrush")), ESlateDrawEffect::None, BottomEdgeHighlightColor);
	}

	// Draw the four corners + highlights
	{
		FGeometry TopLeftCornerGeometry, TopRightCornerGeometry, BottomLeftCornerGeometry, BottomRightCornerGeometry;
		InTransformWidget.GetCornerGeometry(AllottedGeometry, TopLeftCornerGeometry, TopRightCornerGeometry, BottomLeftCornerGeometry, BottomRightCornerGeometry);

		const bool bTopLeft =		InTransformWidget.SelectedAnchorFlags == (ECurveEditorAnchorFlags::Top		| ECurveEditorAnchorFlags::Left );
		const bool bTopRight =		InTransformWidget.SelectedAnchorFlags == (ECurveEditorAnchorFlags::Top		| ECurveEditorAnchorFlags::Right);
		const bool bBottomLeft =	InTransformWidget.SelectedAnchorFlags == (ECurveEditorAnchorFlags::Bottom	| ECurveEditorAnchorFlags::Left );
		const bool bBottomRight =	InTransformWidget.SelectedAnchorFlags == (ECurveEditorAnchorFlags::Bottom	| ECurveEditorAnchorFlags::Right);

		FLinearColor TopLeftHighlightColor		= bTopLeft		? FLinearColor::White.CopyWithNewOpacity(CurveEditorTransformTool::EdgeHighlightAlpha) : FLinearColor::Transparent;
		FLinearColor TopRightHighlightColor		= bTopRight		? FLinearColor::White.CopyWithNewOpacity(CurveEditorTransformTool::EdgeHighlightAlpha) : FLinearColor::Transparent;
		FLinearColor BottomLeftHighlightColor	= bBottomLeft	? FLinearColor::White.CopyWithNewOpacity(CurveEditorTransformTool::EdgeHighlightAlpha) : FLinearColor::Transparent;
		FLinearColor BottomRightHighlightColor	= bBottomRight  ? FLinearColor::White.CopyWithNewOpacity(CurveEditorTransformTool::EdgeHighlightAlpha) : FLinearColor::Transparent;

		// Top Left (Highlight, Corner Icon)
		FSlateDrawElement::MakeBox(OutDrawElements, PaintOnLayerId, TopLeftCornerGeometry.ToPaintGeometry(), FEditorStyle::GetBrush(TEXT("WhiteBrush")), ESlateDrawEffect::None, TopLeftHighlightColor);
		FSlateDrawElement::MakeBox(OutDrawElements, PaintOnLayerId, TopLeftCornerGeometry.ToPaintGeometry(), FEditorStyle::GetBrush(TEXT("MarqueeSelection")));
		// Top Right										 
		FSlateDrawElement::MakeBox(OutDrawElements, PaintOnLayerId, TopRightCornerGeometry.ToPaintGeometry(), FEditorStyle::GetBrush(TEXT("WhiteBrush")), ESlateDrawEffect::None, TopRightHighlightColor);
		FSlateDrawElement::MakeBox(OutDrawElements, PaintOnLayerId, TopRightCornerGeometry.ToPaintGeometry(), FEditorStyle::GetBrush(TEXT("MarqueeSelection")));
		// Bottom Left										 
		FSlateDrawElement::MakeBox(OutDrawElements, PaintOnLayerId, BottomLeftCornerGeometry.ToPaintGeometry(), FEditorStyle::GetBrush(TEXT("WhiteBrush")), ESlateDrawEffect::None, BottomLeftHighlightColor);
		FSlateDrawElement::MakeBox(OutDrawElements, PaintOnLayerId, BottomLeftCornerGeometry.ToPaintGeometry(), FEditorStyle::GetBrush(TEXT("MarqueeSelection")));
		// Bottom Right										 
		FSlateDrawElement::MakeBox(OutDrawElements, PaintOnLayerId, BottomRightCornerGeometry.ToPaintGeometry(), FEditorStyle::GetBrush(TEXT("WhiteBrush")), ESlateDrawEffect::None, BottomRightHighlightColor);
		FSlateDrawElement::MakeBox(OutDrawElements, PaintOnLayerId, BottomRightCornerGeometry.ToPaintGeometry(), FEditorStyle::GetBrush(TEXT("MarqueeSelection")));
	}
}


void FCurveEditorTransformTool::OnDragStart()
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (!CurveEditor)
	{
		return;
	}

	ActiveTransaction = MakeUnique<FScopedTransaction>(TEXT("CurveEditorTransformTool"), LOCTEXT("CurveEditorTransformToolTransaction", "Transform Key(s)"), nullptr);

	CurveEditor->SuppressBoundTransformUpdates(true);

	// We need to cache our key data because all of our calculations have to be relative to the starting data and not the current per-frame data.
	KeysByCurve.Reset();
	for (const TTuple<FCurveModelID, FKeyHandleSet>& Pair : CurveEditor->GetSelection().GetAll())
	{
		FCurveModelID CurveID = Pair.Key;
		FCurveModel*  Curve = CurveEditor->FindCurve(CurveID);

		if (ensureAlways(Curve))
		{
			Curve->Modify();

			TArrayView<const FKeyHandle> Handles = Pair.Value.AsArray();

			FKeyData& KeyData = KeysByCurve.Emplace_GetRef(CurveID);
			KeyData.Handles = TArray<FKeyHandle>(Handles.GetData(), Handles.Num());

			KeyData.StartKeyPositions.SetNumZeroed(KeyData.Handles.Num());
			Curve->GetKeyPositions(KeyData.Handles, KeyData.StartKeyPositions);
		}
	}
	TransformWidget.StartSize = TransformWidget.Size;
	TransformWidget.StartPosition = TransformWidget.Position;
	SnappingState.Reset();
}

void FCurveEditorTransformTool::OnDrag(const FPointerEvent& MouseEvent)
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (!CurveEditor)
	{
		return;
	}

	TArray<FKeyPosition> NewKeyPositionScratch;

	FSlateLayoutTransform ContainerToAbsolute = CurveEditor->GetPanel()->GetViewContainerGeometry().GetAccumulatedLayoutTransform().Inverse();

	// Dragging the center is the easy case!
	if (TransformWidget.SelectedAnchorFlags == ECurveEditorAnchorFlags::Center)
	{
		FVector2D AxisLockedMousePosition = CurveEditor->GetAxisSnap().GetSnappedPosition(InitialMousePosition, MouseEvent.GetScreenSpacePosition(), MouseEvent, SnappingState);
		{
			FVector2D MouseDelta = AxisLockedMousePosition - InitialMousePosition;
			TransformWidget.Position = TransformWidget.StartPosition + MouseDelta;
		}

		for (const FKeyData& KeyData : KeysByCurve)
		{
			const SCurveEditorView* View = CurveEditor->FindFirstInteractiveView(KeyData.CurveID);
			if (!View)
			{
				continue;
			}

			FCurveModel* CurveModel = CurveEditor->FindCurve(KeyData.CurveID);
			check(CurveModel);

			FCurveEditorScreenSpace CurveSpace = View->GetCurveSpace(KeyData.CurveID);

			double DeltaInput = (AxisLockedMousePosition.X - InitialMousePosition.X) / CurveSpace.PixelsPerInput();
			double DeltaOutput = -(AxisLockedMousePosition.Y - InitialMousePosition.Y) / CurveSpace.PixelsPerOutput();

			NewKeyPositionScratch.Reset();
			NewKeyPositionScratch.Reserve(KeyData.StartKeyPositions.Num());

			for (FKeyPosition StartPosition : KeyData.StartKeyPositions)
			{
				StartPosition.InputValue += DeltaInput;
				StartPosition.OutputValue += DeltaOutput;

				StartPosition.InputValue = View->IsTimeSnapEnabled() ? CurveEditor->GetSnapMetrics().SnapInputSeconds(StartPosition.InputValue) : StartPosition.InputValue;
				StartPosition.OutputValue = View->IsValueSnapEnabled() ? CurveEditor->GetSnapMetrics().SnapOutput(StartPosition.OutputValue) : StartPosition.OutputValue;

				NewKeyPositionScratch.Add(StartPosition);
			}

			CurveModel->SetKeyPositions(KeyData.Handles, NewKeyPositionScratch);
		}
	}
	else if (TransformWidget.SelectedAnchorFlags != ECurveEditorAnchorFlags::None)
	{
		// Rescaling is where things get tricky. If a user is selecting an edge we scale on one axis at once, and if they select a corner we scale on two axis at once.
		// If they press alt, we scale relative to the center (instead of relative to the opposite corner). Because these are all bit-flag'd together we can implement
		// this generically by scaling coordinates relative to an arbitrary center and then masking off whether it affects the X or Y axis depending on anchor flags.
		const bool bAffectsX = ((TransformWidget.SelectedAnchorFlags & ECurveEditorAnchorFlags::Left) != ECurveEditorAnchorFlags::None ||
			(TransformWidget.SelectedAnchorFlags & ECurveEditorAnchorFlags::Right) != ECurveEditorAnchorFlags::None);

		const bool bAffectsY = ((TransformWidget.SelectedAnchorFlags & ECurveEditorAnchorFlags::Top) != ECurveEditorAnchorFlags::None ||
			(TransformWidget.SelectedAnchorFlags & ECurveEditorAnchorFlags::Bottom) != ECurveEditorAnchorFlags::None);

		// We calculate this in [0-1] space for the widget to make the logic easier to follow.
		FVector2D ScaleCenter = FVector2D(0.5f, 0.5f);
		const bool bScaleFromEdge = !MouseEvent.IsAltDown();

		if (bScaleFromEdge)
		{
			if ((TransformWidget.SelectedAnchorFlags & ECurveEditorAnchorFlags::Left) != ECurveEditorAnchorFlags::None)
			{
				// Anchor to the right side
				ScaleCenter.X = 1.0f; 
			}

			if ((TransformWidget.SelectedAnchorFlags & ECurveEditorAnchorFlags::Right) != ECurveEditorAnchorFlags::None)
			{
				// Anchor to the left side
				ScaleCenter.X = 0.0f;
			}

			if ((TransformWidget.SelectedAnchorFlags & ECurveEditorAnchorFlags::Top) != ECurveEditorAnchorFlags::None)
			{
				// Anchor to the bottom side. Slate uses top left as 0 so we flip here.
				ScaleCenter.Y = 1.0f;
			}

			if ((TransformWidget.SelectedAnchorFlags & ECurveEditorAnchorFlags::Bottom) != ECurveEditorAnchorFlags::None)
			{
				// Anchor to the top side
				ScaleCenter.Y = 0.0f;
			}
		}

		// This is the absolute change since our KeysByCurve was initialized.
		const FVector2D MouseDelta = MouseEvent.GetScreenSpacePosition() - InitialMousePosition;
		// TransformWidget.Size = TransformWidget.StartSize += MouseDelta;

		// We have to flip the delta depending on which edge you grabbed so that the change always goes towards the mouse.
		const float InputMulSign = (TransformWidget.SelectedAnchorFlags & ECurveEditorAnchorFlags::Left) != ECurveEditorAnchorFlags::None ? -1.0f : 1.0f;
		const float OutputMulSign = (TransformWidget.SelectedAnchorFlags & ECurveEditorAnchorFlags::Top) != ECurveEditorAnchorFlags::None ? -1.0f : 1.0f;
		const FVector2D AxisFixedMouseDelta = FVector2D(MouseDelta.X * InputMulSign, MouseDelta.Y * OutputMulSign);
		const FVector2D PanelSpaceCenter = TransformWidget.StartPosition + (TransformWidget.StartSize * ScaleCenter);
		const FVector2D PercentChanged = FVector2D(1.0f, 1.0f) + (AxisFixedMouseDelta / TransformWidget.StartSize); // ie: 5 pixel change on a 100 wide gives you 1.05

		// We now know if we need to affect both X and Y, and we know where we're scaling from. Now we can loop through the keys and actually modify their positions.
		// We perform the scale on both axis (for simplicity) and then read which axis it should effect before assigning it back to the key position.
		for (const FKeyData& KeyData : KeysByCurve)
		{
			const SCurveEditorView* View = CurveEditor->FindFirstInteractiveView(KeyData.CurveID);
			if (!View)
			{
				continue;
			}

			FCurveModel* CurveModel = CurveEditor->FindCurve(KeyData.CurveID);
			check(CurveModel);

			// Compute the curve-space center for transformation by transforming the center from panel space to view space, then to curve space
			FSlateLayoutTransform OuterToInnerTransform = Concatenate(ContainerToAbsolute, View->GetCachedGeometry().GetAccumulatedLayoutTransform()).Inverse();
			FVector2D ViewSpaceCenter = OuterToInnerTransform.TransformPoint(PanelSpaceCenter);

			FCurveEditorScreenSpace CurveSpace = View->GetCurveSpace(KeyData.CurveID);
			const double CurveSpaceCenterInput =  CurveSpace.ScreenToSeconds(ViewSpaceCenter.X);
			const double CurveSpaceCenterOutput = CurveSpace.ScreenToValue(ViewSpaceCenter.Y);

			// UE_LOG(LogTemp, Log, TEXT("ScaleCenter Input: %f ScaleCenter Output: %f PercentChanged: %s"), CenterInput, CenterOutput, *PercentChanged.ToString());

			NewKeyPositionScratch.Reset();
			NewKeyPositionScratch.Reserve(KeyData.StartKeyPositions.Num());

			// UE_LOG(LogTemp, Log, TEXT("MouseDelta: %s PercentChanged: %s CenterInput: %f CenterOutput: %f"),
			// *MouseDelta.ToString(), *PercentChanged.ToString(), CenterInput, CenterOutput);

			int32 i = 0;
			for (FKeyPosition StartPosition : KeyData.StartKeyPositions)
			{
				// Step 1 is to rescale the position of the key by the percentage change on each axis.
				double ScaledInput = (StartPosition.InputValue - CurveSpaceCenterInput) * PercentChanged.X; // *InputMulSign;
				double ScaledOutput = (StartPosition.OutputValue - CurveSpaceCenterOutput) * PercentChanged.Y; // *OutputMulSign;

				// Step 2 is to subtract it from the center position so we support scaling from places other than zero
				double NewInput = CurveSpaceCenterInput + ScaledInput;
				double NewOutput = CurveSpaceCenterOutput + ScaledOutput;

				// UE_LOG(LogTemp, Log, TEXT("[%d] InputValue: %f OutputValue: %f ScaledInput: %f ScaledOutput: %f NewInput: %f NewOutput: %f"),
				// i, StartPosition.InputValue, StartPosition.OutputValue, ScaledInput, ScaledOutput, NewInput, NewOutput);
				i++;

				
				// Snap the new values to the grid. We calculate both X and Y changes for ease of programming above and just limit which one it applies to.
				// This includes snapping, otherwise dragging on an edge can cause it to snap on the opposite axis.
				if (bAffectsX)
				{
					StartPosition.InputValue = View->IsTimeSnapEnabled() ? CurveEditor->GetSnapMetrics().SnapInputSeconds(NewInput) : NewInput;
				}
				if (bAffectsY)
				{
					StartPosition.OutputValue = View->IsValueSnapEnabled() ? CurveEditor->GetSnapMetrics().SnapOutput(NewOutput) : NewOutput;
				}


				NewKeyPositionScratch.Add(StartPosition);
			}

			CurveModel->SetKeyPositions(KeyData.Handles, NewKeyPositionScratch);
		}
	}
}

void FCurveEditorTransformTool::OnDragEnd()
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (!CurveEditor.IsValid())
	{
		return;
	}

	CurveEditor->SuppressBoundTransformUpdates(false);

	// This finalizes the transaction
	ActiveTransaction.Reset();
	// PreDragCurveData.Reset();
	// TotalDeltaTime = 0.0;
}

void FCurveEditorTransformTool::StopDragIfPossible()
{
	if (DelayedDrag.IsSet())
	{
		if (DelayedDrag->IsDragging())
		{
			OnDragEnd();
		}

		DelayedDrag.Reset();
	}
}

void FCurveEditorTransformTool::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// We update the size and position of the box every frame as some scale operations aren't 1:1
	// so this keeps the box visually containing all keys even if the mouse position no longer quite 
	// matches up.
	UpdateMarqueeBoundingBox();
}

void FCurveEditorTransformTool::BindCommands(TSharedRef<FUICommandList> CommandBindings)
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (CurveEditor)
	{
		FIsActionChecked TransformToolIsActive = FIsActionChecked::CreateSP(CurveEditor.ToSharedRef(), &FCurveEditor::IsToolActive, ToolID);
		FExecuteAction ActivateTransformTool = FExecuteAction::CreateSP(CurveEditor.ToSharedRef(), &FCurveEditor::MakeToolActive, ToolID);

		CommandBindings->MapAction(FCurveEditorToolCommands::Get().ActivateTransformTool, ActivateTransformTool, FCanExecuteAction(), TransformToolIsActive);
	}
}
#undef LOCTEXT_NAMESPACE // "CurveEditorToolCommands"