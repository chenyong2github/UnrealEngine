// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Simulation/RigUnit_SimBase.h"
#include "RigUnit_Accumulate.generated.h"

/**
 * Adds a value over time over and over again
 */
USTRUCT(meta=(DisplayName="Accumulate Add (Float)", PrototypeName="AccumulateAdd", Keywords="Simulate,++"))
struct FRigUnit_AccumulateFloatAdd : public FRigUnit_SimBase
{
	GENERATED_BODY()
	
	FRigUnit_AccumulateFloatAdd()
	{
		InitialValue = Increment = Result = AccumulatedValue = 0.f;
		bIntegrateDeltaTime = false;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	float Increment;

	UPROPERTY(meta = (Input))
	float InitialValue;

	UPROPERTY(meta=(Input))
	bool bIntegrateDeltaTime;

	UPROPERTY(meta=(Output))
	float Result;

	UPROPERTY()
	float AccumulatedValue;
};

/**
 * Adds a vector over time over and over again
 */
USTRUCT(meta = (DisplayName="Accumulate Add (Vector)", PrototypeName="AccumulateAdd", Keywords="Simulate,++"))
struct FRigUnit_AccumulateVectorAdd : public FRigUnit_SimBase
{
	GENERATED_BODY()

	FRigUnit_AccumulateVectorAdd()
	{
		InitialValue = Increment = Result = AccumulatedValue = FVector::ZeroVector;
		bIntegrateDeltaTime = false;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FVector Increment;

	UPROPERTY(meta = (Input))
	FVector InitialValue;

	UPROPERTY(meta = (Input))
	bool bIntegrateDeltaTime;

	UPROPERTY(meta = (Output))
	FVector Result;

	UPROPERTY()
	FVector AccumulatedValue;
};

/**
 * Multiplies a value over time over and over again
 */
USTRUCT(meta=(DisplayName="Accumulate Mul (Float)", PrototypeName="AccumulateMul", Keywords="Simulate,**"))
struct FRigUnit_AccumulateFloatMul : public FRigUnit_SimBase
{
	GENERATED_BODY()
	
	FRigUnit_AccumulateFloatMul()
	{
		InitialValue = Multiplier = Result = AccumulatedValue = 1.f;
		bIntegrateDeltaTime = false;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	float Multiplier;

	UPROPERTY(meta = (Input))
	float InitialValue;

	UPROPERTY(meta=(Input))
	bool bIntegrateDeltaTime;

	UPROPERTY(meta=(Output))
	float Result;

	UPROPERTY()
	float AccumulatedValue;
};

/**
 * Multiplies a vector over time over and over again
 */
USTRUCT(meta = (DisplayName="Accumulate Mul (Vector)", PrototypeName="AccumulateMul", Keywords="Simulate,**"))
struct FRigUnit_AccumulateVectorMul : public FRigUnit_SimBase
{
	GENERATED_BODY()

	FRigUnit_AccumulateVectorMul()
	{
		InitialValue = Multiplier = Result = AccumulatedValue = FVector::OneVector;
		bIntegrateDeltaTime = false;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FVector Multiplier;

	UPROPERTY(meta = (Input))
	FVector InitialValue;

	UPROPERTY(meta = (Input))
	bool bIntegrateDeltaTime;

	UPROPERTY(meta = (Output))
	FVector Result;

	UPROPERTY()
	FVector AccumulatedValue;
};

/**
 * Multiplies a quaternion over time over and over again
 */
USTRUCT(meta=(DisplayName="Accumulate Mul (Quaternion)", PrototypeName="AccumulateMul", Keywords="Simulate,**"))
struct FRigUnit_AccumulateQuatMul : public FRigUnit_SimBase
{
	GENERATED_BODY()
	
	FRigUnit_AccumulateQuatMul()
	{
		InitialValue = Multiplier = Result = AccumulatedValue = FQuat::Identity;
		bIntegrateDeltaTime = bFlipOrder = false;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FQuat Multiplier;

	UPROPERTY(meta = (Input))
	FQuat InitialValue;

	UPROPERTY(meta = (Input))
	bool bFlipOrder;

	UPROPERTY(meta=(Input))
	bool bIntegrateDeltaTime;

	UPROPERTY(meta=(Output))
	FQuat Result;

	UPROPERTY()
	FQuat AccumulatedValue;
};

/**
 * Multiplies a transform over time over and over again
 */
USTRUCT(meta = (DisplayName="Accumulate Mul (Transform)", PrototypeName="AccumulateMul", Keywords="Simulate,**"))
struct FRigUnit_AccumulateTransformMul : public FRigUnit_SimBase
{
	GENERATED_BODY()

	FRigUnit_AccumulateTransformMul()
	{
		InitialValue = Multiplier = Result = AccumulatedValue = FTransform::Identity;
		bIntegrateDeltaTime = bFlipOrder = false;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FTransform Multiplier;

	UPROPERTY(meta = (Input))
	FTransform InitialValue;

	UPROPERTY(meta = (Input))
	bool bFlipOrder;

	UPROPERTY(meta = (Input))
	bool bIntegrateDeltaTime;

	UPROPERTY(meta = (Output))
	FTransform Result;

	UPROPERTY()
	FTransform AccumulatedValue;
};

/**
 * Interpolates two values over time over and over again
 */
USTRUCT(meta=(DisplayName="Accumulate Lerp (Float)", PrototypeName="AccumulateLerp", Keywords="Simulate,Ramp"))
struct FRigUnit_AccumulateFloatLerp : public FRigUnit_SimBase
{
	GENERATED_BODY()
	
