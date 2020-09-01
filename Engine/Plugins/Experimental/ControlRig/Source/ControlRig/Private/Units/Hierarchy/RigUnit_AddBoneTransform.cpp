// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Hierarchy/RigUnit_AddBoneTransform.h"
#include "Units/RigUnitContext.h"

FRigUnit_AddBoneTransform_Execute()
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
			}
			case EControlRigState::Update:
			{
				if (!CachedBone.UpdateCache(Bone, Hierarchy))
				{
					UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Bone '%s' is not valid."), *Bone.ToString());
				}
				else
				{
					FTransform TargetTransform;
					const FTransform PreviousTransform = Hierarchy->GetGlobalTransform(CachedBone);

					if (bPostMultiply)
					{
						TargetTransform = PreviousTransform * Transform;
					}
					else
					{
						TargetTransform = Transform * PreviousTransform;
					}

					if (!FMath::IsNearlyEqual(Weight, 1.f))
					{
						float T = FMath::Clamp<float>(Weight, 0.f, 1.f);
						TargetTransform = FControlRigMathLibrary::LerpTransform(PreviousTransform, TargetTransform, T);
					}

					Hierarchy->SetGlobalTransform(CachedBone, TargetTransform, bPropagateToChildren);
				}
			}
			default:
			{
				break;
			}
		}
	}
}

