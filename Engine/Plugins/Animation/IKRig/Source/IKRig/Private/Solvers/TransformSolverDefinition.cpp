// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Contains TransformSolverDefinition Implementation
 *
 */

#include "Solvers/TransformSolverDefinition.h"
#include "TransformSolver.h"

UTransformSolverDefinition::UTransformSolverDefinition()
	: TransformTargetName(TEXT("TransformTarget"))
{
	DisplayName = TEXT("Transform Solver");
	ExecutionClass = UTransformSolver::StaticClass();
}

#if WITH_EDITOR
void UTransformSolverDefinition::UpdateEffectors()
{
	// update tasks - you want to keep old name though if changed
	bool bActiveTask = bEnablePosition || bEnableRotation;

	// not found, we add new
	if (bActiveTask)
	{
		EnsureToAddEffector(TransformTarget, *TransformTargetName);
	}
	// if the opposite, we have to remove
	else if (!bActiveTask)
	{
		EnsureToRemoveEffector(TransformTarget);
	}

	// trigger a delegate for goal has been updated?
	OnGoalHasBeenUpdated();
}

void UTransformSolverDefinition::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UTransformSolverDefinition, bEnablePosition) ||
		PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UTransformSolverDefinition, bEnableRotation))
	{
		UpdateEffectors();
	}

	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}

#endif// WITH_EDITOR