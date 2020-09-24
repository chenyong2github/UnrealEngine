// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "RigUnit_SetRelativeTransform.generated.h"

/**
 * SetRelativeTransform is used to set a single transform from a hierarchy in the space of another item
 */
USTRUCT(meta=(DisplayName="Set Relative Transform", Category="Hierarchy", DocumentationPolicy = "Strict", Keywords = "Offset,Local", Varying))
struct FRigUnit_SetRelativeTransformForItem : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_SetRelativeTransformForItem()
		: Child(NAME_None, ERigElementType::Bone)
		, Parent(NAME_None, ERigElementType::Bone)
		, bParentInitial(false)
		, RelativeTransform(FTransform::Identity)
		, Weight(1.f)
		, bPropagateToChildren(false)
		, CachedChild()
		, CachedParent()
	{}

	virtual FRigElementKey DetermineSpaceForPin(const FString& InPinPath, void* InUserContext) const override
	{
		return Parent;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The child item to set the transform for
	 */
	UPROPERTY(meta = (Input, ExpandByDefault))
	FRigElementKey Child;

	/**
	 * The parent item to use.
	 * The child transform will be set in the space of the parent.
	 */
	UPROPERTY(meta = (Input, ExpandByDefault))
	FRigElementKey Parent;

	/**
	 * Defines if the parent's transform should be determined as current (false) or initial (true).
	 * Initial transforms for bones and other elements in the hierarchy represent the reference pose's value.
	 */ 
	UPROPERTY(meta = (Input))
	bool bParentInitial;

	// The transform of the child item relative to the provided parent
	UPROPERTY(meta=(Input))
	FTransform RelativeTransform;

	// Defines how much the change will be applied
	UPROPERTY(meta = (Input, UIMin = "0.0", UIMax = "1.0"))
	float Weight;

	// If set to true children of affected items in the hierarchy
	// will follow the transform change - otherwise only the parent will move.
	UPROPERTY(meta=(Input))
	bool bPropagateToChildren;

	// Used to cache the child internally
	UPROPERTY()
	FCachedRigElement CachedChild;

	// Used to cache the parent internally
	UPROPERTY()
	FCachedRigElement CachedParent;
};
