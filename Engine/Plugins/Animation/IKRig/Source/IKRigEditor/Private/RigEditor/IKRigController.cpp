// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigEditor/IKRigController.h"

#include "IKRigDefinition.h"
#include "IKRigSolver.h"

#include "Engine/SkeletalMesh.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "IKRigController"


TMap<UIKRigDefinition*, UIKRigController*> UIKRigController::AssetToControllerMap;

UIKRigController* UIKRigController::GetIKRigController(UIKRigDefinition* InIKRigDefinition)
{
	if (!InIKRigDefinition)
	{
		return nullptr;
	}
	
	UIKRigController** Controller = AssetToControllerMap.Find(InIKRigDefinition);
	if (Controller)
	{
		return *Controller;
	}

	UIKRigController* NewController = NewObject<UIKRigController>();
	AssetToControllerMap.Add(InIKRigDefinition) = NewController;
	NewController->IKRigAsset = InIKRigDefinition;
	return NewController;
}

UIKRigDefinition* UIKRigController::GetAsset() const
{
	return IKRigAsset;
}

void UIKRigController::AddBoneSetting(const FName& BoneName, int32 SolverIndex) const
{
	check(IKRigAsset->Solvers.IsValidIndex(SolverIndex))

	if (!CanAddBoneSetting(BoneName, SolverIndex))
	{
		return; // prerequisites not met
	}

	FScopedTransaction Transaction(LOCTEXT("AddBoneSetting_Label", "Add Bone Setting"));
	UIKRigSolver* Solver = IKRigAsset->Solvers[SolverIndex];
	Solver->Modify();
	Solver->AddBoneSetting(BoneName);
	BroadcastNeedsReinitialized();
}

bool UIKRigController::CanAddBoneSetting(const FName& BoneName, int32 SolverIndex) const
{
	const UIKRigSolver* Solver = GetSolver(SolverIndex);
	if (!Solver)
	{
		return false; // solver doesn't exist
	}

	if (IKRigAsset->Skeleton.GetBoneIndexFromName(BoneName) == INDEX_NONE)
	{
		return false; // bone doesn't exist
	}

	if (!IKRigAsset->Solvers[SolverIndex]->UsesBoneSettings())
	{
		return false; // solver doesn't support per-bone settings
	}

	// returns true if the solver in question does NOT already have a settings object for this bone 
	return !static_cast<bool>(IKRigAsset->Solvers[SolverIndex]->GetBoneSetting(BoneName));
}

void UIKRigController::RemoveBoneSetting(const FName& BoneName, int32 SolverIndex) const
{
	check(IKRigAsset->Solvers.IsValidIndex(SolverIndex))

	if (IKRigAsset->Skeleton.GetBoneIndexFromName(BoneName) == INDEX_NONE)
	{
		return; // bone doesn't exist
	}

	FScopedTransaction Transaction(LOCTEXT("RemoveBoneSetting_Label", "Remove Bone Setting"));
	UIKRigSolver* Solver = IKRigAsset->Solvers[SolverIndex];
	Solver->Modify();
	Solver->RemoveBoneSetting(BoneName);
	BroadcastNeedsReinitialized();
}

bool UIKRigController::CanRemoveBoneSetting(const FName& BoneName, int32 SolverIndex) const
{
	const UIKRigSolver* Solver = GetSolver(SolverIndex);
	if (!Solver)
	{
		return false; // solver doesn't exist
	}

	if (!Solver->UsesBoneSettings())
	{
		return false; // solver doesn't use bone settings
	}

	if (IKRigAsset->Skeleton.GetBoneIndexFromName(BoneName) == INDEX_NONE)
	{
		return false; // bone doesn't exist
	}

	if (!Solver->GetBoneSetting(BoneName))
	{
		return false; // solver doesn't have any settings for this bone
	}

	return true;
}

UObject* UIKRigController::GetSettingsForBone(const FName& BoneName, int32 SolverIndex) const
{
	const UIKRigSolver* Solver = GetSolver(SolverIndex);
	if (!Solver)
	{
		return nullptr; // solver doesn't exist
	}
	
	return Solver->GetBoneSetting(BoneName);
}

