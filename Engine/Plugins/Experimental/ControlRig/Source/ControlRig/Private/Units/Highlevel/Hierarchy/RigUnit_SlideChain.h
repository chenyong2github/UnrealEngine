// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Highlevel/RigUnit_HighlevelBase.h"
#include "Math/ControlRigMathLibrary.h"
#include "RigUnit_SlideChain.generated.h"

USTRUCT()
struct FRigUnit_SlideChain_WorkData
{
	GENERATED_BODY()

	FRigUnit_SlideChain_WorkData()
	{
		ChainLength = 0.f;
	}

	UPROPERTY()
	float ChainLength;

	UPROPERTY()
	TArray<float> BoneSegments;

	UPROPERTY()
	TArray<int32> BoneIndices;

	UPROPERTY()
	TArray<FTransform> Transforms;

	UPROPERTY()
	TArray<FTransform> BlendedTransforms;
};

/**
 * Slides an existing chain along itself with control over extrapolation.
 */
USTRUCT(meta=(DisplayName="Slide Chain", Category="Hierarchy", Keywords="Fit,Refit"))
struct FRigUnit_SlideChain: public FRigUnit_HighlevelBaseMutable
{
	GENERATED_BODY()

	FRigUnit_SlideChain()
	{
		StartBone = EndBone = NAME_None;
		SlideAmount = 0.f;
		bPropagateToChildren = false;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/** 
	 * The name of the first bone to slide
	 */
	UPROPERTY(meta = (Input, Constant, BoneName))
	FName StartBone;

	/** 
	 * The name of the last bone to slide
	 */
	UPROPERTY(meta = (Input, Constant, BoneName))
	FName EndBone;

	/** 
	 * The amount of sliding. This unit is multiple of the chain length.
	 */
	UPROPERTY(meta = (Input))
	float SlideAmount;

	/**
	 * If set to true all of the global transforms of the children
	 * of this bone will be recalculated based on their local transforms.
	 * Note: This is computationally more expensive than turning it off.
	 */
	UPROPERTY(meta = (Input))
	bool bPropagateToChildren;

	UPROPERTY(transient)
	FRigUnit_SlideChain_WorkData WorkData;
};
