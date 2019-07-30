// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Math/RigUnit_MathQuaternion.h"
#include "Units/RigUnitContext.h"

UE_RigUnit_MathQuaternionFromAxisAndAngle_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
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

UE_RigUnit_MathQuaternionFromEuler_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FControlRigMathLibrary::QuatFromEuler(Euler, RotationOrder);
}

UE_RigUnit_MathQuaternionFromRotator_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FQuat(Rotator);
}

UE_RigUnit_MathQuaternionFromTwoVectors_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
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

UE_RigUnit_MathQuaternionToAxisAndAngle_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Value.ToAxisAndAngle(Axis, Angle);
}

UE_RigUnit_MathQuaternionToEuler_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FControlRigMathLibrary::EulerFromQuat(Value, RotationOrder);
}

UE_RigUnit_MathQuaternionToRotator_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Value.Rotator();
}

UE_RigUnit_MathQuaternionMul_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A * B;
}

UE_RigUnit_MathQuaternionInverse_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Value.Inverse();
}

UE_RigUnit_MathQuaternionSlerp_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FQuat::Slerp(A, B, T);
}

UE_RigUnit_MathQuaternionEquals_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A == B;
}

UE_RigUnit_MathQuaternionNotEquals_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A != B;
}

UE_RigUnit_MathQuaternionSelectBool_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Condition ? IfTrue : IfFalse;
}

UE_RigUnit_MathQuaternionDot_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A | B;
}

UE_RigUnit_MathQuaternionUnit_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Value.GetNormalized();
}

UE_RigUnit_MathQuaternionRotateVector_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Quaternion.RotateVector(Vector);
}

UE_RigUnit_MathQuaternionGetAxis_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
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


UE_RigUnit_MathQuaternionSwingTwist_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
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
