// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Debug/RigUnit_DebugBezier.h"
#include "Units/RigUnitContext.h"

FRigUnit_DebugBezier_Execute()
{
	FRigUnit_DebugBezierItemSpace::StaticExecute(
		RigVMExecuteContext, 
		Bezier, 
		MinimumU, 
		MaximumU, 
		Color, 
		Thickness, 
		Detail, 
		FRigElementKey(Space, ERigElementType::Bone), 
		WorldOffset, 
		bEnabled,
		ExecuteContext, 
		Context);
}

FRigVMStructUpgradeInfo FRigUnit_DebugBezier::GetUpgradeInfo() const
{
	// this node is no longer supported
	return FRigVMStructUpgradeInfo();
}

FRigUnit_DebugBezierItemSpace_Execute()
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
	if (Space.IsValid())
	{
		Transform = Transform * Context.Hierarchy->GetGlobalTransform(Space);
	}

	Context.DrawInterface->DrawBezier(Transform, Bezier, MinimumU, MaximumU, Color, Thickness, Detail);
}

FRigVMStructUpgradeInfo FRigUnit_DebugBezierItemSpace::GetUpgradeInfo() const
{
	// this node is no longer supported
	return FRigVMStructUpgradeInfo();
}