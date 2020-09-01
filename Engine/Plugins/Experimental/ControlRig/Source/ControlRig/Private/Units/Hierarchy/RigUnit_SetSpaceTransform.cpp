// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_SetSpaceTransform.h"
#include "Units/RigUnitContext.h"
#include "Math/ControlRigMathLibrary.h"

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
				CachedSpaceIndex.Reset();
				break;
			}
			case EControlRigState::Update:
			{
				if (CachedSpaceIndex.UpdateCache(Space, Hierarchy))
				{
					switch (SpaceType)
					{
						case EBoneGetterSetterMode::GlobalSpace:
						{
							if(FMath::IsNearlyEqual(Weight, 1.f))
							{
								Hierarchy->SetGlobalTransform(CachedSpaceIndex, Transform);
							}
							else
							{
								FTransform PreviousTransform = Hierarchy->GetGlobalTransform(CachedSpaceIndex);
								Hierarchy->SetGlobalTransform(CachedSpaceIndex, FControlRigMathLibrary::LerpTransform(PreviousTransform, Transform, FMath::Clamp<float>(Weight, 0.f, 1.f)));
							}
							break;
						}
						case EBoneGetterSetterMode::LocalSpace:
						{
							if(FMath::IsNearlyEqual(Weight, 1.f))
							{
								Hierarchy->SetLocalTransform(CachedSpaceIndex, Transform);
							}
							else
							{
								FTransform PreviousTransform = Hierarchy->GetLocalTransform(CachedSpaceIndex);
								Hierarchy->SetLocalTransform(CachedSpaceIndex, FControlRigMathLibrary::LerpTransform(PreviousTransform, Transform, FMath::Clamp<float>(Weight, 0.f, 1.f)));
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
