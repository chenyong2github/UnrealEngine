// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Contains TransformSolverDefinition Implementation
 *
 */

#include "Solvers/TransformSolverDefinition.h"
#include "TransformSolver.h"

const FName UTransformSolverDefinition::TransformTarget = FName(TEXT("TransformTarget"));

UTransformSolverDefinition::UTransformSolverDefinition()
{
	DisplayName = TEXT("Transform Solver");
	ExecutionClass = UTransformSolver::StaticClass();
}

#if WITH_EDITOR
void UTransformSolverDefinition::UpdateTaskList()
{
	// update tasks - you want to keep old name though if changed
	FName* TaskFound= TaskToGoal.Find(TransformTarget);
	bool bActiveTask = bEnablePosition || bEnableRotation;

	// not found, we add new
	if (bActiveTask && !TaskFound)
	{
		TaskToGoal.Add(TransformTarget) = CreateUniqueGoalName(*TransformTarget.ToString());
	}
	// if the opposite, we have to remove
	else if (!bActiveTask && TaskFound)
	{
		TaskToGoal.FindAndRemoveChecked(TransformTarget);
	}

	// trigger a delegate for goal has been updated?
	OnGoalHasBeenUpdated();
}

void UTransformSolverDefinition::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UTransformSolverDefinition, bEnablePosition) ||
		PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UTransformSolverDefinition, bEnableRotation))
	{
		UpdateTaskList();
	}
}

#endif// WITH_EDITOR