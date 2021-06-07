// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_PBIK.h"
#include "Units/RigUnitContext.h"


FRigUnit_PBIK_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	FRigHierarchyContainer* Hierarchy = ExecuteContext.Hierarchy;
	if (Hierarchy == nullptr)
	{
		return;
	}

	if (Context.State == EControlRigState::Init)
	{
		BoneSettingToSolverBoneIndex.Reset();
		
		// check how many effectors are assigned to a bone
		int NumEffectors = 0;
		for (const FPBIKEffector& Effector : Effectors)
		{
			if (Hierarchy->GetIndex(FRigElementKey(Effector.Bone, ERigElementType::Bone)) != INDEX_NONE)
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
		for (int B = 0; B < Hierarchy->BoneHierarchy.Num(); ++B)
		{
			const FRigBone& Bone = Hierarchy->BoneHierarchy[B];
			const FTransform OrigTransform = Bone.InitialTransform;
			const FVector InOrigPosition = OrigTransform.GetLocation();
			const FQuat InOrigRotation = OrigTransform.GetRotation();
			bool bIsRoot = Bone.Name == Root;
			Solver.AddBone(Bone.Name, Bone.ParentIndex, InOrigPosition, InOrigRotation, bIsRoot);
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
	}

	if (!Solver.IsReadyToSimulate())
	{
		return;
	}

	// set bones to input pose
	for(int32 BoneIndex = 0; BoneIndex < Solver.GetNumBones(); BoneIndex++)
	{
		const FTransform GlobalTransform = Hierarchy->BoneHierarchy[BoneIndex].GlobalTransform;
		Solver.SetBoneTransform(BoneIndex, GlobalTransform);
	}

	// invalidate the name lookup for the settings
	if(BoneSettingToSolverBoneIndex.Num() != BoneSettings.Num())
	{
		BoneSettingToSolverBoneIndex.Reset();
		while(BoneSettingToSolverBoneIndex.Num() < BoneSettings.Num())
		{
			BoneSettingToSolverBoneIndex.Add(INDEX_NONE);
		}
	}

	// update bone settings
	for (int32 BoneSettingIndex = 0; BoneSettingIndex < BoneSettings.Num(); BoneSettingIndex++)
	{
		const FPBIKBoneSetting& BoneSetting = BoneSettings[BoneSettingIndex];

		if(BoneSettingToSolverBoneIndex[BoneSettingIndex] == INDEX_NONE)
		{
			BoneSettingToSolverBoneIndex[BoneSettingIndex] = Solver.GetBoneIndex(BoneSetting.Bone);
			if(BoneSettingToSolverBoneIndex[BoneSettingIndex] == INDEX_NONE)
			{
				continue;
			}
		}

		int32 BoneIndex = BoneSettingToSolverBoneIndex[BoneSettingIndex];
		if (PBIK::FBoneSettings* InternalSettings = Solver.GetBoneSettings(BoneIndex))
		{
			BoneSetting.CopyToCoreStruct(*InternalSettings);
		}
	}

	// update effectors
	for (int E = 0; E < Effectors.Num(); ++E)
	{
		if (EffectorSolverIndices[E] == -1)
		{
			continue;
		}

		const FPBIKEffector& Effector = Effectors[E];
		FVector Position = Effector.Transform.GetLocation();
		FQuat Rotation = Effector.Transform.GetRotation();
		Solver.SetEffectorGoal(EffectorSolverIndices[E], Position, Rotation, Effector.OffsetAlpha, Effector.StrengthAlpha);
	}

	// solve
	Solver.Solve(Settings);

	// copy transforms back
	const bool bPropagateTransform = false;
	for(int32 BoneIndex = 0; BoneIndex < Solver.GetNumBones(); BoneIndex++)
	{
		FTransform NewTransform;
		Solver.GetBoneGlobalTransform(BoneIndex, NewTransform);
		Hierarchy->BoneHierarchy.SetGlobalTransform(BoneIndex, NewTransform, bPropagateTransform);
	}

	// do all debug drawing
	Debug.Draw(Context.DrawInterface, &Solver);
}
