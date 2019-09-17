// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Math/RigUnit_MathBase.h"
#include "RigUnit_MathBool.generated.h"

USTRUCT(meta=(Abstract, Category="Math|Boolean", MenuDescSuffix="(Bool)"))
struct FRigUnit_MathBoolBase : public FRigUnit_MathBase
{
	GENERATED_BODY()
};

USTRUCT(meta=(Abstract))
struct FRigUnit_MathBoolConstant : public FRigUnit_MathBoolBase
{
	GENERATED_BODY()

	FRigUnit_MathBoolConstant()
	{
		Value = false;
	}

	UPROPERTY(meta=(Output, Constant))
	bool Value;
};

USTRUCT(meta=(Abstract))
struct FRigUnit_MathBoolUnaryOp : public FRigUnit_MathBoolBase
{
	GENERATED_BODY()

	FRigUnit_MathBoolUnaryOp()
	{
		Value = Result = 0.f;
	}

	UPROPERTY(meta=(Input))
	bool Value;

	UPROPERTY(meta=(Output))
	bool Result;
};

USTRUCT(meta=(Abstract))
struct FRigUnit_MathBoolBinaryOp : public FRigUnit_MathBoolBase
{
	GENERATED_BODY()

	FRigUnit_MathBoolBinaryOp()
	{
		A = B = Result = 0.f;
	}

	UPROPERTY(meta=(Input))
	bool A;

	UPROPERTY(meta=(Input))
	bool B;

	UPROPERTY(meta=(Output))
	bool Result;
};

/**
 * Returns true
 */
USTRUCT(meta=(DisplayName="True", Keywords="Yes"))
struct FRigUnit_MathBoolConstTrue : public FRigUnit_MathBoolConstant
{
	GENERATED_BODY()
	
	FRigUnit_MathBoolConstTrue()
	{
		Value = true;
	}
};

/**
 * Returns false
 */
USTRUCT(meta=(DisplayName="False", Keywords="No"))
struct FRigUnit_MathBoolConstFalse : public FRigUnit_MathBoolConstant
{
	GENERATED_BODY()
	
	FRigUnit_MathBoolConstFalse()
	{
		Value = false;
	}
};

/**
 * Returns true if the condition is false
 */
USTRUCT(meta=(DisplayName="Not", PrototypeName="Not", Keywords="!"))
struct FRigUnit_MathBoolNot : public FRigUnit_MathBoolUnaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns true if both conditions are true
 */
USTRUCT(meta=(DisplayName="And", PrototypeName="And", Keywords="&&"))
struct FRigUnit_MathBoolAnd : public FRigUnit_MathBoolBinaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns true if both conditions are false
 */
USTRUCT(meta=(DisplayName="Nand", PrototypeName="Nand"))
struct FRigUnit_MathBoolNand : public FRigUnit_MathBoolBinaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns true if one of the conditions is true
 */
USTRUCT(meta=(DisplayName="Or", PrototypeName="Or", Keywords="||"))
struct FRigUnit_MathBoolOr : public FRigUnit_MathBoolBinaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns true if the value A equals B
 */
USTRUCT(meta=(DisplayName="Equals", PrototypeName="Equals", Keywords="Same,=="))
struct FRigUnit_MathBoolEquals : public FRigUnit_MathBoolBase
{
	GENERATED_BODY()
	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	FRigUnit_MathBoolEquals()
	{
		A = B = false;
		Result = true;
	}

	UPROPERTY(meta=(Input))
	bool A;

	UPROPERTY(meta=(Input))
	bool B;

	UPROPERTY(meta=(Output))
	bool Result;
};

/**
 * Returns true if the value A does not equal B
 */
USTRUCT(meta=(DisplayName="Not Equals", PrototypeName="NotEquals", Keywords="Different,!=,Xor"))
struct FRigUnit_MathBoolNotEquals : public FRigUnit_MathBoolBase
{
	GENERATED_BODY()
	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	FRigUnit_MathBoolNotEquals()
	{
		A = B = false;
		Result = false;
	}

	UPROPERTY(meta=(Input))
	bool A;

	UPROPERTY(meta=(Input))
	bool B;

	UPROPERTY(meta=(Output))
	bool Result;
};
