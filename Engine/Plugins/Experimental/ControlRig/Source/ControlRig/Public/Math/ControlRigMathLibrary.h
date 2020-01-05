// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ControlRigDefines.h"
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
	QuadraticEaseIn,
	QuadraticEaseOut,
	QuadraticEaseInOut,
	CubicEaseIn,
	CubicEaseOut,
	CubicEaseInOut,
	QuarticEaseIn,
	QuarticEaseOut,
	QuarticEaseInOut,
	QuinticEaseIn,
	QuinticEaseOut,
	QuinticEaseInOut,
	SineEaseIn,
	SineEaseOut,
	SineEaseInOut,
	CircularEaseIn,
	CircularEaseOut,
	CircularEaseInOut,
	ExponentialEaseIn,
	ExponentialEaseOut,
	ExponentialEaseInOut,
	ElasticEaseIn,
	ElasticEaseOut,
	ElasticEaseInOut,
	BackEaseIn,
	BackEaseOut,
	BackEaseInOut,
	BounceEaseIn,
	BounceEaseOut,
	BounceEaseInOut
};

USTRUCT()
struct FCRFourPointBezier
{
	GENERATED_BODY()

	FCRFourPointBezier()
	{
		A = B = C = D = FVector::ZeroVector;
	}

	UPROPERTY()
	FVector A;

	UPROPERTY()
	FVector B;

	UPROPERTY()
	FVector C;

	UPROPERTY()
	FVector D;
};

class CONTROLRIG_API FControlRigMathLibrary
{
public:
	static float AngleBetween(const FVector& A, const FVector& B);
	static FQuat QuatFromEuler(const FVector& XYZAnglesInDegrees, EControlRigRotationOrder RotationOrderr = EControlRigRotationOrder::ZYX);
	static FVector EulerFromQuat(const FQuat& Rotation, EControlRigRotationOrder RotationOrder = EControlRigRotationOrder::ZYX);
	static void FourPointBezier(const FVector& A, const FVector& B, const FVector& C, const FVector& D, float T, FVector& OutPosition, FVector& OutTangent);
	static void FourPointBezier(const FCRFourPointBezier& Bezier, float T, FVector& OutPosition, FVector& OutTangent);
	static float EaseFloat(float Value, EControlRigAnimEasingType Type);
	static FTransform LerpTransform(const FTransform& A, const FTransform& B, float T);
	static void SolveBasicTwoBoneIK(FTransform& BoneA, FTransform& BoneB, FTransform& Effector, const FVector& PoleVector, const FVector& PrimaryAxis, const FVector& SecondaryAxis, float SecondaryAxisWeight, float BoneALength, float BoneBLength, bool bEnableStretch, float StretchStartRatio, float StretchMaxRatio);
	static FVector ClampSpatially(const FVector& Value, EAxis::Type Axis, EControlRigClampSpatialMode::Type Type, float Minimum, float Maximum, FTransform Space);
};