// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_ForLoop.h"
#include "Units/RigUnitContext.h"

FRigUnit_ForLoopCount_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
   	Continue = Index < Count;
	Ratio = GetRatioFromIndex(Index, Count);
}
