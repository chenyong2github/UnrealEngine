// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "MathLibrary.h"
#include "RigUnit_Float.generated.h"

/** Two args and a result of float type */
USTRUCT(meta=(Abstract, NodeColor = "0.1 0.7 0.1", Deprecated="4.23.0"))
struct FRigUnit_BinaryFloatOp : public FRigUnit
{
	GENERATED_BODY()

	UPROPERTY(meta=(Input))
	float Argument0;

	UPROPERTY(meta=(Input))
	float Argument1;

	UPROPERTY(meta=(Output))
	float Result;
};

USTRUCT(meta=(DisplayName="Multiply", Category="Math|Float", Keywords="*", Deprecated="4.23.0"))
struct FRigUnit_Multiply_FloatFloat : public FRigUnit_BinaryFloatOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

USTRUCT(meta=(DisplayName="Add", Category="Math|Float", Keywords = "+,Sum", Deprecated="4.23.0"))
struct FRigUnit_Add_FloatFloat : public FRigUnit_BinaryFloatOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

USTRUCT(meta=(DisplayName="Subtract", Category="Math|Float", Keywords = "-", Deprecated="4.23.0"))
struct FRigUnit_Subtract_FloatFloat : public FRigUnit_BinaryFloatOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

USTRUCT(meta=(DisplayName="Divide", Category="Math|Float", Keywords = "/", Deprecated="4.23.0"))
struct FRigUnit_Divide_FloatFloat : public FRigUnit_BinaryFloatOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/** Two args and a result of float type */
USTRUCT(meta = (DisplayName = "Clamp", Category = "Math|Float", NodeColor = "0.1 0.7 0.1", Deprecated="4.23.0"))
struct FRigUnit_Clamp_Float: public FRigUnit
{
	GENERATED_BODY()

	UPROPERTY(meta = (Input))
	float Value;

	UPROPERTY(meta = (Input))
	float Min;

	UPROPERTY(meta = (Input))
	float Max;

	UPROPERTY(meta = (Output))
	float Result;

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/** Two args and a result of float type */
USTRUCT(meta = (DisplayName = "MapRange", Category = "Math|Float", Deprecated="4.23.0"))
struct FRigUnit_MapRange_Float: public FRigUnit
{
	GENERATED_BODY()

	UPROPERTY(meta = (Input))
	float Value;

	UPROPERTY(meta = (Input))
	float MinIn;

	UPROPERTY(meta = (Input))
	float MaxIn;

	UPROPERTY(meta = (Input))
	float MinOut;

	UPROPERTY(meta = (Input))
	float MaxOut;

	UPROPERTY(meta = (Output))
	float Result;

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};