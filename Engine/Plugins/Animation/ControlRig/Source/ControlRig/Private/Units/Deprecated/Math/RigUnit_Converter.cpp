// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_Converter.h"

FRigUnit_ConvertTransform_Execute()
{
	Result.FromFTransform(Input);
}

FRigUnit_ConvertEulerTransform_Execute()
{
	Result = Input.ToFTransform();
}

FRigUnit_ConvertRotation_Execute()
{
	Result = Input.Quaternion();
}


FRigUnit_ConvertQuaternion_Execute()
{
	Result = Input.Rotator();
}

FRigUnit_ConvertVectorToRotation_Execute()
{
	Result = Input.Rotation();
}

FRigUnit_ConvertVectorToQuaternion_Execute()
{
	Result = Input.Rotation().Quaternion();
	Result.Normalize();
}

FRigUnit_ConvertRotationToVector_Execute()
{
	Result = Input.RotateVector(FVector(1.f, 0.f, 0.f));
}

FRigUnit_ConvertQuaternionToVector_Execute()
{
	Result = Input.RotateVector(FVector(1.f, 0.f, 0.f));
}

FRigUnit_ToSwingAndTwist_Execute()
{
	if (!TwistAxis.IsZero())
	{
		FVector NormalizedAxis = TwistAxis.GetSafeNormal();
		Input.ToSwingTwist(TwistAxis, Swing, Twist);
	}
}