bool UIKRigController::DoesBoneHaveSettings(const FName& BoneName) const
{
	if (IKRigAsset->Skeleton.GetBoneIndexFromName(BoneName) == INDEX_NONE)
	{
		return false; // bone doesn't exist
	}
	
	for (UIKRigSolver* Solver : IKRigAsset->Solvers)
	{
		if (UObject* BoneSetting = Solver->GetBoneSetting(BoneName))
		{
			return true;
		}
	}

	return false;
}

bool UIKRigController::AddRetargetChain(const FName& ChainName, const FName& StartBone, const FName& EndBone) const
{
	if (IKRigAsset->GetRetargetChainByName(ChainName))
	{
		return false; // bone chain already exists with that name
	}

	FScopedTransaction Transaction(LOCTEXT("AddRetargetChain_Label", "Add Retarget Chain"));
	IKRigAsset->Modify();
	
	IKRigAsset->RetargetDefinition.BoneChains.Emplace(ChainName, StartBone, EndBone);
	SortRetargetChains();
	BroadcastNeedsReinitialized();
	return true;
}

bool UIKRigController::RemoveRetargetChain(const FName& ChainName) const
{
	FScopedTransaction Transaction(LOCTEXT("RemoveRetargetChain_Label", "Remove Retarget Chain"));
	IKRigAsset->Modify();
	
	auto Pred = [ChainName](const FBoneChain& Element)
	{
		if (Element.ChainName == ChainName)
		{
			return true;
		}

		return false;
	};
	
	if (IKRigAsset->RetargetDefinition.BoneChains.RemoveAll(Pred) > 0)
	{
		SortRetargetChains();
		BroadcastNeedsReinitialized();
		return true;
	}

	return false;
}

FName UIKRigController::RenameRetargetChain(const FName& ChainName, const FName& NewChainName) const
{
	FBoneChain* Chain = IKRigAsset->RetargetDefinition.GetEditableBoneChainByName(ChainName);
	if (!Chain)
	{
		return ChainName; // chain doesn't exist to rename
	}

	if (IKRigAsset->GetRetargetChainByName(NewChainName))
	{
		return ChainName; // bone chain already exists with the new name
	}
	
	FScopedTransaction Transaction(LOCTEXT("RenameRetargetChain_Label", "Rename Retarget Chain"));
	IKRigAsset->Modify();
	Chain->ChainName = NewChainName;
	RetargetChainRenamed.Broadcast(GetAsset(), ChainName, NewChainName);
	BroadcastNeedsReinitialized();
	return NewChainName;
}

bool UIKRigController::SetRetargetChainStartBone(const FName& ChainName, const FName& StartBoneName) const
{
	if (FBoneChain* BoneChain = IKRigAsset->RetargetDefinition.GetEditableBoneChainByName(ChainName))
	{
		FScopedTransaction Transaction(LOCTEXT("SetRetargetChainStartBone_Label", "Set Retarget Chain Start Bone"));
		IKRigAsset->Modify();
		BoneChain->StartBone = StartBoneName;
		SortRetargetChains();
		BroadcastNeedsReinitialized();
		return true;
	}

	return false; // no bone chain with that name
}

bool UIKRigController::SetRetargetChainEndBone(const FName& ChainName, const FName& EndBoneName) const
{
	if (FBoneChain* BoneChain = IKRigAsset->RetargetDefinition.GetEditableBoneChainByName(ChainName))
	{
		FScopedTransaction Transaction(LOCTEXT("SetRetargetChainEndBone_Label", "Set Retarget Chain End Bone"));
		IKRigAsset->Modify();
		BoneChain->EndBone = EndBoneName;
		BroadcastNeedsReinitialized();
		return true;
	}

	return false; // no bone chain with that name
}

