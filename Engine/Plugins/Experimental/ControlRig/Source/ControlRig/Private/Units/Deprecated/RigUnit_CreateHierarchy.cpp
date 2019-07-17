// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigUnit_CreateHierarchy.h"
#include "Units/RigUnitContext.h"

void FRigUnit_CreateHierarchy::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if (Context.State == EControlRigState::Init)
	{
		// create hierarchy
		if (!NewHierarchy.CreateHierarchy(Root, SourceHierarchy))
		{
			// if failed, print
		}
	}
}
