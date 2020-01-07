// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Highlevel/RigUnit_HighlevelBase.h"
#include "Math/ControlRigMathLibrary.h"
#include "RigUnit_DistributeRotation.generated.h"

USTRUCT()
struct FRigUnit_DistributeRotation_Rotation
{
	GENERATED_BODY()

	FRigUnit_DistributeRotation_Rotation()
	{
		Rotation = FQuat::Identity;
		Ratio = 0.f;
	}

	/**
	 * The rotation to be applied
	 */
	UPROPERTY(meta = (Input))
	FQuat Rotation;

	/**
	 * The ratio of where this rotation sits along the chain
	 */
	UPROPERTY(meta = (Input, Constant))
	float Ratio;
};

USTRUCT()
struct FRigUnit_DistributeRotation_WorkData
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<int32> BoneIndices;

	UPROPERTY()
	TArray<int32> BoneRotationA;

	UPROPERTY()
	TArray<int32> BoneRotationB;

	UPROPERTY()
	TArray<float> BoneRotationT;

	UPROPERTY()
	TArray<FTransform> BoneLocalTransforms;
};

/**
 * Distributes rotations provided along a chain.
 * Each rotation is expressed by a quaternion and a ratio, where the ratio is between 0.0 and 1.0
 * Note: This node adds rotation in local space of each bone!
 */
USTRUCT(meta=(DisplayName="Distribute Rotation", Category="Hierarchy", Keywords="TwistBones"))
struct FRigUnit_DistributeRotation : public FRigUnit_HighlevelBaseMutable
{
	GENERATED_BODY()

	FRigUnit_DistributeRotation()
	{
		StartBone = EndBone = NAME_None;
		RotationEaseType = EControlRigAnimEasingType::Linear;
		bPropagateToChildren = false;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/** 
	 * The name of the first bone to align
	 */
	UPROPERTY(meta = (Input, Constant, BoneName))
	FName StartBone;

	/** 
	 * The name of the last bone to align
	 */
	UPROPERTY(meta = (Input, Constant, BoneName))
	FName EndBone;

	/** 
	 * The list of rotations to be applied
	 */
	UPROPERTY(meta = (Input))
	TArray<FRigUnit_DistributeRotation_Rotation> Rotations;

	/**
	 * The easing to use between to rotations.
	 */
	UPROPERTY(meta = (Input, Constant))
	EControlRigAnimEasingType RotationEaseType;

	/**
	 * If set to true all of the global transforms of the children
	 * of this bone will be recalculated based on their local transforms.
	 * Note: This is computationally more expensive than turning it off.
	 */
	UPROPERTY(meta = (Input))
	bool bPropagateToChildren;

	UPROPERTY(transient)
	FRigUnit_DistributeRotation_WorkData WorkData;
};
