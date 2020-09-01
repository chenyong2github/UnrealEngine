// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "RigUnit_GetTransform.generated.h"

/**
 * GetTransform is used to retrieve a single transform from a hierarchy.
 */
USTRUCT(meta=(DisplayName="Get Transform", Category="Hierarchy", DocumentationPolicy = "Strict", Keywords="GetBoneTransform,GetControlTransform,GetInitialTransform,GetSpaceTransform,GetTransform", Varying))
struct FRigUnit_GetTransform : public FRigUnit
{
	GENERATED_BODY()

	FRigUnit_GetTransform()
		: Item(NAME_None, ERigElementType::Bone)
		, Space(EBoneGetterSetterMode::GlobalSpace)
		, bInitial(false)
		, Transform(FTransform::Identity)
		, CachedIndex()
	{}

	virtual FString GetUnitLabel() const override;

	virtual FRigElementKey DetermineSpaceForPin(const FString& InPinPath, void* InUserContext) const override
	{
		if(Space == EBoneGetterSetterMode::LocalSpace)
		{
			if (const FRigHierarchyContainer* Container = (const FRigHierarchyContainer*)InUserContext)
			{
				return Container->GetParentKey(Item);
			}
		}
		return FRigElementKey();
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The item to retrieve the transform for
	 */
	UPROPERTY(meta = (Input, ExpandByDefault))
	FRigElementKey Item;

	/**
	 * Defines if the transform should be retrieved in local or global space
	 */ 
	UPROPERTY(meta = (Input))
	EBoneGetterSetterMode Space;

	/**
	 * Defines if the transform should be retrieved as current (false) or initial (true).
	 * Initial transforms for bones and other elements in the hierarchy represent the reference pose's value.
	 */ 
	UPROPERTY(meta = (Input))
	bool bInitial;

	// The current transform of the given item - or identity in case it wasn't found.
	UPROPERTY(meta=(Output))
	FTransform Transform;

	// Used to cache the internally
	UPROPERTY()
	FCachedRigElement CachedIndex;
};
