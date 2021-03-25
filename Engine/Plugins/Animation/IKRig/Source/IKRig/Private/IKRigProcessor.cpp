// Copyright Epic Games, Inc. All Rights Reserved.

#include "IKRigProcessor.h"
#include "IKRigDefinition.h"
#include "IKRigSolver.h"


UIKRigProcessor* UIKRigProcessor::MakeNewIKRigProcessor(UObject* Outer)
{
	return NewObject<UIKRigProcessor>(Outer);
}

void UIKRigProcessor::Initialize(UIKRigDefinition* InRigDefinition)
{
	bInitialized = false;

	if (!ensureMsgf(InRigDefinition, TEXT("Trying to initialize IKRigProcessor with a null IKRigDefinition asset.")))
	{
		return;
	}

	// copy skeleton data from IKRigDefinition
	Skeleton = InRigDefinition->Skeleton; // trivial copy assignment operator for POD

	// initialize goal names based on solvers
	TArray<FName> GoalNames;
	InRigDefinition->GetGoalNamesFromSolvers(GoalNames);
	Goals.InitializeGoalsFromNames(GoalNames);

	// create copies of all the solvers in the IK rig
	Solvers.Reset(InRigDefinition->Solvers.Num());
	for (UIKRigSolver* IKRigSolver : InRigDefinition->Solvers)
	{
		if (!IKRigSolver)
		{
			// this can happen if asset references deleted IK Solver type
			// which should only happen during development (if at all)
			continue;
		}
		
		UIKRigSolver* Solver = DuplicateObject(IKRigSolver, this);
		Solver->Initialize(Skeleton);
		Solvers.Add(Solver);
	}

	bInitialized = true;
}

void UIKRigProcessor::SetInputPoseGlobal(const TArray<FTransform>& InGlobalBoneTransforms) 
{
	check(bInitialized);
	check(InGlobalBoneTransforms.Num() == Skeleton.CurrentPoseGlobal.Num());
	Skeleton.CurrentPoseGlobal = InGlobalBoneTransforms;
	Skeleton.UpdateAllLocalTransformFromGlobal();
}

void UIKRigProcessor::SetInputPoseToRefPose()
{
	check(bInitialized);
	Skeleton.CurrentPoseGlobal = Skeleton.RefPoseGlobal;
	Skeleton.UpdateAllLocalTransformFromGlobal();
}

void UIKRigProcessor::SetGoalTransform(
    const FName& GoalName,
    const FVector& Position,
    const FQuat& Rotation)
{
	check(bInitialized);
	Goals.SetGoalTransform(GoalName, Position, Rotation);
}

void UIKRigProcessor::Solve()
{
	check(bInitialized);
	DrawInterface.Reset();
	for (UIKRigSolver* Solver : Solvers)
	{
		Solver->Solve(Skeleton, Goals, &DrawInterface);
	}
}

void UIKRigProcessor::CopyOutputGlobalPoseToArray(TArray<FTransform>& OutputPoseGlobal) const
{
	check(bInitialized);
	OutputPoseGlobal = Skeleton.CurrentPoseGlobal;
}

void UIKRigProcessor::GetGoalNames(TArray<FName>& OutGoalNames) const
{
	check(bInitialized);
	return Goals.GetNames(OutGoalNames);
}

int UIKRigProcessor::GetNumGoals() const
{
	check(bInitialized);
	return Goals.GetNumGoals();
}

FIKRigSkeleton& UIKRigProcessor::GetSkeleton()
{
	check(bInitialized);
	return Skeleton;
}
