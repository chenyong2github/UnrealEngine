// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Math/RigUnit_MathBase.h"
#include "Math/ControlRigMathLibrary.h"
#include "RigUnit_MathQuaternion.generated.h"

USTRUCT(meta=(Abstract, Category="Math|Quaternion", MenuDescSuffix="(Quaternion)"))
struct FRigUnit_MathQuaternionBase : public FRigUnit_MathBase
{
	GENERATED_BODY()
};

USTRUCT(meta=(Abstract))
struct FRigUnit_MathQuaternionUnaryOp : public FRigUnit_MathQuaternionBase
{
	GENERATED_BODY()

	FRigUnit_MathQuaternionUnaryOp()
	{
		Value = Result = FQuat::Identity;
	}

	UPROPERTY(meta=(Input))
	FQuat Value;

	UPROPERTY(meta=(Output))
	FQuat Result;
};

USTRUCT(meta=(Abstract))
struct FRigUnit_MathQuaternionBinaryOp : public FRigUnit_MathQuaternionBase
{
	GENERATED_BODY()

	FRigUnit_MathQuaternionBinaryOp()
	{
		A = B = Result = FQuat::Identity;
	}

	UPROPERTY(meta=(Input))
	FQuat A;

	UPROPERTY(meta=(Input))
	FQuat B;

	UPROPERTY(meta=(Output))
	FQuat Result;
};

/**
 * Makes a quaternion from an axis and an angle in radians
 */
USTRUCT(meta=(DisplayName="From Axis And Angle", PrototypeName="FromAxisAndAngle", Keywords="Make,Construct"))
struct FRigUnit_MathQuaternionFromAxisAndAngle : public FRigUnit_MathQuaternionBase
{
	GENERATED_BODY()

	FRigUnit_MathQuaternionFromAxisAndAngle()
	{
		Axis = FVector(1.f, 0.f, 0.f);
		Angle = 0.f;
		Result = FQuat::Identity;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FVector Axis;

	UPROPERTY(meta=(Input))
	float Angle;

	UPROPERTY(meta=(Output))
	FQuat Result;
};

/**
 * Makes a quaternion from euler values in degrees
 */
USTRUCT(meta=(DisplayName="From Euler", PrototypeName="FromEuler", Keywords="Make,Construct"))
struct FRigUnit_MathQuaternionFromEuler : public FRigUnit_MathQuaternionBase
{
	GENERATED_BODY()

	FRigUnit_MathQuaternionFromEuler()
	{
		Euler = FVector::ZeroVector;
		RotationOrder = EControlRigRotationOrder::ZYX;
		Result = FQuat::Identity;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FVector Euler;

	UPROPERTY(meta = (Input))
	EControlRigRotationOrder RotationOrder;

	UPROPERTY(meta=(Output))
	FQuat Result;
};

/**
 * Makes a quaternion from a rotator
 */
USTRUCT(meta=(DisplayName="From Rotator", PrototypeName="FromRotator", Keywords="Make,Construct"))
struct FRigUnit_MathQuaternionFromRotator : public FRigUnit_MathQuaternionBase
{
	GENERATED_BODY()
	
	FRigUnit_MathQuaternionFromRotator()
	{
		Rotator = FRotator::ZeroRotator;
		Result = FQuat::Identity;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FRotator Rotator;

	UPROPERTY(meta=(Output))
	FQuat Result;
};

/**
 * Makes a quaternion from two vectors, representing the shortest rotation between the two vectors.
 */
USTRUCT(meta=(DisplayName="From Two Vectors", PrototypeName="FromTwoVectors", Keywords="Make,Construct"))
struct FRigUnit_MathQuaternionFromTwoVectors : public FRigUnit_MathQuaternionBase
{
	GENERATED_BODY()
	
	FRigUnit_MathQuaternionFromTwoVectors()
	{
		A = B = FVector(1.f, 0.f, 0.f);
		Result = FQuat::Identity;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FVector A;

	UPROPERTY(meta=(Input))
	FVector B;

	UPROPERTY(meta=(Output))
	FQuat Result;
};

/**
 * Retrieves the axis and angle of a quaternion in radians
 */
USTRUCT(meta=(DisplayName="To Axis And Angle", PrototypeName="ToAxisAndAngle", Keywords="Make,Construct,GetAxis,GetAngle"))
struct FRigUnit_MathQuaternionToAxisAndAngle : public FRigUnit_MathQuaternionBase
{
	GENERATED_BODY()

