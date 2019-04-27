// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ControlRigMathLibrary.generated.h"

UENUM()
enum class EControlRigRotationOrder : uint8
{
	XYZ,
	XZY,
	YXZ,
	YZX,
	ZXY,
	ZYX
};

UENUM()
enum class EControlRigAnimEasingType : uint8
{
	Linear,
	QuadraticIn,
	QuadraticOut,
	QuadraticInOut,
	CubicIn,
	CubicOut,
	CubicInOut,
	Sinusoidal
};

class CONTROLRIG_API FControlRigMathLibrary
{
public:
	static FQuat QuatFromEuler(const FVector& XYZAnglesInDegrees, EControlRigRotationOrder RotationOrderr = EControlRigRotationOrder::ZYX);
	static FVector EulerFromQuat(const FQuat& Rotation, EControlRigRotationOrder RotationOrder = EControlRigRotationOrder::ZYX);
	static void FourPointBezier(const FVector& A, const FVector& B, const FVector& C, const FVector& D, float T, FVector& OutPosition, FVector& OutTangent);
	static float EaseFloat(float Value, EControlRigAnimEasingType Type);
};