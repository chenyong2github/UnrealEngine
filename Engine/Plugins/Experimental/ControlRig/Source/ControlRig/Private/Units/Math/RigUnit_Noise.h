// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Math/RigUnit_MathBase.h"
#include "RigUnit_Noise.generated.h"

/**
 * Generates a float through a noise fluctuation process between a min and a max through speed
 */
USTRUCT(meta=(DisplayName="Noise (Float)", Category="Math|Noise", PrototypeName="Noise"))
struct FRigUnit_NoiseFloat : public FRigUnit_MathBase
{
	GENERATED_BODY()

	FRigUnit_NoiseFloat()
	{
		Value = Minimum = Result = Time = 0.f;
		Speed = 0.1f;
		Frequency = Maximum = 1.f;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	float Value;

	UPROPERTY(meta = (Input))
	float Speed;

	UPROPERTY(meta = (Input))
	float Frequency;

	UPROPERTY(meta = (Input))
	float Minimum;

	UPROPERTY(meta = (Input))
	float Maximum;

	UPROPERTY(meta = (Output))
	float Result;

	UPROPERTY()
	float Time;
};

/**
 * Generates a vector through a noise fluctuation process between a min and a max through speed
 */
USTRUCT(meta = (DisplayName = "Noise (Vector)", Category = "Math|Noise", PrototypeName = "Noise"))
struct FRigUnit_NoiseVector : public FRigUnit_MathBase
{
	GENERATED_BODY()

	FRigUnit_NoiseVector()
	{
		Position = Result = Time = FVector::ZeroVector;
		Frequency = FVector::OneVector;
		Speed = FVector(0.1f, 0.1f, 0.1f);
		Minimum = 0.f;
		Maximum = 1.f;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FVector Position;

	UPROPERTY(meta = (Input))
	FVector Speed;

	UPROPERTY(meta = (Input))
	FVector Frequency;

	UPROPERTY(meta = (Input))
	float Minimum;

	UPROPERTY(meta = (Input))
	float Maximum;

	UPROPERTY(meta = (Output))
	FVector Result;

	UPROPERTY()
	FVector Time;
};