// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Debug/RigUnit_DebugBezier.h"
#include "Units/RigUnitContext.h"

void FRigUnit_DebugBezier::Execute(const FRigUnitContext& Context)
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

	FTransform Transform = WorldOffset;
	if (Space != NAME_None && Context.HierarchyReference.GetBones() != nullptr)
	{
		Transform = Transform * Context.HierarchyReference.GetBones()->GetGlobalTransform(Space);
	}


	Context.DrawInterface->DrawBezier(Transform, Bezier, MinimumU, MaximumU, Color, Thickness, Detail);
}