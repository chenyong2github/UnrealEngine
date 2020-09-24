// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_SetBoneTranslation.h"
#include "Units/RigUnitContext.h"

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
				CachedBone.Reset();
				// fall through to update
			}
			case EControlRigState::Update:
			{
				if (!CachedBone.UpdateCache(Bone, Hierarchy))
				{
					UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Bone '%s' is not valid."), *Bone.ToString());
				}
				else
				{
					switch (Space)
					{
						case EBoneGetterSetterMode::GlobalSpace:
						{
							FTransform Transform = Hierarchy->GetGlobalTransform(CachedBone);

							if (FMath::IsNearlyEqual(Weight, 1.f))
							{
								Transform.SetTranslation(Translation);
							}
							else
							{
								float T = FMath::Clamp<float>(Weight, 0.f, 1.f);
								Transform.SetTranslation(FMath::Lerp<FVector>(Transform.GetTranslation(), Translation, T));
							}

							Hierarchy->SetGlobalTransform(CachedBone, Transform, bPropagateToChildren);
							break;
						}
						case EBoneGetterSetterMode::LocalSpace:
						{
							FTransform Transform = Hierarchy->GetLocalTransform(CachedBone);

							if (FMath::IsNearlyEqual(Weight, 1.f))
							{
								Transform.SetTranslation(Translation);
							}
							else
							{
								float T = FMath::Clamp<float>(Weight, 0.f, 1.f);
								Transform.SetTranslation(FMath::Lerp<FVector>(Transform.GetTranslation(), Translation, T));
							}

							Hierarchy->SetLocalTransform(CachedBone, Transform, bPropagateToChildren);
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
