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

#if WITH_EDITOR

#include "HAL/PlatformApplicationMisc.h"

#endif

#define LOCTEXT_NAMESPACE "IKRigEditorController"

bool UIKRigBoneDetails::CurrentTransformRelative[3] = {true, true, true};
bool UIKRigBoneDetails::ReferenceTransformRelative[3] = {true, true, true};

TOptional<FTransform> UIKRigBoneDetails::GetTransform(EIKRigTransformType::Type TransformType) const
{
	if(!AnimInstancePtr.IsValid() || !AssetPtr.IsValid())
	{
		return TOptional<FTransform>();
	}
	
	FTransform LocalTransform = FTransform::Identity;
	FTransform GlobalTransform = FTransform::Identity;
	const bool* IsRelative = nullptr;

	const int32 BoneIndex = AssetPtr->Skeleton.GetBoneIndexFromName(SelectedBone);
	if(BoneIndex == INDEX_NONE)
	{
		return TOptional<FTransform>();
	}

	switch(TransformType)
	{
		case EIKRigTransformType::Current:
		{
			IsRelative = CurrentTransformRelative;
			
			USkeletalMeshComponent* SkeletalMeshComponent = AnimInstancePtr->GetSkelMeshComponent();
			const bool IsSkelMeshValid = SkeletalMeshComponent != nullptr &&
										SkeletalMeshComponent->SkeletalMesh != nullptr;
			if (IsSkelMeshValid)
			{
				GlobalTransform = SkeletalMeshComponent->GetBoneTransform(BoneIndex);
				const TArray<FTransform>& LocalTransforms = SkeletalMeshComponent->GetBoneSpaceTransforms();
				LocalTransform = LocalTransforms.IsValidIndex(BoneIndex) ? LocalTransforms[BoneIndex] : FTransform::Identity;
			}
			else
			{
				GlobalTransform = AssetPtr->Skeleton.CurrentPoseGlobal[BoneIndex];
				LocalTransform = AssetPtr->Skeleton.CurrentPoseLocal[BoneIndex];
			}
			break;
		}
		case EIKRigTransformType::Reference:
		{
			IsRelative = ReferenceTransformRelative;
			GlobalTransform = AssetPtr->Skeleton.RefPoseGlobal[BoneIndex];
			LocalTransform = GlobalTransform;
			const int32 ParentBoneIndex = AssetPtr->Skeleton.ParentIndices[BoneIndex];
			if(ParentBoneIndex != INDEX_NONE)
			{
				const FTransform ParentTransform = AssetPtr->Skeleton.RefPoseGlobal[ParentBoneIndex];;
				LocalTransform = GlobalTransform.GetRelativeTransform(ParentTransform);
			}
			break;
		}
	}
	checkSlow(IsRelative);

	FTransform Transform = LocalTransform;
	if(!IsRelative[0]) Transform.SetLocation(GlobalTransform.GetLocation());
	if(!IsRelative[1]) Transform.SetRotation(GlobalTransform.GetRotation());
	if(!IsRelative[2]) Transform.SetScale3D(GlobalTransform.GetScale3D());
	return Transform;
}

bool UIKRigBoneDetails::IsComponentRelative(
	ESlateTransformComponent::Type Component,
	EIKRigTransformType::Type TransformType) const
{
	switch(TransformType)
	{
		case EIKRigTransformType::Current:
		{
			return CurrentTransformRelative[(int32)Component]; 
		}
		case EIKRigTransformType::Reference:
		{
			return ReferenceTransformRelative[(int32)Component]; 
		}
	}
	return true;
}

void UIKRigBoneDetails::OnComponentRelativeChanged(
	ESlateTransformComponent::Type Component,
	bool bIsRelative,
	EIKRigTransformType::Type TransformType)
{
	switch(TransformType)
	{
		case EIKRigTransformType::Current:
		{
			CurrentTransformRelative[(int32)Component] = bIsRelative;
			break; 
		}
		case EIKRigTransformType::Reference:
		{
			ReferenceTransformRelative[(int32)Component] = bIsRelative;
			break; 
		}
	}
}

#if WITH_EDITOR

void UIKRigBoneDetails::OnCopyToClipboard(ESlateTransformComponent::Type Component, EIKRigTransformType::Type TransformType)
{
	TOptional<FTransform> Optional = GetTransform(TransformType);
	if(!Optional.IsSet())
	{
		return;
	}

	const FTransform Xfo = Optional.GetValue();
	
	FString Content;
	switch(Component)
	{
	case ESlateTransformComponent::Location:
		{
			const FVector Data = Xfo.GetLocation();
			TBaseStructure<FVector>::Get()->ExportText(Content, &Data, &Data, nullptr, PPF_None, nullptr);
			break;
		}
	case ESlateTransformComponent::Rotation:
		{
			const FRotator Data = Xfo.Rotator();
			TBaseStructure<FRotator>::Get()->ExportText(Content, &Data, &Data, nullptr, PPF_None, nullptr);
			break;
		}
	case ESlateTransformComponent::Scale:
		{
			const FVector Data = Xfo.GetScale3D();
			TBaseStructure<FVector>::Get()->ExportText(Content, &Data, &Data, nullptr, PPF_None, nullptr);
			break;
		}
	case ESlateTransformComponent::Max:
	default:
		{
			TBaseStructure<FTransform>::Get()->ExportText(Content, &Xfo, &Xfo, nullptr, PPF_None, nullptr);
			break;
		}
	}

	if(!Content.IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardCopy(*Content);
	}
}

