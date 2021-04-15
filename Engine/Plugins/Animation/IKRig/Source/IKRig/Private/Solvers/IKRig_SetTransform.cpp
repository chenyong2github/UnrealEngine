// Copyright Epic Games, Inc. All Rights Reserved.

#include "Solvers/IKRig_SetTransform.h"
#include "IKRigDataTypes.h"
#include "IKRigSkeleton.h"


void UIKRig_SetTransform::Initialize(const FIKRigSkeleton& IKRigSkeleton)
{
	BoneIndex = IKRigSkeleton.GetBoneIndexFromName(Effector.Bone);
}

void UIKRig_SetTransform::Solve(
	FIKRigSkeleton& IKRigSkeleton,
	const FIKRigGoalContainer& Goals,
	FControlRigDrawInterface* InOutDrawInterface)
{
	FIKRigGoal OutGoal;
	if (!Goals.GetGoalByName(Effector.Goal, OutGoal))
	{
		return;
	}

	FTransform& CurrentTransform = IKRigSkeleton.CurrentPoseGlobal[BoneIndex];

	if (bEnablePosition)
	{
		CurrentTransform.SetLocation(OutGoal.FinalBlendedPosition);
	}
	
	if (bEnableRotation)
	{
		CurrentTransform.SetRotation(OutGoal.FinalBlendedRotation);
	}

	IKRigSkeleton.PropagateGlobalPoseBelowBone(BoneIndex);
}

void UIKRig_SetTransform::AddGoalsInSolver(TArray<FIKRigEffectorGoal>& OutGoals) const
{
	AddGoalToArrayNoDuplicates(Effector,OutGoals);
}
