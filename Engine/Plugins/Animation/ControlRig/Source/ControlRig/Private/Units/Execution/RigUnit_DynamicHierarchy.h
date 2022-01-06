// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "ControlRigDefines.h" 
#include "RigUnit_DynamicHierarchy.generated.h"

USTRUCT(meta = (Abstract, NodeColor="0.262745, 0.8, 0, 0.229412", Category = "DynamicHierarchy"))
struct CONTROLRIG_API FRigUnit_DynamicHierarchyBase : public FRigUnit
{
	GENERATED_BODY()
};

USTRUCT(meta = (Abstract, NodeColor="0.262745, 0.8, 0, 0.229412", Category = "DynamicHierarchy"))
struct CONTROLRIG_API FRigUnit_DynamicHierarchyBaseMutable : public FRigUnitMutable
{
	GENERATED_BODY()
};

/**
* Adds a new parent to an element. The weight for the new parent will be 0.0.
* You can use the SetParentWeights node to change the parent weights later.
*/
USTRUCT(meta=(DisplayName="Add Parent", Keywords="Children,Parent,Constraint,Space", Varying))
struct CONTROLRIG_API FRigUnit_AddParent : public FRigUnit_DynamicHierarchyBaseMutable
{
	GENERATED_BODY()

	FRigUnit_AddParent()
	{
		Child = Parent = FRigElementKey(NAME_None, ERigElementType::Control);
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/*
	 * The child to be parented under the new parent
	 */
	UPROPERTY(meta = (Input, ExpandByDefault))
	FRigElementKey Child;

	/*
	 * The new parent to be added to the child
	 */
	UPROPERTY(meta = (Input, ExpandByDefault))
	FRigElementKey Parent;
};

UENUM()
enum class ERigSwitchParentMode : uint8
{
	/** Switches the element to be parented to the world */
	World,

	/** Switches back to the original / default parent */
	DefaultParent,

	/** Switches the child to the provided parent item */
	ParentItem
};

/**
* Switches an element to a new parent.
*/
USTRUCT(meta=(DisplayName="Switch Parent", Keywords="Children,Parent,Constraint,Space,Switch", Varying))
struct CONTROLRIG_API FRigUnit_SwitchParent : public FRigUnit_DynamicHierarchyBaseMutable
{
	GENERATED_BODY()

	FRigUnit_SwitchParent()
	{
		Mode = ERigSwitchParentMode::ParentItem;
		Child = Parent = FRigElementKey(NAME_None, ERigElementType::Control);
		bMaintainGlobal = true;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/* Depending on this the child will switch to the world,
	 * back to its default or to the item provided by the Parent pin
	 */
	UPROPERTY(meta = (Input))
	ERigSwitchParentMode Mode;

	/* The child to switch to a new parent */
	UPROPERTY(meta = (Input, ExpandByDefault))
	FRigElementKey Child;

	/* The optional parent to switch to. This is only used if the mode is set to 'Parent Item' */
	UPROPERTY(meta = (Input, ExpandByDefault, EditCondition="Mode==ParentItem"))
	FRigElementKey Parent;

	/* If set to true the item will maintain its global transform,
	 * otherwise it will maintain local
	 */
	UPROPERTY(meta = (Input))
	bool bMaintainGlobal;
};

/**
* Returns the item's parents' weights
*/
USTRUCT(meta=(DisplayName="Get Parent Weights", Keywords="Chain,Parents,Hierarchy", Varying, Deprecated = "5.0"))
struct CONTROLRIG_API FRigUnit_HierarchyGetParentWeights : public FRigUnit_DynamicHierarchyBase
{
	GENERATED_BODY()

	FRigUnit_HierarchyGetParentWeights()
	{
		Child = FRigElementKey(NAME_None, ERigElementType::Control);
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/*
	 * The child to retrieve the weights for
	 */
	UPROPERTY(meta = (Input, ExpandByDefault))
	FRigElementKey Child;

	/*
	 * The weight of each parent
	 */
	UPROPERTY(meta = (Output))
	TArray<FRigElementWeight> Weights;

	/*
	 * The key for each parent
	 */
	UPROPERTY(meta = (Output))
	FRigElementKeyCollection Parents;
};

/**
* Returns the item's parents' weights
*/
USTRUCT(meta=(DisplayName="Get Parent Weights", Keywords="Chain,Parents,Hierarchy", Varying))
struct CONTROLRIG_API FRigUnit_HierarchyGetParentWeightsArray : public FRigUnit_DynamicHierarchyBase
{
	GENERATED_BODY()

	FRigUnit_HierarchyGetParentWeightsArray()
	{
		Child = FRigElementKey(NAME_None, ERigElementType::Control);
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/*
	 * The child to retrieve the weights for
	 */
	UPROPERTY(meta = (Input, ExpandByDefault))
	FRigElementKey Child;

	/*
	 * The weight of each parent
	 */
	UPROPERTY(meta = (Output))
	TArray<FRigElementWeight> Weights;

	/*
	 * The key for each parent
	 */
	UPROPERTY(meta = (Output))
	TArray<FRigElementKey> Parents;
};

/**
* Sets the item's parents' weights
*/
USTRUCT(meta=(DisplayName="Set Parent Weights", Keywords="Chain,Parents,Hierarchy", Varying))
struct CONTROLRIG_API FRigUnit_HierarchySetParentWeights : public FRigUnit_DynamicHierarchyBaseMutable
{
	GENERATED_BODY()

	FRigUnit_HierarchySetParentWeights()
	{
		Child = FRigElementKey(NAME_None, ERigElementType::Control);
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/*
	 * The child to set the parents' weights for
	 */
	UPROPERTY(meta = (Input, ExpandByDefault))
	FRigElementKey Child;

	/*
	 * The weights to set for the child's parents.
	 * The number of weights needs to match the current number of parents.
	 */
	UPROPERTY(meta = (Input))
	TArray<FRigElementWeight> Weights;
};