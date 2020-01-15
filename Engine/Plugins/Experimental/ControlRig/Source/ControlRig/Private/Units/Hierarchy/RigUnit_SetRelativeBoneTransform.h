// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "RigUnit_SetRelativeBoneTransform.generated.h"


/**
 * SetRelativeBoneTransform is used to perform a change in the hierarchy by setting a single bone's transform.
 */
USTRUCT(meta=(DisplayName="Set Relative Transform", Category="Hierarchy", DocumentationPolicy="Strict", Keywords = "SetRelativeBoneTransform"))
struct FRigUnit_SetRelativeBoneTransform : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_SetRelativeBoneTransform()
		: Weight(1.f)
		, bPropagateToChildren(false)
		, CachedBoneIndex(INDEX_NONE)
		, CachedSpaceIndex(INDEX_NONE)
	{}

	virtual FString GetUnitLabel() const override;
	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the Bone to set the transform for.
	 */
	UPROPERTY(meta = (Input, BoneName, Constant))
	FName Bone;

	/**
	 * The name of the Bone to set the transform relative within.
	 */
	UPROPERTY(meta = (Input, BoneName, Constant))
	FName Space;

	/**
	 * The transform value to set for the given Bone.
	 */
	UPROPERTY(meta = (Input))
	FTransform Transform;

	/**
	 * The weight of the change - how much the change should be applied
	 */
	UPROPERTY(meta = (Input, UIMin = "0.0", UIMax = "1.0"))
	float Weight;

	/**
	 * If set to true all of the global transforms of the children 
	 * of this bone will be recalculated based on their local transforms.
	 * Note: This is computationally more expensive than turning it off.
	 */
	UPROPERTY(meta = (Input))
	bool bPropagateToChildren;

	// Used to cache the internally used bone index
	UPROPERTY()
	int32 CachedBoneIndex;

	// Used to cache the internally used space index
	UPROPERTY()
	int32 CachedSpaceIndex;
};
