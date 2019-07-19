// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Filters/CurveEditorReduceFilter.h"
#include "CurveEditor.h"
#include "CurveEditorSelection.h"
#include "ScopedTransaction.h"
#include "CurveDataAbstraction.h"
#include "Misc/FrameRate.h"
#include "CurveModel.h"
#include "Templates/SharedPointer.h"
#include "CurveEditorSnapMetrics.h"

/**
The following key reduction is the same as that found in FRichCurve.
It would be nice if there was just one implementation of the reduction (and) baking algorithms.
*/
/** Util to find float value on bezier defined by 4 control points */
static float BezierInterp(float P0, float P1, float P2, float P3, float Alpha)
{
	const float P01 = FMath::Lerp(P0, P1, Alpha);
	const float P12 = FMath::Lerp(P1, P2, Alpha);
	const float P23 = FMath::Lerp(P2, P3, Alpha);
	const float P012 = FMath::Lerp(P01, P12, Alpha);
	const float P123 = FMath::Lerp(P12, P23, Alpha);
	const float P0123 = FMath::Lerp(P012, P123, Alpha);

	return P0123;
}

static float EvalForTwoKeys(const FKeyPosition& Key1Pos, const FKeyAttributes& Key1Attrib,
	const FKeyPosition& Key2Pos, const FKeyAttributes& Key2Attrib,
	const float InTime)
{
	const float Diff = Key2Pos.InputValue - Key1Pos.InputValue;

	if (Diff > 0.f && Key1Attrib.GetInterpMode() != RCIM_Constant)
	{
		const float Alpha = (InTime - Key1Pos.InputValue) / Diff;
		const float P0 = Key1Pos.OutputValue;
		const float P3 = Key2Pos.OutputValue;

		if (Key1Attrib.GetInterpMode() == RCIM_Linear)
		{
			return FMath::Lerp(P0, P3, Alpha);
		}
		else
		{
			const float OneThird = 1.0f / 3.0f;
			const float P1 = Key1Attrib.HasLeaveTangent() ? P0 + (Key1Attrib.GetLeaveTangent() * Diff*OneThird) : P0;
			const float P2 = Key2Attrib.HasArriveTangent() ? P3 - (Key2Attrib.GetArriveTangent() * Diff*OneThird) : P3;

			return BezierInterp(P0, P1, P2, P3, Alpha);
		}
	}
	else
	{
		return Key1Pos.OutputValue;
	}
}

void UCurveEditorReduceFilter::ApplyFilter_Impl(TSharedRef<FCurveEditor> InCurveEditor, const TMap<FCurveModelID, FKeyHandleSet>& InKeysToOperateOn, TMap<FCurveModelID, FKeyHandleSet>& OutKeysToSelect)
{
	TArray<FKeyHandle> KeyHandles;
	TArray<FKeyPosition> SelectedKeyPositions;
	TArray<FKeyAttributes> SelectedKeyAttributes;

	FFrameRate BakeRate;

	// Since we only remove keys we'll just copy the whole set and then remove the handles as well.
	OutKeysToSelect = InKeysToOperateOn;

	for (const TTuple<FCurveModelID, FKeyHandleSet>& Pair : InKeysToOperateOn)
	{
		FCurveModel* Curve = InCurveEditor->FindCurve(Pair.Key);
		if (!Curve)
		{
			continue;
		}

		BakeRate = InCurveEditor->GetCurveSnapMetrics(Pair.Key).InputSnapRate;

		KeyHandles.Reset(Pair.Value.Num());
		KeyHandles.Append(Pair.Value.AsArray().GetData(), Pair.Value.Num());

		// Get all the selected keys
		SelectedKeyPositions.SetNum(KeyHandles.Num());
		Curve->GetKeyPositions(KeyHandles, SelectedKeyPositions);

		// Find the hull of the range of the selected keys
		double MinKey = TNumericLimits<double>::Max(), MaxKey = TNumericLimits<double>::Lowest();
		for (FKeyPosition Key : SelectedKeyPositions)
		{
			MinKey = FMath::Min(Key.InputValue, MinKey);
			MaxKey = FMath::Max(Key.InputValue, MaxKey);
		}

		// Get all keys that exist between the time range
		KeyHandles.Reset();
		Curve->GetKeys(*InCurveEditor, MinKey, MaxKey, TNumericLimits<double>::Lowest(), TNumericLimits<double>::Max(), KeyHandles);
		if (KeyHandles.Num() > 2) //need at least 3 keys to reduce
		{
			SelectedKeyPositions.SetNum(KeyHandles.Num());
			Curve->GetKeyPositions(KeyHandles, SelectedKeyPositions);
			SelectedKeyAttributes.SetNum(KeyHandles.Num());
			Curve->GetKeyAttributes(KeyHandles, SelectedKeyAttributes);

			FKeyHandleSet& OutHandleSet = OutKeysToSelect.FindOrAdd(Pair.Key);
			int32 MostRecentKeepKeyIndex = 0;
			TArray<FKeyHandle> KeysToRemove;
			for (int32 TestIndex = 1; TestIndex < KeyHandles.Num() - 1; ++TestIndex)
			{

				const float KeyValue = SelectedKeyPositions[TestIndex].OutputValue;
				const float ValueWithoutKey = EvalForTwoKeys(SelectedKeyPositions[MostRecentKeepKeyIndex], SelectedKeyAttributes[MostRecentKeepKeyIndex],
					SelectedKeyPositions[TestIndex + 1], SelectedKeyAttributes[TestIndex + 1],
					SelectedKeyPositions[TestIndex].InputValue);

				// Check if there is a great enough change in value to consider this key needed.
				if (FMath::Abs(ValueWithoutKey - KeyValue) > Tolerance)
				{
					MostRecentKeepKeyIndex = TestIndex;
				}
				else
				{
					KeysToRemove.Add(KeyHandles[TestIndex]);
					OutHandleSet.Remove(KeyHandles[TestIndex]);
				}
			}
			Curve->Modify();
			Curve->RemoveKeys(KeysToRemove);
		}
	}
}