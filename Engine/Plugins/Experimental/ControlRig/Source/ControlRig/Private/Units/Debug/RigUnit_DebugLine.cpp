// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Debug/RigUnit_DebugLine.h"
#include "Units/RigUnitContext.h"

void FRigUnit_DebugLine::Execute(const FRigUnitContext& Context)
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

	FVector DrawA = A, DrawB = B;
	if (Space != NAME_None && Context.HierarchyReference.Get() != nullptr)
	{
		FTransform Transform = Context.HierarchyReference.Get()->GetGlobalTransform(Space);
		DrawA = Transform.TransformPosition(DrawA);
		DrawB = Transform.TransformPosition(DrawB);
	}

	Context.DrawInterface->DrawLine(WorldOffset, DrawA, DrawB, Color, Thickness);
}
