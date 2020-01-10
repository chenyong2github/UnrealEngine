// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Math/RigUnit_MathBase.h"
#include "RigUnit_MathFloat.generated.h"

USTRUCT(meta=(Abstract, Category="Math|Float", MenuDescSuffix="(Float)"))
struct FRigUnit_MathFloatBase : public FRigUnit_MathBase
{
	GENERATED_BODY()
};

USTRUCT(meta=(Abstract))
struct FRigUnit_MathFloatConstant : public FRigUnit_MathFloatBase
{
	GENERATED_BODY()

	FRigUnit_MathFloatConstant()
	{
		Value = 0.f;
	}

	UPROPERTY(meta=(Output, Constant))
	float Value;
};

USTRUCT(meta=(Abstract))
struct FRigUnit_MathFloatUnaryOp : public FRigUnit_MathFloatBase
{
	GENERATED_BODY()

	FRigUnit_MathFloatUnaryOp()
	{
		Value = Result = 0.f;
	}

	UPROPERTY(meta=(Input))
	float Value;

	UPROPERTY(meta=(Output))
	float Result;
};

USTRUCT(meta=(Abstract))
struct FRigUnit_MathFloatBinaryOp : public FRigUnit_MathFloatBase
{
	GENERATED_BODY()

	FRigUnit_MathFloatBinaryOp()
	{
		A = B = Result = 0.f;
	}

	UPROPERTY(meta=(Input))
	float A;

	UPROPERTY(meta=(Input))
	float B;

	UPROPERTY(meta=(Output))
	float Result;
};

/**
 * Returns PI
 */
USTRUCT(meta=(DisplayName="Pi"))
struct FRigUnit_MathFloatConstPi : public FRigUnit_MathFloatConstant
{
	GENERATED_BODY()
	
	FRigUnit_MathFloatConstPi()
	{
		Value = PI;
	}
};

/**
 * Returns PI * 0.5
 */
USTRUCT(meta=(DisplayName="Half Pi"))
struct FRigUnit_MathFloatConstHalfPi : public FRigUnit_MathFloatConstant
{
	GENERATED_BODY()

	FRigUnit_MathFloatConstHalfPi()
	{
		Value = HALF_PI;
	}
};

/**
 * Returns PI * 2.0
 */
USTRUCT(meta=(DisplayName="Two Pi"))
struct FRigUnit_MathFloatConstTwoPi : public FRigUnit_MathFloatConstant
{
	GENERATED_BODY()
	
	FRigUnit_MathFloatConstTwoPi()
	{
		Value = PI * 2.f;
	}
};

/**
 * Returns the sum of the two values
 */
USTRUCT(meta=(DisplayName="Add", PrototypeName="Add", Keywords="Sum,+"))
struct FRigUnit_MathFloatAdd : public FRigUnit_MathFloatBinaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the difference of the two values
 */
USTRUCT(meta=(DisplayName="Subtract", PrototypeName="Subtract", Keywords="-"))
struct FRigUnit_MathFloatSub : public FRigUnit_MathFloatBinaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the product of the two values
 */
USTRUCT(meta=(DisplayName="Multiply", PrototypeName="Multiply", Keywords="Product,*"))
struct FRigUnit_MathFloatMul : public FRigUnit_MathFloatBinaryOp
{
	GENERATED_BODY()

