// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigEditor/IKRigEditorController.h"

#include "IPersonaToolkit.h"
#include "SKismetInspector.h"
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
	}
}

void FIKRigEditorController::OnIKRigNeedsInitialized(UIKRigDefinition* ModifiedIKRig)
{
	if (ModifiedIKRig != AssetController->GetAsset())
	{
		return;
	}

	AnimInstance->SetProcessorNeedsInitialized();
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
		PromptToAddSolver();	
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

void FIKRigEditorController::PromptToAddSolver() const
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

	const UIKRigSolver* Solver = AssetController->GetSolver(SolverIndex);
	if (TreeElement->ElementType == IKRigTreeElementType::BONE)
	{
		// is this bone affected by this solver?
		return Solver->IsBoneAffectedBySolver(TreeElement->Key, AssetController->GetIKRigSkeleton());
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

bool FIKRigEditorController::IsElementExcludedBone(TSharedRef<FIKRigTreeElement> TreeElement)
{
	if (TreeElement->ElementType != IKRigTreeElementType::BONE)
	{
		return false;
	}
	
	// is this bone excluded?
	return AssetController->GetBoneExcluded(TreeElement->Key);
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
	const UIKRigEffectorGoal* Goal = AssetController->GetGoal(GoalName);
	DetailsView->SetObject(const_cast<UIKRigEffectorGoal*>(Goal));
}

void FIKRigEditorController::ShowDetailsForEffector(const FName GoalName, const int32 SolverIndex)
{
	// get solver that owns this effector
	if (const UIKRigSolver* SolverWithEffector = AssetController->GetSolver(SolverIndex))
	{
		if (UObject* EffectorSettings = SolverWithEffector->GetEffectorWithGoal(GoalName))
		{
			DetailsView->SetObject(EffectorSettings);
		}
	}
}

void FIKRigEditorController::ShowDetailsForSolver(const int32 SolverIndex)
{
	DetailsView->SetObject(const_cast<UIKRigSolver*>(AssetController->GetSolver(SolverIndex)));
}

void FIKRigEditorController::ShowEmptyDetails()
{
	DetailsView->SetObject(const_cast<UIKRigDefinition*>(AssetController->GetAsset()));
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

#undef LOCTEXT_NAMESPACE
