// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Deprecated/RigUnit_TwoBoneIKFK.h"
#include "Units/RigUnitContext.h"
#include "HelperUtil.h"
#include "TwoBoneIK.h"

FRigUnit_TwoBoneIKFK_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if (Context.State == EControlRigState::Init)
	{
		const FRigBoneHierarchy* Hierarchy = Context.GetBones();
		if (Hierarchy)
		{
			// reset
			StartJointIndex = MidJointIndex = EndJointIndex = INDEX_NONE;
			UpperLimbLength = LowerLimbLength = 0.f;

			// verify the chain
			int32 StartIndex = Hierarchy->GetIndex(StartJoint);
			int32 EndIndex = Hierarchy->GetIndex(EndJoint);
			if (StartIndex != INDEX_NONE && EndIndex != INDEX_NONE)
			{
				// ensure the chain
				int32 EndParentIndex = (*Hierarchy)[EndIndex].ParentIndex;
				if (EndParentIndex != INDEX_NONE)
				{
					int32 MidParentIndex = (*Hierarchy)[EndParentIndex].ParentIndex;
					if (MidParentIndex == StartIndex)
					{
						StartJointIndex = StartIndex;
						MidJointIndex = EndParentIndex;
						EndJointIndex = EndIndex;

						// set length for upper/lower length
						FTransform StartTransform = Hierarchy->GetInitialGlobalTransform(StartJointIndex);
						FTransform MidTransform = Hierarchy->GetInitialGlobalTransform(MidJointIndex);
						FTransform EndTransform = Hierarchy->GetInitialGlobalTransform(EndJointIndex);

						FVector UpperLimb = StartTransform.GetLocation() - MidTransform.GetLocation();
						FVector LowerLimb = MidTransform.GetLocation() - EndTransform.GetLocation();

						UpperLimbLength = UpperLimb.Size();
						LowerLimbLength = LowerLimb.Size();
						StartJointIKTransform = StartJointFKTransform = StartTransform;
						MidJointIKTransform = MidJointFKTransform = MidTransform;
						EndJointIKTransform = EndJointFKTransform = EndTransform;
					}
				}
			}
		}
	}
	else  if (Context.State == EControlRigState::Update)
	{
		if (StartJointIndex != INDEX_NONE && MidJointIndex != INDEX_NONE && EndJointIndex != INDEX_NONE)
		{
			FTransform StartJointTransform;
			FTransform MidJointTransform;
			FTransform EndJointTransform;

			// FK only
			if (FMath::IsNearlyZero(IKBlend))
			{
				StartJointTransform = StartJointFKTransform;
				MidJointTransform = MidJointFKTransform;
				EndJointTransform = EndJointFKTransform;
			}
			// IK only
			else if (FMath::IsNearlyEqual(IKBlend, 1.f))
			{
				// update transform before going through IK
				const FRigBoneHierarchy* Hierarchy = Context.GetBones();
				check(Hierarchy);

				StartJointIKTransform = Hierarchy->GetGlobalTransform(StartJointIndex);
				MidJointIKTransform = Hierarchy->GetGlobalTransform(MidJointIndex);
				EndJointIKTransform = Hierarchy->GetGlobalTransform(EndJointIndex);

				AnimationCore::SolveTwoBoneIK(StartJointIKTransform, MidJointIKTransform, EndJointIKTransform, PoleTarget, EndEffector.GetLocation(), UpperLimbLength, LowerLimbLength, false, 1.0f, 1.05f);
				EndJointIKTransform.SetRotation(EndEffector.GetRotation());

				StartJointTransform = StartJointIKTransform;
				MidJointTransform = MidJointIKTransform;
				EndJointTransform = EndJointIKTransform;
			}
			else
			{
				// update transform before going through IK
				const FRigBoneHierarchy* Hierarchy = Context.GetBones();
				check(Hierarchy);

				StartJointIKTransform = Hierarchy->GetGlobalTransform(StartJointIndex);
				MidJointIKTransform = Hierarchy->GetGlobalTransform(MidJointIndex);
				EndJointIKTransform = Hierarchy->GetGlobalTransform(EndJointIndex);

				AnimationCore::SolveTwoBoneIK(StartJointIKTransform, MidJointIKTransform, EndJointIKTransform, PoleTarget, EndEffector.GetLocation(), UpperLimbLength, LowerLimbLength, false, 1.0f, 1.05f);
				EndJointIKTransform.SetRotation(EndEffector.GetRotation());

				StartJointTransform.Blend(StartJointFKTransform, StartJointIKTransform, IKBlend);
				MidJointTransform.Blend(MidJointFKTransform, MidJointIKTransform, IKBlend);
				EndJointTransform.Blend(MidJointFKTransform, EndJointIKTransform, IKBlend);
			}

			FRigBoneHierarchy* Hierarchy = ExecuteContext.GetBones();
			check(Hierarchy);
			Hierarchy->SetGlobalTransform(StartJointIndex, StartJointTransform);
			Hierarchy->SetGlobalTransform(MidJointIndex, MidJointTransform);
			Hierarchy->SetGlobalTransform(EndJointIndex, EndJointTransform);

			PreviousFKIKBlend = IKBlend;
		}
	}
}

