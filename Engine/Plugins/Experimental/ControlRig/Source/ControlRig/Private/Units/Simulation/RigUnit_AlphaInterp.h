// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Simulation/RigUnit_SimBase.h"
#include "Animation/InputScaleBias.h"
#include "RigUnit_AlphaInterp.generated.h"

/**
 * Adds a value over time over and over again
 */
USTRUCT(meta=(DisplayName="Alpha Interpolate", Keywords="Alpha,Lerp,LinearInterpolate"))
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

	UPROPERTY(meta=(Input))
	float Value;

	UPROPERTY(meta=(Input, Constant))
	bool bMapRange;

	UPROPERTY(meta=(Input, Constant))
	bool bClampResult;

	UPROPERTY(meta=(Input, Constant))
	bool bInterpResult;

	UPROPERTY(meta=(Input, Constant))
	FInputRange InRange;

	UPROPERTY(meta=(Input, Constant))
	FInputRange OutRange;

	UPROPERTY(meta=(Input))
	float Scale;

	UPROPERTY(meta=(Input))
	float Bias;

	UPROPERTY(meta=(Input, Constant))
	float ClampMin;

	UPROPERTY(meta=(Input, Constant))
	float ClampMax;

	UPROPERTY(meta=(Input))
	float InterpSpeedIncreasing;

	UPROPERTY(meta=(Input))
	float InterpSpeedDecreasing;

	UPROPERTY(meta=(Output))
	float Result;

	UPROPERTY()
	FInputScaleBiasClamp ScaleBiasClamp;
};
