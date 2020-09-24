// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_Hierarchy.h"
#include "Units/Execution/RigUnit_Item.h"
#include "Units/RigUnitContext.h"

FRigUnit_HierarchyGetParent_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if (Context.State == EControlRigState::Init)
	{
		CachedChild.Reset();
		CachedParent.Reset();
	}

	if(CachedChild.IsIdentical(Child, Context.Hierarchy))
	{
		Parent = CachedParent.GetKey();
	}
	else
	{
		Parent.Reset();
		CachedParent.Reset();

		if(CachedChild.UpdateCache(Child, Context.Hierarchy))
		{
			Parent = Context.Hierarchy->GetParentKey(Child);
			if(Parent.IsValid())
			{
				CachedParent.UpdateCache(Parent, Context.Hierarchy);
			}
		}
	}
}

FRigUnit_HierarchyGetParents_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if (Context.State == EControlRigState::Init)
	{
		CachedChild.Reset();
		CachedParents.Reset();
	}

	if(!CachedChild.IsIdentical(Child, Context.Hierarchy))
	{
		CachedParents.Reset();

		if(CachedChild.UpdateCache(Child, Context.Hierarchy))
		{
			TArray<FRigElementKey> Keys;
			FRigElementKey Parent = Child;
			do
			{
				if(bIncludeChild || Parent != Child)
				{
					Keys.Add(Parent);
				}
				Parent = Context.Hierarchy->GetParentKey(Parent);
			}
			while(Parent.IsValid());

			CachedParents = FRigElementKeyCollection(Keys);
			if(bReverse)
			{
				CachedParents = FRigElementKeyCollection::MakeReversed(CachedParents);
			}
		}
	}

	Parents = CachedParents;
}

FRigUnit_HierarchyGetChildren_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if (Context.State == EControlRigState::Init)
	{
		CachedParent.Reset();
		CachedChildren.Reset();
	}

	if(!CachedParent.IsIdentical(Parent, Context.Hierarchy))
	{
		CachedChildren.Reset();

		if(CachedParent.UpdateCache(Parent, Context.Hierarchy))
		{
			TArray<FRigElementKey> Keys;

			if(bIncludeParent)
			{
				Keys.Add(Parent);
			}
			Keys.Append(Context.Hierarchy->GetChildKeys(Parent, bRecursive));

			CachedChildren = FRigElementKeyCollection(Keys);
		}
	}

	Children = CachedChildren;
}

FRigUnit_HierarchyGetSiblings_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if (Context.State == EControlRigState::Init)
	{
		CachedItem.Reset();
		CachedSiblings.Reset();
	}

	if(!CachedItem.IsIdentical(Item, Context.Hierarchy))
	{
		CachedSiblings.Reset();

		if(CachedItem.UpdateCache(Item, Context.Hierarchy))
		{
			TArray<FRigElementKey> Keys;

			FRigElementKey Parent = Context.Hierarchy->GetParentKey(Item);
			if(Parent.IsValid())
			{
				TArray<FRigElementKey> Children = Context.Hierarchy->GetChildKeys(Parent, false);
				for(FRigElementKey Child : Children)
				{
					if(bIncludeItem || Child != Item)
					{
						Keys.Add(Child);
					}
				}
			}

			if(Keys.Num() == 0 && bIncludeItem)
			{
				Keys.Add(Item);
			}

			CachedSiblings = FRigElementKeyCollection(Keys);
		}
	}

	Siblings = CachedSiblings;
}