// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_GetControlInitialTransform.h"
#include "Units/RigUnitContext.h"

FRigUnit_GetControlInitialTransform_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	const URigHierarchy* Hierarchy = Context.Hierarchy;
	if (Hierarchy)
	{
		switch (Context.State)
		{
			case EControlRigState::Init:
			{
				CachedControlIndex.Reset();
			}
			case EControlRigState::Update:
			{
				const FRigElementKey Key(Control, ERigElementType::Control); 
				if (!CachedControlIndex.UpdateCache(Key, Hierarchy))
				{
					UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Control '%s' is not valid."), *Control.ToString());
				}
				else
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
							Transform = Hierarchy->GetInitialLocalTransform(CachedControlIndex);
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
