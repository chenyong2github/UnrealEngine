// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Debug/RigUnit_DebugHierarchy.h"
#include "Units/RigUnitContext.h"

FRigUnit_DebugHierarchy_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if (Context.State == EControlRigState::Init)
	{
		return;
	}

	if (Context.DrawInterface == nullptr || !bEnabled)
	{
		return;
	}

	URigHierarchy* Hierarchy = Context.Hierarchy;
	if (Hierarchy)
	{
		Context.DrawInterface->DrawHierarchy(WorldOffset, Hierarchy, EControlRigDrawHierarchyMode::Axes, Scale, Color, Thickness);
	}
}

FRigUnit_DebugPose_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if (Context.State == EControlRigState::Init)
	{
		return;
	}

	if (Context.DrawInterface == nullptr || !bEnabled)
	{
		return;
	}

	URigHierarchy* Hierarchy = Context.Hierarchy;
	if (Hierarchy)
	{
		Context.DrawInterface->DrawHierarchy(WorldOffset, Hierarchy, EControlRigDrawHierarchyMode::Axes, Scale, Color, Thickness, &Pose);
	}
}
