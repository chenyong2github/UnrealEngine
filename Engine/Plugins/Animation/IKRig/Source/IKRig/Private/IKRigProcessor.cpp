// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Contains IKRigProcessor - Runtime Implementation
 *
 */

#include "IKRigProcessor.h"
#include "IKRigDefinition.h"
#include "IKRigSolverDefinition.h"
#include "IKRigSolver.h"
#include "IKRigConstraintSolver.h"

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
		TransformModifier = FIKRigTransformModifier(&RigDefinition->GetHierarchy());
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

	UIKRigSolver::FIKRigTransformGetter RefTransformGetter = UIKRigSolver::FIKRigTransformGetter::CreateUObject(this, &UIKRigProcessor::GetRefPoseGetter);
	UIKRigSolver::FIKRigGoalGetter GoalGetter = UIKRigSolver::FIKRigGoalGetter::CreateUObject(this, &UIKRigProcessor::GoalGetter);

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
				// const transform modifier for hierarchy query
				NewSolver->Init(SolverDefinitions[Index], TransformModifier, RefTransformGetter, GoalGetter);
				Solvers.Add(NewSolver);
			}
		}
	}

	// initialize constraint solver
	CostraintSolver = NewObject<UIKRigConstraintSolver>(this);
	check (CostraintSolver);
	CostraintSolver->SetConstraintDefinition(RigDefinition->ConstraintDefinitions);
	CostraintSolver->Init(RigDefinition->ConstraintDefinitions, TransformModifier, RefTransformGetter, GoalGetter);

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

		DrawInterface.Reset();
		for (int32 Index = 0; Index < Solvers.Num(); ++Index)
		{
			// Draw interface is pointer in case we want to nullify for optimization
			Solvers[Index]->Solve(TransformModifier, &DrawInterface);
		}

		// solve constraint
		CostraintSolver->Solve(TransformModifier, &DrawInterface);
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