bool UIKRigController::SetRetargetChainGoal(const FName& ChainName, const FName& GoalName) const
{
	FName GoalNameToUse = GoalName;
	if (!GetGoal(GoalName))
	{
		GoalNameToUse = NAME_None; // no goal with that name	
	}
	
	if (FBoneChain* BoneChain = IKRigAsset->RetargetDefinition.GetEditableBoneChainByName(ChainName))
	{
		FScopedTransaction Transaction(LOCTEXT("SetRetargetChainGoal_Label", "Set Retarget Chain Goal"));
		IKRigAsset->Modify();
		BoneChain->IKGoalName = GoalNameToUse;
		BroadcastNeedsReinitialized();
		return true;
	}

	return false; // no bone chain with that name
}

FName UIKRigController::GetRetargetChainGoal(const FName& ChainName) const
{
	check(IKRigAsset)
	const FBoneChain* Chain = IKRigAsset->GetRetargetChainByName(ChainName);
	if (!Chain)
	{
		return NAME_None;
	}
	
	return Chain->IKGoalName;
}

FName UIKRigController::GetRetargetChainStartBone(const FName& ChainName) const
{
	check(IKRigAsset)
	const FBoneChain* Chain = IKRigAsset->GetRetargetChainByName(ChainName);
	if (!Chain)
	{
		return NAME_None;
	}
	
	return Chain->StartBone;
}

FName UIKRigController::GetRetargetChainEndBone(const FName& ChainName) const
{
	check(IKRigAsset)
	const FBoneChain* Chain = IKRigAsset->GetRetargetChainByName(ChainName);
	if (!Chain)
	{
		return NAME_None;
	}
	
	return Chain->EndBone;
}

const TArray<FBoneChain>& UIKRigController::GetRetargetChains() const
{
	check(IKRigAsset)
	return IKRigAsset->GetRetargetChains();
}

void UIKRigController::SetRetargetRoot(const FName& RootBoneName) const
{
	check(IKRigAsset)

	FScopedTransaction Transaction(LOCTEXT("SetRetargetRootBone_Label", "Set Retarget Root Bone"));
	IKRigAsset->Modify();
	
	IKRigAsset->RetargetDefinition.RootBone = RootBoneName;

	BroadcastNeedsReinitialized();
}

FName UIKRigController::GetRetargetRoot() const
{
	check(IKRigAsset)

	return IKRigAsset->RetargetDefinition.RootBone;
}

void UIKRigController::SortRetargetChains() const
{
	IKRigAsset->RetargetDefinition.BoneChains.Sort([this](const FBoneChain& A, const FBoneChain& B)
	{
		const int32 IndexA = IKRigAsset->Skeleton.GetBoneIndexFromName(A.StartBone);
		const int32 IndexB = IKRigAsset->Skeleton.GetBoneIndexFromName(B.StartBone);
		return IndexA < IndexB;
	});
}

void UIKRigController::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	// since static member, we just add only for default object
	if (InThis && InThis->IsTemplate())
	{
		for (auto Iter = AssetToControllerMap.CreateIterator(); Iter; ++Iter)
		{
			// we add controllers to the list. so that it doesn't GCed
			// when do we delete from the list?
			UIKRigController* Obj = Iter.Value();
			Collector.AddReferencedObject(Obj);
		}
	}
}

// -------------------------------------------------------
// SKELETON
//

void UIKRigController::SetSourceSkeletalMesh(USkeletalMesh* SkeletalMesh, bool bReImportBones) const
{
	FScopedTransaction Transaction(LOCTEXT("SetSkeletalMesh_Label", "Set Skeletal Mesh"));
	IKRigAsset->Modify();

	// update stored skeletal mesh used for previewing results
	IKRigAsset->PreviewSkeletalMesh = SkeletalMesh;

	if (bReImportBones)
	{
		// reimport the skeleton data
		if (SkeletalMesh)
		{
			SetSkeleton(SkeletalMesh->GetRefSkeleton());		
		}
		else
		{
			IKRigAsset->Skeleton.Reset();
		}
	}

	BroadcastNeedsReinitialized();
}

void UIKRigController::SetSkeleton(const FReferenceSkeleton& InSkeleton) const
{
	FScopedTransaction Transaction(LOCTEXT("SetSkeleton_Label", "Set Skeleton"));
	IKRigAsset->Modify();
	IKRigAsset->Skeleton.Initialize(InSkeleton, IKRigAsset->Skeleton.ExcludedBones);
	BroadcastNeedsReinitialized();
}