	FRigUnit_AccumulateFloatLerp()
	{
		InitialValue = TargetValue = Blend = Result = AccumulatedValue = 0.f;
		bIntegrateDeltaTime = false;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	float TargetValue;

	UPROPERTY(meta = (Input))
	float InitialValue;

	UPROPERTY(meta = (Input))
	float Blend;

	UPROPERTY(meta=(Input))
	bool bIntegrateDeltaTime;

	UPROPERTY(meta=(Output))
	float Result;

	UPROPERTY()
	float AccumulatedValue;
};

/**
 * Interpolates two vectors over time over and over again
 */
USTRUCT(meta = (DisplayName="Accumulate Lerp (Vector)", PrototypeName="AccumulateLerp", Keywords="Simulate,Ramp"))
struct FRigUnit_AccumulateVectorLerp : public FRigUnit_SimBase
{
	GENERATED_BODY()

	FRigUnit_AccumulateVectorLerp()
	{
		InitialValue = TargetValue = Result = AccumulatedValue = FVector::ZeroVector;
		Blend = 0.f;
		bIntegrateDeltaTime = false;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FVector TargetValue;

	UPROPERTY(meta = (Input))
	FVector InitialValue;

	UPROPERTY(meta = (Input))
	float Blend;

	UPROPERTY(meta = (Input))
	bool bIntegrateDeltaTime;

	UPROPERTY(meta = (Output))
	FVector Result;

	UPROPERTY()
	FVector AccumulatedValue;
};

/**
 * Interpolates two quaternions over time over and over again
 */
USTRUCT(meta=(DisplayName="Accumulate Lerp (Quaternion)", PrototypeName="AccumulateLerp", Keywords="Simulate,Ramp"))
struct FRigUnit_AccumulateQuatLerp : public FRigUnit_SimBase
{
	GENERATED_BODY()
	
	FRigUnit_AccumulateQuatLerp()
	{
		InitialValue = TargetValue = Result = AccumulatedValue = FQuat::Identity;
		Blend = 0.f;
		bIntegrateDeltaTime = false;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FQuat TargetValue;

	UPROPERTY(meta = (Input))
	FQuat InitialValue;

	UPROPERTY(meta = (Input))
	float Blend;

	UPROPERTY(meta=(Input))
	bool bIntegrateDeltaTime;

	UPROPERTY(meta=(Output))
	FQuat Result;

	UPROPERTY()
	FQuat AccumulatedValue;
};

/**
 * Interpolates two transforms over time over and over again
 */
USTRUCT(meta = (DisplayName="Accumulate Lerp (Transform)", PrototypeName="AccumulateLerp", Keywords="Simulate,Ramp"))
struct FRigUnit_AccumulateTransformLerp : public FRigUnit_SimBase
{
	GENERATED_BODY()

	FRigUnit_AccumulateTransformLerp()
	{
		InitialValue = TargetValue = Result = AccumulatedValue = FTransform::Identity;
		Blend = 0.f;
		bIntegrateDeltaTime = false;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FTransform TargetValue;

	UPROPERTY(meta = (Input))
	FTransform InitialValue;

	UPROPERTY(meta = (Input))
	float Blend;

	UPROPERTY(meta = (Input))
	bool bIntegrateDeltaTime;

	UPROPERTY(meta = (Output))
	FTransform Result;

	UPROPERTY()
	FTransform AccumulatedValue;
};

/**
 * Accumulates the min and max values over time
 */
USTRUCT(meta=(DisplayName="Accumulate Range (Float)", PrototypeName="AccumulateRange", Keywords="Range"))
struct FRigUnit_AccumulateFloatRange : public FRigUnit_SimBase
{
	GENERATED_BODY()
	
	FRigUnit_AccumulateFloatRange()
	{
		Value = Minimum = Maximum = AccumulatedMinimum = AccumulatedMaximum = 0.f;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	float Value;

	UPROPERTY(meta= (Output))
	float Minimum;

	UPROPERTY(meta= (Output))
	float Maximum;

	UPROPERTY()
	float AccumulatedMinimum;

	UPROPERTY()
	float AccumulatedMaximum;
};

/**
 * Accumulates the min and max values over time
 */
USTRUCT(meta = (DisplayName="Accumulate Range (Vector)", PrototypeName="AccumulateRange", Keywords="Range"))
struct FRigUnit_AccumulateVectorRange : public FRigUnit_SimBase
{
	GENERATED_BODY()

		FRigUnit_AccumulateVectorRange()
	{
		Value = Minimum = Maximum = AccumulatedMinimum = AccumulatedMaximum = FVector::ZeroVector;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FVector Value;

	UPROPERTY(meta= (Output))
	FVector Minimum;

	UPROPERTY(meta= (Output))
	FVector Maximum;

	UPROPERTY()
	FVector AccumulatedMinimum;

	UPROPERTY()
	FVector AccumulatedMaximum;
};
