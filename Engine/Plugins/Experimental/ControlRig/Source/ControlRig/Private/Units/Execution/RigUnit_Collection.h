// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "ControlRigDefines.h" 
#include "RigUnit_Collection.generated.h"

USTRUCT(meta = (Abstract, NodeColor = "0.7 0.05 0.5", Category = "Collections"))
struct CONTROLRIG_API FRigUnit_CollectionBase : public FRigUnit
{
	GENERATED_BODY()
};

USTRUCT(meta = (Abstract, NodeColor = "0.7 0.05 0.5", Category = "Collections"))
struct CONTROLRIG_API FRigUnit_CollectionBaseMutable : public FRigUnitMutable
{
	GENERATED_BODY()
};

/**
 * Creates a collection based on a first and last item within a chain.
 * Chains can refer to bone chains or chains within a control hierarchy.
 */
USTRUCT(meta=(DisplayName="Item Chain", Keywords="Bone,Joint,Collection", Varying))
struct CONTROLRIG_API FRigUnit_CollectionChain : public FRigUnit_CollectionBase
{
	GENERATED_BODY()

	FRigUnit_CollectionChain()
	{
		FirstItem = LastItem = FRigElementKey(NAME_None, ERigElementType::Bone);
		Reverse = false;
		CachedHierarchyHash = INDEX_NONE;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input, ExpandByDefault))
	FRigElementKey FirstItem;

	UPROPERTY(meta = (Input, ExpandByDefault))
	FRigElementKey LastItem;

	UPROPERTY(meta = (Input))
	bool Reverse;

	UPROPERTY(meta = (Output))
	FRigElementKeyCollection Collection;

	UPROPERTY()
	FRigElementKeyCollection CachedCollection;

	UPROPERTY()
	int32 CachedHierarchyHash;
};

/**
 * Creates a collection based on a name search.
 * The name search is case sensitive.
 */
USTRUCT(meta = (DisplayName = "Item Name Search", Keywords = "Bone,Joint,Collection,Filter", Varying))
struct CONTROLRIG_API FRigUnit_CollectionNameSearch : public FRigUnit_CollectionBase
{
	GENERATED_BODY()

	FRigUnit_CollectionNameSearch()
	{
		PartialName = NAME_None;
		TypeToSearch = ERigElementType::All;
		CachedHierarchyHash = INDEX_NONE;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FName PartialName;

	UPROPERTY(meta = (Input))
	ERigElementType TypeToSearch;

	UPROPERTY(meta = (Output))
	FRigElementKeyCollection Collection;

	UPROPERTY()
	FRigElementKeyCollection CachedCollection;

	UPROPERTY()
	int32 CachedHierarchyHash;
};

/**
 * Creates a collection based on the direct or recursive children
 * of a provided parent item. Returns an empty collection for an invalid parent item.
 */
USTRUCT(meta = (DisplayName = "Children", Keywords = "Bone,Joint,Collection,Filter,Parent", Varying))
struct CONTROLRIG_API FRigUnit_CollectionChildren : public FRigUnit_CollectionBase
{
	GENERATED_BODY()

	FRigUnit_CollectionChildren()
	{
		Parent = FRigElementKey(NAME_None, ERigElementType::Bone);
		bIncludeParent = false;
		bRecursive = false;
		TypeToSearch = ERigElementType::All;
		CachedHierarchyHash = INDEX_NONE;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input, ExpandByDefault))
	FRigElementKey Parent;

	UPROPERTY(meta = (Input))
	bool bIncludeParent;

	UPROPERTY(meta = (Input))
	bool bRecursive;

	UPROPERTY(meta = (Input))
	ERigElementType TypeToSearch;

	UPROPERTY(meta = (Output))
	FRigElementKeyCollection Collection;

	UPROPERTY()
	FRigElementKeyCollection CachedCollection;

	UPROPERTY()
	int32 CachedHierarchyHash;
};

/**
 * Replaces all names within the collection
 */
USTRUCT(meta = (DisplayName = "Replace Items", Keywords = "Replace,Find", Varying))
struct CONTROLRIG_API FRigUnit_CollectionReplaceItems : public FRigUnit_CollectionBase
{
	GENERATED_BODY()

	FRigUnit_CollectionReplaceItems()
	{
		Old = New = NAME_None;
		RemoveInvalidItems = false;
		CachedHierarchyHash = INDEX_NONE;
		bAllowDuplicates = false;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FRigElementKeyCollection Items;

	UPROPERTY(meta = (Input))
	FName Old;

	UPROPERTY(meta = (Input))
	FName New;

	UPROPERTY(meta = (Input))
	bool RemoveInvalidItems;

	UPROPERTY(meta = (Input))
	bool bAllowDuplicates;

	UPROPERTY(meta = (Output))
	FRigElementKeyCollection Collection;

	UPROPERTY()
	FRigElementKeyCollection CachedCollection;

	UPROPERTY()
	int32 CachedHierarchyHash;
};

/**
 * Returns a collection provided a specific list of items.
 */
USTRUCT(meta = (DisplayName = "Items", Keywords = "Collection,Array", Varying))
struct CONTROLRIG_API FRigUnit_CollectionItems : public FRigUnit_CollectionBase
{
	GENERATED_BODY()

	FRigUnit_CollectionItems()
	{
		Items.Add(FRigElementKey(NAME_None, ERigElementType::Bone));
		bAllowDuplicates = false;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input, ExpandByDefault))
	TArray<FRigElementKey> Items;

	UPROPERTY(meta = (Input))
	bool bAllowDuplicates;

	UPROPERTY(meta = (Output))
	FRigElementKeyCollection Collection;
};

