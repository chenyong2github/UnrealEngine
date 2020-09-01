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

	FRigUnit_NameReplace::StaticExecute(RigVMExecuteContext, Item.Name, Old, New, Result.Name, Context);
}