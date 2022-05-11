// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_InteractionExecution.h"
#include "Units/RigUnitContext.h"

FName FRigUnit_InteractionExecution::EventName = TEXT("Interaction");

FRigUnit_InteractionExecution_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	ExecuteContext.CopyFrom(RigVMExecuteContext);
	ExecuteContext.Hierarchy = Context.Hierarchy;
	ExecuteContext.EventName = FRigUnit_InteractionExecution::EventName;
}
