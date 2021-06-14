// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Math/RigUnit_MathMatrix.h"
#include "Units/Math/RigUnit_MathTransform.h"
#include "Units/RigUnitContext.h"

FRigUnit_MathMatrixFromTransform_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Transform.ToMatrixWithScale();
}

FRigUnit_MathMatrixToTransform_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result.SetFromMatrix(Value);
}

FRigUnit_MathMatrixFromVectors_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMatrix(X, Y, Z, FVector::ZeroVector);
	Result.SetOrigin(Origin);
}

FRigUnit_MathMatrixToVectors_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	X = Value.GetScaledAxis(EAxis::X);
	Y = Value.GetScaledAxis(EAxis::Y);
	Z = Value.GetScaledAxis(EAxis::Z);
	Origin = Value.GetOrigin();
}

FRigUnit_MathMatrixMul_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A * B;
}

FRigUnit_MathMatrixInverse_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Value.Inverse();
}
