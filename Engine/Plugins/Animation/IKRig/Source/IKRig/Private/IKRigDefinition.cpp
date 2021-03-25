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

void UIKRigDefinition::GetGoalNamesFromSolvers(TArray<FName>& OutGoalNames) const
{
	for (UIKRigSolver* Solver : Solvers)
	{
		if (!Solver)
		{
			continue;
		}
		
		TSet<FName> GoalNames;
		Solver->CollectGoalNames(GoalNames);

		// not using a TSet here because user code relies on indices
		for (FName Name : GoalNames)
		{
			if (!OutGoalNames.Contains(Name))
			{
				OutGoalNames.Add(Name);
			}
		}
	}
}