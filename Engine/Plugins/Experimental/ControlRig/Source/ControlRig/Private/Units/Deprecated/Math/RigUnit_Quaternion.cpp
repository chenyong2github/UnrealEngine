// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_Quaternion.h"

FRigUnit_MultiplyQuaternion_Execute()
{
	Result = Argument0*Argument1;
	Result.Normalize();
}

FRigUnit_InverseQuaterion_Execute()
{
	Result = Argument.Inverse();
	Result.Normalize();
}

FRigUnit_QuaternionToAxisAndAngle_Execute()
{
	FVector NewAxis = Axis.GetSafeNormal();
	Argument.ToAxisAndAngle(NewAxis, Angle);
	Angle = FMath::RadiansToDegrees(Angle);
}

FRigUnit_QuaternionFromAxisAndAngle_Execute()
{
	FVector NewAxis = Axis.GetSafeNormal();
	Result = FQuat(NewAxis, FMath::DegreesToRadians(Angle));
}

FRigUnit_QuaternionToAngle_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	FQuat Swing, Twist;
	FVector SafeAxis = Axis.GetSafeNormal();

	FQuat Input = Argument;
	Input.Normalize();
	Input.ToSwingTwist(SafeAxis, Swing, Twist);

	FVector TwistAxis;
	float Radian;
	Twist.ToAxisAndAngle(TwistAxis, Radian);
	// Our range here is from [0, 360)
	Angle = FMath::Fmod(FMath::RadiansToDegrees(Radian), 360);
	if ((TwistAxis | SafeAxis) < 0)
	{
		Angle = 360 - Angle;
	}
}

