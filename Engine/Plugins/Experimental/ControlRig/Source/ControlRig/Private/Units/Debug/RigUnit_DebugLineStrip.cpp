// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Debug/RigUnit_DebugLineStrip.h"
#include "Units/RigUnitContext.h"

FRigUnit_DebugLineStrip_Execute()
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

	if (Space != NAME_None && Context.GetBones() != nullptr)
	{
		FTransform Transform = Context.GetBones()->GetGlobalTransform(Space);
		TArray<FVector> PointsTransformed;
		PointsTransformed.Reserve(Points.Num());
		for(const FVector& Point : Points)
		{
			PointsTransformed.Add(Transform.TransformPosition(Point));
		}
		Context.DrawInterface->DrawLineStrip(WorldOffset, PointsTransformed, Color, Thickness);
	}
	else
	{
		Context.DrawInterface->DrawLineStrip(WorldOffset, Points, Color, Thickness);
	}
}