	FRigUnit_MathQuaternionToAxisAndAngle()
	{
		Value = FQuat::Identity;
		Axis = FVector(1.f, 0.f, 0.f);
		Angle = 0.f;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FQuat Value;

	UPROPERTY(meta=(Output))
	FVector Axis;

	UPROPERTY(meta=(Output))
	float Angle;
};

/**
 * Retrieves the euler angles in degrees
 */
USTRUCT(meta=(DisplayName="To Euler", PrototypeName="ToEuler", Keywords="Make,Construct"))
struct FRigUnit_MathQuaternionToEuler : public FRigUnit_MathQuaternionBase
{
	GENERATED_BODY()

	FRigUnit_MathQuaternionToEuler()
	{
		Value = FQuat::Identity;
		RotationOrder = EControlRigRotationOrder::ZYX;
		Result = FVector::ZeroVector;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FQuat Value;

	UPROPERTY(meta = (Input))
	EControlRigRotationOrder RotationOrder;

	UPROPERTY(meta=(Output))
	FVector Result;
};

/**
 * Retrieves the rotator
 */
USTRUCT(meta=(DisplayName="To Rotator", PrototypeName="ToRotator", Keywords="Make,Construct"))
struct FRigUnit_MathQuaternionToRotator : public FRigUnit_MathQuaternionBase
{
	GENERATED_BODY()

	FRigUnit_MathQuaternionToRotator()
	{
		Value = FQuat::Identity;
		Result = FRotator::ZeroRotator;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FQuat Value;

	UPROPERTY(meta=(Output))
	FRotator Result;
};

/**
 * Returns the product of the two values
 */
USTRUCT(meta=(DisplayName="Multiply", PrototypeName="Multiply", Keywords="Product,*"))
struct FRigUnit_MathQuaternionMul : public FRigUnit_MathQuaternionBinaryOp
{
	GENERATED_BODY()
	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the negative value
 */
USTRUCT(meta=(DisplayName="Inverse", PrototypeName="Inverse"))
struct FRigUnit_MathQuaternionInverse : public FRigUnit_MathQuaternionUnaryOp
{
	GENERATED_BODY()
	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Linearly interpolates between A and B using the ratio T
 */
USTRUCT(meta=(DisplayName="Interpolate", PrototypeName="Interpolate", Keywords="Lerp,Mix,Blend,Slerp,SphericalInterpolate"))
struct FRigUnit_MathQuaternionSlerp : public FRigUnit_MathQuaternionBase
{
	GENERATED_BODY()

	FRigUnit_MathQuaternionSlerp()
	{
		A = B = Result = FQuat::Identity;
		T = 0.f;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FQuat A;

	UPROPERTY(meta=(Input))
	FQuat B;

	UPROPERTY(meta=(Input))
	float T;

	UPROPERTY(meta=(Output))
	FQuat Result;
};

/**
 * Returns true if the value A equals B
 */
USTRUCT(meta=(DisplayName="Equals", PrototypeName="Equals", Keywords="Same,=="))
struct FRigUnit_MathQuaternionEquals : public FRigUnit_MathQuaternionBase
{
	GENERATED_BODY()

	FRigUnit_MathQuaternionEquals()
	{
		A = B = FQuat::Identity;
		Result = true;
	}	

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FQuat A;

	UPROPERTY(meta=(Input))
	FQuat B;

	UPROPERTY(meta=(Output))
	bool Result;
};

/**
 * Returns true if the value A does not equal B
 */
USTRUCT(meta=(DisplayName="Not Equals", PrototypeName="NotEquals", Keywords="Different,!="))
struct FRigUnit_MathQuaternionNotEquals : public FRigUnit_MathQuaternionBase
{
	GENERATED_BODY()

