// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Math/RigUnit_MathTransform.h"
#include "Units/RigUnitContext.h"

void FRigUnit_MathTransformFromEulerTransform::Execute(const FRigUnitContext& Context)
{
	Result = EulerTransform.ToFTransform();
}

void FRigUnit_MathTransformToEulerTransform::Execute(const FRigUnitContext& Context)
{
	Result.FromFTransform(Value);
}

void FRigUnit_MathTransformMul::Execute(const FRigUnitContext& Context)
{
	Result = A * B;
}

void FRigUnit_MathTransformMakeRelative::Execute(const FRigUnitContext& Context)
{
	Local = Global.GetRelativeTransform(Parent);
}

void FRigUnit_MathTransformInverse::Execute(const FRigUnitContext& Context)
{
	Result = Value.Inverse();
}

void FRigUnit_MathTransformLerp::Execute(const FRigUnitContext& Context)
{
	Result.SetLocation(FMath::Lerp<FVector>(A.GetLocation(), B.GetLocation(), T));
	Result.SetRotation(FQuat::Slerp(A.GetRotation(), B.GetRotation(), T));
	Result.SetScale3D(FMath::Lerp<FVector>(A.GetScale3D(), B.GetScale3D(), T));
}

void FRigUnit_MathTransformSelectBool::Execute(const FRigUnitContext& Context)
{
	Result = Condition ? IfTrue : IfFalse;
}

void FRigUnit_MathTransformRotateVector::Execute(const FRigUnitContext& Context)
{
	Result = Transform.TransformVector(Direction);
}

void FRigUnit_MathTransformTransformVector::Execute(const FRigUnitContext& Context)
{
	Result = Transform.TransformPosition(Location);
}
