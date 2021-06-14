// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_SetSpaceTransform.h"
#include "Units/RigUnitContext.h"
#include "Math/ControlRigMathLibrary.h"

FRigUnit_SetSpaceTransform_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	URigHierarchy* Hierarchy = ExecuteContext.Hierarchy;
	if (Hierarchy)
	{
		switch (Context.State)
		{
			case EControlRigState::Init:
			{
				CachedSpaceIndex.Reset();
				break;
			}
			case EControlRigState::Update:
			{
				const FRigElementKey SpaceKey(Space, ERigElementType::Null);

				if (CachedSpaceIndex.UpdateCache(SpaceKey, Hierarchy))
				{
					switch (SpaceType)
					{
						case EBoneGetterSetterMode::GlobalSpace:
						{
							if(FMath::IsNearlyEqual(Weight, 1.f))
							{
								Hierarchy->SetGlobalTransform(CachedSpaceIndex, Transform, true);
							}
							else
							{
								const FTransform PreviousTransform = Hierarchy->GetGlobalTransform(CachedSpaceIndex);
								Hierarchy->SetGlobalTransform(CachedSpaceIndex, FControlRigMathLibrary::LerpTransform(PreviousTransform, Transform, FMath::Clamp<float>(Weight, 0.f, 1.f)), true);
							}
							break;
						}
						case EBoneGetterSetterMode::LocalSpace:
						{
							if(FMath::IsNearlyEqual(Weight, 1.f))
							{
								Hierarchy->SetLocalTransform(CachedSpaceIndex, Transform, true);
							}
							else
							{
								const FTransform PreviousTransform = Hierarchy->GetLocalTransform(CachedSpaceIndex);
								Hierarchy->SetLocalTransform(CachedSpaceIndex, FControlRigMathLibrary::LerpTransform(PreviousTransform, Transform, FMath::Clamp<float>(Weight, 0.f, 1.f)), true);
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
			default:
			{
				break;
			}
		}
	}
}
