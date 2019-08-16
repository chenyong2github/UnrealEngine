// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Math/RigUnit_MathFloat.h"
#include "Units/RigUnitContext.h"

void FRigUnit_MathFloatAdd::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A + B;
}

void FRigUnit_MathFloatSub::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A - B;
}

void FRigUnit_MathFloatMul::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A * B;
}

void FRigUnit_MathFloatDiv::Execute(const FRigUnitContext& Context)
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

void FRigUnit_MathFloatMod::Execute(const FRigUnitContext& Context)
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

void FRigUnit_MathFloatMin::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::Min<float>(A, B);
}

void FRigUnit_MathFloatMax::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::Max<float>(A, B);
}

void FRigUnit_MathFloatPow::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::Pow(A, B);
}

void FRigUnit_MathFloatSqrt::Execute(const FRigUnitContext& Context)
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

void FRigUnit_MathFloatNegate::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = -Value;
}

void FRigUnit_MathFloatAbs::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::Abs(Value);
}

void FRigUnit_MathFloatFloor::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::FloorToFloat(Value);
}

void FRigUnit_MathFloatCeil::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::CeilToFloat(Value);
}

void FRigUnit_MathFloatRound::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::RoundToFloat(Value);
}

void FRigUnit_MathFloatSign::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Value < 0.f ? -1.f : 1.f;
}

void FRigUnit_MathFloatClamp::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::Clamp<float>(Value, Minimum, Maximum);
}

void FRigUnit_MathFloatLerp::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::Lerp<float>(A, B, T);
}

void FRigUnit_MathFloatRemap::Execute(const FRigUnitContext& Context)
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

void FRigUnit_MathFloatEquals::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A == B;
}

void FRigUnit_MathFloatNotEquals::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A != B;
}

void FRigUnit_MathFloatGreater::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A > B;
}

void FRigUnit_MathFloatLess::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A < B;
}

void FRigUnit_MathFloatGreaterEqual::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A >= B;
}

void FRigUnit_MathFloatLessEqual::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A <= B;
}

void FRigUnit_MathFloatIsNearlyZero::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if(Tolerance < 0.f)
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Tolerance is below zero"));
	}
	Result = FMath::IsNearlyZero(Value, FMath::Max<float>(Tolerance, SMALL_NUMBER));
}

void FRigUnit_MathFloatIsNearlyEqual::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if(Tolerance < 0.f)
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Tolerance is below zero"));
	}
	Result = FMath::IsNearlyEqual(A, B, FMath::Max<float>(Tolerance, SMALL_NUMBER));
}

void FRigUnit_MathFloatSelectBool::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Condition ? IfTrue : IfFalse;
}

void FRigUnit_MathFloatDeg::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::RadiansToDegrees(Value);
}

void FRigUnit_MathFloatRad::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::DegreesToRadians(Value);
}

void FRigUnit_MathFloatSin::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::Sin(Value);
}

void FRigUnit_MathFloatCos::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::Cos(Value);
}

void FRigUnit_MathFloatTan::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::Tan(Value);
}

void FRigUnit_MathFloatAsin::Execute(const FRigUnitContext& Context)
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

void FRigUnit_MathFloatAcos::Execute(const FRigUnitContext& Context)
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

void FRigUnit_MathFloatAtan::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::Atan(Value);
}

void FRigUnit_MathFloatLawOfCosine::Execute(const FRigUnitContext& Context)
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
