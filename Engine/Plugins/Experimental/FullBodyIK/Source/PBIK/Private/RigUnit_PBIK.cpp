// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_PBIK.h"
#include "Units/RigUnitContext.h"

//#pragma optimize("", off)

FRigUnit_PBIK_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	URigHierarchy* Hierarchy = ExecuteContext.Hierarchy;
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
		
		// create solver bone index to elements index map
		TArray<FRigBoneElement*> BoneElements = Hierarchy->GetBones(true);
		SolverBoneToElementIndex.Reset();
		for (int B = 0; B < BoneElements.Num(); ++B)
		{
			SolverBoneToElementIndex.Add(BoneElements[B]->GetIndex());
		}

		// create bones
		for (int B = 0; B < BoneElements.Num(); ++B)
		{
			FName Name = BoneElements[B]->GetName();

			// get the parent bone solver index
			const int ParentElementIndex = Hierarchy->GetFirstParent(BoneElements[B]->GetIndex());
			int ParentIndex = -1;
			for (int P=0; P<SolverBoneToElementIndex.Num(); ++P)
			{
				if (SolverBoneToElementIndex[P] == ParentElementIndex)
				{
					ParentIndex = P;
					break;
				}
			}
			
			const FTransform OrigTransform = Hierarchy->GetTransform(BoneElements[B], ERigTransformType::InitialGlobal);
			const FVector InOrigPosition = OrigTransform.GetLocation();
			const FQuat InOrigRotation = OrigTransform.GetRotation();
			bool bIsRoot = BoneElements[B]->GetName() == Root;
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
	}

	if (!Solver.IsReadyToSimulate())
	{
		return;
	}

	// set bones to input pose
	for(int32 BoneIndex = 0; BoneIndex < Solver.GetNumBones(); BoneIndex++)
	{
		const FTransform GlobalTransform = Hierarchy->GetGlobalTransform(SolverBoneToElementIndex[BoneIndex]);
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
		Hierarchy->SetGlobalTransform(SolverBoneToElementIndex[BoneIndex], NewTransform, false, bPropagateTransform);
	}

	// do all debug drawing
	Debug.Draw(Context.DrawInterface, &Solver);
}
