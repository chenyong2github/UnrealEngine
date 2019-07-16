// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Animation/RigUnit_AnimEvalRichCurve.h"
#include "Units/RigUnitContext.h"

void FRigUnit_AnimEvalRichCurve::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if (FMath::IsNearlyEqual(SourceMinimum, SourceMaximum))
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("The source minimum and maximum are the same."));
	}

	Result = FMath::Clamp<float>((Value - SourceMinimum) / (SourceMaximum - SourceMinimum), 0.f, 1.f);

	if (Curve.GetRichCurveConst()->GetNumKeys() > 0)
	{
		Result = Curve.GetRichCurveConst()->Eval(Result, 0.f);
	}

	Result = FMath::Lerp<float>(TargetMinimum, TargetMaximum, Result);
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Units/RigUnitTest.h"

IMPLEMENT_RIGUNIT_AUTOMATION_TEST(FRigUnit_AnimEvalRichCurve)
{
	Unit.Value = 0.f;
	Execute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result, 0.f), TEXT("unexpected curve result"));
	Unit.Value = 0.5f;
	Execute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result, 0.5f), TEXT("unexpected curve result"));
	Unit.Value = 1.f;
	Execute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result, 1.f), TEXT("unexpected curve result"));
	Unit.Value = 1.5f;
	Execute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result, 1.f), TEXT("unexpected curve result"));
	return true;
}
#endif