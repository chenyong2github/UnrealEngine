// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigEditor/IKRigEditorController.h"

#include "IPersonaToolkit.h"
#include "SKismetInspector.h"
#include "Dialogs/CustomDialog.h"
#include "Dialogs/Dialogs.h"

#include "RigEditor/IKRigController.h"
#include "RigEditor/SIKRigSkeleton.h"
#include "RigEditor/SIKRigSolverStack.h"
#include "RigEditor/IKRigAnimInstance.h"

#define LOCTEXT_NAMESPACE "IKRigEditorController"

void FIKRigEditorController::Initialize(TSharedPtr<FIKRigEditorToolkit> Toolkit, UIKRigDefinition* IKRigAsset)
{
	EditorToolkit = Toolkit;
	AssetController = UIKRigController::GetIKRigController(IKRigAsset);
	
	// register callback to be informed when rig asset is modified by editor
	if (!AssetController->OnIKRigNeedsInitialized().IsBoundToObject(this))
	{
		AssetController->OnIKRigNeedsInitialized().AddSP(this, &FIKRigEditorController::OnIKRigNeedsInitialized);

		// Initialize editor's instances at first initialization
		InitializeSolvers();
	}
}

void FIKRigEditorController::OnIKRigNeedsInitialized(UIKRigDefinition* ModifiedIKRig)
{
	if (ModifiedIKRig != AssetController->GetAsset())
	{
		return;
	}

	AnimInstance->SetProcessorNeedsInitialized();

	// Initialize editor's instances on request
	InitializeSolvers();
}

void FIKRigEditorController::Reset() const
{
	AssetController->ResetGoalTransforms();
}

void FIKRigEditorController::RefreshAllViews() const
{
	SolverStackView->RefreshStackView();
	SkeletonView->RefreshTreeView();
	DetailsView->ForceRefresh();
	RetargetingView->RefreshView();
}

void FIKRigEditorController::AddNewGoals(const TArray<FName>& GoalNames, const TArray<FName>& BoneNames)
{
	check(GoalNames.Num() == BoneNames.Num());

	// add a default solver if there isn't one already
	if (AssetController->GetNumSolvers() == 0)
	{
		if (!PromptToAddSolver())
		{
			return; // user cancelled
		}
	}

	// get selected solvers
	TArray<TSharedPtr<FSolverStackElement>> SelectedSolvers;
	GetSelectedSolvers(SelectedSolvers);

	// create goals
	FName LastCreatedGoalName = NAME_None;
	for (int32 I=0; I<GoalNames.Num(); ++I)
	{
		const FName& GoalName = GoalNames[I];
		const FName& BoneName = BoneNames[I];

		// create a new goal
		UIKRigEffectorGoal* NewGoal = AssetController->AddNewGoal(GoalName, BoneName);
		if (!NewGoal)
		{
			continue; // already exists
		}
		
		// connect the new goal to all the selected solvers
		for (const TSharedPtr<FSolverStackElement>& SolverElement : SelectedSolvers)
		{
			AssetController->ConnectGoalToSolver(*NewGoal, SolverElement->IndexInStack);	
		}

		LastCreatedGoalName = GoalName;
	}
	
	// were any goals created?
	if (LastCreatedGoalName != NAME_None)
	{
		// show last created goal in details view
		ShowDetailsForGoal(LastCreatedGoalName);
		// update all views
		RefreshAllViews();
	}
}

void FIKRigEditorController::DeleteGoal(const FName& GoalToDelete)
{
	AssetController->RemoveGoal(GoalToDelete);
	SelectedGoals.Remove(GoalToDelete);
}

bool FIKRigEditorController::IsGoalSelected(const FName& GoalName) const
{
	return SelectedGoals.Contains(GoalName);
}

