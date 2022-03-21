// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeCommonAnimationPayload.h"

#include "CoreMinimal.h"

#if WITH_ENGINE
#include "Curves/RichCurve.h"

void FInterchangeCurveKey::ToRichCurveKey(FRichCurveKey& RichCurveKey) const
{
	RichCurveKey.Time = Time;
	RichCurveKey.Value = Value;
	switch (InterpMode)
	{
		case EInterchangeCurveInterpMode::Constant:
			RichCurveKey.InterpMode = ERichCurveInterpMode::RCIM_Constant;
			break;
		case EInterchangeCurveInterpMode::Cubic:
			RichCurveKey.InterpMode = ERichCurveInterpMode::RCIM_Cubic;
			break;
		case EInterchangeCurveInterpMode::Linear:
			RichCurveKey.InterpMode = ERichCurveInterpMode::RCIM_Linear;
			break;
		default:
			RichCurveKey.InterpMode = ERichCurveInterpMode::RCIM_None;
			break;
	}
	switch (TangentMode)
	{
		case EInterchangeCurveTangentMode::Auto:
			RichCurveKey.TangentMode = ERichCurveTangentMode::RCTM_Auto;
			break;
		case EInterchangeCurveTangentMode::Break:
			RichCurveKey.TangentMode = ERichCurveTangentMode::RCTM_Break;
			break;
		case EInterchangeCurveTangentMode::User:
			RichCurveKey.TangentMode = ERichCurveTangentMode::RCTM_User;
			break;
		default:
			RichCurveKey.TangentMode = ERichCurveTangentMode::RCTM_None;
			break;
	}
	switch (TangentWeightMode)
	{
		case EInterchangeCurveTangentWeightMode::WeightedArrive:
			RichCurveKey.TangentWeightMode = ERichCurveTangentWeightMode::RCTWM_WeightedArrive;
			break;
		case EInterchangeCurveTangentWeightMode::WeightedBoth:
			RichCurveKey.TangentWeightMode = ERichCurveTangentWeightMode::RCTWM_WeightedBoth;
			break;
		case EInterchangeCurveTangentWeightMode::WeightedLeave:
			RichCurveKey.TangentWeightMode = ERichCurveTangentWeightMode::RCTWM_WeightedLeave;
			break;
		default:
			RichCurveKey.TangentWeightMode = ERichCurveTangentWeightMode::RCTWM_WeightedNone;
			break;
	}
	RichCurveKey.ArriveTangent = ArriveTangent;
	RichCurveKey.ArriveTangentWeight = ArriveTangentWeight;
	RichCurveKey.LeaveTangent = LeaveTangent;
	RichCurveKey.LeaveTangentWeight = LeaveTangentWeight;

}

void FInterchangeCurve::ToRichCurve(FRichCurve& OutRichCurve) const
{
	OutRichCurve.Keys.Reserve(Keys.Num());
	for (const FInterchangeCurveKey& CurveKey : Keys)
	{
		FKeyHandle RichCurveKeyHandle = OutRichCurve.AddKey(CurveKey.Time, CurveKey.Value);
		CurveKey.ToRichCurveKey(OutRichCurve.GetKey(RichCurveKeyHandle));
	}
	OutRichCurve.AutoSetTangents();
}

#endif //WITH_ENGINE