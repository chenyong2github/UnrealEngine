// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Math/RigUnit_MathTransform.h"
#include "Math/ControlRigMathLibrary.h"
#include "Units/RigUnitContext.h"

UE_RigUnit_MathTransformFromEulerTransform_IMPLEMENT_RIGVM(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = EulerTransform.ToFTransform();
}

UE_RigUnit_MathTransformToEulerTransform_IMPLEMENT_RIGVM(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result.FromFTransform(Value);
}

UE_RigUnit_MathTransformMul_IMPLEMENT_RIGVM(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A * B;
}

UE_RigUnit_MathTransformMakeRelative_IMPLEMENT_RIGVM(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Local = Global.GetRelativeTransform(Parent);
}

UE_RigUnit_MathTransformInverse_IMPLEMENT_RIGVM(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Value.Inverse();
}

UE_RigUnit_MathTransformLerp_IMPLEMENT_RIGVM(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FControlRigMathLibrary::LerpTransform(A, B, T);
}

UE_RigUnit_MathTransformSelectBool_IMPLEMENT_RIGVM(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Condition ? IfTrue : IfFalse;
}

UE_RigUnit_MathTransformRotateVector_IMPLEMENT_RIGVM(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Transform.TransformVector(Direction);
}

UE_RigUnit_MathTransformTransformVector_IMPLEMENT_RIGVM(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Transform.TransformPosition(Location);
}

UE_RigUnit_MathTransformFromSRT_IMPLEMENT_RIGVM(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Transform.SetLocation(Location);
	Transform.SetRotation(FControlRigMathLibrary::QuatFromEuler(Rotation, RotationOrder));
	Transform.SetScale3D(Scale);
	EulerTransform.FromFTransform(Transform);
}
