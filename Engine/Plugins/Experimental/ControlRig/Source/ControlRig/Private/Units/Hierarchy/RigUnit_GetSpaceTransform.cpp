// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigUnit_GetSpaceTransform.h"
#include "Units/RigUnitContext.h"

FString FRigUnit_GetSpaceTransform::GetUnitLabel() const
{
	return FString::Printf(TEXT("Get Transform %s"), *Space.ToString());
}

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
				CachedSpaceIndex = Hierarchy->GetIndex(Space);
			}
			case EControlRigState::Update:
			{
				if (CachedSpaceIndex != INDEX_NONE)
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
