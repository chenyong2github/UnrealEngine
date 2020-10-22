// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigUnit_SimBase.h"
#include "Animation/InputScaleBias.h"
#include "RigUnit_AlphaInterp.generated.h"

/**
 * Adds a float value over time over and over again
 */
USTRUCT(meta=(DisplayName="Interpolate", Keywords="Alpha,Lerp,LinearInterpolate", PrototypeName = "AlphaInterp"))
struct FRigUnit_AlphaInterp : public FRigUnit_SimBase
{
	GENERATED_BODY()
	
	FRigUnit_AlphaInterp()
	{
		Value = Result = 0.f;
		ScaleBiasClamp = FInputScaleBiasClamp();
		bMapRange = ScaleBiasClamp.bMapRange;
		bClampResult = ScaleBiasClamp.bClampResult;
		bInterpResult = ScaleBiasClamp.bInterpResult;
		InRange = ScaleBiasClamp.InRange;
		OutRange = ScaleBiasClamp.OutRange;
		Scale = ScaleBiasClamp.Scale;
		Bias = ScaleBiasClamp.Bias;
		ClampMin = ScaleBiasClamp.ClampMin;
		ClampMax = ScaleBiasClamp.ClampMax;
		InterpSpeedIncreasing = ScaleBiasClamp.InterpSpeedIncreasing;
		InterpSpeedDecreasing = ScaleBiasClamp.InterpSpeedDecreasing;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	virtual FString ProcessPinLabelForInjection(const FString& InLabel) const override;

	UPROPERTY(meta=(Input))
	float Value;

	UPROPERTY(meta = (Input))
	float Scale;

	UPROPERTY(meta = (Input))
	float Bias;

	UPROPERTY(meta=(Input, Constant))
	bool bMapRange;

	UPROPERTY(meta=(Input, EditCondition = "bMapRange"))
	FInputRange InRange;

	UPROPERTY(meta=(Input, EditCondition = "bMapRange"))
	FInputRange OutRange;

	UPROPERTY(meta = (Input, Constant))
	bool bClampResult;

	UPROPERTY(meta=(Input, EditCondition = "bClampResult"))
	float ClampMin;

	UPROPERTY(meta=(Input, EditCondition = "bClampResult"))
	float ClampMax;

	UPROPERTY(meta = (Input, Constant))
	bool bInterpResult;

	UPROPERTY(meta=(Input, EditCondition = "bInterpResult"))
	float InterpSpeedIncreasing;

	UPROPERTY(meta=(Input, EditCondition = "bInterpResult"))
	float InterpSpeedDecreasing;

	UPROPERTY(meta=(Output))
	float Result;

	UPROPERTY()
	FInputScaleBiasClamp ScaleBiasClamp;
};

/**
 * Adds a vector value over time over and over again
 */
USTRUCT(meta=(DisplayName="Interpolate", Keywords="Alpha,Lerp,LinearInterpolate", PrototypeName = "AlphaInterp", MenuDescSuffix = "(Vector)"))
struct FRigUnit_AlphaInterpVector : public FRigUnit_SimBase
{
	GENERATED_BODY()
	
	FRigUnit_AlphaInterpVector()
	{
		Value = Result = FVector::ZeroVector;
		ScaleBiasClamp = FInputScaleBiasClamp();
		bMapRange = ScaleBiasClamp.bMapRange;
		bClampResult = ScaleBiasClamp.bClampResult;
		bInterpResult = ScaleBiasClamp.bInterpResult;
		InRange = ScaleBiasClamp.InRange;
		OutRange = ScaleBiasClamp.OutRange;
		Scale = ScaleBiasClamp.Scale;
		Bias = ScaleBiasClamp.Bias;
		ClampMin = ScaleBiasClamp.ClampMin;
		ClampMax = ScaleBiasClamp.ClampMax;
		InterpSpeedIncreasing = ScaleBiasClamp.InterpSpeedIncreasing;
		InterpSpeedDecreasing = ScaleBiasClamp.InterpSpeedDecreasing;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	virtual FString ProcessPinLabelForInjection(const FString& InLabel) const override;

	UPROPERTY(meta=(Input))
	FVector Value;

	UPROPERTY(meta = (Input))
	float Scale;

	UPROPERTY(meta = (Input))
	float Bias;

	UPROPERTY(meta = (Input, Constant))
	bool bMapRange;

	UPROPERTY(meta = (Input, EditCondition = "bMapRange"))
	FInputRange InRange;

	UPROPERTY(meta = (Input, EditCondition = "bMapRange"))
	FInputRange OutRange;

	UPROPERTY(meta = (Input, Constant))
	bool bClampResult;

	UPROPERTY(meta = (Input, EditCondition = "bClampResult"))
	float ClampMin;

	UPROPERTY(meta = (Input, EditCondition = "bClampResult"))
	float ClampMax;

	UPROPERTY(meta = (Input, Constant))
	bool bInterpResult;

	UPROPERTY(meta = (Input, EditCondition = "bInterpResult"))
	float InterpSpeedIncreasing;

	UPROPERTY(meta = (Input, EditCondition = "bInterpResult"))
	float InterpSpeedDecreasing;

	UPROPERTY(meta=(Output))
	FVector Result;

	UPROPERTY()
	FInputScaleBiasClamp ScaleBiasClamp;
};