const FIKRigSkeleton& UIKRigController::GetIKRigSkeleton() const
{
	return IKRigAsset->Skeleton;
}

USkeleton* UIKRigController::GetSkeleton() const
{
	if (IKRigAsset->PreviewSkeletalMesh.IsNull())
    {
        return nullptr;
    }
    
    return IKRigAsset->PreviewSkeletalMesh->GetSkeleton();
}

void UIKRigController::SetBoneExcluded(const FName& BoneName, const bool bExclude) const
{
	const bool bIsExcluded = IKRigAsset->Skeleton.ExcludedBones.Contains(BoneName);
	if (bIsExcluded == bExclude)
	{
		return; // already in the requested state of exclusion
	}

	FScopedTransaction Transaction(LOCTEXT("SetBoneExcluded_Label", "Set Bone Excluded"));
	IKRigAsset->Modify();

	if (bExclude)
	{
		IKRigAsset->Skeleton.ExcludedBones.Add(BoneName);
	}
	else
	{
		IKRigAsset->Skeleton.ExcludedBones.Remove(BoneName);
	}

	BroadcastNeedsReinitialized();
}

bool UIKRigController::GetBoneExcluded(const FName& BoneName) const
{
	return IKRigAsset->Skeleton.ExcludedBones.Contains(BoneName);
}

FTransform UIKRigController::GetBoneRetargetPose(const FName& BoneName) const
{	
	const int32 BoneIndex = IKRigAsset->Skeleton.GetBoneIndexFromName(BoneName);
	check(BoneIndex != INDEX_NONE) // must initialize IK Rig before getting here
	return IKRigAsset->Skeleton.RefPoseGlobal[BoneIndex];
}

USkeletalMesh* UIKRigController::GetSourceSkeletalMesh() const
{
	return IKRigAsset->PreviewSkeletalMesh.Get();
}

// -------------------------------------------------------
// SOLVERS
//

int32 UIKRigController::AddSolver(TSubclassOf<UIKRigSolver> InIKRigSolverClass) const
{
	check(IKRigAsset)

	FScopedTransaction Transaction(LOCTEXT("AddSolver_Label", "Add Solver"));
	IKRigAsset->Modify();

	UIKRigSolver* NewSolver = NewObject<UIKRigSolver>(IKRigAsset, InIKRigSolverClass, NAME_None, RF_Transactional);
	check(NewSolver);

	const int32 SolverIndex = IKRigAsset->Solvers.Add(NewSolver);

	BroadcastNeedsReinitialized();

	return SolverIndex;
}

void UIKRigController::RemoveSolver(const int32 SolverIndex) const
{
	check(IKRigAsset)
	
	if (!IKRigAsset->Solvers.IsValidIndex(SolverIndex))
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("RemoveSolver_Label", "Remove Solver"));
	IKRigAsset->Modify();

	IKRigAsset->Solvers.RemoveAt(SolverIndex);

	BroadcastNeedsReinitialized();
}

bool UIKRigController::MoveSolverInStack(int32 SolverToMoveIndex, int32 TargetSolverIndex) const
{
	if (!IKRigAsset->Solvers.IsValidIndex(SolverToMoveIndex))
	{
		return false;
	}

	if (!IKRigAsset->Solvers.IsValidIndex(TargetSolverIndex))
	{
		return false;
	}

	FScopedTransaction Transaction(LOCTEXT("ReorderSolver_Label", "Reorder Solvers"));
	IKRigAsset->Modify();

	UIKRigSolver* SolverToMove = IKRigAsset->Solvers[SolverToMoveIndex];
	IKRigAsset->Solvers.Insert(SolverToMove, TargetSolverIndex + 1);
	const int32 SolverToRemove = TargetSolverIndex > SolverToMoveIndex ? SolverToMoveIndex : SolverToMoveIndex + 1;
	IKRigAsset->Solvers.RemoveAt(SolverToRemove);

	BroadcastNeedsReinitialized();
	
	return true;
}

