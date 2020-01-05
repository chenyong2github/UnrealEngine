// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EulerTransform.h"
#include "Units/Math/RigUnit_MathBase.h"
#include "Math/ControlRigMathLibrary.h"
#include "RigUnit_MathTransform.generated.h"

USTRUCT(meta=(Abstract, Category="Math|Transform", MenuDescSuffix="(Transform)"))
struct FRigUnit_MathTransformBase : public FRigUnit_MathBase
{
	GENERATED_BODY()
};

USTRUCT(meta=(Abstract))
struct FRigUnit_MathTransformUnaryOp : public FRigUnit_MathTransformBase
{
	GENERATED_BODY()

	FRigUnit_MathTransformUnaryOp()
	{
		Value = Result = FTransform::Identity;
	}

	UPROPERTY(meta=(Input))
	FTransform Value;

	UPROPERTY(meta=(Output))
	FTransform Result;
};

USTRUCT(meta=(Abstract))
struct FRigUnit_MathTransformBinaryOp : public FRigUnit_MathTransformBase
{
	GENERATED_BODY()

	FRigUnit_MathTransformBinaryOp()
	{
		A = B = Result = FTransform::Identity;
	}

	UPROPERTY(meta=(Input))
	FTransform A;

	UPROPERTY(meta=(Input))
	FTransform B;

	UPROPERTY(meta=(Output))
	FTransform Result;
};

/**
 * Makes a quaternion based transform from a euler based transform
 */
USTRUCT(meta=(DisplayName="From Euler Transform", PrototypeName="FromEulerTransform", Keywords="Make,Construct"))
struct FRigUnit_MathTransformFromEulerTransform : public FRigUnit_MathTransformBase
{
	GENERATED_BODY()

	FRigUnit_MathTransformFromEulerTransform()
	{
		EulerTransform = FEulerTransform::Identity;
		Result = FTransform::Identity;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FEulerTransform EulerTransform;

	UPROPERTY(meta=(Output))
	FTransform Result;
};

/**
 * Retrieves the euler transform
 */
USTRUCT(meta=(DisplayName="To Euler Transform", PrototypeName="ToEulerTransform", Keywords="Make,Construct"))
struct FRigUnit_MathTransformToEulerTransform : public FRigUnit_MathTransformBase
{
	GENERATED_BODY()

	FRigUnit_MathTransformToEulerTransform()
	{
		Value = FTransform::Identity;
		Result = FEulerTransform::Identity;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FTransform Value;

	UPROPERTY(meta=(Output))
	FEulerTransform Result;
};

/**
 * Returns the product of the two values
 */
USTRUCT(meta=(DisplayName="Multiply", PrototypeName="Multiply", Keywords="Product,*,Global"))
struct FRigUnit_MathTransformMul : public FRigUnit_MathTransformBinaryOp
{
	GENERATED_BODY()
	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Returns the relative local transform within a parent's transform
 */
USTRUCT(meta=(DisplayName="Make Relative", PrototypeName="MakeRelative", Keywords="Local,Global,Absolute"))
struct FRigUnit_MathTransformMakeRelative : public FRigUnit_MathTransformBase
{
	GENERATED_BODY()

	FRigUnit_MathTransformMakeRelative()
	{
		Global = Parent = Local = FTransform::Identity;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FTransform Global;

	UPROPERTY(meta=(Input))
	FTransform Parent;

	UPROPERTY(meta=(Output))
	FTransform Local;
};

/**
 * Returns the absolute global transform within a parent's transform
 */
USTRUCT(meta = (DisplayName = "Make Absolute", PrototypeName = "MakeAbsolute", Keywords = "Local,Global,Relative"))
struct FRigUnit_MathTransformMakeAbsolute : public FRigUnit_MathTransformBase
{
	GENERATED_BODY()

	FRigUnit_MathTransformMakeAbsolute()
	{
		Global = Parent = Local = FTransform::Identity;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FTransform Local;

	UPROPERTY(meta = (Input))
	FTransform Parent;

	UPROPERTY(meta = (Output))
	FTransform Global;
};

/**
 * Returns the negative value
 */
USTRUCT(meta=(DisplayName="Inverse", PrototypeName="Inverse"))
struct FRigUnit_MathTransformInverse : public FRigUnit_MathTransformUnaryOp
{
	GENERATED_BODY()
	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
};

/**
 * Linearly interpolates between A and B using the ratio T
 */
USTRUCT(meta=(DisplayName="Interpolate", PrototypeName="Interpolate", Keywords="Lerp,Mix,Blend"))
struct FRigUnit_MathTransformLerp : public FRigUnit_MathTransformBase
{
	GENERATED_BODY()

