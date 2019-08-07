// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Debug/RigUnit_DebugPoint.h"
#include "Units/RigUnitContext.h"

FRigUnit_DebugPoint_Execute()
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

	FVector Center = FVector::ZeroVector;
	FVector DrawVector = Vector;
	if (Space != NAME_None && Context.GetBones() != nullptr)
	{
		FTransform Transform = Context.GetBones()->GetGlobalTransform(Space);
		Center = Transform.GetLocation();
		DrawVector = Transform.TransformPosition(DrawVector);
	}

	switch (Mode)
	{
		case ERigUnitDebugPointMode::Point:
		{
			Context.DrawInterface->DrawPoint(WorldOffset, DrawVector, Scale, Color);
			break;
		}
		case ERigUnitDebugPointMode::Vector:
		{
			Context.DrawInterface->DrawLine(WorldOffset, Center, DrawVector, Color, Thickness);
			break;
		}
	}
}

FRigUnit_DebugPointMutable_Execute()
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

	FVector Center = FVector::ZeroVector;
	FVector DrawVector = Vector;
	if (Space != NAME_None && Context.GetBones() != nullptr)
	{
		FTransform Transform = Context.GetBones()->GetGlobalTransform(Space);
		Center = Transform.GetLocation();
		DrawVector = Transform.TransformPosition(DrawVector);
	}

	switch (Mode)
	{
		case ERigUnitDebugPointMode::Point:
		{
			Context.DrawInterface->DrawPoint(WorldOffset, DrawVector, Scale, Color);
			break;
		}
		case ERigUnitDebugPointMode::Vector:
		{
			Context.DrawInterface->DrawLine(WorldOffset, Center, DrawVector, Color, Thickness);
			break;
		}
	}
}