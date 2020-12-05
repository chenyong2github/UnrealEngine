// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Contains FullBodyIKSolverDefinition Implementation
 *
 */

#include "FullBodyIKSolverDefinition.h"
#include "FullBodyIKSolver.h"

const FName UFullBodyIKSolverDefinition::EffectorTargetPrefix = FName(TEXT("FullBodyIKTarget"));

UFullBodyIKSolverDefinition::UFullBodyIKSolverDefinition()
{
	DisplayName = TEXT("FullBodyIK Solver");
	ExecutionClass = UFullBodyIKSolver::StaticClass();
}

#if WITH_EDITOR
void UFullBodyIKSolverDefinition::UpdateEffectors()
{
	// first ensure we add all the effectors
	for (int32 Index = 0; Index < Effectors.Num(); ++Index)
	{
		EnsureToAddEffector(Effectors[Index].Target, TEXT("FBIK_Effector"));
	}

	// we have more nodes than goals
	// which means we have something deleted
	if (Effectors.Num() < EffectorToGoal.Num())
	{
		TArray<FIKRigEffector> GoalEffectors;
		// we have to remove things that don't belong
		for (auto Iter = EffectorToGoal.CreateIterator(); Iter; ++Iter)
		{
			GoalEffectors.Add(Iter.Key());
		}

		TBitArray<> RemoveFlags(false, GoalEffectors.Num());

		for (int32 Index = 0; Index < Effectors.Num(); ++Index)
		{
			int32 Found = GoalEffectors.Find(Effectors[Index].Target);

			if (Found != INDEX_NONE)
			{
				RemoveFlags[Found] = true;
			}
		}

		for (TConstSetBitIterator<> Iter(RemoveFlags); Iter; ++Iter)
		{
			// remove things that don't belong
			EffectorToGoal.Remove(GoalEffectors[Iter.GetIndex()]);
		}
	}

 	// trigger a delegate for goal has been updated?
 	OnGoalHasBeenUpdated();
}

void UFullBodyIKSolverDefinition::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UFullBodyIKSolverDefinition, Effectors))
	{
		UpdateEffectors();
	}

	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}

#endif// WITH_EDITOR