// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Debug/RigUnit_DebugBezier.h"
#include "Units/RigUnitContext.h"

void FRigUnit_DebugBezier::Execute(const FRigUnitContext& Context)
{
	if (Context.State == EControlRigState::Init)
	{
		return;
	}

	if (Context.DrawInterface == nullptr || !bEnabled)
	{
		return;
	}

	FVector DrawA = A, DrawB = B, DrawC = C, DrawD = D;
	if (Space != NAME_None && Context.HierarchyReference.Get() != nullptr)
	{
		FTransform Transform = Context.HierarchyReference.Get()->GetGlobalTransform(Space);
		DrawA = Transform.TransformPosition(DrawA);
		DrawB = Transform.TransformPosition(DrawB);
		DrawC = Transform.TransformPosition(DrawC);
		DrawD = Transform.TransformPosition(DrawD);
	}


	Context.DrawInterface->DrawBezier(WorldOffset, DrawA, DrawB, DrawC, DrawD, MinimumU, MaximumU, Color, Thickness, Detail);
}