bool UIKRigController::SetSolverEnabled(int32 SolverIndex, bool bIsEnabled) const
{
	if (!IKRigAsset->Solvers.IsValidIndex(SolverIndex))
	{
		return false;
	}
	
	FScopedTransaction Transaction(LOCTEXT("SetSolverEndabled_Label", "Enable/Disable Solver"));
	UIKRigSolver* Solver = IKRigAsset->Solvers[SolverIndex];
	Solver->Modify();
	IKRigAsset->Modify();

	Solver->SetEnabled(bIsEnabled);

	BroadcastNeedsReinitialized();
	
	return true;
}

void UIKRigController::SetRootBone(const FName& RootBoneName, int32 SolverIndex) const
{
	if (!IKRigAsset->Solvers.IsValidIndex(SolverIndex))
	{
		return; // solver doesn't exist
	}

	if (IKRigAsset->Skeleton.GetBoneIndexFromName(RootBoneName) == INDEX_NONE)
	{
		return; // bone doesn't exist
	}

	FScopedTransaction Transaction(LOCTEXT("SetRootBone_Label", "Set Root Bone"));
	UIKRigSolver* Solver = IKRigAsset->Solvers[SolverIndex];
	Solver->Modify();

	Solver->SetRootBone(RootBoneName);

	BroadcastNeedsReinitialized();
}

const TArray<UIKRigSolver*>& UIKRigController::GetSolverArray() const
{
	return IKRigAsset->Solvers;
}

int32 UIKRigController::GetNumSolvers() const
{
	check(IKRigAsset)
	return IKRigAsset->Solvers.Num();
}

UIKRigSolver* UIKRigController::GetSolver(int32 Index) const
{
	check(IKRigAsset)

	if (IKRigAsset->Solvers.IsValidIndex(Index))
	{
		return IKRigAsset->Solvers[Index];
	}
	
	return nullptr;
}

// -------------------------------------------------------
// GOALS
//

UIKRigEffectorGoal* UIKRigController::AddNewGoal(const FName& GoalName, const FName& BoneName) const
{
	if (GetGoalIndex(GoalName) != INDEX_NONE)
	{
		return nullptr; // goal already exists!
	}
	
	FScopedTransaction Transaction(LOCTEXT("AddSetting_Label", "Add Bone Setting"));
	IKRigAsset->Modify();

	UIKRigEffectorGoal* NewGoal = NewObject<UIKRigEffectorGoal>(IKRigAsset, UIKRigEffectorGoal::StaticClass(), NAME_None, RF_Transactional);
	NewGoal->BoneName = BoneName;
	NewGoal->GoalName = GoalName;
	IKRigAsset->Goals.Add(NewGoal);

	// set initial transform
	NewGoal->InitialTransform = GetBoneRetargetPose(NewGoal->BoneName);
	NewGoal->CurrentTransform = NewGoal->InitialTransform;

	BroadcastNeedsReinitialized();
	
	return NewGoal;
}

bool UIKRigController::RemoveGoal(const FName& GoalName) const
{
	const int32 GoalIndex = GetGoalIndex(GoalName);
	if (GoalIndex == INDEX_NONE)
	{
		return false; // can't remove goal we don't have
	}

	FScopedTransaction Transaction(LOCTEXT("RemoveGoal_Label", "Remove Goal"));

	// remove from all the solvers
	const FName& GoalToRemove = IKRigAsset->Goals[GoalIndex]->GoalName;
	for (UIKRigSolver* Solver : IKRigAsset->Solvers)
	{
		Solver->Modify();
		Solver->RemoveGoal(GoalToRemove);
	}

	// remove from core system
	IKRigAsset->Modify();
	IKRigAsset->Goals.RemoveAt(GoalIndex);

	// clean any retarget chains that might reference the missing goal
	for (FBoneChain& BoneChain : IKRigAsset->RetargetDefinition.BoneChains)
	{
		if (BoneChain.IKGoalName == GoalName)
		{
			BoneChain.IKGoalName = NAME_None;
		}
	}

	BroadcastNeedsReinitialized();
	
	return true;
}

