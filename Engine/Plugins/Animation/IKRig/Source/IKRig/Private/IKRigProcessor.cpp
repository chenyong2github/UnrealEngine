// Copyright Epic Games, Inc. All Rights Reserved.

#include "IKRigProcessor.h"
#include "IKRigDefinition.h"
#include "IKRigSolver.h"
#include "Solvers/ConstraintSolver.h"


void UIKRigProcessor::Initialize(UIKRigDefinition* InRigDefinition)
{
	bInitialized = false;

	RigDefinition = InRigDefinition;
	if (!RigDefinition)
	{
		return;
	}
	
	RefPoseGlobalTransforms = RigDefinition->RefPoseTransforms;
	GlobalBoneTransforms = FIKRigTransforms(&RigDefinition->Hierarchy);
	GlobalBoneTransforms.SetAllGlobalTransforms(RefPoseGlobalTransforms);

	TArray<FName> GoalNames;
	RigDefinition->GetGoalNamesFromSolvers(GoalNames);
	Goals.InitializeGoalsFromNames(GoalNames);

	TArray<UIKRigSolver*> RigSolvers = RigDefinition->Solvers;
	const int32 NumSolvers = RigSolvers.Num();
	Solvers.Reset(NumSolvers);
	for (int32 Index = 0; Index < NumSolvers; ++Index)
	{
		if (!RigSolvers[Index])
		{
			continue;
		}
		
		UIKRigSolver* Solver = DuplicateObject(RigSolvers[Index], this);
		Solver->Init(GlobalBoneTransforms);
		Solvers.Add(Solver);
	}

	bInitialized = true;
}

void UIKRigProcessor::Solve()
{
	if (!ensure(bInitialized))
	{
		return;
	}

	check (RigDefinition);

	DrawInterface.Reset();

	for (int32 Index = 0; Index < Solvers.Num(); ++Index)
	{
		Solvers[Index]->Solve(GlobalBoneTransforms, Goals, &DrawInterface);
	}
}

FIKRigTransforms& UIKRigProcessor::GetCurrentGlobalTransforms() 
{
	check(bInitialized);
	return GlobalBoneTransforms;
}

void UIKRigProcessor::SetGoalTransform(
	const FName& GoalName,
	const FVector& Position,
	const FQuat& Rotation)
{
	check(bInitialized);
	Goals.SetGoalTransform(GoalName, Position, Rotation);
}

void UIKRigProcessor::GetGoalNames(TArray<FName>& OutGoalNames) const
{
	check(bInitialized);
	return Goals.GetNames(OutGoalNames);
}

int UIKRigProcessor::GetNumGoals() const
{
	return Goals.GetNumGoals();
}

const FIKRigHierarchy* UIKRigProcessor::GetHierarchy() const
{
	check(bInitialized);
	return ensure(RigDefinition) ? &RigDefinition->Hierarchy : nullptr;
}

void UIKRigProcessor::ResetToRefPose()
{
	check(bInitialized);
	GlobalBoneTransforms.SetAllGlobalTransforms(RefPoseGlobalTransforms);
}