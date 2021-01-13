// Copyright Epic Games, Inc. All Rights Reserved.


#include "Solvers/TransformSolver.h"
#include "IKRigDataTypes.h"
#include "IKRigHierarchy.h"


UTransformSolver::UTransformSolver()
	: TransformTargetName(TEXT("TransformTarget"))
{
}

void UTransformSolver::InitInternal(const FIKRigTransforms& InGlobalTransform)
{
	
}

bool UTransformSolver::IsSolverActive() const
{
	return Super::IsSolverActive() && (bEnablePosition || bEnableRotation);
}

void UTransformSolver::SolveInternal(FIKRigTransforms& InOutGlobalTransform, FControlRigDrawInterface* InOutDrawInterface)
{
	FIKRigTarget Target;
	if (!GetEffectorTarget(TransformTarget, Target))
	{
		return;
	}
	
	int32 Index = InOutGlobalTransform.Hierarchy->GetIndex(TransformTarget.Bone);
	if (Index == INDEX_NONE)
	{
		return;
	}

	FTransform CurrentTransform = InOutGlobalTransform.GetGlobalTransform(Index);

	if (bEnablePosition)
	{
		CurrentTransform.SetLocation(Target.PositionTarget.Position);
	}
	if (bEnableRotation)
	{
		CurrentTransform.SetRotation(Target.RotationTarget.Rotation.Quaternion());
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