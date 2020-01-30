// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_SetControlColor.h"
#include "Units/RigUnitContext.h"
#include "Math/ControlRigMathLibrary.h"

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
				CachedControlIndex = Hierarchy->GetIndex(Control);
				break;
			}
			case EControlRigState::Update:
			{
				if (CachedControlIndex != INDEX_NONE)
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
