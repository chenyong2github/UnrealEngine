// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Math/RigUnit_MathVector.h"
#include "Units/RigUnitContext.h"
#include "Math/ControlRigMathLibrary.h"

void FRigUnit_MathVectorFromFloat::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FVector(Value, Value, Value);
}

void FRigUnit_MathVectorAdd::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A + B;
}

void FRigUnit_MathVectorSub::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A - B;
}

void FRigUnit_MathVectorMul::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A * B;
}

void FRigUnit_MathVectorScale::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Value * Factor;
}

void FRigUnit_MathVectorDiv::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if(FMath::IsNearlyZero(B.X) || FMath::IsNearlyZero(B.Y) || FMath::IsNearlyZero(B.Z))
	{
		if (FMath::IsNearlyZero(B.X))
		{
			UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("B.X is nearly 0.f"));
		}
		if (FMath::IsNearlyZero(B.Y))
		{
			UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("B.Y is nearly 0.f"));
		}
		if (FMath::IsNearlyZero(B.Z))
		{
			UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("B.Z is nearly 0.f"));
		}
		Result = FVector::ZeroVector;
		return;
	}
	Result = A / B;
}

void FRigUnit_MathVectorMod::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if(FMath::IsNearlyZero(B.X) || FMath::IsNearlyZero(B.Y) || FMath::IsNearlyZero(B.Z) || B.X < 0.f || B.Y < 0.f || B.Z < 0.f)
	{
		if (FMath::IsNearlyZero(B.X) || B.X < 0.f)
		{
			UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("B.X needs to be greater than 0"));
		}
		if (FMath::IsNearlyZero(B.Y) || B.Y < 0.f)
		{
			UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("B.Y needs to be greater than 0"));
		}
		if (FMath::IsNearlyZero(B.Z) || B.Z < 0.f)
		{
			UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("B.Z needs to be greater than 0"));
		}
		Result = FVector::ZeroVector;
		return;
	}

	Result.X = FMath::Fmod(A.X, B.X);
	Result.Y = FMath::Fmod(A.Y, B.Y);
	Result.Z = FMath::Fmod(A.Z, B.Z);
}

void FRigUnit_MathVectorMin::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result.X = FMath::Min<float>(A.X, B.X);
	Result.Y = FMath::Min<float>(A.Y, B.Y);
	Result.Z = FMath::Min<float>(A.Z, B.Z);
}

void FRigUnit_MathVectorMax::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result.X = FMath::Max<float>(A.X, B.X);
	Result.Y = FMath::Max<float>(A.Y, B.Y);
	Result.Z = FMath::Max<float>(A.Z, B.Z);
}

void FRigUnit_MathVectorNegate::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = -Value;
}

void FRigUnit_MathVectorAbs::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result.X = FMath::Abs(Value.X);
	Result.Y = FMath::Abs(Value.Y);
	Result.Z = FMath::Abs(Value.Z);
}

void FRigUnit_MathVectorFloor::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result.X = FMath::FloorToFloat(Value.X);
	Result.Y = FMath::FloorToFloat(Value.Y);
	Result.Z = FMath::FloorToFloat(Value.Z);
}

void FRigUnit_MathVectorCeil::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result.X = FMath::CeilToFloat(Value.X);
	Result.Y = FMath::CeilToFloat(Value.Y);
	Result.Z = FMath::CeilToFloat(Value.Z);
}

void FRigUnit_MathVectorRound::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result.X = FMath::RoundToFloat(Value.X);
	Result.Y = FMath::RoundToFloat(Value.Y);
	Result.Z = FMath::RoundToFloat(Value.Z);
}

void FRigUnit_MathVectorSign::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Value.GetSignVector();
}

void FRigUnit_MathVectorClamp::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result.X = FMath::Clamp<float>(Value.X, Minimum.X, Maximum.X);
	Result.Y = FMath::Clamp<float>(Value.Y, Minimum.Y, Maximum.Y);
	Result.Z = FMath::Clamp<float>(Value.Z, Minimum.Z, Maximum.Z);
}

void FRigUnit_MathVectorLerp::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::Lerp<FVector>(A, B, T);
}

