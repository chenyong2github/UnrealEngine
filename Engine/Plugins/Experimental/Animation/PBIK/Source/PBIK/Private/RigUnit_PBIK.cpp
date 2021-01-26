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

	FRigBoneHierarchy& Bones = Hierarchy->BoneHierarchy;

	if (Context.State == EControlRigState::Init)
	{
		// check how many effectors are assigned to a bone
		int NumEffectors = 0;
		for (const FPBIKEffector& Effector : Effectors)
		{
			if (Bones.GetIndex(Effector.Bone) != NAME_None)
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
		for (const FPBIKEffector& Effector : Effectors)
		{
			if (!Solver.AddEffector(Effector.Bone))
			{
				return;
			}
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
	for (int B = 0; B < Bones.Num(); ++B)
	{
		Solver.SetBoneTransform(B, Bones[B].GlobalTransform);
	}

	// update bone settings
	for (const FPBIKBoneSetting& BoneSetting : BoneSettings)
	{
		int32 BoneIndex = Bones.GetIndex(BoneSetting.Bone);
		if (BoneIndex == INDEX_NONE)
		{
			continue; // no bones to apply it to
		}

		if (PBIK::FBoneSettings* InternalSettings = Solver.GetBoneSettings(BoneIndex))
		{
			BoneSetting.CopyToCoreStruct(*InternalSettings);
		}
	}

	// update effectors
	for (int E = 0; E < Effectors.Num(); ++E)
	{
		FVector Position = Effectors[E].Transform.GetLocation();
		FQuat Rotation = Effectors[E].Transform.GetRotation();
		Solver.SetEffectorGoal(E, Position, Rotation, Effectors[E].Alpha);
	}

	// solve
	Solver.Solve(Settings);

	// copy transforms back
	for (int B = 0; B < Bones.Num(); ++B)
	{
		Solver.GetBoneGlobalTransform(B, Bones[B].GlobalTransform);
	}

	// do all debug drawing
	Debug.Draw(Context.DrawInterface, &Solver);
}
