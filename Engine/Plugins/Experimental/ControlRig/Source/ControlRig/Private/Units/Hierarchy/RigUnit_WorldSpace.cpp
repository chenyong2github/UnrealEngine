// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_WorldSpace.h"
#include "Units/RigUnitContext.h"

FRigUnit_ToWorldSpace_Transform_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
    World = Context.ToWorldSpace(Transform);
}

FRigUnit_ToRigSpace_Transform_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
    Global = Context.ToRigSpace(Transform);
}

FRigUnit_ToWorldSpace_Location_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
    World = Context.ToWorldSpace(Location);
}

FRigUnit_ToRigSpace_Location_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
    Global = Context.ToRigSpace(Location);
}

FRigUnit_ToWorldSpace_Rotation_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
    World = Context.ToWorldSpace(Rotation);
}

FRigUnit_ToRigSpace_Rotation_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
    Global = Context.ToRigSpace(Rotation);
}