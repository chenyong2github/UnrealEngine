// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_InverseExecution.h"
#include "Units/RigUnitContext.h"

FName FRigUnit_InverseExecution::EventName = TEXT("Inverse");

FRigUnit_InverseExecution_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	ExecuteContext.Hierarchy = (FRigHierarchyContainer*)Context.Hierarchy;
	ExecuteContext.EventName = FRigUnit_InverseExecution::EventName;
}
