// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigUnit_GetCurveValue.h"
#include "Units/RigUnitContext.h"

FString FRigUnit_GetCurveValue::GetUnitLabel() const
{
	return FString::Printf(TEXT("Get Curve %s"), *Curve.ToString());
}

FRigUnit_GetCurveValue_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	const FRigCurveContainer* CurveContainer = Context.GetCurves();
	if (CurveContainer)
	{
		switch (Context.State)
		{
			case EControlRigState::Init:
			{
				CachedCurveIndex = CurveContainer->GetIndex(Curve);
			}
			case EControlRigState::Update:
			{
				if (CachedCurveIndex != INDEX_NONE)
				{
					Value = CurveContainer->GetValue(CachedCurveIndex);
				}
			}
			default:
			{
				break;
			}
		}
	}
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Units/RigUnitTest.h"

IMPLEMENT_RIGUNIT_AUTOMATION_TEST(FRigUnit_GetCurveValue)
{
/*	Hierarchy.AddCurve(TEXT("Root"), NAME_None, FTransform(FVector(1.f, 0.f, 0.f)));
	Hierarchy.AddCurve(TEXT("CurveA"), TEXT("Root"), FTransform(FVector(1.f, 2.f, 3.f)));
	Hierarchy.Initialize();

	Unit.Curve = TEXT("Unknown");
	Unit.Space = ECurveGetterSetterMode::GlobalSpace;
	InitAndExecute();
	AddErrorIfFalse(Unit.Transform.GetTranslation().Equals(FVector(0.f, 0.f, 0.f)), TEXT("unexpected global transform"));
	Unit.Space = ECurveGetterSetterMode::LocalSpace;
	InitAndExecute();
	AddErrorIfFalse(Unit.Transform.GetTranslation().Equals(FVector(0.f, 0.f, 0.f)), TEXT("unexpected local transform"));

	Unit.Curve = TEXT("Root");
	Unit.Space = ECurveGetterSetterMode::GlobalSpace;
	InitAndExecute();
	AddErrorIfFalse(Unit.Transform.GetTranslation().Equals(FVector(1.f, 0.f, 0.f)), TEXT("unexpected global transform"));
	Unit.Space = ECurveGetterSetterMode::LocalSpace;
	InitAndExecute();
	AddErrorIfFalse(Unit.Transform.GetTranslation().Equals(FVector(1.f, 0.f, 0.f)), TEXT("unexpected local transform"));

	Unit.Curve = TEXT("CurveA");
	Unit.Space = ECurveGetterSetterMode::GlobalSpace;
	InitAndExecute();
	AddErrorIfFalse(Unit.Transform.GetTranslation().Equals(FVector(1.f, 2.f, 3.f)), TEXT("unexpected global transform"));
	Unit.Space = ECurveGetterSetterMode::LocalSpace;
	InitAndExecute();
	AddErrorIfFalse(Unit.Transform.GetTranslation().Equals(FVector(0.f, 2.f, 3.f)), TEXT("unexpected local transform"));
	*/
	return true;
}
#endif