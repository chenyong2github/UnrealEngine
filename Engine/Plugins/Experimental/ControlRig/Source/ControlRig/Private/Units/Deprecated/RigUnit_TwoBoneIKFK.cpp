// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Deprecated/RigUnit_TwoBoneIKFK.h"
#include "Units/RigUnitContext.h"
#include "HelperUtil.h"
#include "TwoBoneIK.h"

void FRigUnit_TwoBoneIKFK::Execute(const FRigUnitContext& Context)
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
						FTransform StartTransform = Hierarchy->GetInitialTransform(StartJointIndex);
						FTransform MidTransform = Hierarchy->GetInitialTransform(MidJointIndex);
						FTransform EndTransform = Hierarchy->GetInitialTransform(EndJointIndex);

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
		else
		{
			UnitLogHelpers::PrintMissingHierarchy(RigUnitName);
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

				SolveIK();

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

				SolveIK();

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

void FRigUnit_TwoBoneIKFK::SolveIK()
{
	AnimationCore::SolveTwoBoneIK(StartJointIKTransform, MidJointIKTransform, EndJointIKTransform, PoleTarget, EndEffector.GetLocation(), UpperLimbLength, LowerLimbLength, false, 1.0f, 1.05f);
	// set end effector rotation to current rotation
	EndJointIKTransform.SetRotation(EndEffector.GetRotation());
}

