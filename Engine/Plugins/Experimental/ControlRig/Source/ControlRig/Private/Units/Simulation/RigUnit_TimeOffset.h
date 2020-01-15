// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Simulation/RigUnit_SimBase.h"
#include "RigUnit_TimeOffset.generated.h"

/**
 * Records a value over time and can access the value from the past
 */
USTRUCT(meta=(DisplayName="Value Over Time (Float)", PrototypeName="TimeOffset", Keywords="Buffer,Delta,History,Previous,TimeOffset"))
struct FRigUnit_TimeOffsetFloat : public FRigUnit_SimBase
{
	GENERATED_BODY()
	
	FRigUnit_TimeOffsetFloat()
	{
		SecondsAgo = 0.5f;
		Value = Result = 0.f;
		BufferSize = 16;
		TimeRange = 1.f;
		LastInsertIndex = 0;
		UpperBound = 0;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
	
	/** The value to record */
	UPROPERTY(meta = (Input))
	float Value;

	/** Seconds of time in the past you want to query the value for */
	UPROPERTY(meta = (Input))
	float SecondsAgo;

	/** The sampling precision of the buffer. The higher the more precise - the more memory usage. */
	UPROPERTY(meta=(Input, Constant))
	int32 BufferSize;

	/** The maximum time required for offsetting in seconds. */
	UPROPERTY(meta=(Input, Constant))
	float TimeRange;

	UPROPERTY(meta=(Output))
	float Result;

	UPROPERTY(meta = (MaxArraySize = "FMath::Clamp<int32>(BufferSize, 2, 512)"))
	TArray<float> Buffer;

	UPROPERTY(meta = (MaxArraySize = "FMath::Clamp<int32>(BufferSize, 2, 512)"))
	TArray<float> DeltaTimes;

	UPROPERTY()
	int32 LastInsertIndex;

	UPROPERTY()
	int32 UpperBound;
};

/**
 * Records a value over time and can access the value from the past
 */
USTRUCT(meta=(DisplayName="Value Over Time (Vector)", PrototypeName="TimeOffset", Keywords="Buffer,Delta,History,Previous,TimeOffset"))
struct FRigUnit_TimeOffsetVector : public FRigUnit_SimBase
{
	GENERATED_BODY()
	
	FRigUnit_TimeOffsetVector()
	{
		SecondsAgo = 0.5f;
		Value = Result = FVector::ZeroVector;
		BufferSize = 16;
		TimeRange = 1.f;
		LastInsertIndex = 0;
		UpperBound = 0;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
	
	/** The value to record */
	UPROPERTY(meta = (Input))
	FVector Value;

	/** Seconds of time in the past you want to query the value for */
	UPROPERTY(meta = (Input))
	float SecondsAgo;

	/** The sampling precision of the buffer. The higher the more precise - the more memory usage. */
	UPROPERTY(meta=(Input, Constant))
	int32 BufferSize;

	/** The maximum time required for offsetting in seconds. */
	UPROPERTY(meta=(Input, Constant))
	float TimeRange;

	UPROPERTY(meta=(Output))
	FVector Result;

	UPROPERTY(meta = (MaxArraySize = "FMath::Clamp<int32>(BufferSize, 2, 512)"))
	TArray<FVector> Buffer;

	UPROPERTY(meta = (MaxArraySize = "FMath::Clamp<int32>(BufferSize, 2, 512)"))
	TArray<float> DeltaTimes;

	UPROPERTY()
	int32 LastInsertIndex;

	UPROPERTY()
	int32 UpperBound;
};

/**
 * Records a value over time and can access the value from the past
 */
USTRUCT(meta=(DisplayName="Value Over Time (Transform)", PrototypeName="TimeOffset", Keywords="Buffer,Delta,History,Previous,TimeOffset"))
struct FRigUnit_TimeOffsetTransform : public FRigUnit_SimBase
{
	GENERATED_BODY()
	
	FRigUnit_TimeOffsetTransform()
	{
		SecondsAgo = 0.5f;
		Value = Result = FTransform::Identity;
		BufferSize = 16;
		TimeRange = 1.f;
		LastInsertIndex = 0;
		UpperBound = 0;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;
	
	/** The value to record */
	UPROPERTY(meta = (Input))
	FTransform Value;

	/** Seconds of time in the past you want to query the value for */
	UPROPERTY(meta = (Input))
	float SecondsAgo;

	/** The sampling precision of the buffer. The higher the more precise - the more memory usage. */
	UPROPERTY(meta=(Input, Constant))
	int32 BufferSize;

	/** The maximum time required for offsetting in seconds. */
	UPROPERTY(meta=(Input, Constant))
	float TimeRange;

	UPROPERTY(meta=(Output))
	FTransform Result;

	UPROPERTY(meta = (MaxArraySize = "FMath::Clamp<int32>(BufferSize, 2, 512)"))
	TArray<FTransform> Buffer;

	UPROPERTY(meta = (MaxArraySize = "FMath::Clamp<int32>(BufferSize, 2, 512)"))
	TArray<float> DeltaTimes;

	UPROPERTY()
	int32 LastInsertIndex;

	UPROPERTY()
	int32 UpperBound;
};
