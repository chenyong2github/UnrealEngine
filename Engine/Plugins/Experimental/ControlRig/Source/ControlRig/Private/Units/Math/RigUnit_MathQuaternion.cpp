// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Math/RigUnit_MathQuaternion.h"
#include "Units/RigUnitContext.h"

void FRigUnit_MathQuaternionFromAxisAndAngle::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if (Axis.IsNearlyZero())
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Axis is nearly zero"));
		Result = FQuat::Identity;
		return;
	}
	Result = FQuat(Axis.GetUnsafeNormal(), Angle);
}

void FRigUnit_MathQuaternionFromEuler::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FControlRigMathLibrary::QuatFromEuler(Euler, RotationOrder);
}

void FRigUnit_MathQuaternionFromRotator::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FQuat(Rotator);
}

void FRigUnit_MathQuaternionFromTwoVectors::Execute(const FRigUnitContext& Context)
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
		Result = FQuat::Identity;
		return;
	}
	Result = FQuat::FindBetweenVectors(A, B).GetNormalized();
}

void FRigUnit_MathQuaternionToAxisAndAngle::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Value.ToAxisAndAngle(Axis, Angle);
}

void FRigUnit_MathQuaternionToEuler::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FControlRigMathLibrary::EulerFromQuat(Value, RotationOrder);
}

void FRigUnit_MathQuaternionToRotator::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Value.Rotator();
}

void FRigUnit_MathQuaternionMul::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A * B;
}

void FRigUnit_MathQuaternionInverse::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Value.Inverse();
}

void FRigUnit_MathQuaternionSlerp::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FQuat::Slerp(A, B, T);
}

void FRigUnit_MathQuaternionEquals::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A == B;
}

void FRigUnit_MathQuaternionNotEquals::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A != B;
}

void FRigUnit_MathQuaternionSelectBool::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Condition ? IfTrue : IfFalse;
}

void FRigUnit_MathQuaternionDot::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A | B;
}

void FRigUnit_MathQuaternionUnit::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Value.GetNormalized();
}

void FRigUnit_MathQuaternionRotateVector::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Quaternion.RotateVector(Vector);
}

void FRigUnit_MathQuaternionGetAxis::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	switch (Axis)
	{
		default:
		case EAxis::X:
		{
			Result = Quaternion.GetAxisX();
			break;
		}
		case EAxis::Y:
		{
			Result = Quaternion.GetAxisY();
			break;
		}
		case EAxis::Z:
		{
			Result = Quaternion.GetAxisZ();
			break;
		}
	}
}


void FRigUnit_MathQuaternionSwingTwist::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if (TwistAxis.IsNearlyZero())
	{
		Swing = Twist = FQuat::Identity;
		return;
	}

	FVector NormalizedAxis = TwistAxis.GetSafeNormal();
	Input.ToSwingTwist(NormalizedAxis, Swing, Twist);
}