void UIKRigBoneDetails::OnPasteFromClipboard(ESlateTransformComponent::Type Component, EIKRigTransformType::Type TransformType)
{
	// paste is not supported yet.
}

#endif

void FIKRigEditorController::Initialize(TSharedPtr<FIKRigEditorToolkit> Toolkit, UIKRigDefinition* IKRigAsset)
{
	EditorToolkit = Toolkit;
	AssetController = UIKRigController::GetIKRigController(IKRigAsset);
	BoneDetails = NewObject<UIKRigBoneDetails>();
	
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

	// update the bone details so it can pull on the current data
	BoneDetails->AnimInstancePtr = AnimInstance;
	BoneDetails->AssetPtr = ModifiedIKRig;
}

void FIKRigEditorController::Reset() const
{
	AssetController->ResetGoalTransforms();
}

void FIKRigEditorController::RefreshAllViews() const
{
	if (SolverStackView.IsValid())
	{
		SolverStackView->RefreshStackView();
	}

	if (SkeletonView.IsValid())
	{
		SkeletonView->RefreshTreeView();
	}

	if (DetailsView.IsValid())
	{
		DetailsView->ForceRefresh();
	}

	if (RetargetingView.IsValid())
	{
		RetargetingView->RefreshView();
	}
}

void FIKRigEditorController::RefreshTreeView() const
{
	if (SkeletonView.IsValid())
	{
		SkeletonView->RefreshTreeView();
	}
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

void FIKRigEditorController::ClearSelection()
{
	if (SkeletonView.IsValid())
	{
		SkeletonView->TreeView->ClearSelection();	
	}
	
	ShowEmptyDetails();
}

void FIKRigEditorController::HandleGoalSelectedInViewport(const FName& GoalName, bool bReplace)
{
	if (SkeletonView.IsValid())
	{
		SkeletonView->AddSelectedItemFromViewport(GoalName, IKRigTreeElementType::GOAL, bReplace);
	}
	
	ShowDetailsForGoal(GoalName);
}

void FIKRigEditorController::HandleBoneSelectedInViewport(const FName& BoneName, bool bReplace)
{
	if (SkeletonView.IsValid())
	{
		SkeletonView->AddSelectedItemFromViewport(BoneName, IKRigTreeElementType::BONE, bReplace);
	}
	
	ShowDetailsForBone(BoneName);
}

void FIKRigEditorController::GetSelectedSolvers(TArray<TSharedPtr<FSolverStackElement>>& OutSelectedSolvers)
{
	if (SolverStackView.IsValid())
	{
		OutSelectedSolvers.Reset();
		OutSelectedSolvers.Append(SolverStackView->ListView->GetSelectedItems());
	}
}

int32 FIKRigEditorController::GetSelectedSolverIndex()
{
	if (!SolverStackView.IsValid())
	{
		return INDEX_NONE;
	}
	
	TArray<TSharedPtr<FSolverStackElement>> SelectedSolvers = SolverStackView->ListView->GetSelectedItems();
	if (SelectedSolvers.IsEmpty())
	{
		return INDEX_NONE;
	}

	return SelectedSolvers[0]->IndexInStack;
}

void FIKRigEditorController::GetSelectedGoalNames(TArray<FName>& OutGoalNames) const
{
	if (!SkeletonView.IsValid())
	{
		return;
	}

	SkeletonView->GetSelectedGoalNames(OutGoalNames);
}

int32 FIKRigEditorController::GetNumSelectedGoals() const
{
	if (!SkeletonView.IsValid())
	{
		return 0;
	}

	return SkeletonView->GetNumSelectedGoals();
}

void FIKRigEditorController::GetSelectedBoneNames(TArray<FName>& OutBoneNames) const
{
	if (!SkeletonView.IsValid())
	{
		return;
	}

	SkeletonView->GetSelectedBoneNames(OutBoneNames);
}

void FIKRigEditorController::GetSelectedBones(TArray<TSharedPtr<FIKRigTreeElement>>& OutBoneItems) const
{
	if (!SkeletonView.IsValid())
	{
		return;
	}

	SkeletonView->GetSelectedBones(OutBoneItems);
}

bool FIKRigEditorController::IsGoalSelected(const FName& GoalName) const
{
	if (!SkeletonView.IsValid())
	{
		return false;
	}

	return SkeletonView->IsGoalSelected(GoalName);
}

FName FIKRigEditorController::GetSelectedChain() const
{
	if (!RetargetingView.IsValid())
	{
		return NAME_None;
	}

	return RetargetingView->GetSelectedChain();
}

bool FIKRigEditorController::DoesSkeletonHaveSelectedItems() const
{
	if (!SkeletonView.IsValid())
	{
		return false;
	}
	return SkeletonView->HasSelectedItems();
}

bool FIKRigEditorController::PromptToAddSolver() const
{
	if (AssetController->GetNumSolvers() > 0)
	{
		return true;
	}

	TArray<TSharedPtr<FIKRigSolverTypeAndName>> SolverTypes;
	for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
	{
		UClass* Class = *ClassIt;
		if (!Class->IsNative())
		{
			continue;
		}

		if (!ClassIt->IsChildOf(UIKRigSolver::StaticClass()))
		{
			continue;
		}

		if (Class == UIKRigSolver::StaticClass())
		{
			continue; // skip base class
		}

		const UIKRigSolver* SolverCDO = CastChecked<UIKRigSolver>(Class->ClassDefaultObject);
		TSharedPtr<FIKRigSolverTypeAndName> SolverType = MakeShared<FIKRigSolverTypeAndName>();
		SolverType->NiceName = SolverCDO->GetNiceName();
		SolverType->SolverType = TSubclassOf<UIKRigSolver>(Class);
		SolverTypes.Add(SolverType);
	}

	TSharedPtr<FIKRigSolverTypeAndName> SelectedSolver = SolverTypes[0];
	TSharedRef<SComboBox<TSharedPtr<FIKRigSolverTypeAndName>>> SolverOptionBox = SNew(SComboBox<TSharedPtr<FIKRigSolverTypeAndName>>)
	.OptionsSource(&SolverTypes)
	.OnGenerateWidget_Lambda([](TSharedPtr<FIKRigSolverTypeAndName> Item)
	{
		return SNew(STextBlock).Text(Item->NiceName);
	})
	.OnSelectionChanged_Lambda([&SelectedSolver](TSharedPtr<FIKRigSolverTypeAndName> Item, ESelectInfo::Type)
	{
		SelectedSolver = Item;
	})
	.Content()
	[
		SNew(STextBlock).Text_Lambda([&SelectedSolver]()
			{
			return SelectedSolver->NiceName;
		})
	];
	
	TSharedRef<SCustomDialog> AddSolverDialog = SNew(SCustomDialog)
		.Title(FText(LOCTEXT("EditorController_IKRigFirstSolver", "Add Default Solver")))
		.DialogContent(SolverOptionBox)
		.Buttons({
			SCustomDialog::FButton(LOCTEXT("OK", "OK")),
			SCustomDialog::FButton(LOCTEXT("Cancel", "Cancel"))
	});

	if (AddSolverDialog->ShowModal() != 0)
	{
		return false; // cancel button pressed, or window closed
	}

	if (SelectedSolver->SolverType != nullptr && SolverStackView.IsValid())
	{
		SolverStackView->AddNewSolver(SelectedSolver->SolverType);
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
	BoneDetails->SetBone(BoneName);
	DetailsView->SetObject(BoneDetails);
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

void FIKRigEditorController::ShowDetailsForElements(const TArray<TSharedPtr<FIKRigTreeElement>>& InItems)
{
	// TODO
	// this function is currently only dealing with displaying a single element in the details panel (the last one)
	// as this is the current state of the details panel in the IKRig editor. However, it should be able to handle
	// multiple items. This is a new feature that has to be done that's why this function's interface takes an array
	// of items as parameter for future implementation.
	
	if (InItems.Num() > 0)
	{
		const TSharedPtr<FIKRigTreeElement>& LastItem = InItems.Last();
		switch (LastItem->ElementType)
		{
			case IKRigTreeElementType::BONE:
				ShowDetailsForBone(LastItem->BoneName);
				break;
			
			case IKRigTreeElementType::GOAL:
				ShowDetailsForGoal(LastItem->GoalName);
				break;
			
			case IKRigTreeElementType::SOLVERGOAL:
				ShowDetailsForGoalSettings(LastItem->SolverGoalName, LastItem->SolverGoalIndex);
				break;
			
			case IKRigTreeElementType::BONE_SETTINGS:
				ShowDetailsForBoneSettings(LastItem->BoneSettingBoneName, LastItem->BoneSettingsSolverIndex);
				break;
			
			default:
				ensure(false);
				break;
		}
	}
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

EIKRigSelectionType FIKRigEditorController::GetLastSelectedType() const
{
	return LastSelectedType;
}

void FIKRigEditorController::SetLastSelectedType(EIKRigSelectionType SelectionType)
{
	LastSelectedType = SelectionType;
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
