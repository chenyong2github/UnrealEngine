// Copyright Epic Games, Inc. All Rights Reserved.


#include "Solvers/TransformSolver.h"
#include "IKRigDataTypes.h"
#include "IKRigSkeleton.h"

UTransformSolver::UTransformSolver()
{
	Effector.Goal = "DefaultGoal";
}

void UTransformSolver::Initialize(const FIKRigSkeleton& IKRigSkeleton)
{
	BoneIndex = IKRigSkeleton.GetBoneIndexFromName(Effector.Bone);
}

void UTransformSolver::Solve(
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
		CurrentTransform.SetLocation(OutGoal.Position);
	}
	
	if (bEnableRotation)
	{
		CurrentTransform.SetRotation(OutGoal.Rotation.Quaternion());
	}

	IKRigSkeleton.PropagateGlobalPoseBelowBone(BoneIndex);
}

void UTransformSolver::CollectGoalNames(TSet<FIKRigEffectorGoal>& OutGoals) const
{
	OutGoals.Add(Effector);
}
