// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_SetControlColor.h"
#include "Units/RigUnitContext.h"

FRigUnit_SetControlColor_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	FRigControlHierarchy* Hierarchy = ExecuteContext.GetControls();
	if (Hierarchy)
	{
		switch (Context.State)
		{
			case EControlRigState::Init:
			{
				CachedControlIndex.Reset();
				break;
			}
			case EControlRigState::Update:
			{
				if (CachedControlIndex.UpdateCache(Control, Hierarchy))
				{
					(*Hierarchy)[CachedControlIndex].GizmoColor = Color;
				}
				break;
			}
			default:
			{
				break;
			}
		}
	}
}
