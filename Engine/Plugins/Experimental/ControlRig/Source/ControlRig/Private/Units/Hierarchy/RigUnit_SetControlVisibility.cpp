// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_SetControlVisibility.h"
#include "Units/RigUnitContext.h"


FRigUnit_SetControlVisibility_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	FRigControlHierarchy* Controls = ExecuteContext.GetControls();
	if (Controls)
	{
		switch (Context.State)
		{
			case EControlRigState::Init:
			{
				CachedControlIndices.Reset();
			}
			case EControlRigState::Update:
			{
				TArray<FRigElementKey> Keys;

				if (Item.IsValid())
				{
					if (Item.Type != ERigElementType::Control)
					{
						return;
					}

					Keys.Add(Item);
				}
				else if (!Pattern.IsEmpty())
				{
					for (int32 Index = 0; Index < Controls->Num(); Index++)
					{
						const FRigControl& Control = (*Controls)[Index];
						if (Control.Name.ToString().Contains(Pattern, ESearchCase::CaseSensitive))
						{
							Keys.Add(Control.GetElementKey());
						}
					}
				}

				if (CachedControlIndices.Num() != Keys.Num())
				{
					CachedControlIndices.Reset();
					CachedControlIndices.SetNumZeroed(Keys.Num());
				}

				for (int32 Index = 0; Index < Keys.Num(); Index++)
				{
					CachedControlIndices[Index].UpdateCache(Keys[Index], ExecuteContext.Hierarchy);
				}

				for (const FCachedRigElement& CachedControlIndex : CachedControlIndices)
				{
					if (CachedControlIndex.IsValid())
					{
						(*Controls)[CachedControlIndex].bGizmoVisible = bVisible;
					}
				}
				break;
			}
			default:
			{
				break;
			}
		}
	}
}
