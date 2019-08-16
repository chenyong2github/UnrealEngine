// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Debug/RigUnit_DebugTransform.h"
#include "Units/RigUnitContext.h"

void FRigUnit_DebugTransform::Execute(const FRigUnitContext& Context)
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

	FTransform DrawTransform = Transform;
	if (Space != NAME_None && Context.HierarchyReference.Get() != nullptr)
	{
		DrawTransform = Transform * Context.HierarchyReference.Get()->GetGlobalTransform(Space);
	}

	switch (Mode)
	{
		case ERigUnitDebugTransformMode::Axes:
		{
			Context.DrawInterface->DrawAxes(WorldOffset, DrawTransform, Scale, Thickness);
			break;
		}
		case ERigUnitDebugTransformMode::Point:
		{
			Context.DrawInterface->DrawPoint(WorldOffset, DrawTransform.GetLocation(), Scale, Color);
			break;
		}
		case ERigUnitDebugTransformMode::Box:
		{
			DrawTransform.SetScale3D(DrawTransform.GetScale3D() * Scale);
			Context.DrawInterface->DrawBox(WorldOffset, DrawTransform, Color, Thickness);
			break;
		}
	}
}

void FRigUnit_DebugTransformMutable::Execute(const FRigUnitContext& Context)
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

	FTransform DrawTransform = Transform;
	if (Space != NAME_None && Context.HierarchyReference.Get() != nullptr)
	{
		DrawTransform = Transform * Context.HierarchyReference.Get()->GetGlobalTransform(Space);
	}

	switch (Mode)
	{
		case ERigUnitDebugTransformMode::Axes:
		{
			Context.DrawInterface->DrawAxes(WorldOffset, DrawTransform, Scale, Thickness);
			break;
		}
		case ERigUnitDebugTransformMode::Point:
		{
			Context.DrawInterface->DrawPoint(WorldOffset, DrawTransform.GetLocation(), Scale, Color);
			break;
		}
		case ERigUnitDebugTransformMode::Box:
		{
			DrawTransform.SetScale3D(DrawTransform.GetScale3D() * Scale);
			Context.DrawInterface->DrawBox(WorldOffset, DrawTransform, Color, Thickness);
			break;
		}
	}
}

void FRigUnit_DebugTransformArrayMutable::Execute(const FRigUnitContext& Context)
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

	DrawTransforms.SetNumUninitialized(Transforms.Num());
	if (Space != NAME_None && Context.HierarchyReference.Get() != nullptr)
	{
		FTransform SpaceTransform = Context.HierarchyReference.Get()->GetGlobalTransform(Space);
		for(int32 Index=0;Index<Transforms.Num();Index++)
		{
			DrawTransforms[Index] = Transforms[Index] * SpaceTransform;
		}
	}
	else
	{
		for(int32 Index=0;Index<Transforms.Num();Index++)
		{
			DrawTransforms[Index] = Transforms[Index];
		}
	}

	for(FTransform& DrawTransform : DrawTransforms)
	{
		switch (Mode)
		{
			case ERigUnitDebugTransformMode::Axes:
			{
				Context.DrawInterface->DrawAxes(WorldOffset, DrawTransform, Scale, Thickness);
				break;
			}
			case ERigUnitDebugTransformMode::Point:
			{
				Context.DrawInterface->DrawPoint(WorldOffset, DrawTransform.GetLocation(), Scale, Color);
				break;
			}
			case ERigUnitDebugTransformMode::Box:
			{
				DrawTransform.SetScale3D(DrawTransform.GetScale3D() * Scale);
				Context.DrawInterface->DrawBox(WorldOffset, DrawTransform, Color, Thickness);
				break;
			}
		}
	}
}