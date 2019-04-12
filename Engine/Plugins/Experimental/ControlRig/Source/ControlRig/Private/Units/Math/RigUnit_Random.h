// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Math/RigUnit_MathBase.h"
#include "RigUnit_Random.generated.h"

USTRUCT(meta=(DisplayName="Random (Float)", Category="Math|Random", PrototypeName="Random"))
struct FRigUnit_RandomFloat : public FRigUnit_MathBase
{
	GENERATED_BODY()

	FRigUnit_RandomFloat()
	{
		Seed = LastSeed = 217;
		Minimum = Result = 0.f;
		Maximum = 1.f;
	}

	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input, Constant))
	int32 Seed;

	UPROPERTY(meta = (Input))
	float Minimum;

	UPROPERTY(meta = (Input))
	float Maximum;

	UPROPERTY(meta = (Output))
	float Result;

	UPROPERTY(meta = (Output))
	int32 LastSeed;
};

USTRUCT(meta = (DisplayName = "Random (Vector)", Category = "Math|Random", PrototypeName = "Random"))
struct FRigUnit_RandomVector: public FRigUnit_MathBase
{
	GENERATED_BODY()

	FRigUnit_RandomVector()
	{
		Seed = LastSeed = 217;
		Minimum = 0.f;
		Maximum = 1.f;
		Result = FVector::ZeroVector;
	}

	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	int32 Seed;

	UPROPERTY(meta = (Input))
	float Minimum;

	UPROPERTY(meta = (Input))
	float Maximum;

	UPROPERTY(meta = (Output))
	FVector Result;

	UPROPERTY(meta = (Output))
	int32 LastSeed;
};