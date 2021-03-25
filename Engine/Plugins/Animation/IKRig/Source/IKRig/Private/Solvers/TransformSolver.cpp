// Copyright Epic Games, Inc. All Rights Reserved.


#include "Solvers/TransformSolver.h"
#include "IKRigDataTypes.h"


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
	FIKRigGoal Goal;
	if (!GetGoalForEffector(Effector, Goals, Goal))
	{
		return;
	}

	FTransform& CurrentTransform = IKRigSkeleton.CurrentPoseGlobal[BoneIndex];

	if (bEnablePosition)
	{
		CurrentTransform.SetLocation(Goal.Position);
	}
	
	if (bEnableRotation)
	{
		CurrentTransform.SetRotation(Goal.Rotation);
	}

	IKRigSkeleton.PropagateGlobalPoseBelowBone(BoneIndex);
}

void UTransformSolver::CollectGoalNames(TSet<FName>& OutGoals) const
{
	OutGoals.Add(Effector.Goal);
}