/**
 * Returns the union of two provided collections
 * (the combination of all items from both A and B).
 */
USTRUCT(meta = (DisplayName = "Union", Keywords = "Combine,Add,Merge,Collection,Hierarchy"))
struct CONTROLRIG_API FRigUnit_CollectionUnion : public FRigUnit_CollectionBase
{
	GENERATED_BODY()

	FRigUnit_CollectionUnion()
	{
		bAllowDuplicates = false;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FRigElementKeyCollection A;

	UPROPERTY(meta = (Input))
	FRigElementKeyCollection B;

	UPROPERTY(meta = (Input))
	bool bAllowDuplicates;

	UPROPERTY(meta = (Output))
	FRigElementKeyCollection Collection;
};

/**
 * Returns the intersection of two provided collections
 * (the items present in both A and B).
 */
USTRUCT(meta = (DisplayName = "Intersection", Keywords = "Combine,Merge,Collection,Hierarchy"))
struct CONTROLRIG_API FRigUnit_CollectionIntersection : public FRigUnit_CollectionBase
{
	GENERATED_BODY()

	FRigUnit_CollectionIntersection()
	{
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FRigElementKeyCollection A;

	UPROPERTY(meta = (Input))
	FRigElementKeyCollection B;

	UPROPERTY(meta = (Output))
	FRigElementKeyCollection Collection;
};

/**
 * Returns the difference between two collections
 * (the items present in A but not in B).
 */
USTRUCT(meta = (DisplayName = "Difference", Keywords = "Collection,Exclude,Subtract"))
struct CONTROLRIG_API FRigUnit_CollectionDifference : public FRigUnit_CollectionBase
{
	GENERATED_BODY()

	FRigUnit_CollectionDifference()
	{
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FRigElementKeyCollection A;

	UPROPERTY(meta = (Input))
	FRigElementKeyCollection B;

	UPROPERTY(meta = (Output))
	FRigElementKeyCollection Collection;
};

/**
 * Returns the collection in reverse order
 */
USTRUCT(meta = (DisplayName = "Reverse", Keywords = "Direction,Order,Reverse"))
struct CONTROLRIG_API FRigUnit_CollectionReverse : public FRigUnit_CollectionBase
{
	GENERATED_BODY()

	FRigUnit_CollectionReverse()
	{
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FRigElementKeyCollection Collection;

	UPROPERTY(meta = (Output))
	FRigElementKeyCollection Reversed;
};

/**
 * Returns the number of elements in a collection
 */
USTRUCT(meta = (DisplayName = "Count", Keywords = "Collection,Array,Count,Num,Length,Size"))
struct CONTROLRIG_API FRigUnit_CollectionCount : public FRigUnit_CollectionBase
{
	GENERATED_BODY()

	FRigUnit_CollectionCount()
	{
		Collection = FRigElementKeyCollection();
		Count = 0;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FRigElementKeyCollection Collection;

	UPROPERTY(meta = (Output))
	int32 Count;
};

/**
 * Returns a single item within a collection by index
 */
USTRUCT(meta = (DisplayName = "Item At Index", Keywords = "Item,GetIndex,AtIndex,At,ForIndex,[]"))
struct CONTROLRIG_API FRigUnit_CollectionItemAtIndex : public FRigUnit_CollectionBase
{
	GENERATED_BODY()

	FRigUnit_CollectionItemAtIndex()
	{
		Collection = FRigElementKeyCollection();
		Index = 0;
		Item = FRigElementKey();
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FRigElementKeyCollection Collection;

	UPROPERTY(meta = (Input))
	int32 Index;

	UPROPERTY(meta = (Output))
	FRigElementKey Item;
};

/**
 * Given a collection of items, execute iteratively across all items in a given collection
 */
USTRUCT(meta=(DisplayName="For Each Item", Keywords="Collection,Loop,Iterate"))
struct CONTROLRIG_API FRigUnit_CollectionLoop : public FRigUnit_CollectionBaseMutable
{
	GENERATED_BODY()

	FRigUnit_CollectionLoop()
	{
		Count = 0;
		Index = 0;
		Ratio = 0.f;
		Continue = false;
	}

	// FRigVMStruct overrides
	FORCEINLINE virtual bool IsForLoop() const override { return true; }
	FORCEINLINE virtual int32 GetNumSlices() const override { return Count; }

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FRigElementKeyCollection Collection;

	UPROPERTY(meta = (Output))
	FRigElementKey Item;

	UPROPERTY(meta = (Singleton, Output))
	int32 Index;

	UPROPERTY(meta = (Singleton, Output))
	int32 Count;

	/**
	 * Ranging from 0.0 (first item) and 1.0 (last item)
	 * This is useful to drive a consecutive node with a 
	 * curve or an ease to distribute a value.
	 */
	UPROPERTY(meta = (Singleton, Output))
	float Ratio;

	UPROPERTY(meta = (Singleton))
	bool Continue;

	UPROPERTY(meta = (Output))
	FControlRigExecuteContext Completed;
};

/**
* Adds an element to an existing collection
*/
USTRUCT(meta = (DisplayName = "Add Item", Keywords = "Item,Add,Push,Insert"))
struct CONTROLRIG_API FRigUnit_CollectionAddItem : public FRigUnit_CollectionBase
{
	GENERATED_BODY()

	FRigUnit_CollectionAddItem()
	{
		Collection = Result = FRigElementKeyCollection();
		Item = FRigElementKey();
	}

	RIGVM_METHOD()
    virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FRigElementKeyCollection Collection;

	UPROPERTY(meta = (Input))
	FRigElementKey Item;

	UPROPERTY(meta = (Output))
	FRigElementKeyCollection Result;
};