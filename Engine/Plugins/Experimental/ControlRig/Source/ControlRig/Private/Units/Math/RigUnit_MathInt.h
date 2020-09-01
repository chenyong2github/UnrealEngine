// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Math/RigUnit_MathBase.h"
#include "RigUnit_MathInt.generated.h"

USTRUCT(meta=(Abstract, Category="Math|Int", MenuDescSuffix="(Int)"))
struct FRigUnit_MathIntBase : public FRigUnit_MathBase
{
	GENERATED_BODY()
};

USTRUCT(meta=(Abstract))
struct FRigUnit_MathIntUnaryOp : public FRigUnit_MathIntBase
{
	GENERATED_BODY()

	FRigUnit_MathIntUnaryOp()
	{
		Value = Result = 0;
	}

	UPROPERTY(meta=(Input))
	int32 Value;

	UPROPERTY(meta=(Output))
	int32 Result;
};

USTRUCT(meta=(Abstract))
struct FRigUnit_MathIntBinaryOp : public FRigUnit_MathIntBase
{
	GENERATED_BODY()

	FRigUnit_MathIntBinaryOp()
	{
		A = B = Result = 0;
	}

	UPROPERTY(meta=(Input))
	int32 A;

	UPROPERTY(meta=(Input))
	int32 B;

	UPROPERTY(meta=(Output))
	int32 Result;
};

/**
 * Returns the sum of the two values
 */
USTRUCT(meta=(DisplayName="Add", PrototypeName="Add", Keywords="Sum,+"))
struct FRigUnit_MathIntAdd : public FRigUnit_MathIntBinaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the difference of the two values
 */
USTRUCT(meta=(DisplayName="Subtract", PrototypeName="Subtract", Keywords="-"))
struct FRigUnit_MathIntSub : public FRigUnit_MathIntBinaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the product of the two values
 */
USTRUCT(meta=(DisplayName="Multiply", PrototypeName="Multiply", Keywords="Product,*"))
struct FRigUnit_MathIntMul : public FRigUnit_MathIntBinaryOp
{
	GENERATED_BODY()

