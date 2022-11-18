// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_ForLoop.h"
#include "Units/RigUnitContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigUnit_ForLoop)

FRigUnit_ForLoopCount_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if(BlockToRun.IsNone())
	{
		Index = 0;
		BlockToRun = ExecuteContextName;
	}
	else if(BlockToRun == ExecuteContextName)
	{
		Index++;
	}

	if(Index == Count)
	{
		BlockToRun = ControlFlowCompletedName;
	}

	Ratio = GetRatioFromIndex(Index, Count);
}

