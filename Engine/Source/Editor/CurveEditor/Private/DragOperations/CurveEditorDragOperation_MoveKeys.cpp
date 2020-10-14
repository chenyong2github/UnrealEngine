// Copyright Epic Games, Inc. All Rights Reserved.

#include "DragOperations/CurveEditorDragOperation_MoveKeys.h"
#include "CurveEditorScreenSpace.h"
#include "CurveEditor.h"
#include "SCurveEditorView.h"

void FCurveEditorDragOperation_MoveKeys::OnInitialize(FCurveEditor* InCurveEditor, const TOptional<FCurvePointHandle>& InCardinalPoint)
{
	CurveEditor = InCurveEditor;
	CardinalPoint = InCardinalPoint;
}

void FCurveEditorDragOperation_MoveKeys::OnBeginDrag(FVector2D InitialPosition, FVector2D CurrentPosition, const FPointerEvent& MouseEvent)
{
	int32 NumKeys = CurveEditor->GetSelection().Count();
	Transaction = MakeUnique<FScopedTransaction>(FText::Format(NSLOCTEXT("CurveEditor", "MoveKeysFormat", "Move {0}|plural(one=Key, other=Keys)"), NumKeys));

	KeysByCurve.Reset();
	CurveEditor->SuppressBoundTransformUpdates(true);

	for (const TTuple<FCurveModelID, FKeyHandleSet>& Pair : CurveEditor->GetSelection().GetAll())
	{
		FCurveModelID CurveID = Pair.Key;
		FCurveModel*  Curve   = CurveEditor->FindCurve(CurveID);

		if (ensureAlways(Curve))
		{
			Curve->Modify();

			TArrayView<const FKeyHandle> Handles = Pair.Value.AsArray();

			FKeyData& KeyData = KeysByCurve.Emplace_GetRef(CurveID);
			KeyData.Handles = TArray<FKeyHandle>(Handles.GetData(), Handles.Num());

			KeyData.StartKeyPositions.SetNumZeroed(KeyData.Handles.Num());
			Curve->GetKeyPositions(KeyData.Handles, KeyData.StartKeyPositions);
			KeyData.LastDraggedKeyPositions = KeyData.StartKeyPositions;
		}
	}

	SnappingState.Reset();
}

void FCurveEditorDragOperation_MoveKeys::OnDrag(FVector2D InitialPosition, FVector2D CurrentPosition, const FPointerEvent& MouseEvent)
{
	TArray<FKeyPosition> NewKeyPositionScratch;
	FVector2D MousePosition = CurveEditor->GetAxisSnap().GetSnappedPosition(InitialPosition, CurrentPosition, MouseEvent, SnappingState);

	for (FKeyData& KeyData : KeysByCurve)
	{
		const SCurveEditorView* View = CurveEditor->FindFirstInteractiveView(KeyData.CurveID);
		if (!View)
		{
			continue;
		}

		FCurveModel* Curve = CurveEditor->FindCurve(KeyData.CurveID);
		if (!ensureAlways(Curve))
		{
			continue;
		}

		FCurveEditorScreenSpace CurveSpace = View->GetCurveSpace(KeyData.CurveID);

		double DeltaInput = (MousePosition.X - InitialPosition.X) / CurveSpace.PixelsPerInput();
		double DeltaOutput = -(MousePosition.Y - InitialPosition.Y) / CurveSpace.PixelsPerOutput();

		NewKeyPositionScratch.Reset();
		NewKeyPositionScratch.Reserve(KeyData.StartKeyPositions.Num());

		FCurveSnapMetrics SnapMetrics = CurveEditor->GetCurveSnapMetrics(KeyData.CurveID);

		// If view is not absolute, snap based on the key that was grabbed, not all keys individually.
		if (CardinalPoint.IsSet() && (View->IsTimeSnapEnabled() || View->IsValueSnapEnabled()) && View->ViewTypeID != ECurveEditorViewID::Absolute)
		{
			for (int KeyIndex = 0; KeyIndex < KeyData.StartKeyPositions.Num(); ++KeyIndex)
			{
				FKeyHandle KeyHandle = KeyData.Handles[KeyIndex];
				if (CardinalPoint->KeyHandle == KeyHandle)
				{
					FKeyPosition StartPosition = KeyData.StartKeyPositions[KeyIndex];

					if (View->IsTimeSnapEnabled())
					{
						DeltaInput = SnapMetrics.SnapInputSeconds(StartPosition.InputValue + DeltaInput) - StartPosition.InputValue;
					}
					if (View->IsValueSnapEnabled())
					{
						DeltaOutput = SnapMetrics.SnapOutput(StartPosition.OutputValue + DeltaOutput) - StartPosition.OutputValue;
					}
				}
			}
		}

		for (FKeyPosition StartPosition : KeyData.StartKeyPositions)
		{
			StartPosition.InputValue  += DeltaInput;
			StartPosition.OutputValue += DeltaOutput;

			// Snap keys individually if view mode is absolute.
			if (View->ViewTypeID == ECurveEditorViewID::Absolute)
			{
				StartPosition.InputValue  = View->IsTimeSnapEnabled() ? SnapMetrics.SnapInputSeconds(StartPosition.InputValue) : StartPosition.InputValue;
				StartPosition.OutputValue = View->IsValueSnapEnabled() ? SnapMetrics.SnapOutput(StartPosition.OutputValue) : StartPosition.OutputValue;
			}
			
			NewKeyPositionScratch.Add(StartPosition);
		}

		Curve->SetKeyPositions(KeyData.Handles, NewKeyPositionScratch, EPropertyChangeType::Interactive);
		KeyData.LastDraggedKeyPositions = NewKeyPositionScratch;
	}
}

void FCurveEditorDragOperation_MoveKeys::OnCancelDrag()
{
	ICurveEditorKeyDragOperation::OnCancelDrag();

	for (const FKeyData& KeyData : KeysByCurve)
	{
		if (FCurveModel* Curve = CurveEditor->FindCurve(KeyData.CurveID))
		{
			Curve->SetKeyPositions(KeyData.Handles, KeyData.StartKeyPositions, EPropertyChangeType::ValueSet);
		}
	}

	CurveEditor->SuppressBoundTransformUpdates(false);
}

void FCurveEditorDragOperation_MoveKeys::OnEndDrag(FVector2D InitialPosition, FVector2D CurrentPosition, const FPointerEvent& MouseEvent)
{
	ICurveEditorKeyDragOperation::OnEndDrag(InitialPosition, CurrentPosition, MouseEvent);

	for (const FKeyData& KeyData : KeysByCurve)
	{
		if (FCurveModel* Curve = CurveEditor->FindCurve(KeyData.CurveID))
		{
			Curve->SetKeyPositions(KeyData.Handles, KeyData.LastDraggedKeyPositions, EPropertyChangeType::ValueSet);
		}
	}

	CurveEditor->SuppressBoundTransformUpdates(false);
}
