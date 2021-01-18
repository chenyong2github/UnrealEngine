// Copyright Epic Games, Inc. All Rights Reserved.


#include "Solvers/TransformSolver.h"
#include "IKRigDataTypes.h"
#include "IKRigHierarchy.h"


UTransformSolver::UTransformSolver()
	: TransformTargetName(TEXT("TransformTarget"))
{
}

void UTransformSolver::Init(const FIKRigTransforms& InGlobalTransform)
{
	
}

bool UTransformSolver::IsSolverActive() const
{
	return Super::IsSolverActive() && (bEnablePosition || bEnableRotation);
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
		CurrentTransform.SetRotation(Goal.Rotation.Quaternion());
	}

	InOutGlobalTransform.SetGlobalTransform(Index, CurrentTransform, true);
}

#if WITH_EDITOR

void UTransformSolver::UpdateEffectors()
{
	// KIARAN - this doesn't make any sense, why would we remove an effector just because
	// the solver settings have been toggled off?

	// update tasks - you want to keep old name though if changed
	bool bActiveTask = bEnablePosition || bEnableRotation;

	// not found, we add new
	if (bActiveTask)
	{
		EnsureToAddEffector(Effector, *TransformTargetName);
	}
	// if the opposite, we have to remove
	else if (!bActiveTask)
	{
		EnsureToRemoveEffector(Effector);
	}

	// trigger a delegate for goal has been updated?
	OnGoalHasBeenUpdated();
}

void UTransformSolver::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UTransformSolver, bEnablePosition) ||
		PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UTransformSolver, bEnableRotation))
	{
		UpdateEffectors();
	}

	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}

#endif// WITH_EDITOR