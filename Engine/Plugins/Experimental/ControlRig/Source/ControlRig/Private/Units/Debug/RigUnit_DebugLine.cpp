// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Debug/RigUnit_DebugLine.h"
#include "Units/RigUnitContext.h"

FRigUnit_DebugLine_Execute()
{
	FRigUnit_DebugLineItemSpace::StaticExecute(
		RigVMExecuteContext, 
		A,
		B,
		Color,
		Thickness,
		FRigElementKey(Space, ERigElementType::Bone), 
		WorldOffset, 
		bEnabled,
		ExecuteContext, 
		Context);
}

FRigUnit_DebugLineItemSpace_Execute()
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
	if (Space.IsValid())
	{
		FTransform Transform = Context.Hierarchy->GetGlobalTransform(Space);
		DrawA = Transform.TransformPosition(DrawA);
		DrawB = Transform.TransformPosition(DrawB);
	}

	Context.DrawInterface->DrawLine(WorldOffset, DrawA, DrawB, Color, Thickness);
}