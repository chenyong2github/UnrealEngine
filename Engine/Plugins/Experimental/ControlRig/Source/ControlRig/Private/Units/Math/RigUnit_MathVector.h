// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Math/RigUnit_MathBase.h"
#include "Math/ControlRigMathLibrary.h"
#include "RigUnit_MathVector.generated.h"

USTRUCT(meta=(Abstract, Category="Math|Vector", MenuDescSuffix="(Vector)"))
struct FRigUnit_MathVectorBase : public FRigUnit_MathBase
{
	GENERATED_BODY()
};

USTRUCT(meta=(Abstract))
struct FRigUnit_MathVectorUnaryOp : public FRigUnit_MathVectorBase
{
	GENERATED_BODY()
	
	FRigUnit_MathVectorUnaryOp()
	{
		Value = Result = FVector::ZeroVector;
	}

	UPROPERTY(meta=(Input))
	FVector Value;

	UPROPERTY(meta=(Output))
	FVector Result;
};

USTRUCT(meta=(Abstract))
struct FRigUnit_MathVectorBinaryOp : public FRigUnit_MathVectorBase
{
	GENERATED_BODY()

	FRigUnit_MathVectorBinaryOp()
	{
		A = B = Result = FVector::ZeroVector;
	}

	UPROPERTY(meta=(Input))
	FVector A;

	UPROPERTY(meta=(Input))
	FVector B;

	UPROPERTY(meta=(Output))
	FVector Result;
};

/**
 * Makes a vector from a single float
 */
USTRUCT(meta=(DisplayName="From Float", PrototypeName="FromFloat", Keywords="Make,Construct"))
struct FRigUnit_MathVectorFromFloat : public FRigUnit_MathVectorBase
{
	GENERATED_BODY()
	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	FRigUnit_MathVectorFromFloat()
	{
		Value = 0.f;
		Result = FVector::ZeroVector;
	}

	UPROPERTY(meta=(Input))
	float Value;

	UPROPERTY(meta=(Output))
	FVector Result;
};

/**
 * Returns the sum of the two values
 */
USTRUCT(meta=(DisplayName="Add", PrototypeName="Add", Keywords="Sum,+"))
struct FRigUnit_MathVectorAdd : public FRigUnit_MathVectorBinaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the difference of the two values
 */
USTRUCT(meta=(DisplayName="Subtract", PrototypeName="Subtract", Keywords="-"))
struct FRigUnit_MathVectorSub : public FRigUnit_MathVectorBinaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the product of the two values
 */
USTRUCT(meta=(DisplayName="Multiply", PrototypeName="Multiply", Keywords="Product,*"))
struct FRigUnit_MathVectorMul : public FRigUnit_MathVectorBinaryOp
{
	GENERATED_BODY()

