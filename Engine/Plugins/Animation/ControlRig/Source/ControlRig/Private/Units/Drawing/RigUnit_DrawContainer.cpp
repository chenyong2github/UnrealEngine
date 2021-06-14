// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Drawing/RigUnit_DrawContainer.h"
#include "Units/RigUnitContext.h"

FRigUnit_DrawContainerGetInstruction_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

    if(Context.DrawContainer == nullptr)
    {
    	return;
    }
	
	switch(Context.State)
	{
		case EControlRigState::Update:
		{
			int32 Index = Context.DrawContainer->GetIndex(InstructionName);
			if (Index != INDEX_NONE)
			{
				const FControlRigDrawInstruction& Instruction = (*Context.DrawContainer)[Index];
				Color = Instruction.Color;
				Transform = Instruction.Transform;
			}
			else
			{
				Color = FLinearColor::Red;
				Transform = FTransform::Identity;
			}
			break;
		}
	}
}

FRigUnit_DrawContainerSetThickness_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if (Context.DrawContainer == nullptr)
	{
		return;
	}

	switch (Context.State)
	{
		case EControlRigState::Update:
		{
			int32 Index = Context.DrawContainer->GetIndex(InstructionName);
			if (Index != INDEX_NONE)
			{
				FControlRigDrawInstruction& Instruction = (*Context.DrawContainer)[Index];
				Instruction.Thickness = Thickness;
			}
			break;
		}
	}
}

FRigUnit_DrawContainerSetColor_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if (Context.DrawContainer == nullptr)
	{
		return;
	}

	switch (Context.State)
	{
		case EControlRigState::Update:
		{
			int32 Index = Context.DrawContainer->GetIndex(InstructionName);
			if (Index != INDEX_NONE)
			{
				FControlRigDrawInstruction& Instruction = (*Context.DrawContainer)[Index];
				Instruction.Color = Color;
			}
			break;
		}
	}
}

FRigUnit_DrawContainerSetTransform_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if (Context.DrawContainer == nullptr)
	{
		return;
	}

	switch (Context.State)
	{
		case EControlRigState::Update:
		{
			int32 Index = Context.DrawContainer->GetIndex(InstructionName);
			if (Index != INDEX_NONE)
			{
				FControlRigDrawInstruction& Instruction = (*Context.DrawContainer)[Index];
				Instruction.Transform = Transform;
			}
			break;
		}
	}
}