	FRigUnit_MathQuaternionNotEquals()
	{
		A = B = FQuat::Identity;
		Result = false;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FQuat A;

	UPROPERTY(meta=(Input))
	FQuat B;

	UPROPERTY(meta=(Output))
	bool Result;
};

/**
 * Return one of the two values based on the condition
 */
USTRUCT(meta=(DisplayName="Select", PrototypeName="Select", Keywords="Pick,If"))
struct FRigUnit_MathQuaternionSelectBool : public FRigUnit_MathQuaternionBase
{
	GENERATED_BODY()

	FRigUnit_MathQuaternionSelectBool()
	{
		IfTrue = IfFalse = Result = FQuat::Identity;
		Condition = true;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	bool Condition;

	UPROPERTY(meta=(Input))
	FQuat IfTrue;

	UPROPERTY(meta=(Input))
	FQuat IfFalse;

	UPROPERTY(meta=(Output))
	FQuat Result;
};

/**
 * Returns the dot product between two quaternions
 */
USTRUCT(meta=(DisplayName="Dot", PrototypeName="Dot,|"))
struct FRigUnit_MathQuaternionDot : public FRigUnit_MathQuaternionBase
{
	GENERATED_BODY()

	FRigUnit_MathQuaternionDot()
	{
		A = B = FQuat::Identity;
		Result = 0.f;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FQuat A;

	UPROPERTY(meta=(Input))
	FQuat B;

	UPROPERTY(meta=(Output))
	float Result;
};

/**
 * Returns the normalized quaternion
 */
USTRUCT(meta=(DisplayName="Unit", PrototypeName="Unit", Keywords="Normalize"))
struct FRigUnit_MathQuaternionUnit : public FRigUnit_MathQuaternionUnaryOp
{
	GENERATED_BODY()
	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Rotates a given vector by the quaternion
 */
USTRUCT(meta=(DisplayName="Rotate", PrototypeName="Multiply", Keywords="Transform"))
struct FRigUnit_MathQuaternionRotateVector : public FRigUnit_MathQuaternionBase
{
	GENERATED_BODY()

	FRigUnit_MathQuaternionRotateVector()
	{
		Quaternion = FQuat::Identity;
		Vector = Result = FVector::ZeroVector;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FQuat Quaternion;

	UPROPERTY(meta=(Input))
	FVector Vector;

	UPROPERTY(meta=(Output))
	FVector Result;
};

/**
 * Rotates a given vector by the quaternion
 */
USTRUCT(meta = (DisplayName = "Axis", PrototypeName = "Axis", Keywords = "GetAxis,xAxis,yAxis,zAxis"))
struct FRigUnit_MathQuaternionGetAxis: public FRigUnit_MathQuaternionBase
{
	GENERATED_BODY()

	FRigUnit_MathQuaternionGetAxis()
	{
		Quaternion = FQuat::Identity;
		Axis = EAxis::X;
		Result = FVector(1.f, 0.f, 0.f);
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FQuat Quaternion;

	UPROPERTY(meta = (Input))
	TEnumAsByte<EAxis::Type> Axis;

	UPROPERTY(meta = (Output))
	FVector Result;
};


/**
 * Computes the swing and twist components of a quaternion
 */
USTRUCT(meta = (DisplayName = "To Swing & Twist"))
struct FRigUnit_MathQuaternionSwingTwist : public FRigUnit_MathQuaternionBase
{
	GENERATED_BODY()

	FRigUnit_MathQuaternionSwingTwist()
	{
		Input = Swing = Twist = FQuat::Identity;
		TwistAxis = FVector(1.f, 0.f, 0.f);
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FQuat	Input;

	UPROPERTY(meta = (Input))
	FVector TwistAxis;

	UPROPERTY(meta = (Output))
	FQuat Swing;

	UPROPERTY(meta = (Output))
	FQuat Twist;
};

USTRUCT(meta = (DisplayName = "Rotation Order", Category = "Math|Rotation"))
struct FRigUnit_MathQuaternionRotationOrder : public FRigUnit_MathBase
{
	GENERATED_BODY()

	FRigUnit_MathQuaternionRotationOrder()
	{
		RotationOrder = EControlRigRotationOrder::ZYX;
	}

	UPROPERTY(meta = (Input, Output))
	EControlRigRotationOrder RotationOrder;
};