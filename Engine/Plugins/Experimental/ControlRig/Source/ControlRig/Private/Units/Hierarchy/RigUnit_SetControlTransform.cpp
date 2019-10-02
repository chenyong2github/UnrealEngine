// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigUnit_SetControlTransform.h"
#include "Units/RigUnitContext.h"

FString FRigUnit_SetControlTransform::GetUnitLabel() const
{
	return FString::Printf(TEXT("Set Control %s"), *Control.ToString());
}

FRigUnit_SetControlTransform_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	FRigControlHierarchy* Hierarchy = ExecuteContext.GetControls();
	if (Hierarchy)
	{
		switch (Context.State)
		{
			case EControlRigState::Init:
			{
				CachedControlIndex = Hierarchy->GetIndex(Control);
				break;
			}
			case EControlRigState::Update:
			{
				if (CachedControlIndex != INDEX_NONE)
				{
					switch (Space)
					{
						case EBoneGetterSetterMode::GlobalSpace:
						{
							Hierarchy->SetGlobalTransform(CachedControlIndex, Transform);
							break;
						}
						case EBoneGetterSetterMode::LocalSpace:
						{
							Hierarchy->SetLocalTransform(CachedControlIndex, Transform);
							break;
						}
						default:
						{
							break;
						}
					}
				}
			}
			default:
			{
				break;
			}
		}
	}
}
