// Copyright Epic Games, Inc. All Rights Reserved.

#include "IKRigDataTypes.h"
#include "IKRigSolver.h"

void FIKRigGoalContainer::InitializeGoalsFromNames(const TArray<FIKRigEffectorGoal>& InGoalNames)
{
	Goals.Empty(InGoalNames.Num());
	for (const FIKRigEffectorGoal& GoalNames : InGoalNames)
	{
		Goals.Emplace(GoalNames.Goal, GoalNames.Goal);
	}
}

void FIKRigGoalContainer::SetIKGoal(const FIKRigGoal& InGoal)
{
	Goals.Add(InGoal.Name, InGoal);
}

bool FIKRigGoalContainer::GetGoalByName(const FName& InGoalName, FIKRigGoal& OutGoal) const
{
	if (const FIKRigGoal* Goal = Goals.Find(InGoalName))
	{
		OutGoal = *Goal;
		return true;
	}

	return false;
}