void FRigUnit_MathVectorRemap::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	FVector Ratio(0.f, 0.f, 0.f);
	if (FMath::IsNearlyEqual(SourceMinimum.X, SourceMaximum.X) || FMath::IsNearlyEqual(SourceMinimum.Y, SourceMaximum.Y) || FMath::IsNearlyEqual(SourceMinimum.Z, SourceMaximum.Z))
	{
		if (FMath::IsNearlyEqual(SourceMinimum.X, SourceMaximum.X))
		{
			UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("The source minimum and maximum X are the same."));
		}
		if (FMath::IsNearlyEqual(SourceMinimum.Y, SourceMaximum.Y))
		{
			UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("The source minimum and maximum Y are the same."));
		}
		if (FMath::IsNearlyEqual(SourceMinimum.Z, SourceMaximum.Z))
		{
			UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("The source minimum and maximum Z are the same."));
		}
	}
	else
	{
		Ratio = (Value - SourceMinimum) / (SourceMaximum - SourceMinimum);
	}
	if (bClamp)
	{
		Ratio.X = FMath::Clamp<float>(Ratio.X, 0.f, 1.f);
		Ratio.Y = FMath::Clamp<float>(Ratio.Y, 0.f, 1.f);
		Ratio.Z = FMath::Clamp<float>(Ratio.Z, 0.f, 1.f);
	}
	Result = FMath::Lerp<FVector>(TargetMinimum, TargetMaximum, Ratio);
}

void FRigUnit_MathVectorEquals::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A == B;
}

void FRigUnit_MathVectorNotEquals::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A != B;
}

void FRigUnit_MathVectorIsNearlyZero::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if(Tolerance < 0.f)
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Tolerance is below zero"));
	}
	Result = Value.IsNearlyZero(FMath::Max<float>(Tolerance, SMALL_NUMBER));
}

void FRigUnit_MathVectorIsNearlyEqual::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if(Tolerance < 0.f)
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Tolerance is below zero"));
	}
	Result = (A - B).IsNearlyZero(FMath::Max<float>(Tolerance, SMALL_NUMBER));
}

void FRigUnit_MathVectorSelectBool::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Condition ? IfTrue : IfFalse;
}

void FRigUnit_MathVectorDeg::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::RadiansToDegrees(Value);
}

void FRigUnit_MathVectorRad::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::DegreesToRadians(Value);
}

void FRigUnit_MathVectorLengthSquared::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Value.SizeSquared();
}

void FRigUnit_MathVectorLength::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Value.Size();
}

void FRigUnit_MathVectorDistance::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FVector::Distance(A, B);
}

void FRigUnit_MathVectorCross::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A ^ B;
}

void FRigUnit_MathVectorDot::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A | B;
}

void FRigUnit_MathVectorUnit::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if (Value.IsNearlyZero())
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Value is nearly zero"));
		Result = FVector::ZeroVector;
		return;
	}
	Result = Value.GetUnsafeNormal();
}

void FRigUnit_MathVectorMirror::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if (Normal.IsNearlyZero())
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Normal is nearly zero"));
		Result = FVector::ZeroVector;
		return;
	}
	Result = Value.MirrorByVector(Normal.GetSafeNormal());
}

void FRigUnit_MathVectorAngle::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if (A.IsNearlyZero() || B.IsNearlyZero())
	{
		if (A.IsNearlyZero())
		{
			UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("A is nearly zero"));
		}
		if (B.IsNearlyZero())
		{
			UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("B is nearly zero"));
		}
		Result = 0.f;
		return;
	}
	Result = FQuat::FindBetween(A, B).GetAngle();
}

void FRigUnit_MathVectorParallel::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if (A.IsNearlyZero() || B.IsNearlyZero())
	{
		if (A.IsNearlyZero())
		{
			UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("A is nearly zero"));
		}
		if (B.IsNearlyZero())
		{
			UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("B is nearly zero"));
		}
		Result = false;
		return;
	}
	Result = FVector::Parallel(A, B);
}

void FRigUnit_MathVectorOrthogonal::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if (A.IsNearlyZero() || B.IsNearlyZero())
	{
		if (A.IsNearlyZero())
		{
			UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("A is nearly zero"));
		}
		if (B.IsNearlyZero())
		{
			UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("B is nearly zero"));
		}
		Result = false;
		return;
	}
	Result = FVector::Orthogonal(A, B);
}

void FRigUnit_MathVectorBezierFourPoint::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	FControlRigMathLibrary::FourPointBezier(Bezier, T, Result, Tangent);
}