// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_PrepareForExecution.h"
#include "Units/RigUnitContext.h"

FName FRigUnit_PrepareForExecution::EventName = TEXT("Setup");

FRigUnit_PrepareForExecution_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	ExecuteContext.Hierarchy = (FRigHierarchyContainer*)Context.Hierarchy;
	ExecuteContext.EventName = FRigUnit_PrepareForExecution::EventName;
}
