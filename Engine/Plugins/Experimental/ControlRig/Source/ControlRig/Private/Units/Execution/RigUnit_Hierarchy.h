// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "ControlRigDefines.h" 
#include "RigUnit_Hierarchy.generated.h"

USTRUCT(meta = (Abstract, NodeColor = "0.7 0.05 0.5", Category = "Hierarchy"))
struct FRigUnit_HierarchyBase : public FRigUnit
{
	GENERATED_BODY()
};

/**
 * Returns the item's parent
 */
USTRUCT(meta=(DisplayName="Get Parent", Keywords="Child,Parent,Root,Up,Top", Varying))
struct FRigUnit_HierarchyGetParent : public FRigUnit_HierarchyBase
{
	GENERATED_BODY()

	FRigUnit_HierarchyGetParent()
	{
		Child = Parent = FRigElementKey(NAME_None, ERigElementType::Bone);
		CachedChild = CachedParent = FCachedRigElement();
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input, ExpandByDefault))
	FRigElementKey Child;

	UPROPERTY(meta = (Output))
	FRigElementKey Parent;

	// Used to cache the internally
	UPROPERTY()
	FCachedRigElement CachedChild;

	// Used to cache the internally
	UPROPERTY()
	FCachedRigElement CachedParent;
};

/**
 * Returns the item's parents
 */
USTRUCT(meta=(DisplayName="Get Parents", Keywords="Chain,Parents,Hierarchy", Varying))
struct FRigUnit_HierarchyGetParents : public FRigUnit_HierarchyBase
{
	GENERATED_BODY()

	FRigUnit_HierarchyGetParents()
	{
		Child = FRigElementKey(NAME_None, ERigElementType::Bone);
		CachedChild = FCachedRigElement();
		Parents = CachedParents = FRigElementKeyCollection();
		bIncludeChild = false;
		bReverse = false;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input, ExpandByDefault))
	FRigElementKey Child;

	UPROPERTY(meta = (Input))
	bool bIncludeChild;

	UPROPERTY(meta = (Input))
	bool bReverse;

	UPROPERTY(meta = (Output))
	FRigElementKeyCollection Parents;

	// Used to cache the internally
	UPROPERTY()
	FCachedRigElement CachedChild;

	// Used to cache the internally
	UPROPERTY()
	FRigElementKeyCollection CachedParents;
};

/**
 * Returns the item's children
 */
USTRUCT(meta=(DisplayName="Get Children", Keywords="Chain,Children,Hierarchy", Deprecated = "4.25.0", Varying))
struct FRigUnit_HierarchyGetChildren : public FRigUnit_HierarchyBase
{
	GENERATED_BODY()

	FRigUnit_HierarchyGetChildren()
	{
		Parent = FRigElementKey(NAME_None, ERigElementType::Bone);
		CachedParent = FCachedRigElement();
		Children = CachedChildren = FRigElementKeyCollection();
		bIncludeParent = false;
		bRecursive = false;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input, ExpandByDefault))
	FRigElementKey Parent;

	UPROPERTY(meta = (Input))
	bool bIncludeParent;

	UPROPERTY(meta = (Input))
	bool bRecursive;

	UPROPERTY(meta = (Output))
	FRigElementKeyCollection Children;

	// Used to cache the internally
	UPROPERTY()
	FCachedRigElement CachedParent;

	// Used to cache the internally
	UPROPERTY()
	FRigElementKeyCollection CachedChildren;
};

/**
 * Returns the item's siblings
 */
USTRUCT(meta=(DisplayName="Get Siblings", Keywords="Chain,Siblings,Hierarchy", Varying))
struct FRigUnit_HierarchyGetSiblings : public FRigUnit_HierarchyBase
{
	GENERATED_BODY()

	FRigUnit_HierarchyGetSiblings()
	{
		Item = FRigElementKey(NAME_None, ERigElementType::Bone);
		CachedItem = FCachedRigElement();
		Siblings = CachedSiblings = FRigElementKeyCollection();
		bIncludeItem = false;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input, ExpandByDefault))
	FRigElementKey Item;

	UPROPERTY(meta = (Input))
	bool bIncludeItem;

	UPROPERTY(meta = (Output))
	FRigElementKeyCollection Siblings;

	// Used to cache the internally
	UPROPERTY()
	FCachedRigElement CachedItem;

	// Used to cache the internally
	UPROPERTY()
	FRigElementKeyCollection CachedSiblings;
};