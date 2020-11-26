// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Contains IKRigProcessor - Runtime Implementation
 *
 */

#include "IKRigProcessor.h"
#include "IKRigDefinition.h"
#include "IKRigSolverDefinition.h"
#include "IKRigSolver.h"

 // IKRigProcessor implementation functions
void UIKRigProcessor::SetIKRigDefinition(UIKRigDefinition* InRigDefinition, bool bInitialize /*= false*/)
{
	if (RigDefinition)
	{
		Terminate();
	}

	RigDefinition = InRigDefinition;

	TransformModifier = FIKRigTransformModifier(&RigDefinition->GetHierarchy());

	if (RigDefinition && bInitialize)
	{
		Initialize(RigDefinition->GetReferencePose());
	}
}

void UIKRigProcessor::Initialize(const FIKRigTransform& InRefTransform)
{
	if (RigDefinition)
	{
		ReferenceTransform = InRefTransform;
		Reinitialize();
	}
}

void UIKRigProcessor::Reinitialize()
{
	TransformModifier.ResetGlobalTransform(ReferenceTransform);

	IKGoals = RigDefinition->GetGoals();

	// i'm copying this here 
	TArray<UIKRigSolverDefinition*> SolverDefinitions = RigDefinition->GetSolverDefinitions();

	const int32 NumSolvers = SolverDefinitions.Num();
	Solvers.Reset(NumSolvers);
	for (int32 Index = 0; Index < NumSolvers; ++Index)
	{
		// it's possible SolverDefinitions.Num() != Solvers.Num()
		if (SolverDefinitions[Index])
		{
			TSubclassOf<UIKRigSolver> ClassType = SolverDefinitions[Index]->GetExecutionClass();
			if (ClassType != nullptr)
			{
				UIKRigSolver* NewSolver = NewObject<UIKRigSolver>(this, ClassType);
				NewSolver->Init(SolverDefinitions[Index], UIKRigSolver::FIKRigTransformGetter::CreateUObject(this, &UIKRigProcessor::GetRefPoseGetter), 
					UIKRigSolver::FIKRigGoalGetter::CreateUObject(this, &UIKRigProcessor::GoalGetter));
				Solvers.Add(NewSolver);
			}
		}
	}

	bInitialized = true;
}

const FIKRigTransform& UIKRigProcessor::GetRefPoseGetter() 
{
	return ReferenceTransform;
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

		for (int32 Index = 0; Index < Solvers.Num(); ++Index)
		{
			Solvers[Index]->Solve(TransformModifier);
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

FIKRigTransformModifier& UIKRigProcessor::GetIKRigTransformModifier() 
{
	return TransformModifier;
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
	TransformModifier.ResetGlobalTransform(ReferenceTransform);
}