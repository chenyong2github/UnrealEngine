// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Debug/RigUnit_DebugHierarchy.h"
#include "Units/RigUnitContext.h"

void FRigUnit_DebugHierarchy::Execute(const FRigUnitContext& Context)
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

	const FRigHierarchy* Hierarchy = Context.HierarchyReference.Get();
	if (Hierarchy)
	{
		Context.DrawInterface->DrawHierarchy(WorldOffset, *Hierarchy, Mode, Scale, Color, Thickness);
	}
}