	FRigUnit_MathIntMul()
	{
		A = 1;
		B = 1;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the division of the two values
 */
USTRUCT(meta=(DisplayName="Divide", PrototypeName="Divide", Keywords="Division,Divisor,/"))
struct FRigUnit_MathIntDiv : public FRigUnit_MathIntBinaryOp
{
	GENERATED_BODY()

	FRigUnit_MathIntDiv()
	{
		B = 1;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the modulo of the two values
 */
USTRUCT(meta=(DisplayName="Modulo", PrototypeName="Modulo", Keywords="%,fmod"))
struct FRigUnit_MathIntMod : public FRigUnit_MathIntBinaryOp
{
	GENERATED_BODY()

	FRigUnit_MathIntMod()
	{
		A = 0;
		B = 1;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the smaller of the two values
 */
USTRUCT(meta=(DisplayName="Minimum", PrototypeName="Minimum"))
struct FRigUnit_MathIntMin : public FRigUnit_MathIntBinaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the larger of the two values
 */
USTRUCT(meta=(DisplayName="Maximum", PrototypeName="Maximum"))
struct FRigUnit_MathIntMax : public FRigUnit_MathIntBinaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the value of A raised to the power of B.
 */
USTRUCT(meta=(DisplayName="Power", PrototypeName="Power"))
struct FRigUnit_MathIntPow : public FRigUnit_MathIntBinaryOp
{
	GENERATED_BODY()

	FRigUnit_MathIntPow()
	{
		A = 1;
		B = 1;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the negative value
 */
USTRUCT(meta=(DisplayName="Negate", PrototypeName="Negate", Keywords="-,Abs"))
struct FRigUnit_MathIntNegate : public FRigUnit_MathIntUnaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the absolute (positive) value
 */
USTRUCT(meta=(DisplayName="Absolute", PrototypeName="Absolute", Keywords="Abs,Neg"))
struct FRigUnit_MathIntAbs : public FRigUnit_MathIntUnaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the int cast to a float
 */
USTRUCT(meta=(DisplayName="To Float", PrototypeName="Convert"))
struct FRigUnit_MathIntToFloat : public FRigUnit_MathIntBase
{
	GENERATED_BODY()

	FRigUnit_MathIntToFloat()
	{
		Value = 0;
		Result = 0.f;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	int32 Value;

	UPROPERTY(meta=(Output))
	float Result;
};

/**
 * Returns the sign of the value (+1 for >= 0, -1 for < 0)
 */
USTRUCT(meta=(DisplayName="Sign", PrototypeName="Sign"))
struct FRigUnit_MathIntSign : public FRigUnit_MathIntUnaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Clamps the given value within the range provided by minimum and maximum
 */
USTRUCT(meta=(DisplayName="Clamp", PrototypeName="Clamp", Keywords="Range,Remap"))
struct FRigUnit_MathIntClamp : public FRigUnit_MathIntBase
{
	GENERATED_BODY()

	FRigUnit_MathIntClamp()
	{
		Value = Minimum = Maximum = Result = 0;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	int32 Value;

	UPROPERTY(meta=(Input))
	int32 Minimum;

	UPROPERTY(meta=(Input))
	int32 Maximum;

	UPROPERTY(meta=(Output))
	int32 Result;
};

/**
 * Returns true if the value A equals B
 */
USTRUCT(meta=(DisplayName="Equals", PrototypeName="Equals", Keywords="Same,=="))
struct FRigUnit_MathIntEquals : public FRigUnit_MathIntBase
{
	GENERATED_BODY()
	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	FRigUnit_MathIntEquals()
	{
		A = B = 0;
		Result = true;
	}

	UPROPERTY(meta=(Input))
	int32 A;

	UPROPERTY(meta=(Input))
	int32 B;

	UPROPERTY(meta=(Output))
	bool Result;
};

/**
 * Returns true if the value A does not equal B
 */
USTRUCT(meta=(DisplayName="Not Equals", PrototypeName="NotEquals", Keywords="Different,!="))
struct FRigUnit_MathIntNotEquals : public FRigUnit_MathIntBase
{
	GENERATED_BODY()
	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	FRigUnit_MathIntNotEquals()
	{
		A = B = 0;
		Result = false;
	}

	UPROPERTY(meta=(Input))
	int32 A;

	UPROPERTY(meta=(Input))
	int32 B;

	UPROPERTY(meta=(Output))
	bool Result;
};

/**
 * Returns true if the value A is greater than B
 */
USTRUCT(meta=(DisplayName="Greater", PrototypeName="Greater", Keywords="Larger,Bigger,>"))
struct FRigUnit_MathIntGreater : public FRigUnit_MathIntBase
{
	GENERATED_BODY()

	FRigUnit_MathIntGreater()
	{
		A = B = 0;
		Result = false;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	int32 A;

	UPROPERTY(meta=(Input))
	int32 B;

	UPROPERTY(meta=(Output))
	bool Result;
};

/**
 * Returns true if the value A is less than B
 */
USTRUCT(meta=(DisplayName="Less", PrototypeName="Less", Keywords="Smaller,<"))
struct FRigUnit_MathIntLess : public FRigUnit_MathIntBase
{
	GENERATED_BODY()
	
	FRigUnit_MathIntLess()
	{
		A = B = 0;
		Result = false;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	int32 A;

	UPROPERTY(meta=(Input))
	int32 B;

	UPROPERTY(meta=(Output))
	bool Result;
};

/**
 * Returns true if the value A is greater than or equal to B
 */
USTRUCT(meta=(DisplayName="Greater Equal", PrototypeName="GreaterEqual", Keywords="Larger,Bigger,>="))
struct FRigUnit_MathIntGreaterEqual : public FRigUnit_MathIntBase
{
	GENERATED_BODY()

	FRigUnit_MathIntGreaterEqual()
	{
		A = B = 0;
		Result = true;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	int32 A;

	UPROPERTY(meta=(Input))
	int32 B;

	UPROPERTY(meta=(Output))
	bool Result;
};

/**
 * Returns true if the value A is less than or equal to B
 */
USTRUCT(meta=(DisplayName="Less Equal", PrototypeName="LessEqual", Keywords="Smaller,<="))
struct FRigUnit_MathIntLessEqual : public FRigUnit_MathIntBase
{
	GENERATED_BODY()

	FRigUnit_MathIntLessEqual()
	{
		A = B = 0;
		Result = true;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	int32 A;

	UPROPERTY(meta=(Input))
	int32 B;

	UPROPERTY(meta=(Output))
	bool Result;
};
