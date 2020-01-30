// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_GetControlInitialTransform.h"
#include "Units/RigUnitContext.h"

FString FRigUnit_GetControlInitialTransform::GetUnitLabel() const
{
	return FString::Printf(TEXT("Get Initial Control %s"), *Control.ToString());
}

FRigUnit_GetControlInitialTransform_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	const FRigControlHierarchy* Hierarchy = Context.GetControls();
	if (Hierarchy)
	{
		switch (Context.State)
		{
			case EControlRigState::Init:
			{
				CachedControlIndex = Hierarchy->GetIndex(Control);
			}
			case EControlRigState::Update:
			{
				if (CachedControlIndex != INDEX_NONE)
				{
					switch (Space)
					{
						case EBoneGetterSetterMode::GlobalSpace:
						{
							Transform = Hierarchy->GetInitialGlobalTransform(CachedControlIndex);
							break;
						}
						case EBoneGetterSetterMode::LocalSpace:
						{
							Transform = Hierarchy->GetInitialValue<FTransform>(CachedControlIndex);
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
