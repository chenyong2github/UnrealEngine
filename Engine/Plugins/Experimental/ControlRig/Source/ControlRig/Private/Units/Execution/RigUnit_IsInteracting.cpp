// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_IsInteracting.h"
#include "Units/RigUnitContext.h"

FRigUnit_IsInteracting_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	bIsInteracting = Context.bDuringInteraction;
}
