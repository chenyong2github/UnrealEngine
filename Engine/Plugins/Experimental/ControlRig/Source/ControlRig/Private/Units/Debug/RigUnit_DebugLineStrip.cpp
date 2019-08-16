// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Debug/RigUnit_DebugLineStrip.h"
#include "Units/RigUnitContext.h"

void FRigUnit_DebugLineStrip::Execute(const FRigUnitContext& Context)
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

	if (Space != NAME_None && Context.HierarchyReference.Get() != nullptr)
	{
		FTransform Transform = Context.HierarchyReference.Get()->GetGlobalTransform(Space);
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
