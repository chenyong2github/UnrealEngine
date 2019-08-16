// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Debug/RigUnit_DebugPoint.h"
#include "Units/RigUnitContext.h"

void FRigUnit_DebugPoint::Execute(const FRigUnitContext& Context)
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
	if (Space != NAME_None && Context.HierarchyReference.Get() != nullptr)
	{
		FTransform Transform = Context.HierarchyReference.Get()->GetGlobalTransform(Space);
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

void FRigUnit_DebugPointMutable::Execute(const FRigUnitContext& Context)
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
	if (Space != NAME_None && Context.HierarchyReference.Get() != nullptr)
	{
		FTransform Transform = Context.HierarchyReference.Get()->GetGlobalTransform(Space);
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