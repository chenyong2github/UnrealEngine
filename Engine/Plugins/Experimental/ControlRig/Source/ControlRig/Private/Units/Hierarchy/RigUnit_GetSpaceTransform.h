// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "RigUnit_GetSpaceTransform.generated.h"

/**
 * GetSpaceTransform is used to retrieve a single transform from a hierarchy.
 */
USTRUCT(meta=(DisplayName="Get Space Transform", Category="Spaces", DocumentationPolicy = "Strict", Keywords="GetSpaceTransform"))
struct FRigUnit_GetSpaceTransform : public FRigUnit
{
	GENERATED_BODY()

	FRigUnit_GetSpaceTransform()
		: SpaceType(EBoneGetterSetterMode::LocalSpace)
		, CachedSpaceIndex(INDEX_NONE)
	{}

	virtual FString GetUnitLabel() const override;
	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the Space to retrieve the transform for.
	 */
	UPROPERTY(meta = (Input, SpaceName, Constant))
	FName Space;

	/**
	 * Defines if the Space's transform should be retrieved
	 * in local or global space.
	 */ 
	UPROPERTY(meta = (Input))
	EBoneGetterSetterMode SpaceType;

	// The current transform of the given bone - or identity in case it wasn't found.
	UPROPERTY(meta=(Output))
	FTransform Transform;

	// Used to cache the internally used bone index
	UPROPERTY()
	int32 CachedSpaceIndex;
};
