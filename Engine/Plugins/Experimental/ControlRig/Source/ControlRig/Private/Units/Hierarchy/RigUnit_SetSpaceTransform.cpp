// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_SetSpaceTransform.h"
#include "Units/RigUnitContext.h"
#include "Math/ControlRigMathLibrary.h"

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
