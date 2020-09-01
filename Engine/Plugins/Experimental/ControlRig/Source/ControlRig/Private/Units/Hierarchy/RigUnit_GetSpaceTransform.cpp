// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_GetSpaceTransform.h"
#include "Units/RigUnitContext.h"

FRigUnit_GetSpaceTransform_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	const FRigSpaceHierarchy* Hierarchy = Context.GetSpaces();
	if (Hierarchy)
	{
		switch (Context.State)
		{
			case EControlRigState::Init:
			{
				CachedSpaceIndex.Reset();
			}
			case EControlRigState::Update:
			{
				if (CachedSpaceIndex.UpdateCache(Space, Hierarchy))
				{
					switch (SpaceType)
					{
						case EBoneGetterSetterMode::GlobalSpace:
						{
							Transform = Hierarchy->GetGlobalTransform(CachedSpaceIndex);
							break;
						}
						case EBoneGetterSetterMode::LocalSpace:
						{
							Transform = Hierarchy->GetLocalTransform(CachedSpaceIndex);
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
