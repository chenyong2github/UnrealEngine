// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Math/RigUnit_MathTransform.h"
#include "Math/ControlRigMathLibrary.h"
#include "Units/RigUnitContext.h"

void FRigUnit_MathTransformFromEulerTransform::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = EulerTransform.ToFTransform();
}

void FRigUnit_MathTransformToEulerTransform::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result.FromFTransform(Value);
}

void FRigUnit_MathTransformMul::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A * B;
}

void FRigUnit_MathTransformMakeRelative::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Local = Global.GetRelativeTransform(Parent);
}

void FRigUnit_MathTransformInverse::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Value.Inverse();
}

void FRigUnit_MathTransformLerp::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FControlRigMathLibrary::LerpTransform(A, B, T);
}

void FRigUnit_MathTransformSelectBool::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Condition ? IfTrue : IfFalse;
}

void FRigUnit_MathTransformRotateVector::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Transform.TransformVector(Direction);
}

void FRigUnit_MathTransformTransformVector::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Transform.TransformPosition(Location);
}

void FRigUnit_MathTransformFromSRT::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Transform.SetLocation(Location);
	Transform.SetRotation(FControlRigMathLibrary::QuatFromEuler(Rotation, RotationOrder));
	Transform.SetScale3D(Scale);
	EulerTransform.FromFTransform(Transform);
}
