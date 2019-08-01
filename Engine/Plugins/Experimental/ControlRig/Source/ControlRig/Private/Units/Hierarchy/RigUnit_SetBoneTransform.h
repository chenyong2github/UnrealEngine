// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "RigUnit_SetBoneTransform.generated.h"


/**
 * SetBoneTransform is used to perform a change in the hierarchy by setting a single bone's transform.
 */
USTRUCT(meta=(DisplayName="Set Transform", Category="Hierarchy", DocumentationPolicy="Strict", Keywords = "SetBoneTransform"))
struct FRigUnit_SetBoneTransform : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_SetBoneTransform()
		: Space(EBoneGetterSetterMode::LocalSpace)
		, bPropagateToChildren(false)
		, CachedBoneIndex(INDEX_NONE)
	{}

	virtual FString GetUnitLabel() const override;

	MULTIPLEX_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the Bone to set the transform for.
	 */
	UPROPERTY(meta = (Input, BoneName, Constant))
	FName Bone;

	/**
	 * The transform value to set for the given Bone.
	 */
	UPROPERTY(meta = (Input))
	FTransform Transform;

	/**
	 * Defines if the bone's transform should be set
	 * in local or global space.
	 */
	UPROPERTY(meta = (Input))
	EBoneGetterSetterMode Space;

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
};