void FIKRigEditorController::ReplaceGoalInSelection(const FName& OldName, const FName& NewName)
{
	const int32 GoalIndex = SelectedGoals.IndexOfByKey(OldName);
	if (GoalIndex == INDEX_NONE)
	{
		return;
	}

	SelectedGoals[GoalIndex] = NewName;
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
	}
	else
	{
		if (GoalName != NAME_None)
		{
			const bool bAlreadySelected = SelectedGoals.Contains(GoalName);
			if (bAlreadySelected)
			{
				SelectedGoals.Remove(GoalName);	
			}
			else
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

bool FIKRigEditorController::PromptToAddSolver() const
{
	if (AssetController->GetNumSolvers() > 0)
	{
		return true;
	}

	// prompt user to add a default solver
	FIKRigAddFirstSolverSettings Settings;
	const TSharedPtr<FStructOnScope> StructToDisplay = MakeShareable(new FStructOnScope(FIKRigAddFirstSolverSettings::StaticStruct(), (uint8*)&Settings));
	TSharedRef<SKismetInspector> KismetInspector = SNew(SKismetInspector);
	KismetInspector->ShowSingleStruct(StructToDisplay);
	
	TSharedRef<SCustomDialog> AddSolverDialog = SNew(SCustomDialog)
		.Title(FText(LOCTEXT("EditorController_IKRigFirstSolver", "Add Default Solver")))
		.DialogContent(KismetInspector)
		.Buttons({
			SCustomDialog::FButton(LOCTEXT("OK", "OK")),
			SCustomDialog::FButton(LOCTEXT("Cancel", "Cancel"))
	});

	if (AddSolverDialog->ShowModal() == 1)
	{
		return false; // cancelled
	}

	if (Settings.SolverType != nullptr)
	{
		AssetController->AddSolver(Settings.SolverType);
		SolverStackView->RefreshStackView();
	}

	return true;
}

bool FIKRigEditorController::IsElementConnectedToSolver(TSharedRef<FIKRigTreeElement> TreeElement, int32 SolverIndex)
{
	if (!AssetController->GetSolverArray().IsValidIndex(SolverIndex))
	{
		return false; // not a valid solver index
	}

	const UIKRigSolver* Solver = AssetController->GetSolver(SolverIndex);
	if (TreeElement->ElementType == IKRigTreeElementType::BONE)
	{
		// is this bone affected by this solver?
		return Solver->IsBoneAffectedBySolver(TreeElement->BoneName, AssetController->GetIKRigSkeleton());
	}

	if (TreeElement->ElementType == IKRigTreeElementType::BONE_SETTINGS)
	{
		// is this bone setting belonging to the solver?
		return (Solver->GetBoneSetting(TreeElement->BoneSettingBoneName) != nullptr);
	}

	if (TreeElement->ElementType == IKRigTreeElementType::GOAL)
	{
		// is goal connected to the solver?
		return AssetController->IsGoalConnectedToSolver(TreeElement->GoalName, SolverIndex);
	}

	if (TreeElement->ElementType == IKRigTreeElementType::SOLVERGOAL)
	{
		// is this an effector for this solver?
		return TreeElement->SolverGoalIndex == SolverIndex;
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

bool FIKRigEditorController::IsElementExcludedBone(TSharedRef<FIKRigTreeElement> TreeElement)
{
	if (TreeElement->ElementType != IKRigTreeElementType::BONE)
	{
		return false;
	}
	
	// is this bone excluded?
	return AssetController->GetBoneExcluded(TreeElement->BoneName);
}

void FIKRigEditorController::ShowDetailsForBone(const FName BoneName)
{
	ShowEmptyDetails();
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
	DetailsView->SetObject(AssetController->GetGoal(GoalName));
}

void FIKRigEditorController::ShowDetailsForGoalSettings(const FName GoalName, const int32 SolverIndex)
{
	// get solver that owns this effector
	if (const UIKRigSolver* SolverWithEffector = AssetController->GetSolver(SolverIndex))
	{
		if (UObject* EffectorSettings = SolverWithEffector->GetGoalSettings(GoalName))
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
	DetailsView->SetObject(AssetController->GetAsset());
}

void FIKRigEditorController::AddNewRetargetChain(const FName ChainName, const FName StartBone, const FName EndBone)
{
	FIKRigRetargetChainSettings Settings(ChainName, StartBone, EndBone);
	const TSharedPtr<FStructOnScope> StructToDisplay = MakeShareable(new FStructOnScope(FIKRigRetargetChainSettings::StaticStruct(), (uint8*)&Settings));
	TSharedRef<SKismetInspector> KismetInspector = SNew(SKismetInspector);
	KismetInspector->ShowSingleStruct(StructToDisplay);

	SGenericDialogWidget::FArguments DialogArguments;
	DialogArguments.OnOkPressed_Lambda([&Settings, this] ()
	{
		// add the retarget chain
		AssetController->AddRetargetChain(Settings.ChainName, Settings.StartBone, Settings.EndBone);
		RefreshAllViews();
	});

	SGenericDialogWidget::OpenDialog(
		LOCTEXT("SIKRigRetargetChains", "Add New Retarget Chain"),
		KismetInspector,
		DialogArguments,
		true);
}

void FIKRigEditorController::PlayAnimationAsset(UAnimationAsset* AssetToPlay)
{
	if (AssetToPlay && AnimInstance.IsValid())
	{
		AnimInstance->SetAnimationAsset(AssetToPlay);
	}
}

void FIKRigEditorController::InitializeSolvers() const
{
	if (AssetController)
	{
		const FIKRigSkeleton& IKRigSkeleton = AssetController->GetIKRigSkeleton();
		const TArray<UIKRigSolver*>& Solvers = AssetController->GetSolverArray(); 
		for (UIKRigSolver* Solver: Solvers)
		{
			Solver->Initialize(IKRigSkeleton);
		}
	}
}

#undef LOCTEXT_NAMESPACE