FName UIKRigController::RenameGoal(const FName& OldName, const FName& PotentialNewName) const
{
	// sanitize the potential new name
	FString CleanName = PotentialNewName.ToString();
	SanitizeGoalName(CleanName);
	const FName NewName = FName(CleanName);

	// validate new name
	const int32 ExistingGoalIndex = GetGoalIndex(NewName);
	if (ExistingGoalIndex != INDEX_NONE)
	{
		return NAME_None; // name already in use, can't use that
	}
	const int32 GoalIndex = GetGoalIndex(OldName);
	if (GoalIndex == INDEX_NONE)
	{
		return NAME_None; // can't rename goal we don't have
	}
	
	FScopedTransaction Transaction(LOCTEXT("RenameGoal_Label", "Rename Goal"));
	IKRigAsset->Modify();

	// rename in core
	IKRigAsset->Goals[GoalIndex]->Modify();
	IKRigAsset->Goals[GoalIndex]->GoalName = NewName;

	// update any retarget chains that might reference the goal
	for (FBoneChain& BoneChain : IKRigAsset->RetargetDefinition.BoneChains)
	{
		if (BoneChain.IKGoalName == OldName)
		{
			BoneChain.IKGoalName = NewName;
		}
	}

	// rename in solvers
	for (UIKRigSolver* Solver : IKRigAsset->Solvers)
	{
		Solver->Modify();
		Solver->RenameGoal(OldName, NewName);
	}

	BroadcastNeedsReinitialized();

	return NewName;
}

bool UIKRigController::SetGoalBone(const FName& GoalName, const FName& NewBoneName) const
{
	const int32 GoalIndex = GetGoalIndex(GoalName);
	if (GoalIndex == INDEX_NONE)
	{
		return false; // goal doesn't exist in the rig
	}

	const int32 BoneIndex = GetIKRigSkeleton().GetBoneIndexFromName(NewBoneName);
	if (BoneIndex == INDEX_NONE)
	{
		return false; // bone does not exist in the skeleton
	}

	if (GetBoneForGoal(GoalName) == NewBoneName)
	{
		return false; // goal is already using this bone
	}
	
	FScopedTransaction Transaction(LOCTEXT("RenameGoal_Label", "Rename Goal"));
	IKRigAsset->Modify();

	// update goal
	IKRigAsset->Goals[GoalIndex]->BoneName = NewBoneName;
	
	// update in solvers
	for (UIKRigSolver* Solver : IKRigAsset->Solvers)
	{
		Solver->Modify();
		Solver->SetGoalBone(GoalName, NewBoneName);
	}

	// update initial transforms
	ResetGoalTransforms();

	BroadcastNeedsReinitialized();
	
	return true;
}

FName UIKRigController::GetBoneForGoal(const FName& GoalName) const
{
	for (const UIKRigEffectorGoal* Goal : IKRigAsset->Goals)
	{
		if (Goal->GoalName == GoalName)
		{
			return Goal->BoneName;
		}
	}
	
	return NAME_None;
}

bool UIKRigController::ConnectGoalToSolver(const UIKRigEffectorGoal& Goal, int32 SolverIndex) const
{
	// can't add goal that is not present in the core
	check(GetGoalIndex(Goal.GoalName) != INDEX_NONE);
	// can't add goal to a solver with an invalid index
	check(IKRigAsset->Solvers.IsValidIndex(SolverIndex))

	FScopedTransaction Transaction(LOCTEXT("AddGoalToSolver_Label", "Add Goal"));
	UIKRigSolver* Solver = IKRigAsset->Solvers[SolverIndex];
	Solver->Modify();
	
	Solver->AddGoal(Goal);

	BroadcastNeedsReinitialized();
	return true;
}

bool UIKRigController::DisconnectGoalFromSolver(const FName& GoalToRemove, int32 SolverIndex) const
{
	// can't remove goal that is not present in the core
	check(GetGoalIndex(GoalToRemove) != INDEX_NONE);
	// can't remove goal from a solver with an invalid index
	check(IKRigAsset->Solvers.IsValidIndex(SolverIndex))

    FScopedTransaction Transaction(LOCTEXT("RemoveGoalFromSolver_Label", "Remove Goal"));
	UIKRigSolver* Solver = IKRigAsset->Solvers[SolverIndex];
	Solver->Modify();
	
	Solver->RemoveGoal(GoalToRemove);

	BroadcastNeedsReinitialized();
	return true;
}

