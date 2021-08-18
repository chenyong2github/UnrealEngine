// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_Collection.h"
#include "Units/Execution/RigUnit_Item.h"
#include "Units/RigUnitContext.h"

#if WITH_EDITOR
#include "Units/RigUnitTest.h"
#endif

FRigUnit_CollectionChain_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if (Context.State == EControlRigState::Init)
	{
		CachedHierarchyHash = INDEX_NONE;
	}

	int32 CurrentHierarchyHash = Context.Hierarchy->GetTopologyVersion() * 17;
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

	int32 CurrentHierarchyHash = Context.Hierarchy->GetTopologyVersion() * 17;
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

	int32 CurrentHierarchyHash = Context.Hierarchy->GetTopologyVersion() * 17;
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

#if WITH_EDITOR

IMPLEMENT_RIGUNIT_AUTOMATION_TEST(FRigUnit_CollectionChildren)
{
	const FRigElementKey Root = Controller->AddBone(TEXT("Root"), FRigElementKey(), FTransform(FVector(0.f, 0.f, 0.f)), true, ERigBoneType::User);
	const FRigElementKey BoneA = Controller->AddBone(TEXT("BoneA"), Root, FTransform(FVector(0.f, 0.f, 0.f)), true, ERigBoneType::User);
	const FRigElementKey BoneB = Controller->AddBone(TEXT("BoneB"), BoneA, FTransform(FVector(0.f, 0.f, 0.f)), true, ERigBoneType::User);
	const FRigElementKey BoneC = Controller->AddBone(TEXT("BoneC"), Root, FTransform(FVector(0.f, 0.f, 0.f)), true, ERigBoneType::User);

	Unit.Parent = Root;
	Unit.bIncludeParent = false;
	Unit.bRecursive = false;
	Execute();
	AddErrorIfFalse(Unit.Collection.Num() == 2, TEXT("unexpected result"));
	AddErrorIfFalse(Unit.Collection[0] == BoneA, TEXT("unexpected result"));
	AddErrorIfFalse(Unit.Collection[1] == BoneC, TEXT("unexpected result"));

	Unit.bIncludeParent = true;
	Unit.bRecursive = false;
	Execute();
	AddErrorIfFalse(Unit.Collection.Num() == 3, TEXT("unexpected result"));
	AddErrorIfFalse(Unit.Collection[0] == Root, TEXT("unexpected result"));
	AddErrorIfFalse(Unit.Collection[1] == BoneA, TEXT("unexpected result"));
	AddErrorIfFalse(Unit.Collection[2] == BoneC, TEXT("unexpected result"));

	Unit.bIncludeParent = true;
	Unit.bRecursive = true;
	Execute();
	AddErrorIfFalse(Unit.Collection.Num() == 4, TEXT("unexpected result"));
	AddErrorIfFalse(Unit.Collection[0] == Root, TEXT("unexpected result"));
	AddErrorIfFalse(Unit.Collection[1] == BoneA, TEXT("unexpected result"));
	AddErrorIfFalse(Unit.Collection[2] == BoneC, TEXT("unexpected result"));
	AddErrorIfFalse(Unit.Collection[3] == BoneB, TEXT("unexpected result"));

	return true;
}

#endif

FRigUnit_CollectionReplaceItems_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if (Context.State == EControlRigState::Init)
	{
		CachedHierarchyHash = INDEX_NONE;
	}

	int32 CurrentHierarchyHash = Context.Hierarchy->GetTopologyVersion() * 17;
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
				if(bAllowDuplicates)
				{
					CachedCollection.Add(Key);
				}
				else
				{
					CachedCollection.AddUnique(Key);
				}
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
		if(bAllowDuplicates)
		{
			Collection.Add(Key);
		}
		else
		{
			Collection.AddUnique(Key);
		}
	}
}

FRigUnit_CollectionGetItems_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	Items = Collection.GetKeys();
}


FRigUnit_CollectionUnion_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	Collection = FRigElementKeyCollection::MakeUnion(A, B, bAllowDuplicates);
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

FRigUnit_CollectionAddItem_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT();

	Result = Collection;
	Result.AddUnique(Item);
}