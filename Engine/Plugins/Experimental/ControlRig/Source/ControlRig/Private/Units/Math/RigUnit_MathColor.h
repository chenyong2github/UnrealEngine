// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Math/RigUnit_MathBase.h"
#include "Math/ControlRigMathLibrary.h"
#include "RigUnit_MathColor.generated.h"

USTRUCT(meta=(Abstract, Category="Math|Color", MenuDescSuffix="(Color)"))
struct FRigUnit_MathColorBase : public FRigUnit_MathBase
{
	GENERATED_BODY()
};

USTRUCT(meta=(Abstract))
struct FRigUnit_MathColorBinaryOp : public FRigUnit_MathColorBase
{
	GENERATED_BODY()

	FRigUnit_MathColorBinaryOp()
	{
		A = B = Result = FLinearColor::Black;
	}

	UPROPERTY(meta=(Input))
	FLinearColor A;

	UPROPERTY(meta=(Input))
	FLinearColor B;

	UPROPERTY(meta=(Output))
	FLinearColor Result;
};

/**
 * Makes a vector from a single float
 */
USTRUCT(meta=(DisplayName="From Float", PrototypeName="FromFloat", Keywords="Make,Construct"))
struct FRigUnit_MathColorFromFloat : public FRigUnit_MathColorBase
{
	GENERATED_BODY()
	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	FRigUnit_MathColorFromFloat()
	{
		Value = 0.f;
		Result = FLinearColor::Black;
	}

	UPROPERTY(meta=(Input))
	float Value;

	UPROPERTY(meta=(Output))
	FLinearColor Result;
};

/**
 * Returns the sum of the two values
 */
USTRUCT(meta=(DisplayName="Add", PrototypeName="Add", Keywords="Sum,+"))
struct FRigUnit_MathColorAdd : public FRigUnit_MathColorBinaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the difference of the two values
 */
USTRUCT(meta=(DisplayName="Subtract", PrototypeName="Subtract", Keywords="-"))
struct FRigUnit_MathColorSub : public FRigUnit_MathColorBinaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the product of the two values
 */
USTRUCT(meta=(DisplayName="Multiply", PrototypeName="Multiply", Keywords="Product,*"))
struct FRigUnit_MathColorMul : public FRigUnit_MathColorBinaryOp
{
	GENERATED_BODY()

	FRigUnit_MathColorMul()
	{
		A = B = FLinearColor::White;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Linearly interpolates between A and B using the ratio T
 */
USTRUCT(meta=(DisplayName="Interpolate", PrototypeName="Interpolate", Keywords="Lerp,Mix,Blend"))
struct FRigUnit_MathColorLerp : public FRigUnit_MathColorBase
{
	GENERATED_BODY()

	FRigUnit_MathColorLerp()
	{
		A = Result = FLinearColor::Black;
		B = FLinearColor::White;
		T = 0.f;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FLinearColor A;

	UPROPERTY(meta=(Input))
	FLinearColor B;

	UPROPERTY(meta=(Input))
	float T;

	UPROPERTY(meta=(Output))
	FLinearColor Result;
};

