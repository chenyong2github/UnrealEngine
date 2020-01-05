// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Highlevel/RigUnit_HighlevelBase.h"
#include "Math/ControlRigMathLibrary.h"
#include "RigUnit_BoneHarmonics.generated.h"

USTRUCT()
struct FRigUnit_BoneHarmonics_BoneTarget
{
	GENERATED_BODY()

	FRigUnit_BoneHarmonics_BoneTarget()
	{
		Bone = NAME_None;
		Ratio = 0.f;
	}

	/**
	 * The name of the bone to drive
	 */
	UPROPERTY(meta = (Input, Constant, BoneName))
	FName Bone;

	/**
	 * The ratio of where the bone sits within the harmonics system.
	 * Valid values reach from 0.0 to 1.0
	 */
	UPROPERTY(meta = (Input, Constant))
	float Ratio;
};

USTRUCT()
struct FRigUnit_BoneHarmonics_WorkData
{
	GENERATED_BODY()

	FRigUnit_BoneHarmonics_WorkData()
	{
		WaveTime = FVector::ZeroVector;
	}

	UPROPERTY()
	TArray<int32> BoneIndices;

	UPROPERTY()
	FVector WaveTime;
};

/**
 * Performs point based simulation
 */
USTRUCT(meta=(DisplayName="Harmonics", Keywords="Sin,Wave"))
struct FRigUnit_BoneHarmonics : public FRigUnit_HighlevelBaseMutable
{
	GENERATED_BODY()
	
	FRigUnit_BoneHarmonics()
	{
		WaveSpeed = FVector::OneVector;
		WaveAmplitude = FVector(0.0f, 70.f, 0.f);
		WaveFrequency = FVector(1.f, 0.6f, 0.8f);
		WaveOffset = FVector(0.f, 1.f, 2.f);
		WaveNoise = FVector::ZeroVector;
		WaveEase = EControlRigAnimEasingType::Linear;
		WaveMinimum = 0.5f;
		WaveMaximum = 1.f;
		RotationOrder = EControlRigRotationOrder::YZX;
		bPropagateToChildren = false;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/** The bones to drive. */
	UPROPERTY(meta = (Input, Constant))
	TArray<FRigUnit_BoneHarmonics_BoneTarget> Bones;

	UPROPERTY(meta = (Input))
	FVector WaveSpeed;

	UPROPERTY(meta = (Input))
	FVector WaveFrequency;

	/** The amplitude in degrees per axis */
	UPROPERTY(meta = (Input))
	FVector WaveAmplitude;

	UPROPERTY(meta = (Input))
	FVector WaveOffset;

	UPROPERTY(meta = (Input))
	FVector WaveNoise;

	UPROPERTY(meta = (Input))
	EControlRigAnimEasingType WaveEase;

	UPROPERTY(meta = (Input))
	float WaveMinimum;

	UPROPERTY(meta = (Input))
	float WaveMaximum;

	UPROPERTY(meta = (Input))
	EControlRigRotationOrder RotationOrder;

	/**
	 * If set to true all of the global transforms of the children
	 * of this bone will be recalculated based on their local transforms.
	 * Note: This is computationally more expensive than turning it off.
	 */
	UPROPERTY(meta = (Input))
	bool bPropagateToChildren;

	UPROPERTY(transient)
	FRigUnit_BoneHarmonics_WorkData WorkData;
};

