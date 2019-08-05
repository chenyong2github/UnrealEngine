// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigUnit_BeginExecution.h"
#include "Units/RigUnitContext.h"

UE_RigUnit_BeginExecution_IMPLEMENT_RIGVM(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	ExecuteContext.Hierarchy = (FRigHierarchyContainer*)Context.Hierarchy;
}
