// Copyright Epic Games, Inc. All Rights Reserved.

#include "Solvers/IKRig_PBIKSolver.h"

#include "IKRigDataTypes.h"
#include "IKRigSkeleton.h"

void UIKRigPBIKSolver::Initialize(const FIKRigSkeleton& InSkeleton)
{	
	// check how many effectors are assigned to a bone
	int NumEffectors = 0;
	for (const FIKRig_PBIKEffector& Effector : Effectors)
	{
		if (InSkeleton.GetBoneIndexFromName(Effector.Goal.Bone) != INDEX_NONE)
		{
			++NumEffectors; // bone is set and exists!
		}
	}

	// validate inputs are ready to be initialized
	const bool bHasEffectors = NumEffectors > 0;
	const bool bRootIsAssigned = RootBone != NAME_None;
	if (!(bHasEffectors && bRootIsAssigned))
	{
		return; // not setup yet
	}

	// reset all internal data
	Solver.Reset();

	// create bones
	for (int BoneIndex = 0; BoneIndex < InSkeleton.BoneNames.Num(); ++BoneIndex)
	{
		const FName& Name = InSkeleton.BoneNames[BoneIndex];

		// get the parent bone solver index
		const int32 ParentIndex = InSkeleton.GetParentIndex(BoneIndex);
		const FTransform OrigTransform = InSkeleton.RefPoseGlobal[BoneIndex];
		const FVector InOrigPosition = OrigTransform.GetLocation();
		const FQuat InOrigRotation = OrigTransform.GetRotation();
		const bool bIsRoot = Name == RootBone;
		Solver.AddBone(Name, ParentIndex, InOrigPosition, InOrigRotation, bIsRoot);
	}

	// create effectors
	for (FIKRig_PBIKEffector& Effector : Effectors)
	{
		Effector.IndexInSolver = Solver.AddEffector(Effector.Goal.Bone);
	}
		
	// initialize solver
	Solver.Initialize();
}

void UIKRigPBIKSolver::Solve(
    FIKRigSkeleton& IKRigSkeleton,
    const FIKRigGoalContainer& Goals,
    FControlRigDrawInterface* InOutDrawInterface)
{
	if (!Solver.IsReadyToSimulate())
	{
		return;
	}

	if (Solver.GetNumBones() != IKRigSkeleton.BoneNames.Num())
	{
		return;
	}

	// set bones to input pose
	for(int32 BoneIndex = 0; BoneIndex < Solver.GetNumBones(); BoneIndex++)
	{
		const FTransform GlobalTransform = IKRigSkeleton.CurrentPoseGlobal[BoneIndex];
		Solver.SetBoneTransform(BoneIndex, GlobalTransform);
	}

	// update bone settings
	for (const FPBIKBoneSetting& BoneSetting : BoneSettings)
	{
		const int32 BoneIndex = Solver.GetBoneIndex(BoneSetting.Bone);
		if (PBIK::FBoneSettings* InternalSettings = Solver.GetBoneSettings(BoneIndex))
		{
			BoneSetting.CopyToCoreStruct(*InternalSettings);
		}
	}

	// update effectors
	const float OffsetAlpha = 1.0f; // this is constant because IKRig manages offset alphas itself
	for (const FIKRig_PBIKEffector& Effector : Effectors)
	{
		if (Effector.IndexInSolver < 0)
		{
			continue;
		}
		FIKRigGoal Goal;
		Goals.GetGoalByName(Effector.Goal.Goal, Goal);
		Solver.SetEffectorGoal(
			Effector.IndexInSolver,
			Goal.FinalBlendedPosition,
			Goal.FinalBlendedRotation,
			OffsetAlpha,
			Effector.StrengthAlpha);
	}

	// solve
	Solver.Solve(Settings);

	// copy transforms back
	for(int32 BoneIndex = 0; BoneIndex < Solver.GetNumBones(); BoneIndex++)
	{
		FTransform OutTransform;
		Solver.GetBoneGlobalTransform(BoneIndex, OutTransform);
		IKRigSkeleton.CurrentPoseGlobal[BoneIndex] = OutTransform;
	}
	
	// do all debug drawing
	//Debug.Draw(Context.DrawInterface, &Solver);
}

void UIKRigPBIKSolver::AddGoalsInSolver(TArray<FIKRigEffectorGoal>& OutGoals) const
{
	for (const FIKRig_PBIKEffector& Effector : Effectors)
	{
		AddGoalToArrayNoDuplicates(Effector.Goal,OutGoals);
	}
}