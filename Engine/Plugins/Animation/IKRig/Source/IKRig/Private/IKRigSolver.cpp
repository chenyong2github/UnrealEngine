// Copyright Epic Games, Inc. All Rights Reserved.

#include "IKRigSolver.h"


void UIKRigSolver::AddGoalToArrayNoDuplicates(const FIKRigEffectorGoal& GoalToAdd, TArray<FIKRigEffectorGoal>& OutGoals)
{
	// need to treat OutGoals like an TSet (with no duplicates) but ordered so users can index into it
	for (const FIKRigEffectorGoal& EffectorGoal : OutGoals)
	{
		if (EffectorGoal.Goal == GoalToAdd.Goal)
		{
			return;
		}
	}
	
	OutGoals.Add(GoalToAdd);
}
