// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Math/RigUnit_MathTransform.h"
#include "Units/Math/RigUnit_MathVector.h"
#include "Math/ControlRigMathLibrary.h"
#include "Units/RigUnitContext.h"

FRigUnit_MathTransformFromEulerTransform_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = EulerTransform.ToFTransform();
}

FRigUnit_MathTransformToEulerTransform_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result.FromFTransform(Value);
}

FRigUnit_MathTransformMul_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A * B;
}

FRigUnit_MathTransformMakeRelative_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Local = Global.GetRelativeTransform(Parent);
	Local.NormalizeRotation();
}

FRigUnit_MathTransformMakeAbsolute_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Global = Local * Parent;
	Global.NormalizeRotation();
}

FRigUnit_MathTransformInverse_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Value.Inverse();
}

FRigUnit_MathTransformLerp_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FControlRigMathLibrary::LerpTransform(A, B, T);
}

FRigUnit_MathTransformSelectBool_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Condition ? IfTrue : IfFalse;
}

FRigUnit_MathTransformRotateVector_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Transform.TransformVector(Direction);
}

FRigUnit_MathTransformTransformVector_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Transform.TransformPosition(Location);
}

FRigUnit_MathTransformFromSRT_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Transform.SetLocation(Location);
	Transform.SetRotation(FControlRigMathLibrary::QuatFromEuler(Rotation, RotationOrder));
	Transform.SetScale3D(Scale);
	EulerTransform.FromFTransform(Transform);
}

FRigUnit_MathTransformClampSpatially_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	FVector Position;
	FRigUnit_MathVectorClampSpatially::StaticExecute(Value.GetTranslation(), Axis, Type, Minimum, Maximum, Space, bDrawDebug, DebugColor, DebugThickness, Position, RigUnitName, RigUnitStructName, ExecutionType, Context);
	Result = Value;
	Result.SetTranslation(Position);
}