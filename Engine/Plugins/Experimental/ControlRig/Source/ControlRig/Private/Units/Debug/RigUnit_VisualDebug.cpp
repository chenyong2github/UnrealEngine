// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Debug/RigUnit_VisualDebug.h"
#include "Units/RigUnitContext.h"

FName FRigUnit_VisualDebugVector::DetermineSpaceForPin(const FString& InPinPath, void* InUserContext) const
{
	if (InPinPath == TEXT("Value"))
	{
		return BoneSpace;
	}
	return NAME_None;
}

FRigUnit_VisualDebugVector_Execute()
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

	FTransform WorldOffset = FTransform::Identity;
	if (BoneSpace != NAME_None && Context.GetBones() != nullptr)
	{
		WorldOffset = Context.GetBones()->GetGlobalTransform(BoneSpace);
	}

	switch(Mode)
	{
		case ERigUnitVisualDebugPointMode::Point:
		{
			Context.DrawInterface->DrawPoint(WorldOffset, Value, Thickness, Color);
			break;
		}
		case ERigUnitVisualDebugPointMode::Vector:
		{
			Context.DrawInterface->DrawLine(WorldOffset, FVector::ZeroVector, Value * Scale, Color, Thickness);
			break;
		}
		default:
		{
			checkNoEntry();
		}
	}
}

FName FRigUnit_VisualDebugQuat::DetermineSpaceForPin(const FString& InPinPath, void* InUserContext) const
{
	if (InPinPath == TEXT("Value"))
	{
		return BoneSpace;
	}
	return NAME_None;
}

FRigUnit_VisualDebugQuat_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

    FTransform Transform = FTransform::Identity;
    Transform.SetRotation(Value);

	FRigUnit_VisualDebugTransform::StaticExecute(RigVMOperatorName, RigVMOperatorIndex, Transform, bEnabled, Thickness, Scale, BoneSpace, Context);
}

FName FRigUnit_VisualDebugTransform::DetermineSpaceForPin(const FString& InPinPath, void* InUserContext) const
{
	if (InPinPath == TEXT("Value"))
	{
		return BoneSpace;
	}
	return NAME_None;
}

FRigUnit_VisualDebugTransform_Execute()
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

	FTransform WorldOffset = FTransform::Identity;
	if (BoneSpace != NAME_None && Context.GetBones() != nullptr)
	{
		WorldOffset = Context.GetBones()->GetGlobalTransform(BoneSpace);
	}

	Context.DrawInterface->DrawAxes(WorldOffset, Value, Scale, Thickness);
}
