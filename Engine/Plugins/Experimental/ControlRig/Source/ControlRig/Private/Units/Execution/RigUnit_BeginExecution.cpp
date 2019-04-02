// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigUnit_BeginExecution.h"
#include "Units/RigUnitContext.h"

void FRigUnit_BeginExecution::Execute(const FRigUnitContext& Context)
{
	ExecuteContext.HierarchyReference = Context.HierarchyReference;
}