	FRigUnit_MathTransformLerp()
	{
		A = B = Result = FTransform::Identity;
		T = 0.f;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FTransform A;

	UPROPERTY(meta=(Input))
	FTransform B;

	UPROPERTY(meta=(Input))
	float T;

	UPROPERTY(meta=(Output))
	FTransform Result;
};

/**
 * Return one of the two values based on the condition
 */
USTRUCT(meta=(DisplayName="Select", PrototypeName="Select", Keywords="Pick,If"))
struct FRigUnit_MathTransformSelectBool : public FRigUnit_MathTransformBase
{
	GENERATED_BODY()

	FRigUnit_MathTransformSelectBool()
	{
		Condition = false;
		IfTrue = IfFalse = Result = FTransform::Identity;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	bool Condition;

	UPROPERTY(meta=(Input))
	FTransform IfTrue;

	UPROPERTY(meta=(Input))
	FTransform IfFalse;

	UPROPERTY(meta=(Output))
	FTransform Result;
};

/**
 * Rotates a given vector (direction) by the transform
 */
USTRUCT(meta=(DisplayName="Transform Direction", PrototypeName="Rotate", Keywords="Transform,Direction"))
struct FRigUnit_MathTransformRotateVector : public FRigUnit_MathTransformBase
{
	GENERATED_BODY()

	FRigUnit_MathTransformRotateVector()
	{
		Transform = FTransform::Identity;
		Direction = Result = FVector::ZeroVector;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FTransform Transform;

	UPROPERTY(meta=(Input))
	FVector Direction;

	UPROPERTY(meta=(Output))
	FVector Result;
};

/**
 * Rotates a given vector (location) by the transform
 */
USTRUCT(meta=(DisplayName="Transform Location", PrototypeName="Multiply"))
struct FRigUnit_MathTransformTransformVector : public FRigUnit_MathTransformBase
{
	GENERATED_BODY()

	FRigUnit_MathTransformTransformVector()
	{
		Transform = FTransform::Identity;
		Location = Result = FVector::ZeroVector;
	}
	
	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FTransform Transform;

	UPROPERTY(meta=(Input))
	FVector Location;

	UPROPERTY(meta=(Output))
	FVector Result;
};

/**
 * Composes a Transform (and Euler Transform) from its components.
 */
USTRUCT(meta=(DisplayName="Transform from SRT", Keywords ="EulerTransform,Scale,Rotation,Orientation,Translation,Location"))
struct FRigUnit_MathTransformFromSRT : public FRigUnit_MathTransformBase
{
	GENERATED_BODY()

	FRigUnit_MathTransformFromSRT()
	{
		Location = FVector::ZeroVector;
		Rotation = FVector::ZeroVector;
		RotationOrder = EControlRigRotationOrder::XYZ;
		Scale = FVector::OneVector;
		Transform = FTransform::Identity;
		EulerTransform = FEulerTransform::Identity;
	}
	
	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FVector Location;

	UPROPERTY(meta=(Input))
	FVector Rotation;

	UPROPERTY(meta=(Input))
	EControlRigRotationOrder RotationOrder;

	UPROPERTY(meta=(Input))
	FVector Scale;

	UPROPERTY(meta=(Output))
	FTransform Transform;

	UPROPERTY(meta=(Output))
	FEulerTransform EulerTransform;
};

/**
 * Clamps a position using a plane collision, cylindric collision or spherical collision.
 */
USTRUCT(meta = (DisplayName = "Clamp Spatially", PrototypeName = "ClampSpatially", Keywords = "Collide,Collision"))
struct FRigUnit_MathTransformClampSpatially : public FRigUnit_MathTransformBase
{
	GENERATED_BODY()

	FRigUnit_MathTransformClampSpatially()
	{
		Value = Result = FTransform::Identity;
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
	FTransform Value;

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
	FTransform Result;
};