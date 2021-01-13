// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Contains IKRigProcessor - Runtime Implementation
 *
 */

#include "IKRigProcessor.h"
#include "IKRigDefinition.h"
#include "IKRigSolver.h"
#include "Solvers/ConstraintSolver.h"

 // IKRigProcessor implementation functions
void UIKRigProcessor::SetIKRigDefinition(UIKRigDefinition* InRigDefinition, bool bInitialize /*= false*/)
{
	if (RigDefinition)
	{
		Terminate();
	}

	RigDefinition = InRigDefinition;

	if (RigDefinition && bInitialize)
	{
		Transforms = FIKRigTransforms(&RigDefinition->GetHierarchy());
		Initialize(RigDefinition->GetReferencePose());
	}
}

void UIKRigProcessor::Initialize(const TArray<FTransform>& InRefPoseTransforms)
{
	if (RigDefinition)
	{
		RefPoseTransforms = InRefPoseTransforms;
		Reinitialize();
	}
}

void UIKRigProcessor::Reinitialize()
{
	Transforms.SetAllGlobalTransforms(RefPoseTransforms);
	IKGoals = RigDefinition->GetGoals();
	UIKRigSolver::FIKRigTransformGetter RefTransformGetter = UIKRigSolver::FIKRigTransformGetter::CreateUObject(this, &UIKRigProcessor::GetRefPoseGetter);
	UIKRigSolver::FIKRigGoalGetter GoalGetter = UIKRigSolver::FIKRigGoalGetter::CreateUObject(this, &UIKRigProcessor::GoalGetter);

	TArray<UIKRigSolver*> RigSolvers = RigDefinition->GetSolvers();
	const int32 NumSolvers = RigSolvers.Num();
	Solvers.Reset(NumSolvers);

	for (int32 Index = 0; Index < NumSolvers; ++Index)
	{
		TSubclassOf<UIKRigSolver> ClassType = RigSolvers[Index]->GetClass();
		if (ClassType != nullptr)
		{
			UIKRigSolver* NewSolver = DuplicateObject(RigSolvers[Index], this);
			NewSolver->Init(Transforms, RefTransformGetter, GoalGetter);
			Solvers.Add(NewSolver);
		}
	}

	bInitialized = true;
}

const TArray<FTransform>& UIKRigProcessor::GetRefPoseGetter()
{
	return RefPoseTransforms;
}

bool UIKRigProcessor::GoalGetter(const FName& InGoalName, FIKRigTarget& OutTarget)
{
	FIKRigTarget* Target = FindGoal(InGoalName);
	if (Target)
	{
		OutTarget = *Target;

		return true;
	}

	return false;
}

void UIKRigProcessor::Terminate()
{
	bInitialized = false;
}

void UIKRigProcessor::Solve()
{
	if (bInitialized)
	{
		check (RigDefinition);

		DrawInterface.Reset();
		for (int32 Index = 0; Index < Solvers.Num(); ++Index)
		{
			// Draw interface is pointer in case we want to nullify for optimization
			Solvers[Index]->Solve(Transforms, &DrawInterface);
		}
	}
}

// update goal functions
void UIKRigProcessor::SetGoalPosition(const FName& GoalName, const FVector& InPosition)
{
	FIKRigTarget* Target = FindGoal(GoalName);
	if (Target)
	{
		Target->PositionTarget.Position = InPosition;
	}
}

void UIKRigProcessor::SetGoalRotation(const FName& GoalName, const FRotator& InRotation)
{
	FIKRigTarget* Target = FindGoal(GoalName);
	if (Target)
	{
		Target->RotationTarget.Rotation = InRotation;
	}
}

void UIKRigProcessor::SetGoalTarget(const FName& GoalName, const FIKRigTarget& InTarget)
{
	FIKRigTarget* Target = FindGoal(GoalName);
	if (Target)
	{
		*Target = InTarget;
	}
}

FIKRigTarget* UIKRigProcessor::FindGoal(const FName& GoalName)
{
	FIKRigGoal* RigGoal = IKGoals.Find(GoalName);
	if (RigGoal)
	{
		return &RigGoal->Target;
	}

	return nullptr;
}

void UIKRigProcessor::GetGoals(TArray<FName>& OutNames) const
{
	IKGoals.GenerateKeyArray(OutNames);
}

FIKRigTransforms& UIKRigProcessor::GetIKRigTransformModifier() 
{
	return Transforms;
}

const FIKRigHierarchy* UIKRigProcessor::GetHierarchy() const
{
	if (RigDefinition)
	{
		return &RigDefinition->GetHierarchy();
	}

	return nullptr;
}

void UIKRigProcessor::ResetToRefPose()
{
	Transforms.SetAllGlobalTransforms(RefPoseTransforms);
}