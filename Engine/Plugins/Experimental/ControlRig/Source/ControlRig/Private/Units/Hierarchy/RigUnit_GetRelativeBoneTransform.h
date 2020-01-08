// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "RigUnit_GetRelativeBoneTransform.generated.h"

/**
 * GetBoneTransform is used to retrieve a single transform from a hierarchy.
 */
USTRUCT(meta=(DisplayName="Get Relative Transform", Category="Hierarchy", DocumentationPolicy = "Strict", Keywords = "GetRelativeBoneTransform"))
struct FRigUnit_GetRelativeBoneTransform : public FRigUnit
{
	GENERATED_BODY()

	FRigUnit_GetRelativeBoneTransform()
		: CachedBoneIndex(INDEX_NONE)
		, CachedSpaceIndex(INDEX_NONE)
	{}

	virtual FString GetUnitLabel() const override;
	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the Bone to retrieve the transform for.
	 */
	UPROPERTY(meta = (Input, BoneName, Constant))
	FName Bone;

	/**
	 * The name of the Bone to retrieve the transform relative within.
	 */
	UPROPERTY(meta = (Input, BoneName, Constant))
	FName Space;

	// The current transform of the given bone - or identity in case it wasn't found.
	UPROPERTY(meta=(Output))
	FTransform Transform;

	// Used to cache the internally used bone index
	UPROPERTY()
	int32 CachedBoneIndex;

	// Used to cache the internally used space index
	UPROPERTY()
	int32 CachedSpaceIndex;
};