	FRigUnit_MathVectorMul()
	{
		A = B = FVector::OneVector;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the product of the the vector and the float value
 */
USTRUCT(meta = (DisplayName = "Scale", PrototypeName = "Scale", Keywords = "Multiply,Product,*,ByScalar,ByFloat"))
struct FRigUnit_MathVectorScale : public FRigUnit_MathVectorBase
{
	GENERATED_BODY()

		FRigUnit_MathVectorScale()
	{
		Value = Result = FVector::ZeroVector;
		Factor = 1.f;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FVector Value;

	UPROPERTY(meta = (Input))
	float Factor;

	UPROPERTY(meta = (Output))
	FVector Result;
};

/**
 * Returns the division of the two values
 */
USTRUCT(meta=(DisplayName="Divide", PrototypeName="Divide", Keywords="Division,Divisor,/"))
struct FRigUnit_MathVectorDiv : public FRigUnit_MathVectorBinaryOp
{
	GENERATED_BODY()

	FRigUnit_MathVectorDiv()
	{
		B = FVector::OneVector;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the modulo of the two values
 */
USTRUCT(meta=(DisplayName="Modulo", PrototypeName="Modulo", Keywords="%,fmod"))
struct FRigUnit_MathVectorMod : public FRigUnit_MathVectorBinaryOp
{
	GENERATED_BODY()

	FRigUnit_MathVectorMod()
	{
		A = FVector::ZeroVector;
		B = FVector::OneVector;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the smaller of the two values for each component
 */
USTRUCT(meta=(DisplayName="Minimum", PrototypeName="Minimum"))
struct FRigUnit_MathVectorMin : public FRigUnit_MathVectorBinaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the larger of the two values each component
 */
USTRUCT(meta=(DisplayName="Maximum", PrototypeName="Maximum"))
struct FRigUnit_MathVectorMax : public FRigUnit_MathVectorBinaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the negative value
 */
USTRUCT(meta=(DisplayName="Negate", PrototypeName="Negate", Keywords="-,Abs"))
struct FRigUnit_MathVectorNegate : public FRigUnit_MathVectorUnaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the absolute (positive) value
 */
USTRUCT(meta=(DisplayName="Absolute", PrototypeName="Absolute", Keywords="Abs,Neg"))
struct FRigUnit_MathVectorAbs : public FRigUnit_MathVectorUnaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the closest lower full number (integer) of the value for each component
 */
USTRUCT(meta=(DisplayName="Floor", PrototypeName="Floor", Keywords="Round"))
struct FRigUnit_MathVectorFloor : public FRigUnit_MathVectorUnaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the closest higher full number (integer) of the value for each component
 */
USTRUCT(meta=(DisplayName="Ceiling", PrototypeName="Ceiling", Keywords="Round"))
struct FRigUnit_MathVectorCeil : public FRigUnit_MathVectorUnaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the closest higher full number (integer) of the value for each component
 */
USTRUCT(meta=(DisplayName="Round", PrototypeName="Round"))
struct FRigUnit_MathVectorRound : public FRigUnit_MathVectorUnaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the sign of the value (+1 for >= FVector(0.f, 0.f, 0.f), -1 for < 0.f) for each component
 */
USTRUCT(meta=(DisplayName="Sign", PrototypeName="Sign"))
struct FRigUnit_MathVectorSign : public FRigUnit_MathVectorUnaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Clamps the given value within the range provided by minimum and maximum for each component
 */
USTRUCT(meta=(DisplayName="Clamp", PrototypeName="Clamp", Keywords="Range,Remap"))
struct FRigUnit_MathVectorClamp : public FRigUnit_MathVectorBase
{
	GENERATED_BODY()

	FRigUnit_MathVectorClamp()
	{
		Value = Minimum = Result = FVector::ZeroVector;
		Maximum = FVector::OneVector;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FVector Value;

	UPROPERTY(meta=(Input))
	FVector Minimum;

	UPROPERTY(meta=(Input))
	FVector Maximum;

	UPROPERTY(meta=(Output))
	FVector Result;
};

/**
 * Linearly interpolates between A and B using the ratio T
 */
USTRUCT(meta=(DisplayName="Interpolate", PrototypeName="Interpolate", Keywords="Lerp,Mix,Blend"))
struct FRigUnit_MathVectorLerp : public FRigUnit_MathVectorBase
{
	GENERATED_BODY()

	FRigUnit_MathVectorLerp()
	{
		A = Result = FVector::ZeroVector;
		B = FVector::OneVector;
		T = 0.f;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FVector A;

	UPROPERTY(meta=(Input))
	FVector B;

	UPROPERTY(meta=(Input))
	float T;

	UPROPERTY(meta=(Output))
	FVector Result;
};

/**
 * Remaps the given value from a source range to a target range for each component
 */
USTRUCT(meta=(DisplayName="Remap", PrototypeName="Remap", Keywords="Rescale,Scale"))
struct FRigUnit_MathVectorRemap : public FRigUnit_MathVectorBase
{
	GENERATED_BODY()

	FRigUnit_MathVectorRemap()
	{
		Value = SourceMinimum = TargetMinimum = Result = FVector::ZeroVector;
		SourceMaximum = TargetMaximum = FVector::OneVector;
		bClamp = false;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FVector Value;

	UPROPERTY(meta=(Input))
	FVector SourceMinimum;

	UPROPERTY(meta=(Input))
	FVector SourceMaximum;

	UPROPERTY(meta=(Input))
	FVector TargetMinimum;

	UPROPERTY(meta=(Input))
	FVector TargetMaximum;

	/** If set to true the result is clamped to the target range */
	UPROPERTY(meta=(Input))
	bool bClamp;

	UPROPERTY(meta=(Output))
	FVector Result;
};

/**
 * Returns true if the value A equals B
 */
USTRUCT(meta=(DisplayName="Equals", PrototypeName="Equals", Keywords="Same,=="))
struct FRigUnit_MathVectorEquals : public FRigUnit_MathVectorBase
{
	GENERATED_BODY()

	FRigUnit_MathVectorEquals()
	{
		A = B = FVector::ZeroVector;
		Result = true;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FVector A;

	UPROPERTY(meta=(Input))
	FVector B;

	UPROPERTY(meta=(Output))
	bool Result;
};

/**
 * Returns true if the value A does not equal B
 */
USTRUCT(meta=(DisplayName="Not Equals", PrototypeName="NotEquals", Keywords="Different,!="))
struct FRigUnit_MathVectorNotEquals : public FRigUnit_MathVectorBase
{
	GENERATED_BODY()

	FRigUnit_MathVectorNotEquals()
	{
		A = B = FVector::ZeroVector;
		Result = false;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FVector A;

	UPROPERTY(meta=(Input))
	FVector B;

	UPROPERTY(meta=(Output))
	bool Result;
};

/**
 * Returns true if the value is nearly zero
 */
USTRUCT(meta=(DisplayName="Is Nearly Zero", PrototypeName="IsNearlyZero", Keywords="AlmostZero,0"))
struct FRigUnit_MathVectorIsNearlyZero : public FRigUnit_MathVectorBase
{
	GENERATED_BODY()

	FRigUnit_MathVectorIsNearlyZero()
	{
		Value = FVector::ZeroVector;
		Tolerance = 0.f;
		Result = true;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FVector Value;

	UPROPERTY(meta=(Input))
	float Tolerance;

	UPROPERTY(meta=(Output))
	bool Result;
};

/**
 * Returns true if the value A is almost equal to B
 */
USTRUCT(meta=(DisplayName="Is Nearly Equal", PrototypeName="IsNearlyEqual", Keywords="AlmostEqual,=="))
struct FRigUnit_MathVectorIsNearlyEqual : public FRigUnit_MathVectorBase
{
	GENERATED_BODY()

	FRigUnit_MathVectorIsNearlyEqual()
	{
		A = B = FVector::ZeroVector;
		Tolerance = 0.f;
		Result = true;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FVector A;

	UPROPERTY(meta=(Input))
	FVector B;

	UPROPERTY(meta=(Input))
	float Tolerance;

	UPROPERTY(meta=(Output))
	bool Result;
};

/**
 * Return one of the two values based on the condition
 */
USTRUCT(meta=(DisplayName="Select", PrototypeName="Select", Keywords="Pick,If"))
struct FRigUnit_MathVectorSelectBool : public FRigUnit_MathVectorBase
{
	GENERATED_BODY()

	FRigUnit_MathVectorSelectBool()
	{
		Condition = false;
		IfTrue = IfFalse = Result = FVector::ZeroVector;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	bool Condition;

	UPROPERTY(meta=(Input))
	FVector IfTrue;

	UPROPERTY(meta=(Input))
	FVector IfFalse;

	UPROPERTY(meta=(Output))
	FVector Result;
};

/**
 * Returns the degrees of a given value in radians
 */
USTRUCT(meta=(DisplayName="Degrees", PrototypeName="Degrees"))
struct FRigUnit_MathVectorDeg : public FRigUnit_MathVectorUnaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the radians of a given value in degrees
 */
USTRUCT(meta=(DisplayName="Radians", PrototypeName="Radians"))
struct FRigUnit_MathVectorRad : public FRigUnit_MathVectorUnaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the squared length of the vector
 */
USTRUCT(meta=(DisplayName="Length Squared", PrototypeName="LengthSquared", Keywords="Length,Size,Magnitude"))
struct FRigUnit_MathVectorLengthSquared : public FRigUnit_MathVectorBase
{
	GENERATED_BODY()

	FRigUnit_MathVectorLengthSquared()
	{
		Value = FVector::ZeroVector;
		Result = 0.f;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FVector Value;

	UPROPERTY(meta=(Output))
	float Result;
};

/**
 * Returns the length of the vector
 */
USTRUCT(meta=(DisplayName="Length", PrototypeName="Length", Keywords="Size,Magnitude"))
struct FRigUnit_MathVectorLength : public FRigUnit_MathVectorBase
{
	GENERATED_BODY()

	FRigUnit_MathVectorLength()
	{
		Value = FVector::ZeroVector;
		Result = 0.f;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FVector Value;

	UPROPERTY(meta=(Output))
	float Result;
};

/**
 * Returns the distance from A to B
 */
USTRUCT(meta=(DisplayName="Distance Between", PrototypeName="Distance"))
struct FRigUnit_MathVectorDistance : public FRigUnit_MathVectorBase
{
	GENERATED_BODY()

	FRigUnit_MathVectorDistance()
	{
		A = B = FVector::ZeroVector;
		Result = 0.f;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FVector A;

	UPROPERTY(meta=(Input))
	FVector B;

	UPROPERTY(meta=(Output))
	float Result;
};

/**
 * Returns the cross product between two vectors
 */
USTRUCT(meta=(DisplayName="Cross", PrototypeName="Cross", Keywords="^"))
struct FRigUnit_MathVectorCross : public FRigUnit_MathVectorBinaryOp
{
	GENERATED_BODY()
	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the dot product between two vectors
 */
USTRUCT(meta=(DisplayName="Dot", PrototypeName="Dot,|"))
struct FRigUnit_MathVectorDot : public FRigUnit_MathVectorBase
{
	GENERATED_BODY()

	FRigUnit_MathVectorDot()
	{
		A = B = FVector::ZeroVector;
		Result = 0.f;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FVector A;

	UPROPERTY(meta=(Input))
	FVector B;

	UPROPERTY(meta=(Output))
	float Result;
};

/**
 * Returns the normalized value
 */
USTRUCT(meta=(DisplayName="Unit", PrototypeName="Unit", Keywords="Normalize"))
struct FRigUnit_MathVectorUnit : public FRigUnit_MathVectorUnaryOp
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Sets the length of a given vector
 */
USTRUCT(meta = (DisplayName = "SetLength", PrototypeName = "SetLength", Keywords = "Unit,Normalize,Scale"))
struct FRigUnit_MathVectorSetLength: public FRigUnit_MathVectorBase
{
	GENERATED_BODY()

	FRigUnit_MathVectorSetLength()
	{
		Value = Result = FVector(1.f, 0.f, 0.f);
		Length = 1.f;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FVector Value;

	UPROPERTY(meta = (Input))
	float Length;

	UPROPERTY(meta = (Output))
	FVector Result;
};

/**
 * Clamps the length of a given vector between a minimum and maximum
 */
USTRUCT(meta = (DisplayName = "ClampLength", PrototypeName = "ClampLength", Keywords = "Unit,Normalize,Scale"))
struct FRigUnit_MathVectorClampLength: public FRigUnit_MathVectorBase
{
	GENERATED_BODY()

	FRigUnit_MathVectorClampLength()
	{
		Value = Result = FVector(1.f, 0.f, 0.f);
		MinimumLength = 0.f;
		MaximumLength = 1.f;
	}


	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FVector Value;

	UPROPERTY(meta = (Input))
	float MinimumLength;

	UPROPERTY(meta = (Input))
	float MaximumLength;

	UPROPERTY(meta = (Output))
	FVector Result;
};

/**
 * Mirror a vector about a normal vector.
 */
USTRUCT(meta=(DisplayName="Mirror", PrototypeName="Mirror"))
struct FRigUnit_MathVectorMirror : public FRigUnit_MathVectorBase
{
	GENERATED_BODY()

	FRigUnit_MathVectorMirror()
	{
		Value = Result = FVector::ZeroVector;
		Normal = FVector(1.f, 0.f, 0.f);
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FVector Value;

	UPROPERTY(meta=(Input))
	FVector Normal;

	UPROPERTY(meta=(Output))
	FVector Result;
};

/**
 * Returns the angle between two vectors in radians
 */
USTRUCT(meta=(DisplayName="Angle Between", PrototypeName="AngleBetween"))
struct FRigUnit_MathVectorAngle : public FRigUnit_MathVectorBase
{
	GENERATED_BODY()

		FRigUnit_MathVectorAngle()
	{
		A = B = FVector::ZeroVector;
		Result = 0.f;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FVector A;

	UPROPERTY(meta=(Input))
	FVector B;

	UPROPERTY(meta=(Output))
	float Result;
};

/**
 * Returns true if the two vectors are parallel
 */
USTRUCT(meta=(DisplayName="Parallel", PrototypeName="Parallel"))
struct FRigUnit_MathVectorParallel : public FRigUnit_MathVectorBase
{
	GENERATED_BODY()

	FRigUnit_MathVectorParallel()
	{
		A = B = FVector(1.f, 0.f, 0.f);
		Result = true;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FVector A;

	UPROPERTY(meta=(Input))
	FVector B;

	UPROPERTY(meta=(Output))
	bool Result;
};

/**
 * Returns true if the two vectors are orthogonal
 */
USTRUCT(meta=(DisplayName="Orthogonal", PrototypeName="Orthogonal"))
struct FRigUnit_MathVectorOrthogonal : public FRigUnit_MathVectorBase
{
	GENERATED_BODY()

	FRigUnit_MathVectorOrthogonal()
	{
		A = FVector(1.f, 0.f, 0.f);
		B = FVector(0.f, 1.f, 0.f);
		Result = true;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FVector A;

	UPROPERTY(meta=(Input))
	FVector B;

	UPROPERTY(meta=(Output))
	bool Result;
};

/**
 * Returns the 4 point bezier interpolation
 */
USTRUCT(meta=(DisplayName="Bezier Four Point"))
struct FRigUnit_MathVectorBezierFourPoint : public FRigUnit_MathVectorBase
{
	GENERATED_BODY()

	FRigUnit_MathVectorBezierFourPoint()
	{
		Bezier = FCRFourPointBezier();
		T = 0.f;
		Result = Tangent = FVector::ZeroVector;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FCRFourPointBezier Bezier;

	UPROPERTY(meta=(Input))
	float T;

	UPROPERTY(meta=(Output))
	FVector Result;

	UPROPERTY(meta=(Output))
	FVector Tangent;
};

/**
 * Creates a bezier four point
 */
USTRUCT(meta = (DisplayName = "Make Bezier Four Point"))
struct FRigUnit_MathVectorMakeBezierFourPoint : public FRigUnit_MathVectorBase
{
	GENERATED_BODY()

	FRigUnit_MathVectorMakeBezierFourPoint()
	{
		Bezier = FCRFourPointBezier();
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input, Output))
	FCRFourPointBezier Bezier;
};

/**
 * Clamps a position using a plane collision, cylindric collision or spherical collision.
 */
USTRUCT(meta = (DisplayName = "Clamp Spatially", PrototypeName = "ClampSpatially", Keywords="Collide,Collision"))
struct FRigUnit_MathVectorClampSpatially: public FRigUnit_MathVectorBase
{
	GENERATED_BODY()

	FRigUnit_MathVectorClampSpatially()
	{
		Value = Result = FVector::ZeroVector;
		Axis = EAxis::X;
		Type = EControlRigClampSpatialMode::Plane;
		Minimum = 0.f;
		Maximum = 100.f;
		Space = FTransform::Identity;
		bDrawDebug = false;
		DebugColor = FLinearColor::Red;
		DebugThickness = 1.f;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FVector Value;

	UPROPERTY(meta = (Input))
	TEnumAsByte<EAxis::Type> Axis;

	UPROPERTY(meta = (Input))
	TEnumAsByte<EControlRigClampSpatialMode::Type> Type;

	UPROPERTY(meta = (Input))
	float Minimum;

	UPROPERTY(meta = (Input))
	float Maximum;

	// The space this spatial clamp happens within.
	// The input position will be projected into this space.
	UPROPERTY(meta = (Input))
	FTransform Space;

	UPROPERTY(meta = (Input))
	bool bDrawDebug;

	UPROPERTY(meta = (Input))
	FLinearColor DebugColor;

	UPROPERTY(meta = (Input))
	float DebugThickness;

	UPROPERTY(meta = (Output))
	FVector Result;
};