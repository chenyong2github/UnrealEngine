// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "RigUnit_BoneName.generated.h"

/**
 * The Item node is used to share a specific item across the graph
 */
USTRUCT(meta=(DisplayName="Item", Category="Hierarchy", DocumentationPolicy = "Strict", Constant))
struct FRigUnit_Item : public FRigUnit
{
	GENERATED_BODY()

	FRigUnit_Item()
	{}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The item
	 */
	UPROPERTY(meta = (Input, Output, ExpandByDefault))
	FRigElementKey Item;
};

/**
 * BoneName is used to represent a bone name in the graph
 */
USTRUCT(meta=(DisplayName="Bone Name", Category="Hierarchy", DocumentationPolicy = "Strict", Constant, Deprecated = "4.25"))
struct FRigUnit_BoneName : public FRigUnit
{
	GENERATED_BODY()

	FRigUnit_BoneName()
	{}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the Bone
	 */
	UPROPERTY(meta = (Input, Output))
	FName Bone;
};

/**
 * SpaceName is used to represent a Space name in the graph
 */
USTRUCT(meta=(DisplayName="Space Name", Category="Hierarchy", DocumentationPolicy = "Strict", Deprecated = "4.25"))
struct FRigUnit_SpaceName : public FRigUnit
{
	GENERATED_BODY()

	FRigUnit_SpaceName()
	{}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the Space
	 */
	UPROPERTY(meta = (Input, Output))
	FName Space;
};

/**
 * ControlName is used to represent a Control name in the graph
 */
USTRUCT(meta=(DisplayName="Control Name", Category="Hierarchy", DocumentationPolicy = "Strict", Deprecated = "4.25"))
struct FRigUnit_ControlName : public FRigUnit
{
	GENERATED_BODY()

	FRigUnit_ControlName()
	{}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the Control
	 */
	UPROPERTY(meta = (Input, Output))
	FName Control;
};
