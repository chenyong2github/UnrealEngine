// Copyright Epic Games, Inc. All Rights Reserved.

#include "IKRigDefinition.h"
#include "IKRigSolver.h"


FBoneChain* FRetargetDefinition::GetBoneChainByName(FName ChainName)
{
	for (FBoneChain& Chain : BoneChains)
	{
		if (Chain.ChainName == ChainName)
		{
			return &Chain;
		}
	}
	
	return nullptr;
}

void UIKRigDefinition::GetGoalNamesFromSolvers(TArray<FIKRigEffectorGoal>& OutGoalNames) const
{
	TSet<FIKRigEffectorGoal> GoalNames;
	for (UIKRigSolver* Solver : Solvers)
	{
		if (Solver)
		{
			Solver->CollectGoalNames(GoalNames);
		}
	}

	// user code needs to use indices, so we bake set into an array
	OutGoalNames = GoalNames.Array();
}

FName UIKRigDefinition::GetBoneNameForGoal(FName GoalName) const
{
	TArray<FIKRigEffectorGoal> AllGoalNames;
	GetGoalNamesFromSolvers(AllGoalNames);
	for (const FIKRigEffectorGoal& Names : AllGoalNames)
	{
		if (Names.Goal == GoalName)
		{
			return Names.Bone;
		}
	}

	return NAME_None;
}
