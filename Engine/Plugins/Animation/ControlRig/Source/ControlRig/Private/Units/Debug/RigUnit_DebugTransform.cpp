// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Debug/RigUnit_DebugTransform.h"
#include "Units/RigUnitContext.h"

FRigUnit_DebugTransform_Execute()
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
	if (Space != NAME_None && Context.Hierarchy != nullptr)
	{
		DrawTransform = Transform * Context.Hierarchy->GetGlobalTransform(FRigElementKey(Space, ERigElementType::Bone));
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

FRigUnit_DebugTransformMutable_Execute()
{
	FRigUnit_DebugTransformMutableItemSpace::StaticExecute(
		RigVMExecuteContext, 
		Transform,
		Mode,
		Color,
		Thickness,
		Scale,
		FRigElementKey(Space, ERigElementType::Bone), 
		WorldOffset, 
		bEnabled,
		ExecuteContext, 
		Context);
}

FRigUnit_DebugTransformMutableItemSpace_Execute()
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
	if (Space.IsValid() && Context.Hierarchy != nullptr)
	{
		DrawTransform = Transform * Context.Hierarchy->GetGlobalTransform(Space);
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

FRigUnit_DebugTransformArrayMutable_Execute()
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

	WorkData.DrawTransforms.SetNumUninitialized(Transforms.Num());
	if (Space != NAME_None && Context.Hierarchy)
	{
		FTransform SpaceTransform = Context.Hierarchy->GetGlobalTransform(FRigElementKey(Space, ERigElementType::Bone));
		for(int32 Index=0;Index<Transforms.Num();Index++)
		{
			WorkData.DrawTransforms[Index] = Transforms[Index] * SpaceTransform;
		}
	}
	else
	{
		for(int32 Index=0;Index<Transforms.Num();Index++)
		{
			WorkData.DrawTransforms[Index] = Transforms[Index];
		}
	}

	for(FTransform& DrawTransform : WorkData.DrawTransforms)
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

FRigUnit_DebugTransformArrayMutableItemSpace_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if (Context.State == EControlRigState::Init)
	{
		return;
	}

	if (Context.DrawInterface == nullptr || !bEnabled || Transforms.Num() == 0)
	{
		return;
	}

	for(const FTransform& Transform : Transforms)
	{
		FRigUnit_DebugTransformMutableItemSpace::StaticExecute(
			RigVMExecuteContext, 
			Transform,
			Mode,
			Color,
			Thickness,
			Scale,
			Space, 
			WorldOffset, 
			bEnabled,
			ExecuteContext, 
			Context);
	}

	if(ParentIndices.Num() == Transforms.Num())
	{
		for(int32 Index = 0; Index < ParentIndices.Num(); Index++)
		{
			if(Transforms.IsValidIndex(ParentIndices[Index]))
			{
				Context.DrawInterface->DrawLine(
					WorldOffset,
					Transforms[Index].GetTranslation(),
					Transforms[ParentIndices[Index]].GetTranslation(),
					Color,
					Thickness
				);
			}
		}
	}
}
