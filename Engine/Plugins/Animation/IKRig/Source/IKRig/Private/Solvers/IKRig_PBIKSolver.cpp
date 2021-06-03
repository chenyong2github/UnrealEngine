// Copyright Epic Games, Inc. All Rights Reserved.

#include "Solvers/IKRig_PBIKSolver.h"

#include "IKRigDataTypes.h"
#include "IKRigSkeleton.h"

void UIKRigPBIKSolver::Initialize(const FIKRigSkeleton& InSkeleton)
{	
	// check how many effectors are assigned to a bone
	int NumEffectors = 0;
	for (const UIKRig_FBIKEffector* Effector : Effectors)
	{
		if (InSkeleton.GetBoneIndexFromName(Effector->BoneName) != INDEX_NONE)
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
	for (UIKRig_FBIKEffector* Effector : Effectors)
	{
		Effector->IndexInSolver = Solver.AddEffector(Effector->BoneName);
	}
		
	// initialize solver
	Solver.Initialize();
}

void UIKRigPBIKSolver::Solve(FIKRigSkeleton& IKRigSkeleton, const FIKRigGoalContainer& Goals)
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
	for (const UIKRig_PBIKBoneSettings* BoneSetting : BoneSettings)
	{
		const int32 BoneIndex = Solver.GetBoneIndex(BoneSetting->Bone);
		if (PBIK::FBoneSettings* InternalSettings = Solver.GetBoneSettings(BoneIndex))
		{
			BoneSetting->CopyToCoreStruct(*InternalSettings);
		}
	}

	// update effectors
	const float OffsetAlpha = 1.0f; // this is constant because IKRig manages offset alphas itself
	for (const UIKRig_FBIKEffector* Effector : Effectors)
	{
		if (Effector->IndexInSolver < 0)
		{
			continue;
		}
		FIKRigGoal Goal;
		Goals.GetGoalByName(Effector->GoalName, Goal);
		Solver.SetEffectorGoal(
			Effector->IndexInSolver,
			Goal.FinalBlendedPosition,
			Goal.FinalBlendedRotation,
			OffsetAlpha,
			Effector->StrengthAlpha);
	}

	// update settings
	FPBIKSolverSettings Settings;
	Settings.Iterations = Iterations;
	Settings.bAllowStretch = bAllowStretch;
	Settings.MassMultiplier = MassMultiplier;
	Settings.MinMassMultiplier = MinMassMultiplier;
	Settings.bPinRoot = bPinRoot;
	Settings.bStartSolveFromInputPose = bStartSolveFromInputPose;

	// solve
	Solver.Solve(Settings);

	// copy transforms back
	for(int32 BoneIndex = 0; BoneIndex < Solver.GetNumBones(); BoneIndex++)
	{
		FTransform OutTransform;
		Solver.GetBoneGlobalTransform(BoneIndex, OutTransform);
		IKRigSkeleton.CurrentPoseGlobal[BoneIndex] = OutTransform;
	}
}

void UIKRigPBIKSolver::UpdateSolverSettings(UIKRigSolver* InSettings)
{
	if(UIKRigPBIKSolver* Settings = Cast<UIKRigPBIKSolver>(InSettings))
	{
		Iterations = Settings->Iterations;
		bAllowStretch = Settings->bAllowStretch;
		MassMultiplier = Settings->MassMultiplier;
		MinMassMultiplier = Settings->MinMassMultiplier;
		bPinRoot = Settings->bPinRoot;
		bStartSolveFromInputPose = Settings->bStartSolveFromInputPose;

		// copy effector settings
		for (const UIKRig_FBIKEffector* InEffector : Settings->Effectors)
		{
			for (UIKRig_FBIKEffector* Effector : Effectors)
			{
				if (Effector->GoalName == InEffector->GoalName)
				{
					Effector->CopySettings(InEffector);
					break;
				}
			}
		}

		// copy bone settings
		for (const UIKRig_PBIKBoneSettings* InBoneSetting : Settings->BoneSettings)
		{
			for (UIKRig_PBIKBoneSettings* BoneSetting : BoneSettings)
			{
				if (BoneSetting->Bone == InBoneSetting->Bone)
				{
					BoneSetting->CopySettings(InBoneSetting);
					break;	
				}
			}
		}
	}
}

void UIKRigPBIKSolver::AddGoal(const UIKRigEffectorGoal& NewGoal)
{
	UIKRig_FBIKEffector* NewEffector = NewObject<UIKRig_FBIKEffector>(this, UIKRig_FBIKEffector::StaticClass());
	NewEffector->GoalName = NewGoal.GoalName;
	NewEffector->BoneName = NewGoal.BoneName;
	Effectors.Add(NewEffector);
}

