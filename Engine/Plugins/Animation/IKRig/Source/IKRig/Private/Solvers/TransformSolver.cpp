// Copyright Epic Games, Inc. All Rights Reserved.


#include "Solvers/TransformSolver.h"
#include "IKRigDataTypes.h"
#include "IKRigHierarchy.h"


UTransformSolver::UTransformSolver()
{
	Effector.Goal = "DefaultGoal";
}

void UTransformSolver::Init(const FIKRigTransforms& InGlobalTransform)
{
	
}

void UTransformSolver::Solve(
	FIKRigTransforms& InOutGlobalTransform,
	const FIKRigGoalContainer& Goals,
	FControlRigDrawInterface* InOutDrawInterface)
{
	FIKRigGoal Goal;
	if (!GetGoalForEffector(Effector, Goals, Goal))
	{
		return;
	}
	
	int32 Index = InOutGlobalTransform.Hierarchy->GetIndex(Effector.Bone);
	if (Index == INDEX_NONE)
	{
		return;
	}

	FTransform CurrentTransform = InOutGlobalTransform.GetGlobalTransform(Index);

	if (bEnablePosition)
	{
		CurrentTransform.SetLocation(Goal.Position);
	}
	if (bEnableRotation)
	{
		CurrentTransform.SetRotation(Goal.Rotation);
	}

	InOutGlobalTransform.SetGlobalTransform(Index, CurrentTransform, true);
}

void UTransformSolver::CollectGoalNames(TSet<FName>& OutGoals) const
{
	OutGoals.Add(Effector.Goal);
}
