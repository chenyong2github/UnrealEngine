// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_SetBoneTranslation.h"
#include "Units/RigUnitContext.h"

FString FRigUnit_SetBoneTranslation::GetUnitLabel() const
{
	return FString::Printf(TEXT("Set Translation %s"), *Bone.ToString());
}

FRigUnit_SetBoneTranslation_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	FRigBoneHierarchy* Hierarchy = ExecuteContext.GetBones();
	if (Hierarchy)
	{
		switch (Context.State)
		{
			case EControlRigState::Init:
			{
				CachedBoneIndex = Hierarchy->GetIndex(Bone);
				// fall through to update
			}
			case EControlRigState::Update:
			{
				if (CachedBoneIndex != INDEX_NONE)
				{
					switch (Space)
					{
						case EBoneGetterSetterMode::GlobalSpace:
						{
							FTransform Transform = Hierarchy->GetGlobalTransform(CachedBoneIndex);

							if (FMath::IsNearlyEqual(Weight, 1.f))
							{
								Transform.SetTranslation(Translation);
							}
							else
							{
								float T = FMath::Clamp<float>(Weight, 0.f, 1.f);
								Transform.SetTranslation(FMath::Lerp<FVector>(Transform.GetTranslation(), Translation, T));
							}

							Hierarchy->SetGlobalTransform(CachedBoneIndex, Transform, bPropagateToChildren);
							break;
						}
						case EBoneGetterSetterMode::LocalSpace:
						{
							FTransform Transform = Hierarchy->GetLocalTransform(CachedBoneIndex);

							if (FMath::IsNearlyEqual(Weight, 1.f))
							{
								Transform.SetTranslation(Translation);
							}
							else
							{
								float T = FMath::Clamp<float>(Weight, 0.f, 1.f);
								Transform.SetTranslation(FMath::Lerp<FVector>(Transform.GetTranslation(), Translation, T));
							}

							Hierarchy->SetLocalTransform(CachedBoneIndex, Transform, bPropagateToChildren);
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
