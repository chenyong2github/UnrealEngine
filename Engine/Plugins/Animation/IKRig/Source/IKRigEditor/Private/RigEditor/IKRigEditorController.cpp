// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigEditor/IKRigEditorController.h"

#include "SKismetInspector.h"
#include "Dialogs/Dialogs.h"

#include "RigEditor/IKRigController.h"
#include "RigEditor/SIKRigSkeleton.h"
#include "RigEditor/SIKRigSolverStack.h"
#include "RigEditor/IKRigAnimInstance.h"

#define LOCTEXT_NAMESPACE "IKRigEditorController"

void FIKRigEditorController::Reset() const
{
	const TArray<UIKRigEffectorGoal*>& Goals = AssetController->GetAllGoals();
	for (UIKRigEffectorGoal* Goal : Goals)
	{
		Goal->CurrentTransform = Goal->InitialTransform;
	}
}

bool FIKRigEditorController::IsGoalSelected(const FName& GoalName)
{
	return SelectedGoals.Contains(GoalName);
}

int32 FIKRigEditorController::GetNumSelectedGoals()
{
	return SelectedGoals.Num();
}

void FIKRigEditorController::HandleGoalSelectedInViewport(const FName& GoalName, bool bReplace)
{
	if (bReplace)
	{
		SelectedGoals.Reset();
		if (GoalName != NAME_None)
		{
			SelectedGoals.Add(GoalName);
		}
	}else
	{
		if (GoalName != NAME_None)
		{
			const bool bAlreadySelected = SelectedGoals.Contains(GoalName);
			if (bAlreadySelected)
			{
				SelectedGoals.Remove(GoalName);	
			}else
			{
				SelectedGoals.Add(GoalName);
			}
		}	
	}
	
	SkeletonView->SetSelectedGoalsFromViewport(SelectedGoals);
	ShowDetailsForGoal(GoalName);
}

void FIKRigEditorController::HandleGoalsSelectedInTreeView(const TArray<FName>& GoalNames)
{
	SelectedGoals = GoalNames;
}

void FIKRigEditorController::GetSelectedSolvers(TArray<TSharedPtr<FSolverStackElement>>& OutSelectedSolvers)
{
	OutSelectedSolvers.Reset();
	OutSelectedSolvers.Append(SolverStackView->ListView->GetSelectedItems());
}

int32 FIKRigEditorController::GetSelectedSolverIndex()
{
	TArray<TSharedPtr<FSolverStackElement>> SelectedSolvers = SolverStackView->ListView->GetSelectedItems();
	if (SelectedSolvers.IsEmpty())
	{
		return INDEX_NONE;
	}

	return SelectedSolvers[0]->IndexInStack;
}

void FIKRigEditorController::PromptToAddSolverAtStartup() const
{
	if (AssetController->GetNumSolvers() > 0)
	{
		return;
	}

	// prompt user to add a default solver
	FIKRigAddFirstSolverSettings Settings;
	const TSharedPtr<FStructOnScope> StructToDisplay = MakeShareable(new FStructOnScope(FIKRigAddFirstSolverSettings::StaticStruct(), (uint8*)&Settings));
	TSharedRef<SKismetInspector> KismetInspector = SNew(SKismetInspector);
	KismetInspector->ShowSingleStruct(StructToDisplay);

	SGenericDialogWidget::OpenDialog(
        LOCTEXT("EditorController_IKRigFirstSolver", "Add Default Solver"), 
        KismetInspector, 
        SGenericDialogWidget::FArguments(), 
        true);

	if (Settings.SolverType != nullptr)
	{
		AssetController->AddSolver(Settings.SolverType);
		SolverStackView->RefreshStackView();
	}
}

bool FIKRigEditorController::IsElementConnectedToSolver(TSharedRef<FIKRigTreeElement> TreeElement, int32 SolverIndex)
{
	if (!AssetController->GetSolverArray().IsValidIndex(SolverIndex))
	{
		return false; // not a valid solver index
	}

	UIKRigSolver* Solver = AssetController->GetSolver(SolverIndex);
	if (TreeElement->ElementType == IKRigTreeElementType::BONE)
	{
		// is this bone affected by this solver?
		return Solver->IsBoneAffectedBySolver(TreeElement->Key, AssetController->GetSkeleton());
	}

	if (TreeElement->ElementType == IKRigTreeElementType::BONE_SETTINGS)
	{
		// is this bone setting belonging to the solver?
		return (Solver->GetBoneSetting(TreeElement->BoneSettingBoneName) != nullptr);
	}

	if (TreeElement->ElementType == IKRigTreeElementType::GOAL)
	{
		// is goal connected to the solver?
		return AssetController->IsGoalConnectedToSolver(TreeElement->Key, SolverIndex);
	}

	if (TreeElement->ElementType == IKRigTreeElementType::EFFECTOR)
	{
		// is this an effector for this solver?
		return TreeElement->EffectorSolverIndex == SolverIndex;
	}

	checkNoEntry();
	return false;
}

bool FIKRigEditorController::IsElementConnectedToAnySolver(TSharedRef<FIKRigTreeElement> TreeElement)
{
	const int32 NumSolvers = AssetController->GetNumSolvers();
	for (int32 SolverIndex=0; SolverIndex<NumSolvers; ++SolverIndex)
	{
		if (IsElementConnectedToSolver(TreeElement, SolverIndex))
		{
			return true;
		}
	}

	return false;
}

void FIKRigEditorController::ShowDetailsForBone(const FName BoneName)
{
	ShowEmptyDetails();
	// this can be useful during development/debugging, but is slow/confusing so should not be left enabled
	//DetailsView->SetObject(AssetController->GetAsset());
}

void FIKRigEditorController::ShowDetailsForBoneSettings(const FName BoneName, int32 SolverIndex)
{
	if (UObject* BoneSettings = AssetController->GetSettingsForBone(BoneName, SolverIndex))
	{
		DetailsView->SetObject(BoneSettings);
	}
}

void FIKRigEditorController::ShowDetailsForGoal(const FName GoalName)
{
	UIKRigEffectorGoal* Goal = AssetController->GetGoal(GoalName);
	DetailsView->SetObject(Goal);
}

void FIKRigEditorController::ShowDetailsForEffector(const FName GoalName, const int32 SolverIndex)
{
	// get solver that owns this effector
	if (UIKRigSolver* SolverWithEffector = AssetController->GetSolver(SolverIndex))
	{
		if (UObject* EffectorSettings = SolverWithEffector->GetEffectorWithGoal(GoalName))
		{
			DetailsView->SetObject(EffectorSettings);
		}
	}
}

void FIKRigEditorController::ShowDetailsForSolver(const int32 SolverIndex)
{
	DetailsView->SetObject(AssetController->GetSolver(SolverIndex));
}

void FIKRigEditorController::ShowEmptyDetails()
{
	DetailsView->SetObject(nullptr);
}

#undef LOCTEXT_NAMESPACE
