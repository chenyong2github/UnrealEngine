// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Math/RigUnit_MathQuaternion.h"
#include "Units/RigUnitContext.h"

FRigUnit_MathQuaternionFromAxisAndAngle_Execute()
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

FRigUnit_MathQuaternionFromEuler_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FControlRigMathLibrary::QuatFromEuler(Euler, RotationOrder);
}

FRigUnit_MathQuaternionFromRotator_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FQuat(Rotator);
}

FRigUnit_MathQuaternionFromTwoVectors_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if (A.IsNearlyZero() || B.IsNearlyZero())
	{
		Result = FQuat::Identity;
		return;
	}
	Result = FQuat::FindBetweenVectors(A, B).GetNormalized();
}

FRigUnit_MathQuaternionToAxisAndAngle_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Value.GetNormalized().ToAxisAndAngle(Axis, Angle);
	if (Axis.IsNearlyZero())
	{
		Axis = FVector(1.f, 0.f, 0.f);
		Angle = 0.f;
	}


	float AngleSign = Angle < 0.f ? -1.f : 1.f;
	Angle = FMath::Abs(Angle);

	static const float TWO_PI = PI * 2.f;
	if (Angle > TWO_PI)
	{
		Angle = FMath::Fmod(Angle, TWO_PI);
	}
	if (Angle > PI)
	{
		Angle = TWO_PI - Angle;
		AngleSign = -AngleSign;
	}

	Angle = Angle * AngleSign;
}

FRigUnit_MathQuaternionScale_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	FVector Axis = FVector::ZeroVector;
	float Angle = 0.f;
	Value.ToAxisAndAngle(Axis, Angle);
	Value = FQuat(Axis, Angle * Scale);
}

FRigUnit_MathQuaternionToEuler_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FControlRigMathLibrary::EulerFromQuat(Value, RotationOrder);
}

FRigUnit_MathQuaternionToRotator_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Value.Rotator();
}

FRigUnit_MathQuaternionMul_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A * B;
}

FRigUnit_MathQuaternionInverse_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Value.Inverse();
}

FRigUnit_MathQuaternionSlerp_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FQuat::Slerp(A, B, T);
}

FRigUnit_MathQuaternionEquals_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A == B;
}

FRigUnit_MathQuaternionNotEquals_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A != B;
}

FRigUnit_MathQuaternionSelectBool_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Condition ? IfTrue : IfFalse;
}

FRigUnit_MathQuaternionDot_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A | B;
}

FRigUnit_MathQuaternionUnit_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Value.GetNormalized();
}

FRigUnit_MathQuaternionRotateVector_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Quaternion.RotateVector(Vector);
}

FRigUnit_MathQuaternionGetAxis_Execute()
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


FRigUnit_MathQuaternionSwingTwist_Execute()
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

FRigUnit_MathQuaternionRotationOrder_Execute()
{
}