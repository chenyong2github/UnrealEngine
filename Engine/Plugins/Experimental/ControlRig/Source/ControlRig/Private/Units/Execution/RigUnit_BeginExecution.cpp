// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_BeginExecution.h"
#include "Units/RigUnitContext.h"

FName FRigUnit_BeginExecution::EventName = TEXT("Update");

FRigUnit_BeginExecution_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	ExecuteContext.Hierarchy = (FRigHierarchyContainer*)Context.Hierarchy;
	ExecuteContext.EventName = FRigUnit_BeginExecution::EventName;
}
