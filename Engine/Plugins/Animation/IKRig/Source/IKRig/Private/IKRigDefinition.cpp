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

TArray<FIKRigEffectorGoal>& UIKRigDefinition::GetEffectorGoals()
{
	UpdateGoalNameArray();
	return EffectorGoals;
}

#if WITH_EDITOR
void UIKRigDefinition::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// update list of goal names whenever a solver is modified
	const FName PropertyName = (PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None);
	if ((PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UIKRigDefinition, Solvers)))
	{
		bEffectorGoalsDirty = true;
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

FName UIKRigDefinition::GetBoneNameForGoal(const FName& GoalName)
{
	UpdateGoalNameArray();
	
	for (const FIKRigEffectorGoal& EffectorGoal : EffectorGoals)
	{
		if (EffectorGoal.Goal == GoalName)
		{
			return EffectorGoal.Bone;
		}
	}
	
	return NAME_None;
}

FName UIKRigDefinition::GetGoalName(int32 GoalIndex)
{
	UpdateGoalNameArray();
	
	if (!EffectorGoals.IsValidIndex(GoalIndex))
	{
		return NAME_None;
	}

	return EffectorGoals[GoalIndex].Goal;
}

void UIKRigDefinition::UpdateGoalNameArray()
{
	if (!bEffectorGoalsDirty)
	{
		return;	
	}
	
	EffectorGoals.Reset();
	for (UIKRigSolver* Solver : Solvers)
	{
		if (Solver)
		{
			Solver->AddGoalsInSolver(EffectorGoals);
		}
	}

	bEffectorGoalsDirty = false;
}

