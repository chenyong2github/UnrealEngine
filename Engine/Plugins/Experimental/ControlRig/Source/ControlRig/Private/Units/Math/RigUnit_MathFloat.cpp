// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Math/RigUnit_MathFloat.h"
#include "Units/RigUnitContext.h"

UE_RigUnit_MathFloatAdd_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A + B;
}

UE_RigUnit_MathFloatSub_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A - B;
}

UE_RigUnit_MathFloatMul_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A * B;
}

UE_RigUnit_MathFloatDiv_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if(FMath::IsNearlyZero(B))
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("B is nearly 0.f"));
		Result = 0.f;
		return;
	}
	Result = A / B;
}

UE_RigUnit_MathFloatMod_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if(FMath::IsNearlyZero(B) || B < 0.f)
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("B needs to be greater than 0"));
		Result = 0.f;
		return;
	}
	Result = FMath::Fmod(A, B);
}

UE_RigUnit_MathFloatMin_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::Min<float>(A, B);
}

UE_RigUnit_MathFloatMax_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::Max<float>(A, B);
}

UE_RigUnit_MathFloatPow_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::Pow(A, B);
}

UE_RigUnit_MathFloatSqrt_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if(Value < 0.f)
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Value is below zero"));
		Result = 0.f;
		return;
	}
	Result = FMath::Sqrt(Value);
}

UE_RigUnit_MathFloatNegate_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = -Value;
}

UE_RigUnit_MathFloatAbs_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::Abs(Value);
}

UE_RigUnit_MathFloatFloor_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::FloorToFloat(Value);
}

UE_RigUnit_MathFloatCeil_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::CeilToFloat(Value);
}

UE_RigUnit_MathFloatRound_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::RoundToFloat(Value);
}

UE_RigUnit_MathFloatSign_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Value < 0.f ? -1.f : 1.f;
}

UE_RigUnit_MathFloatClamp_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::Clamp<float>(Value, Minimum, Maximum);
}

UE_RigUnit_MathFloatLerp_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::Lerp<float>(A, B, T);
}

UE_RigUnit_MathFloatRemap_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	float Ratio = 0.f;
	if (FMath::IsNearlyEqual(SourceMinimum, SourceMaximum))
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("The source minimum and maximum are the same."));
	}
	else
	{
		Ratio = (Value - SourceMinimum) / (SourceMaximum - SourceMinimum);
	}
	if (bClamp)
	{
		Ratio = FMath::Clamp<float>(Ratio, 0.f, 1.f);
	}
	Result = FMath::Lerp<float>(TargetMinimum, TargetMaximum, Ratio);
}

UE_RigUnit_MathFloatEquals_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A == B;
}

UE_RigUnit_MathFloatNotEquals_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A != B;
}

UE_RigUnit_MathFloatGreater_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A > B;
}

UE_RigUnit_MathFloatLess_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A < B;
}

UE_RigUnit_MathFloatGreaterEqual_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A >= B;
}

UE_RigUnit_MathFloatLessEqual_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A <= B;
}

UE_RigUnit_MathFloatIsNearlyZero_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if(Tolerance < 0.f)
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Tolerance is below zero"));
	}
	Result = FMath::IsNearlyZero(Value, FMath::Max<float>(Tolerance, SMALL_NUMBER));
}

UE_RigUnit_MathFloatIsNearlyEqual_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if(Tolerance < 0.f)
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Tolerance is below zero"));
	}
	Result = FMath::IsNearlyEqual(A, B, FMath::Max<float>(Tolerance, SMALL_NUMBER));
}

UE_RigUnit_MathFloatSelectBool_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Condition ? IfTrue : IfFalse;
}

UE_RigUnit_MathFloatDeg_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::RadiansToDegrees(Value);
}

UE_RigUnit_MathFloatRad_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::DegreesToRadians(Value);
}

UE_RigUnit_MathFloatSin_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::Sin(Value);
}

UE_RigUnit_MathFloatCos_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::Cos(Value);
}

UE_RigUnit_MathFloatTan_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::Tan(Value);
}

UE_RigUnit_MathFloatAsin_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if (!FMath::IsWithinInclusive<float>(Value, -1.f, 1.f))
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Value is outside of valid range (-1 to 1)"));
		Result = 0.f;
		return;
	}
	Result = FMath::Asin(Value);
}

UE_RigUnit_MathFloatAcos_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if (!FMath::IsWithinInclusive<float>(Value, -1.f, 1.f))
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Value is outside of valid range (-1 to 1)"));
		Result = 0.f;
		return;
	}
	Result = FMath::Acos(Value);
}

UE_RigUnit_MathFloatAtan_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::Atan(Value);
}

UE_RigUnit_MathFloatLawOfCosine_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if ((A <= 0.f) || (B <= 0.f) || (C <= 0.f) || (A + B < C) || (A + C < B) || (B + C < A))
	{
		AlphaAngle = BetaAngle = GammaAngle = 0.f;
		bValid = false;
		return;
	}

	GammaAngle = FMath::Acos((A * A + B * B - C * C) / (2.f * A * B));
	BetaAngle = FMath::Acos((A * A + C * C - B * B) / (2.f * A * C));
	AlphaAngle = PI - GammaAngle - BetaAngle;
	bValid = true;
}
