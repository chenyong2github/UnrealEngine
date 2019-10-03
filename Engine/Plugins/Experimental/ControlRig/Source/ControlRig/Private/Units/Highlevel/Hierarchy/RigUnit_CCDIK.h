// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Highlevel/RigUnit_HighlevelBase.h"
#include "CCDIK.h"
#include "RigUnit_CCDIK.generated.h"

USTRUCT()
struct FRigUnit_CCDIK_RotationLimit
{
	GENERATED_BODY()

	FRigUnit_CCDIK_RotationLimit()
	{
		Bone = NAME_None;
		Limit = 30.f;
	}

	/**
	 * The name of the bone to apply the rotation limit to.
	 */
	UPROPERTY(meta = (Input, Constant, BoneName))
	FName Bone;

	/**
	 * The limit of the rotation in degrees.
	 */
	UPROPERTY(meta = (Input, Constant))
	float Limit;
};

USTRUCT()
struct FRigUnit_CCDIK_WorkData
{
	GENERATED_BODY()

	FRigUnit_CCDIK_WorkData()
	{
		EffectorIndex = INDEX_NONE;
	}

	UPROPERTY()
	TArray<FCCDIKChainLink> Chain;

	UPROPERTY()
	TArray<int32> BoneIndices;

	UPROPERTY()
	TArray<int32> RotationLimitIndex;

	UPROPERTY()
	TArray<float> RotationLimitsPerBone;

	UPROPERTY()
	int32 EffectorIndex;
};

/**
 * The CCID solver can solve N-Bone chains using 
 * the Cyclic Coordinate Descent Inverse Kinematics algorithm.
 * For now this node supports single effector chains only.
 */
USTRUCT(meta=(DisplayName="CCDIK", Category="Hierarchy", Keywords="N-Bone,IK"))
struct FRigUnit_CCDIK : public FRigUnit_HighlevelBaseMutable
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	FRigUnit_CCDIK()
	{
		EffectorTransform = FTransform::Identity;
		Precision = 1.f;
		Weight = 1.f;
		MaxIterations = 10;
		bStartFromTail = true;
		bPropagateToChildren = false;
		BaseRotationLimit = 30.f;
	}

	/**
	 * The first bone in the chain to solve
	 */
	UPROPERTY(meta = (Input, Constant, BoneName))
	FName StartBone;

	/**
	 * The last bone in the chain to solve - the effector
	 */
	UPROPERTY(meta = (Input, Constant, BoneName))
	FName EffectorBone;

	/**
	 * The transform of the effector in global space
	 */
	UPROPERTY(meta = (Input))
	FTransform EffectorTransform;

	/**
	 * The precision to use for the fabrik solver
	 */
	UPROPERTY(meta = (Input, Constant))
	float Precision;

	/**
	 * The weight of the solver - how much the IK should be applied.
	 */
	UPROPERTY(meta = (Input))
	float Weight;

	/**
	 * The maximum number of iterations. Values between 4 and 16 are common.
	 */
	UPROPERTY(meta = (Input))
	int32 MaxIterations;

	/**
	 * If set to true the direction of the solvers is flipped.
	 */
	UPROPERTY(meta = (Input, Constant))
	bool bStartFromTail;

	/**
	 * The general rotation limit to be applied to bones
	 */
	UPROPERTY(meta = (Input, Constant))
	float BaseRotationLimit;

	/**
	 * Defines the limits of rotation per bone.
	 */
	UPROPERTY(meta = (Input, Constant))
	TArray<FRigUnit_CCDIK_RotationLimit> RotationLimits;

	/**
	 * If set to true all of the global transforms of the children
	 * of this bone will be recalculated based on their local transforms.
	 * Note: This is computationally more expensive than turning it off.
	 */
	UPROPERTY(meta = (Input))
	bool bPropagateToChildren;

	UPROPERTY(transient)
	FRigUnit_CCDIK_WorkData WorkData;
};
