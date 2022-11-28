// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Debug/RigUnit_DebugHierarchy.h"
#include "Units/RigUnitContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigUnit_DebugHierarchy)

FRigUnit_DebugHierarchy_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if (ExecuteContext.UnitContext.State == EControlRigState::Init)
	{
		return;
	}

	if (ExecuteContext.UnitContext.DrawInterface == nullptr || !bEnabled)
	{
		return;
	}

	URigHierarchy* Hierarchy = ExecuteContext.Hierarchy;
	if (Hierarchy)
	{
		ExecuteContext.UnitContext.DrawInterface->DrawHierarchy(WorldOffset, Hierarchy, EControlRigDrawHierarchyMode::Axes, Scale, Color, Thickness);
	}
}

FRigUnit_DebugPose_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if (ExecuteContext.UnitContext.State == EControlRigState::Init)
	{
		return;
	}

	if (ExecuteContext.UnitContext.DrawInterface == nullptr || !bEnabled)
	{
		return;
	}

	URigHierarchy* Hierarchy = ExecuteContext.Hierarchy;
	if (Hierarchy)
	{
		ExecuteContext.UnitContext.DrawInterface->DrawHierarchy(WorldOffset, Hierarchy, EControlRigDrawHierarchyMode::Axes, Scale, Color, Thickness, &Pose);
	}
}

