// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "ControlRigDefines.h" 
#include "RigUnit_Item.generated.h"

USTRUCT(meta = (Abstract, NodeColor = "0.7 0.05 0.5", Category = "Collections"))
struct FRigUnit_ItemBase : public FRigUnit
{
	GENERATED_BODY()
};

USTRUCT(meta = (Abstract, NodeColor = "0.7 0.05 0.5", Category = "Collections"))
struct FRigUnit_ItemBaseMutable : public FRigUnitMutable
{
	GENERATED_BODY()
};

/**
 * Returns true or false if a given item exists
 */
USTRUCT(meta=(DisplayName="Item Exists", Keywords=""))
struct FRigUnit_ItemExists : public FRigUnit_ItemBase
{
	GENERATED_BODY()

	FRigUnit_ItemExists()
	{
		Item = FRigElementKey();
		Exists = false;
		CachedIndex = FCachedRigElement();
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input, ExpandByDefault))
	FRigElementKey Item;

	UPROPERTY(meta = (Output))
	bool Exists;

	// Used to cache the internally used index
	UPROPERTY()
	FCachedRigElement CachedIndex;
};

/**
 * Replaces the text within the name of the item
 */
USTRUCT(meta=(DisplayName="Item Replace", Keywords="Replace,Name"))
struct FRigUnit_ItemReplace : public FRigUnit_ItemBase
{
	GENERATED_BODY()

	FRigUnit_ItemReplace()
	{
		Item = Result = FRigElementKey();
		Old = New = NAME_None;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FRigElementKey Item;

	UPROPERTY(meta = (Input))
	FName Old;

	UPROPERTY(meta = (Input))
	FName New;

	UPROPERTY(meta = (Output))
	FRigElementKey Result;
};