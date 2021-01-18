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
	
	RefPoseTransforms = RigDefinition->GetReferencePose();
	Transforms = FIKRigTransforms(&RigDefinition->GetHierarchy());
	Transforms.SetAllGlobalTransforms(RefPoseTransforms);
	
	Goals.SetAllGoals(RigDefinition->GetGoals());

	TArray<UIKRigSolver*> RigSolvers = RigDefinition->GetSolvers();
	const int32 NumSolvers = RigSolvers.Num();
	Solvers.Reset(NumSolvers);
	for (int32 Index = 0; Index < NumSolvers; ++Index)
	{
		UIKRigSolver* Solver = DuplicateObject(RigSolvers[Index], this);
		Solver->Init(Transforms);
		Solvers.Add(Solver);
	}

	bInitialized = true;
}

void UIKRigProcessor::Solve()
{
	if (!bInitialized)
	{
		return;
	}

	check (RigDefinition);

	DrawInterface.Reset();

	for (int32 Index = 0; Index < Solvers.Num(); ++Index)
	{
		Solvers[Index]->Solve(Transforms, Goals, &DrawInterface);
	}
}

FIKRigTransforms& UIKRigProcessor::GetTransforms() 
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