// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_SetSpaceTransform.h"
#include "Units/RigUnitContext.h"

FString FRigUnit_SetSpaceTransform::GetUnitLabel() const
{
	return FString::Printf(TEXT("Set Space %s"), *Space.ToString());
}

FRigUnit_SetSpaceTransform_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	FRigSpaceHierarchy* Hierarchy = ExecuteContext.GetSpaces();
	if (Hierarchy)
	{
		switch (Context.State)
		{
			case EControlRigState::Init:
			{
				CachedSpaceIndex = Hierarchy->GetIndex(Space);
				break;
			}
			case EControlRigState::Update:
			{
				if (CachedSpaceIndex != INDEX_NONE)
				{
					switch (SpaceType)
					{
						case EBoneGetterSetterMode::GlobalSpace:
						{
							Hierarchy->SetGlobalTransform(CachedSpaceIndex, Transform);
							break;
						}
						case EBoneGetterSetterMode::LocalSpace:
						{
							Hierarchy->SetLocalTransform(CachedSpaceIndex, Transform);
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