void UIKRigPBIKSolver::RemoveGoal(const FName& GoalName)
{
	// ensure goal even exists in this solver
	const int32 GoalIndex = GetIndexOfGoal(GoalName);
	if (GoalIndex == INDEX_NONE)
	{
		return;
	}

	// remove it
	Effectors.RemoveAt(GoalIndex);
}

void UIKRigPBIKSolver::RenameGoal(const FName& OldName, const FName& NewName)
{
	// ensure goal even exists in this solver
	const int32 GoalIndex = GetIndexOfGoal(OldName);
	if (GoalIndex == INDEX_NONE)
	{
		return;
	}

	// rename
	Effectors[GoalIndex]->GoalName = NewName;
}

void UIKRigPBIKSolver::SetGoalBone(const FName& GoalName, const FName& NewBoneName)
{
	// ensure goal even exists in this solver
	const int32 GoalIndex = GetIndexOfGoal(GoalName);
	if (GoalIndex == INDEX_NONE)
	{
		return;
	}

	// rename
	Effectors[GoalIndex]->BoneName = NewBoneName;
}

bool UIKRigPBIKSolver::IsGoalConnected(const FName& GoalName) const
{
	return GetIndexOfGoal(GoalName) != INDEX_NONE;
}

void UIKRigPBIKSolver::SetRootBone(const FName& RootBoneName)
{
	RootBone = RootBoneName;
}

UObject* UIKRigPBIKSolver::GetEffectorWithGoal(const FName& GoalName)
{
	const int32 GoalIndex = GetIndexOfGoal(GoalName);
	if (GoalIndex == INDEX_NONE)
	{
		return nullptr;
	}

	return Effectors[GoalIndex];
}

void UIKRigPBIKSolver::AddBoneSetting(const FName& BoneName)
{
	if (GetBoneSetting(BoneName))
	{
		return; // already have settings on this bone
	}

	UIKRig_PBIKBoneSettings* NewBoneSettings = NewObject<UIKRig_PBIKBoneSettings>(this, UIKRig_PBIKBoneSettings::StaticClass());
	NewBoneSettings->Bone = BoneName;
	BoneSettings.Add(NewBoneSettings);
}

void UIKRigPBIKSolver::RemoveBoneSetting(const FName& BoneName)
{
	UIKRig_PBIKBoneSettings* BoneSettingToRemove = nullptr; 
	for (UIKRig_PBIKBoneSettings* BoneSetting : BoneSettings)
	{
		if (BoneSetting->Bone == BoneName)
		{
			BoneSettingToRemove = BoneSetting;
			break; // can only be one with this name
		}
	}

	if (BoneSettingToRemove)
	{
		BoneSettings.Remove(BoneSettingToRemove);
	}
}

UObject* UIKRigPBIKSolver::GetBoneSetting(const FName& BoneName) const
{
	for (UIKRig_PBIKBoneSettings* BoneSetting : BoneSettings)
	{
		if (BoneSetting && BoneSetting->Bone == BoneName)
		{
			return BoneSetting;
		}
	}
	
	return nullptr;
}

void UIKRigPBIKSolver::DrawBoneSettings(
	const FName& BoneName,
	const FIKRigSkeleton& IKRigSkeleton,
	FPrimitiveDrawInterface* PDI) const
{
	
}

bool UIKRigPBIKSolver::IsBoneAffectedBySolver(const FName& BoneName, const FIKRigSkeleton& IKRigSkeleton) const
{
	for (UIKRig_FBIKEffector* Effector : Effectors)
	{
		if (IKRigSkeleton.IsBoneInDirectLineage(Effector->BoneName, BoneName))
		{
			return true;
		}
	}
	
	return false;
}

int32 UIKRigPBIKSolver::GetIndexOfGoal(const FName& OldName) const
{
	for (int32 i=0; i<Effectors.Num(); ++i)
	{
		if (Effectors[i]->GoalName == OldName)
		{
			return i;
		}
	}

	return INDEX_NONE;
}

void UIKRigPBIKSolver::PostLoad()
{
	Super::PostLoad();
	
	// patch for loading old effectors, we blow them away
	bool bHasNullEffector = false;
	for (const UIKRig_FBIKEffector* Effector : Effectors)
	{
		if (!Effector)
		{
			bHasNullEffector = true;
		}
	}

	if (bHasNullEffector)
	{
		Effectors.Empty();
	}
}
