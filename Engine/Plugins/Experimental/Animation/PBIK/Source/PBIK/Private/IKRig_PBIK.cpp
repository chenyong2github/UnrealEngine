// Copyright Epic Games, Inc. All Rights Reserved.

#include "IKRig_PBIK.h"


void UIKRig_PBIK::Init(const FIKRigTransforms& InGlobalTransforms)
{
	/*
	// check how many effectors are assigned to a bone
	int NumEffectors = 0;
	for (const FPBIKRigEffector& Effector : Effectors)
	{
		if (Bones.GetIndex(Effector.Bone) != -1)
		{
			++NumEffectors; // bone is set and exists!
		}
	}

	// validate inputs are ready to be initialized
	bool bHasEffectors = NumEffectors > 0;
	bool bRootIsAssigned = Root != NAME_None;
	if (!(bHasEffectors && bRootIsAssigned))
	{
		return; // not setup yet
	}

	// reset all internal data
	Solver.Reset();

	// create bones
	for (int B = 0; B < Bones.Num(); ++B)
	{
		FName Name = Bones[B].Name;
		int ParentIndex = Bones[B].ParentIndex;
		FVector InOrigPosition = Bones[B].InitialTransform.GetLocation();
		FQuat InOrigRotation = Bones[B].InitialTransform.GetRotation();
		bool bIsRoot = Bones[B].Name == Root;
		Solver.AddBone(Name, ParentIndex, InOrigPosition, InOrigRotation, bIsRoot);
	}

	// create effectors
	EffectorSolverIndices.Reset();
	for (const FPBIKEffector& Effector : Effectors)
	{
		int32 IndexInSolver = Solver.AddEffector(Effector.Bone);
		EffectorSolverIndices.Add(IndexInSolver);
	}

	// initialize
	Solver.Initialize();

	return; // don't update during init
	*/
}

void UIKRig_PBIK::Solve(FIKRigTransforms& InOutGlobalTransforms, const FIKRigGoalContainer& Goals, FControlRigDrawInterface* InOutDrawInterface)
{

}

void UIKRig_PBIK::CollectGoalNames(TSet<FName>& OutGoals) const
{

}

