// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigEditor/IKRigController.h"

#include "IKRigDefinition.h"
#include "IKRigSolver.h"

#include "Engine/SkeletalMesh.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE	"IKRigController"


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
	UIKRigSolver* Solver = GetSolver(SolverIndex);
	if (!Solver)
	{
		return; // solver doesn't exist
	}

	if (IKRigAsset->Skeleton.GetBoneIndexFromName(BoneName) == INDEX_NONE)
	{
		return; // bone doesn't exist
	}

	FScopedTransaction Transaction(LOCTEXT("AddBoneSetting_Label", "Add Bone Setting"));
	IKRigAsset->Modify();

	Solver->AddBoneSetting(BoneName);
}

bool UIKRigController::CanAddBoneSetting(const FName& BoneName, int32 SolverIndex) const
{
	UIKRigSolver* Solver = GetSolver(SolverIndex);
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
	UIKRigSolver* Solver = GetSolver(SolverIndex);
	if (!Solver)
	{
		return; // solver doesn't exist
	}

	if (IKRigAsset->Skeleton.GetBoneIndexFromName(BoneName) == INDEX_NONE)
	{
		return; // bone doesn't exist
	}

	FScopedTransaction Transaction(LOCTEXT("AddBoneSetting_Label", "Add Bone Setting"));
	IKRigAsset->Modify();

	Solver->RemoveBoneSetting(BoneName);
}

bool UIKRigController::CanRemoveBoneSetting(const FName& BoneName, int32 SolverIndex) const
{
	UIKRigSolver* Solver = GetSolver(SolverIndex);
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
	UIKRigSolver* Solver = GetSolver(SolverIndex);
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

	if (!bReImportBones)
	{
		return;
	}

	// reimport the skeleton data
	if (SkeletalMesh)
	{
		SetSkeleton(SkeletalMesh->GetRefSkeleton());		
	}else
	{
		IKRigAsset->Skeleton.Reset();
	}
}

void UIKRigController::SetSkeleton(const FReferenceSkeleton& InSkeleton) const
{
	FScopedTransaction Transaction(LOCTEXT("SetSkeleton_Label", "Set Skeleton"));
	IKRigAsset->Modify();

	IKRigAsset->Skeleton.Initialize(InSkeleton);
}

FIKRigSkeleton& UIKRigController::GetSkeleton() const
{
	return IKRigAsset->Skeleton;
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

	UIKRigSolver* NewSolver = NewObject<UIKRigSolver>(IKRigAsset, InIKRigSolverClass);
	check(NewSolver);

	return IKRigAsset->Solvers.Add(NewSolver);
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

void UIKRigController::RemoveSolver(UIKRigSolver* SolverToDelete) const
{
	if (!(IKRigAsset && SolverToDelete))
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("RemoveSolver_Label", "Remove Solver"));
	IKRigAsset->Modify();

	IKRigAsset->Solvers.Remove(SolverToDelete);
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
	IKRigAsset->Modify();

	IKRigAsset->Solvers[SolverIndex]->SetRootBone(RootBoneName);
}

const TArray<UIKRigSolver*>& UIKRigController::GetSolverArray() const
{
	return IKRigAsset->Solvers;
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

	UIKRigEffectorGoal* NewGoal = NewObject<UIKRigEffectorGoal>(GetAsset(), UIKRigEffectorGoal::StaticClass());
	NewGoal->BoneName = BoneName;
	NewGoal->GoalName = GoalName;
	IKRigAsset->Goals.Add(NewGoal);

	// set initial transform
	NewGoal->InitialTransform = IKRigAsset->GetGoalInitialTransform(NewGoal);
	NewGoal->CurrentTransform = NewGoal->InitialTransform;
	
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
	IKRigAsset->Modify();

	// remove from all the solvers
	const FName& GoalToRemove = IKRigAsset->Goals[GoalIndex]->GoalName;
	for (UIKRigSolver* Solver : IKRigAsset->Solvers)
	{
		Solver->RemoveGoal(GoalToRemove);
	}

	// remove from core system
	IKRigAsset->Goals.RemoveAt(GoalIndex);
	
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
	IKRigAsset->Goals[GoalIndex]->GoalName = NewName;

	// rename in solvers
	for (UIKRigSolver* Solver : IKRigAsset->Solvers)
	{
		Solver->RenameGoal(OldName, NewName);
	}

	return NewName;
}

bool UIKRigController::SetGoalBone(const FName& GoalName, const FName& NewBoneName) const
{
	const int32 GoalIndex = GetGoalIndex(GoalName);
	if (GoalIndex == INDEX_NONE)
	{
		return false; // goal doesn't exist in the rig
	}

	const int32 BoneIndex = GetSkeleton().GetBoneIndexFromName(NewBoneName);
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
		Solver->SetGoalBone(GoalName, NewBoneName);
	}

	// update initial transforms
	IKRigAsset->ResetGoalTransforms();
	
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
	IKRigAsset->Modify();
	
	IKRigAsset->Solvers[SolverIndex]->AddGoal(Goal);
	return true;
}

bool UIKRigController::DisconnectGoalFromSolver(const FName& GoalToRemove, int32 SolverIndex) const
{
	// can't remove goal that is not present in the core
	check(GetGoalIndex(GoalToRemove) != INDEX_NONE);
	// can't remove goal from a solver with an invalid index
	check(IKRigAsset->Solvers.IsValidIndex(SolverIndex))

    FScopedTransaction Transaction(LOCTEXT("RemoveGoalFromSolver_Label", "Remove Goal"));
	IKRigAsset->Modify();
	
	IKRigAsset->Solvers[SolverIndex]->RemoveGoal(GoalToRemove);
	return true;
}

bool UIKRigController::IsGoalConnectedToSolver(const FName& Goal, int32 SolverIndex) const
{
	if (!IKRigAsset->Solvers.IsValidIndex(SolverIndex))
	{
		return false;
	}

	return IKRigAsset->Solvers[SolverIndex]->IsGoalConnected(Goal);
}

const TArray<UIKRigEffectorGoal*>& UIKRigController::GetAllGoals() const
{
	return IKRigAsset->Goals;
}

UIKRigEffectorGoal* UIKRigController::GetGoal(int32 GoalIndex) const
{
	if (!IKRigAsset->Goals.IsValidIndex(GoalIndex))
	{
		return nullptr;
	}

	return IKRigAsset->Goals[GoalIndex];
}

UIKRigEffectorGoal* UIKRigController::GetGoal(const FName& GoalName) const
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

FTransform UIKRigController::GetGoalInitialTransform(const FName& GoalName) const
{
	if(UIKRigEffectorGoal* Goal = GetGoal(GoalName))
	{
		return Goal->InitialTransform;
	}
	
	return FTransform::Identity; // no goal with that name
}

FTransform UIKRigController::GetGoalCurrentTransform(const FName& GoalName) const
{
	if(UIKRigEffectorGoal* Goal = GetGoal(GoalName))
	{
		return Goal->CurrentTransform;
	}
	
	return FTransform::Identity; // no goal with that name
}

void UIKRigController::SetGoalInitialTransform(const FName& GoalName, const FTransform& Transform) const
{
	UIKRigEffectorGoal* Goal = GetGoal(GoalName);
	check(Goal);

	Goal->InitialTransform = Transform;
}

void UIKRigController::SetGoalCurrentTransform(const FName& GoalName, const FTransform& Transform) const
{
	UIKRigEffectorGoal* Goal = GetGoal(GoalName);
	check(Goal);

	Goal->CurrentTransform = Transform;
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
