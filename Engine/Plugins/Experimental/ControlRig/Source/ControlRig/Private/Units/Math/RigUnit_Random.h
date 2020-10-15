// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Math/RigUnit_MathBase.h"
#include "RigUnit_Random.generated.h"

/**
 * Generates a random float between a min and a max
 */
USTRUCT(meta=(DisplayName="Random (Float)", Category="Math|Random", PrototypeName="Random"))
struct FRigUnit_RandomFloat : public FRigUnit_MathBase
{
	GENERATED_BODY()

	FRigUnit_RandomFloat()
	{
		Seed = LastSeed = 217;
		Minimum = Result = LastResult = 0.f;
		Maximum = 1.f;
		Duration = 0.f;
		TimeLeft = 0.f;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input, Constant))
	int32 Seed;

	UPROPERTY(meta = (Input))
	float Minimum;

	UPROPERTY(meta = (Input))
	float Maximum;

	/** The duration at which the number won't change. Use 0 for a different number every time. */
	UPROPERTY(meta = (Input))
	float Duration;

	UPROPERTY(meta = (Output))
	float Result;

	UPROPERTY()
	float LastResult;

	UPROPERTY()
	int32 LastSeed;

	UPROPERTY()
	float TimeLeft;
};

/**
 * Generates a random vector between a min and a max
 */
USTRUCT(meta = (DisplayName = "Random (Vector)", Category = "Math|Random", PrototypeName = "Random"))
struct FRigUnit_RandomVector: public FRigUnit_MathBase
{
	GENERATED_BODY()

	FRigUnit_RandomVector()
	{
		Seed = LastSeed = 217;
		Minimum = 0.f;
		Maximum = 1.f;
		Duration = 0.f;
		TimeLeft = 0.f;
		Result = LastResult = FVector::ZeroVector;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	int32 Seed;

	UPROPERTY(meta = (Input))
	float Minimum;

	UPROPERTY(meta = (Input))
	float Maximum;

	/** The duration at which the number won't change. Use 0 for a different number every time. */
	UPROPERTY(meta = (Input))
	float Duration;

	UPROPERTY(meta = (Output))
	FVector Result;

	UPROPERTY()
	FVector LastResult;

	UPROPERTY()
	int32 LastSeed;

	UPROPERTY()
	float TimeLeft;
};