	FRigUnit_MathFloatMul()
	{
		A = 1.f;
		B = 1.f;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the division of the two values
 */
USTRUCT(meta=(DisplayName="Divide", PrototypeName="Divide", Keywords="Division,Divisor,/"))
struct FRigUnit_MathFloatDiv : public FRigUnit_MathFloatBinaryOp
{
	GENERATED_BODY()

	FRigUnit_MathFloatDiv()
	{
		B = 1.f;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the modulo of the two values
 */
USTRUCT(meta=(DisplayName="Modulo", PrototypeName="Modulo", Keywords="%,fmod"))
struct FRigUnit_MathFloatMod : public FRigUnit_MathFloatBinaryOp
{
	GENERATED_BODY()

	FRigUnit_MathFloatMod()
	{
		A = 0.f;
		B = 1.f;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the smaller of the two values
 */
USTRUCT(meta=(DisplayName="Minimum", PrototypeName="Minimum"))
struct FRigUnit_MathFloatMin : public FRigUnit_MathFloatBinaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the larger of the two values
 */
USTRUCT(meta=(DisplayName="Maximum", PrototypeName="Maximum"))
struct FRigUnit_MathFloatMax : public FRigUnit_MathFloatBinaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the value of A raised to the power of B.
 */
USTRUCT(meta=(DisplayName="Power", PrototypeName="Power"))
struct FRigUnit_MathFloatPow : public FRigUnit_MathFloatBinaryOp
{
	GENERATED_BODY()

	FRigUnit_MathFloatPow()
	{
		A = 1.f;
		B = 1.f;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the square root of the given value
 */
USTRUCT(meta=(DisplayName="Sqrt", PrototypeName="Sqrt", Keywords="Root,Square"))
struct FRigUnit_MathFloatSqrt : public FRigUnit_MathFloatUnaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the negative value
 */
USTRUCT(meta=(DisplayName="Negate", PrototypeName="Negate", Keywords="-,Abs"))
struct FRigUnit_MathFloatNegate : public FRigUnit_MathFloatUnaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the absolute (positive) value
 */
USTRUCT(meta=(DisplayName="Absolute", PrototypeName="Absolute", Keywords="Abs,Neg"))
struct FRigUnit_MathFloatAbs : public FRigUnit_MathFloatUnaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the closest lower full number (integer) of the value
 */
USTRUCT(meta=(DisplayName="Floor", PrototypeName="Floor", Keywords="Round"))
struct FRigUnit_MathFloatFloor : public FRigUnit_MathFloatUnaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the closest higher full number (integer) of the value
 */
USTRUCT(meta=(DisplayName="Ceiling", PrototypeName="Ceiling", Keywords="Round"))
struct FRigUnit_MathFloatCeil : public FRigUnit_MathFloatUnaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the closest higher full number (integer) of the value
 */
USTRUCT(meta=(DisplayName="Round", PrototypeName="Round"))
struct FRigUnit_MathFloatRound : public FRigUnit_MathFloatUnaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the sign of the value (+1 for >= 0.f, -1 for < 0.f)
 */
USTRUCT(meta=(DisplayName="Sign", PrototypeName="Sign"))
struct FRigUnit_MathFloatSign : public FRigUnit_MathFloatUnaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Clamps the given value within the range provided by minimum and maximum
 */
USTRUCT(meta=(DisplayName="Clamp", PrototypeName="Clamp", Keywords="Range,Remap"))
struct FRigUnit_MathFloatClamp : public FRigUnit_MathFloatBase
{
	GENERATED_BODY()

	FRigUnit_MathFloatClamp()
	{
		Value = Minimum = Maximum = Result = 0.f;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	float Value;

	UPROPERTY(meta=(Input))
	float Minimum;

	UPROPERTY(meta=(Input))
	float Maximum;

	UPROPERTY(meta=(Output))
	float Result;
};

/**
 * Linearly interpolates between A and B using the ratio T
 */
USTRUCT(meta=(DisplayName="Interpolate", PrototypeName="Interpolate", Keywords="Lerp,Mix,Blend"))
struct FRigUnit_MathFloatLerp : public FRigUnit_MathFloatBase
{
	GENERATED_BODY()

	FRigUnit_MathFloatLerp()
	{
		A = B = T = Result = 0.f;
		B = 1.f;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	float A;

	UPROPERTY(meta=(Input))
	float B;

	UPROPERTY(meta=(Input))
	float T;

	UPROPERTY(meta=(Output))
	float Result;
};

/**
 * Remaps the given value from a source range to a target range.
 */
USTRUCT(meta=(DisplayName="Remap", PrototypeName="Remap", Keywords="Rescale,Scale"))
struct FRigUnit_MathFloatRemap : public FRigUnit_MathFloatBase
{
	GENERATED_BODY()

	FRigUnit_MathFloatRemap()
	{
		Value = SourceMinimum = TargetMinimum = Result = 0.f;
		SourceMaximum = TargetMaximum = 1.f;
		bClamp = false;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	float Value;

	UPROPERTY(meta=(Input))
	float SourceMinimum;

	UPROPERTY(meta=(Input))
	float SourceMaximum;

	UPROPERTY(meta=(Input))
	float TargetMinimum;

	UPROPERTY(meta=(Input))
	float TargetMaximum;

	/** If set to true the result is clamped to the target range */
	UPROPERTY(meta=(Input))
	bool bClamp;

	UPROPERTY(meta=(Output))
	float Result;
};

/**
 * Returns true if the value A equals B
 */
USTRUCT(meta=(DisplayName="Equals", PrototypeName="Equals", Keywords="Same,=="))
struct FRigUnit_MathFloatEquals : public FRigUnit_MathFloatBase
{
	GENERATED_BODY()
	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	FRigUnit_MathFloatEquals()
	{
		A = B = 0.f;
		Result = true;
	}

	UPROPERTY(meta=(Input))
	float A;

	UPROPERTY(meta=(Input))
	float B;

	UPROPERTY(meta=(Output))
	bool Result;
};

/**
 * Returns true if the value A does not equal B
 */
USTRUCT(meta=(DisplayName="Not Equals", PrototypeName="NotEquals", Keywords="Different,!="))
struct FRigUnit_MathFloatNotEquals : public FRigUnit_MathFloatBase
{
	GENERATED_BODY()
	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	FRigUnit_MathFloatNotEquals()
	{
		A = B = 0.f;
		Result = false;
	}

	UPROPERTY(meta=(Input))
	float A;

	UPROPERTY(meta=(Input))
	float B;

	UPROPERTY(meta=(Output))
	bool Result;
};

/**
 * Returns true if the value A is greater than B
 */
USTRUCT(meta=(DisplayName="Greater", PrototypeName="Greater", Keywords="Larger,Bigger,>"))
struct FRigUnit_MathFloatGreater : public FRigUnit_MathFloatBase
{
	GENERATED_BODY()

	FRigUnit_MathFloatGreater()
	{
		A = B = 0.f;
		Result = false;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	float A;

	UPROPERTY(meta=(Input))
	float B;

	UPROPERTY(meta=(Output))
	bool Result;
};

/**
 * Returns true if the value A is less than B
 */
USTRUCT(meta=(DisplayName="Less", PrototypeName="Less", Keywords="Smaller,<"))
struct FRigUnit_MathFloatLess : public FRigUnit_MathFloatBase
{
	GENERATED_BODY()
	
	FRigUnit_MathFloatLess()
	{
		A = B = 0.f;
		Result = false;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	float A;

	UPROPERTY(meta=(Input))
	float B;

	UPROPERTY(meta=(Output))
	bool Result;
};

/**
 * Returns true if the value A is greater than or equal to B
 */
USTRUCT(meta=(DisplayName="Greater Equal", PrototypeName="GreaterEqual", Keywords="Larger,Bigger,>="))
struct FRigUnit_MathFloatGreaterEqual : public FRigUnit_MathFloatBase
{
	GENERATED_BODY()

	FRigUnit_MathFloatGreaterEqual()
	{
		A = B = 0.f;
		Result = true;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	float A;

	UPROPERTY(meta=(Input))
	float B;

	UPROPERTY(meta=(Output))
	bool Result;
};

/**
 * Returns true if the value A is less than or equal to B
 */
USTRUCT(meta=(DisplayName="Less Equal", PrototypeName="LessEqual", Keywords="Smaller,<="))
struct FRigUnit_MathFloatLessEqual : public FRigUnit_MathFloatBase
{
	GENERATED_BODY()

	FRigUnit_MathFloatLessEqual()
	{
		A = B = 0.f;
		Result = true;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	float A;

	UPROPERTY(meta=(Input))
	float B;

	UPROPERTY(meta=(Output))
	bool Result;
};

/**
 * Returns true if the value is nearly zero
 */
USTRUCT(meta=(DisplayName="Is Nearly Zero", PrototypeName="IsNearlyZero", Keywords="AlmostZero,0"))
struct FRigUnit_MathFloatIsNearlyZero : public FRigUnit_MathFloatBase
{
	GENERATED_BODY()
	
	FRigUnit_MathFloatIsNearlyZero()
	{
		Value = Tolerance = 0.f;
		Result = true;
	}
	
	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	float Value;

	UPROPERTY(meta=(Input))
	float Tolerance;

	UPROPERTY(meta=(Output))
	bool Result;
};

/**
 * Returns true if the value A is almost equal to B
 */
USTRUCT(meta=(DisplayName="Is Nearly Equal", PrototypeName="IsNearlyEqual", Keywords="AlmostEqual,=="))
struct FRigUnit_MathFloatIsNearlyEqual : public FRigUnit_MathFloatBase
{
	GENERATED_BODY()

	FRigUnit_MathFloatIsNearlyEqual()
	{
		A = B = Tolerance = 0.f;
		Result = true;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	float A;

	UPROPERTY(meta=(Input))
	float B;

	UPROPERTY(meta=(Input))
	float Tolerance;

	UPROPERTY(meta=(Output))
	bool Result;
};

/**
 * Return one of the two values based on the condition
 */
USTRUCT(meta=(DisplayName="Select", PrototypeName="Select", Keywords="Pick,If"))
struct FRigUnit_MathFloatSelectBool : public FRigUnit_MathFloatBase
{
	GENERATED_BODY()

	FRigUnit_MathFloatSelectBool()
	{
		Condition = false;
		IfTrue = IfFalse = Result = 0.f;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	bool Condition;

	UPROPERTY(meta=(Input))
	float IfTrue;

	UPROPERTY(meta=(Input))
	float IfFalse;

	UPROPERTY(meta=(Output))
	float Result;
};

/**
 * Returns the degrees of a given value in radians
 */
USTRUCT(meta=(DisplayName="Degrees", PrototypeName="Degrees"))
struct FRigUnit_MathFloatDeg : public FRigUnit_MathFloatUnaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the radians of a given value in degrees
 */
USTRUCT(meta=(DisplayName="Radians", PrototypeName="Radians"))
struct FRigUnit_MathFloatRad : public FRigUnit_MathFloatUnaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the sinus value of the given value (in radians)
 */
USTRUCT(meta=(DisplayName="Sin", PrototypeName="Sin"))
struct FRigUnit_MathFloatSin : public FRigUnit_MathFloatUnaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the cosinus value of the given value (in radians)
 */
USTRUCT(meta=(DisplayName="Cos", PrototypeName="Cos"))
struct FRigUnit_MathFloatCos : public FRigUnit_MathFloatUnaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the tangens value of the given value (in radians)
 */
USTRUCT(meta=(DisplayName="Tan", PrototypeName="Tan"))
struct FRigUnit_MathFloatTan : public FRigUnit_MathFloatUnaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the inverse sinus value (in radians) of the given value
 */
USTRUCT(meta=(DisplayName="Asin", PrototypeName="Asin", Keywords="Arcsin"))
struct FRigUnit_MathFloatAsin : public FRigUnit_MathFloatUnaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the inverse cosinus value (in radians) of the given value
 */
USTRUCT(meta=(DisplayName="Acos", PrototypeName="Acos", Keywords="Arccos"))
struct FRigUnit_MathFloatAcos : public FRigUnit_MathFloatUnaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the inverse tangens value (in radians) of the given value
 */
USTRUCT(meta=(DisplayName="Atan", PrototypeName="Atan", Keywords="Arctan"))
struct FRigUnit_MathFloatAtan : public FRigUnit_MathFloatUnaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Computes the angles alpha, beta and gamma (in radians) between the three sides A, B and C
 */
USTRUCT(meta = (DisplayName = "Law Of Cosine"))
struct FRigUnit_MathFloatLawOfCosine : public FRigUnit_MathFloatBase
{
	GENERATED_BODY()

	FRigUnit_MathFloatLawOfCosine()
	{
		A = B = C = AlphaAngle = BetaAngle = GammaAngle = 0.f;
		bValid = false;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	float A;

	UPROPERTY(meta = (Input))
	float B;

	UPROPERTY(meta = (Input))
	float C;

	UPROPERTY(meta = (Output))
	float AlphaAngle;

	UPROPERTY(meta = (Output))
	float BetaAngle;

	UPROPERTY(meta = (Output))
	float GammaAngle;

	UPROPERTY(meta = (Output))
	bool bValid;
};