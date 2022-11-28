// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Debug/RigUnit_DebugBezier.h"
#include "Units/RigUnitContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigUnit_DebugBezier)

FRigUnit_DebugBezier_Execute()
{
	FRigUnit_DebugBezierItemSpace::StaticExecute(
		ExecuteContext, 
		Bezier, 
		MinimumU, 
		MaximumU, 
		Color, 
		Thickness, 
		Detail, 
		FRigElementKey(Space, ERigElementType::Bone), 
		WorldOffset, 
		bEnabled);
}

FRigVMStructUpgradeInfo FRigUnit_DebugBezier::GetUpgradeInfo() const
{
	// this node is no longer supported
	return FRigVMStructUpgradeInfo();
}

FRigUnit_DebugBezierItemSpace_Execute()
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

	FTransform Transform = WorldOffset;
	if (Space.IsValid())
	{
		Transform = Transform * ExecuteContext.Hierarchy->GetGlobalTransform(Space);
	}

	ExecuteContext.UnitContext.DrawInterface->DrawBezier(Transform, Bezier, MinimumU, MaximumU, Color, Thickness, Detail);
}

FRigVMStructUpgradeInfo FRigUnit_DebugBezierItemSpace::GetUpgradeInfo() const
{
	// this node is no longer supported
	return FRigVMStructUpgradeInfo();
}