bool UIKRigController::IsGoalConnectedToSolver(const FName& GoalName, int32 SolverIndex) const
{
	if (!IKRigAsset->Solvers.IsValidIndex(SolverIndex))
	{
		return false;
	}

	return IKRigAsset->Solvers[SolverIndex]->IsGoalConnected(GoalName);
}

const TArray<UIKRigEffectorGoal*>& UIKRigController::GetAllGoals() const
{
	return IKRigAsset->Goals;
}

const UIKRigEffectorGoal* UIKRigController::GetGoal(int32 GoalIndex) const
{
	if (!IKRigAsset->Goals.IsValidIndex(GoalIndex))
	{
		return nullptr;
	}

	return IKRigAsset->Goals[GoalIndex];
}

const UIKRigEffectorGoal* UIKRigController::GetGoal(const FName& GoalName) const
{
	const int32 GoalIndex = GetGoalIndex(GoalName);
	if (GoalIndex == INDEX_NONE)
	{
		return nullptr;
	}
	
	return IKRigAsset->Goals[GoalIndex];
}

UObject* UIKRigController::GetEffectorForGoal(const FName& GoalName, int32 SolverIndex) const
{
	const int32 GoalIndex = GetGoalIndex(GoalName);
	if (GoalIndex == INDEX_NONE)
	{
		return nullptr; // no goal with that index
	}

	if (!IKRigAsset->Solvers.IsValidIndex(SolverIndex))
	{
		return nullptr; // no solver with that index
	}

	return IKRigAsset->Solvers[SolverIndex]->GetEffectorWithGoal(GoalName);
}

FTransform UIKRigController::GetGoalCurrentTransform(const FName& GoalName) const
{
	if(const UIKRigEffectorGoal* Goal = GetGoal(GoalName))
	{
		return Goal->CurrentTransform;
	}
	
	return FTransform::Identity; // no goal with that name
}

void UIKRigController::SetGoalCurrentTransform(const FName& GoalName, const FTransform& Transform) const
{
	UIKRigEffectorGoal* Goal = const_cast<UIKRigEffectorGoal*>(GetGoal(GoalName));
	check(Goal);

	Goal->CurrentTransform = Transform;
}

void UIKRigController::ResetGoalTransforms() const
{
	for (UIKRigEffectorGoal* Goal : IKRigAsset->Goals)
    {
    	const FTransform InitialTransform = GetBoneRetargetPose(Goal->BoneName);
    	Goal->InitialTransform = InitialTransform;
    	Goal->CurrentTransform = InitialTransform;
    }	
}

void UIKRigController::SanitizeGoalName(FString& InOutName)
{
	for (int32 i = 0; i < InOutName.Len(); ++i)
	{
		TCHAR& C = InOutName[i];

		const bool bGoodChar =
            ((C >= 'A') && (C <= 'Z')) || ((C >= 'a') && (C <= 'z')) ||		// A-Z (upper and lowercase) anytime
            (C == '_') || (C == '-') || (C == '.') ||						// _  - . anytime
            ((i > 0) && (C >= '0') && (C <= '9'));							// 0-9 after the first character

		if (!bGoodChar)
		{
			C = '_';
		}
	}

	const int32 MaxNameLength = 20;
	if (InOutName.Len() > MaxNameLength)
	{
		InOutName.LeftChopInline(InOutName.Len() - MaxNameLength);
	}
}

int32 UIKRigController::GetGoalIndex(const FName& GoalName) const
{
	for (int32 i=0; i<IKRigAsset->Goals.Num(); ++i)
	{	
		if (IKRigAsset->Goals[i]->GoalName == GoalName)
		{
			return i;
		}
	}

	return INDEX_NONE;
}

FName UIKRigController::GetGoalName(const int32& GoalIndex) const
{
	if (!IKRigAsset->Goals.IsValidIndex(GoalIndex))
	{
		return NAME_None;
	}

	return IKRigAsset->Goals[GoalIndex]->GoalName;
}

#undef LOCTEXT_NAMESPACE
