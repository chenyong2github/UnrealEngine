// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Debug/RigUnit_DebugLine.h"
#include "Units/RigUnitContext.h"

FRigUnit_DebugLine_Execute()
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
	if (Space != NAME_None && Context.GetBones() != nullptr)
	{
		FTransform Transform = Context.GetBones()->GetGlobalTransform(Space);
		DrawA = Transform.TransformPosition(DrawA);
		DrawB = Transform.TransformPosition(DrawB);
	}

	Context.DrawInterface->DrawLine(WorldOffset, DrawA, DrawB, Color, Thickness);
}
