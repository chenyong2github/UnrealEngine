// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "RigUnit_SetSpaceTransform.generated.h"


/**
 * SetSpaceTransform is used to perform a change in the hierarchy by setting a single bone's transform.
 */
USTRUCT(meta=(DisplayName="Set Space", Category="Hierarchy", DocumentationPolicy="Strict", Keywords = "SetSpaceTransform"))
struct FRigUnit_SetSpaceTransform : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_SetSpaceTransform()
		: SpaceType(EBoneGetterSetterMode::LocalSpace)
		, CachedSpaceIndex(INDEX_NONE)
	{}

	virtual FString GetUnitLabel() const override;

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the Space to set the transform for.
	 */
	UPROPERTY(meta = (Input, SpaceName, Constant))
	FName Space;

	/**
	 * The transform value to set for the given Space.
	 */
	UPROPERTY(meta = (Input, Output))
	FTransform Transform;

	/**
	 * Defines if the bone's transform should be set
	 * in local or global space.
	 */
	UPROPERTY(meta = (Input))
	EBoneGetterSetterMode SpaceType;

	// Used to cache the internally used bone index
	UPROPERTY()
	int32 CachedSpaceIndex;
};
