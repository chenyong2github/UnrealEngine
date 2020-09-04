// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_Collection.h"
#include "Units/Execution/RigUnit_Item.h"
#include "Units/RigUnitContext.h"

FRigUnit_CollectionChain_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if (Context.State == EControlRigState::Init)
	{
		CachedHierarchyHash = INDEX_NONE;
	}

	int32 CurrentHierarchyHash = Context.Hierarchy->Version * 17;
	CurrentHierarchyHash += GetTypeHash(FirstItem);
	CurrentHierarchyHash += GetTypeHash(LastItem);
	CurrentHierarchyHash += Reverse ? 1 : 0;

	if (CachedHierarchyHash != CurrentHierarchyHash || CachedCollection.IsEmpty())
	{
		CachedHierarchyHash = CurrentHierarchyHash;
		CachedCollection = FRigElementKeyCollection::MakeFromChain(Context.Hierarchy, FirstItem, LastItem, Reverse);

		if (CachedCollection.IsEmpty())
		{
			if (Context.Hierarchy->GetIndex(FirstItem) == INDEX_NONE)
			{
				if(Context.State != EControlRigState::Init)
				{
					UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("First Item '%s' is not valid."), *FirstItem.ToString());
				}
			}
			if (Context.Hierarchy->GetIndex(LastItem) == INDEX_NONE)
			{
				if(Context.State != EControlRigState::Init)
				{
					UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Last Item '%s' is not valid."), *LastItem.ToString());
				}
			}
		}
	}

	Collection = CachedCollection;
}

FRigUnit_CollectionNameSearch_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if (Context.State == EControlRigState::Init)
	{
		CachedHierarchyHash = INDEX_NONE;
	}

	int32 CurrentHierarchyHash = Context.Hierarchy->Version * 17;
	CurrentHierarchyHash += GetTypeHash(PartialName);
	CurrentHierarchyHash += (int32)TypeToSearch * 8;

	if (CachedHierarchyHash != CurrentHierarchyHash || CachedCollection.IsEmpty())
	{
		CachedHierarchyHash = CurrentHierarchyHash;
		CachedCollection = FRigElementKeyCollection::MakeFromName(Context.Hierarchy, PartialName, (uint8)TypeToSearch);
	}

	Collection = CachedCollection;
}

FRigUnit_CollectionChildren_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if (Context.State == EControlRigState::Init)
	{
		CachedHierarchyHash = INDEX_NONE;
	}

	int32 CurrentHierarchyHash = Context.Hierarchy->Version * 17;
	CurrentHierarchyHash += GetTypeHash(Parent);
	CurrentHierarchyHash += bRecursive ? 2 : 0;
	CurrentHierarchyHash += bIncludeParent ? 1 : 0;
	CurrentHierarchyHash += (int32)TypeToSearch * 8;

	if (CachedHierarchyHash != CurrentHierarchyHash || CachedCollection.IsEmpty())
	{
		CachedHierarchyHash = CurrentHierarchyHash;
		CachedCollection = FRigElementKeyCollection::MakeFromChildren(Context.Hierarchy, Parent, bRecursive, bIncludeParent, (uint8)TypeToSearch);
		if (CachedCollection.IsEmpty())
		{
			if (Context.Hierarchy->GetIndex(Parent) == INDEX_NONE)
			{
				UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Parent '%s' is not valid."), *Parent.ToString());
			}
		}
	}

	Collection = CachedCollection;
}

FRigUnit_CollectionReplaceItems_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if (Context.State == EControlRigState::Init)
	{
		CachedHierarchyHash = INDEX_NONE;
	}

	int32 CurrentHierarchyHash = Context.Hierarchy->Version * 17;
	CurrentHierarchyHash += GetTypeHash(Items);
	CurrentHierarchyHash += 12 * GetTypeHash(Old);
	CurrentHierarchyHash += 13 * GetTypeHash(New);
	CurrentHierarchyHash += RemoveInvalidItems ? 14 : 0;

	if (CachedHierarchyHash != CurrentHierarchyHash || CachedCollection.IsEmpty())
	{
		CachedHierarchyHash = CurrentHierarchyHash;
		CachedCollection.Reset();

		for (int32 Index = 0; Index < Items.Num(); Index++)
		{
			FRigElementKey Key = Items[Index];
			FRigUnit_ItemReplace::StaticExecute(RigVMExecuteContext, Key, Old, New, Key, Context);

			if (Context.Hierarchy->GetIndex(Key) != INDEX_NONE)
			{
				CachedCollection.AddUnique(Key);
			}
			else if(!RemoveInvalidItems)
			{
				CachedCollection.Add(FRigElementKey());
			}
		}
	}

	Collection = CachedCollection;
}

FRigUnit_CollectionItems_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	Collection.Reset();
	for (const FRigElementKey& Key : Items)
	{
		Collection.AddUnique(Key);
	}
}

FRigUnit_CollectionUnion_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	Collection = FRigElementKeyCollection::MakeUnion(A, B);
}

FRigUnit_CollectionIntersection_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	Collection = FRigElementKeyCollection::MakeIntersection(A, B);
}

FRigUnit_CollectionDifference_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	Collection = FRigElementKeyCollection::MakeDifference(A, B);
}

FRigUnit_CollectionReverse_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	Reversed = FRigElementKeyCollection::MakeReversed(Collection);
}

FRigUnit_CollectionCount_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	Count = Collection.Num();
}

FRigUnit_CollectionItemAtIndex_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if (Collection.IsValidIndex(Index))
	{
		Item = Collection[Index];
	}
	else
	{
		Item = FRigElementKey();
	}
}

FRigUnit_CollectionLoop_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
    Count = Collection.Num();
   	Continue = Collection.IsValidIndex(Index);
	Ratio = GetRatioFromIndex(Index, Count);

	if(Continue)
	{
		Item = Collection[Index];
	}
	else
	{
		Item = FRigElementKey();
	}
}
