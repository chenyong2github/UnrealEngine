// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_Item.h"
#include "Units/Core/RigUnit_Name.h"
#include "Units/RigUnitContext.h"

FRigUnit_ItemExists_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	Exists = false;

	switch (Context.State)
	{
		case EControlRigState::Init:
		{
			CachedIndex.Reset();
			// fall through to update
		}
		case EControlRigState::Update:
		{
			Exists = CachedIndex.UpdateCache(Item, Context.Hierarchy);
	    	break;
	    }
	    default:
	    {
	    	break;
	    }
	}
}

FRigUnit_ItemReplace_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	Result = Item;
	FRigUnit_NameReplace::StaticExecute(RigVMExecuteContext, Item.Name, Old, New, Result.Name, Context);
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Units/RigUnitTest.h"

IMPLEMENT_RIGUNIT_AUTOMATION_TEST(FRigUnit_ItemReplace)
{
	Unit.Item.Name = FName("OldItemName");
	Unit.Item.Type = ERigElementType::Bone;

	Unit.Old = FName("Old");
	Unit.New = FName("New");
	
	Execute();
	AddErrorIfFalse(Unit.Result == FRigElementKey("NewItemName", ERigElementType::Bone), TEXT("unexpected result"));
	return true;
}
#endif