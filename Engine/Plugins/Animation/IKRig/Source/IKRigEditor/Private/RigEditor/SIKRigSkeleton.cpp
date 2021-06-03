// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigEditor/SIKRigSkeleton.h"

#include "IKRigSolver.h"
#include "Engine/SkeletalMesh.h"
#include "IPersonaToolkit.h"
#include "SKismetInspector.h"
#include "Dialogs/Dialogs.h"
#include "SEditorHeaderButton.h"
#include "RigEditor/IKRigEditorController.h"
#include "RigEditor/IKRigEditorStyle.h"
#include "RigEditor/IKRigSkeletonCommands.h"
#include "RigEditor/IKRigToolkit.h"
#include "RigEditor/SIKRigSolverStack.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Commands/UICommandList.h"

#define LOCTEXT_NAMESPACE "SIKRigSkeleton"

FIKRigTreeElement::FIKRigTreeElement(const FName& InKey, IKRigTreeElementType InType)
{
	Key = InKey;
	ElementType = InType;
}

TSharedRef<ITableRow> FIKRigTreeElement::MakeTreeRowWidget(
	TSharedRef<FIKRigEditorController> InEditorController,
	const TSharedRef<STableViewBase>& InOwnerTable,
	TSharedRef<FIKRigTreeElement> InRigTreeElement,
	TSharedRef<FUICommandList> InCommandList,
	TSharedPtr<SIKRigSkeleton> InSkeleton)
{
	return SNew(SIKRigSkeletonItem, InEditorController, InOwnerTable, InRigTreeElement, InCommandList, InSkeleton);
}

void FIKRigTreeElement::RequestRename()
{
	OnRenameRequested.ExecuteIfBound();
}

void SIKRigSkeletonItem::Construct(
	const FArguments& InArgs,
	TSharedRef<FIKRigEditorController> InEditorController,
    const TSharedRef<STableViewBase>& OwnerTable,
    TSharedRef<FIKRigTreeElement> InRigTreeElement,
    TSharedRef<FUICommandList> InCommandList,
    TSharedPtr<SIKRigSkeleton> InSkeleton)
{
	WeakRigTreeElement = InRigTreeElement;
	EditorController = InEditorController;

	// is this element affected by the selected solver?
	bool bIsConnectedToSelectedSolver;
	const int32 SelectedSolver = EditorController.Pin()->GetSelectedSolverIndex();
	if (SelectedSolver == INDEX_NONE)
	{
		bIsConnectedToSelectedSolver = EditorController.Pin()->IsElementConnectedToAnySolver(InRigTreeElement);
	}else
	{
		bIsConnectedToSelectedSolver = EditorController.Pin()->IsElementConnectedToSolver(InRigTreeElement, SelectedSolver);
	}
	
	// determine text style
	FSlateFontInfo TextStyle = FEditorStyle::GetWidgetStyle<FTextBlockStyle>("SkeletonTree.NormalFont").Font;
	// highlight elements connected to the selected solver
	if (bIsConnectedToSelectedSolver)
	{
		TextStyle = FIKRigEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("IKRig.Tree.AffectedBoneText").Font;
	}
		
	// determine which icon to use for tree element
	const FSlateBrush* Brush = FAppStyle::Get().GetBrush("SkeletonTree.Bone");
	switch(InRigTreeElement->ElementType)
	{
		case IKRigTreeElementType::BONE:
			if (bIsConnectedToSelectedSolver)
			{
				Brush = FAppStyle::Get().GetBrush("SkeletonTree.Bone");
			}else
			{
				Brush = FAppStyle::Get().GetBrush("SkeletonTree.BoneNonWeighted");
			}
			break;
		case IKRigTreeElementType::BONE_SETTINGS:
			Brush = FIKRigEditorStyle::Get().GetBrush("IKRig.Tree.BoneWithSettings");
			break;
		case IKRigTreeElementType::GOAL:
			Brush = FIKRigEditorStyle::Get().GetBrush("IKRig.Tree.Goal");
			break;
		case IKRigTreeElementType::EFFECTOR:
			Brush = FIKRigEditorStyle::Get().GetBrush("IKRig.Tree.Effector");
			break;
		default:
			checkNoEntry();
	}
	 
	TSharedPtr<SHorizontalBox> HorizontalBox;
	STableRow<TSharedPtr<FIKRigTreeElement>>::Construct(
        STableRow<TSharedPtr<FIKRigTreeElement>>::FArguments()
        .ShowWires(true)
        .OnDragDetected(InSkeleton.Get(), &SIKRigSkeleton::OnDragDetected)
        .OnCanAcceptDrop(InSkeleton.Get(), &SIKRigSkeleton::OnCanAcceptDrop)
        .OnAcceptDrop(InSkeleton.Get(), &SIKRigSkeleton::OnAcceptDrop)
        .Content()
        [
            SAssignNew(HorizontalBox, SHorizontalBox)
            + SHorizontalBox::Slot()
            .MaxWidth(18)
            .FillWidth(1.0)
            .HAlign(HAlign_Left)
            .VAlign(VAlign_Center)
            [
                SNew(SImage)
                .Image(Brush)
            ]
        ], OwnerTable);
	
	if (InRigTreeElement->ElementType == IKRigTreeElementType::BONE)
	{
		HorizontalBox->AddSlot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        [
			SNew(STextBlock)
			.Text(this, &SIKRigSkeletonItem::GetName)
			.Font(TextStyle)
        ];
	}else
	{
		TSharedPtr< SInlineEditableTextBlock > InlineWidget;
		HorizontalBox->AddSlot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        [
	        SAssignNew(InlineWidget, SInlineEditableTextBlock)
		    .Text(this, &SIKRigSkeletonItem::GetName)
		    .OnTextCommitted(this, &SIKRigSkeletonItem::OnNameCommitted)
		    .MultiLine(false)
        ];
		InRigTreeElement->OnRenameRequested.BindSP(InlineWidget.Get(), &SInlineEditableTextBlock::EnterEditingMode);
	}
}

void SIKRigSkeletonItem::OnNameCommitted(const FText& InText, ETextCommit::Type InCommitType) const
{
	check(WeakRigTreeElement.IsValid());

	if (!(InCommitType == ETextCommit::OnEnter || InCommitType == ETextCommit::OnUserMovedFocus))
	{
		return; // make sure user actually intends to commit a name change
	}
	
	const FName OldName = WeakRigTreeElement.Pin()->Key;
	const FName PotentialNewName = FName(InText.ToString());
	const FName NewName = EditorController.Pin()->AssetController->RenameGoal(OldName, PotentialNewName);
	if (NewName != NAME_None)
	{
		WeakRigTreeElement.Pin()->Key = NewName;
	}

	EditorController.Pin()->SkeletonView->RefreshTreeView();
}

FText SIKRigSkeletonItem::GetName() const
{
	return (FText::FromName(WeakRigTreeElement.Pin()->Key));
}

TSharedRef<FIKRigSkeletonDragDropOp> FIKRigSkeletonDragDropOp::New(TWeakPtr<FIKRigTreeElement> InElement)
{
	TSharedRef<FIKRigSkeletonDragDropOp> Operation = MakeShared<FIKRigSkeletonDragDropOp>();
	Operation->Element = InElement;
	Operation->Construct();
	return Operation;
}

TSharedPtr<SWidget> FIKRigSkeletonDragDropOp::GetDefaultDecorator() const
{
	return SNew(SBorder)
        .Visibility(EVisibility::Visible)
        .BorderImage(FEditorStyle::GetBrush("Menu.Background"))
        [
            SNew(STextBlock)
            .Text(FText::FromString(Element.Pin()->Key.ToString()))
        ];
}

void SIKRigSkeleton::Construct(const FArguments& InArgs, TSharedRef<FIKRigEditorController> InEditorController)
{
	EditorController = InEditorController;
	EditorController.Pin()->SkeletonView = SharedThis(this);
	CommandList = MakeShared<FUICommandList>();
	BindCommands();
	
	ChildSlot
    [
        SNew(SVerticalBox)
        +SVerticalBox::Slot()
        .AutoHeight()
        .VAlign(VAlign_Top)
        .Padding(0.0f)
        [
            SNew(SBorder)
            .Padding(0.0f)
            .BorderImage(FEditorStyle::GetBrush("DetailsView.CategoryTop"))
            .BorderBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.0f))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                .VAlign(VAlign_Top)
                [
                    SNew(SHorizontalBox)
                    .Visibility(this, &SIKRigSkeleton::IsImportButtonVisible)
                    + SHorizontalBox::Slot()
                    .VAlign(VAlign_Center)
                    .HAlign(HAlign_Left)
                    .FillWidth(1.f)
                    .Padding(3.0f, 1.0f)
                    [
						SNew(SEditorHeaderButton)
                        .Icon(FAppStyle::Get().GetBrush("Icons.Plus"))
                        .Text(LOCTEXT("ImportSkeletonLabel", "Import Skeleton"))
                        .ToolTipText(LOCTEXT("ImportSkeletonToolTip", "Import a skeletal mesh to apply IK to."))
                        .OnClicked(FOnClicked::CreateSP(this, &SIKRigSkeleton::OnImportSkeletonClicked))
                    ]
                ]
            ]
        ]
        +SVerticalBox::Slot()
        .Padding(0.0f, 0.0f)
        [
            SNew(SBorder)
            .Padding(2.0f)
            .BorderImage(FEditorStyle::GetBrush("SCSEditor.TreePanel"))
            [
                SAssignNew(TreeView, SIKRigSkeletonTreeView)
                .TreeItemsSource(&RootElements)
                .SelectionMode(ESelectionMode::Multi)
                .OnGenerateRow(this, &SIKRigSkeleton::MakeTableRowWidget)
                .OnGetChildren(this, &SIKRigSkeleton::HandleGetChildrenForTree)
                .OnSelectionChanged(this, &SIKRigSkeleton::OnSelectionChanged)
                .OnContextMenuOpening(this, &SIKRigSkeleton::CreateContextMenu)
                .OnMouseButtonClick(this, &SIKRigSkeleton::OnItemClicked)
                .OnMouseButtonDoubleClick(this, &SIKRigSkeleton::OnItemDoubleClicked)
                .OnSetExpansionRecursive(this, &SIKRigSkeleton::OnSetExpansionRecursive)
                .HighlightParentNodesForSelection(true)
                .ItemHeight(24)
            ]
        ]
    ];

	const bool IsInitialSetup = true;
	RefreshTreeView(IsInitialSetup);
}

void SIKRigSkeleton::SetSelectedGoalsFromViewport(const TArray<FName>& GoalNames)
{
	if (GoalNames.IsEmpty())
	{
		TreeView->ClearSelection();
		return;
	}
	
	for (const auto& Item : AllElements)
	{
		if (GoalNames.Contains(Item->Key))
		{
			TreeView->SetSelection(Item, ESelectInfo::Direct);
		}
	}
}

void SIKRigSkeleton::BindCommands()
{
	const FIKRigSkeletonCommands& Commands = FIKRigSkeletonCommands::Get();

	CommandList->MapAction(Commands.NewGoal,
        FExecuteAction::CreateSP(this, &SIKRigSkeleton::HandleNewGoal),
        FCanExecuteAction::CreateSP(this, &SIKRigSkeleton::CanAddNewGoal));
	
	CommandList->MapAction(Commands.Delete,
        FExecuteAction::CreateSP(this, &SIKRigSkeleton::HandleDeleteItem),
        FCanExecuteAction::CreateSP(this, &SIKRigSkeleton::CanDeleteItem));

	CommandList->MapAction(Commands.ConnectGoalToSolvers,
        FExecuteAction::CreateSP(this, &SIKRigSkeleton::HandleConnectGoalToSolver),
        FCanExecuteAction::CreateSP(this, &SIKRigSkeleton::CanConnectGoalToSolvers));

	CommandList->MapAction(Commands.DisconnectGoalFromSolvers,
        FExecuteAction::CreateSP(this, &SIKRigSkeleton::HandleDisconnectGoalFromSolver),
        FCanExecuteAction::CreateSP(this, &SIKRigSkeleton::CanDisconnectGoalFromSolvers));

	CommandList->MapAction(Commands.SetRootBoneOnSolvers,
        FExecuteAction::CreateSP(this, &SIKRigSkeleton::HandleSetRootBoneOnSolvers),
        FCanExecuteAction::CreateSP(this, &SIKRigSkeleton::CanSetRootBoneOnSolvers));

	CommandList->MapAction(Commands.AddBoneSettings,
        FExecuteAction::CreateSP(this, &SIKRigSkeleton::HandleAddBoneSettings),
        FCanExecuteAction::CreateSP(this, &SIKRigSkeleton::CanAddBoneSettings));

	CommandList->MapAction(Commands.RemoveBoneSettings,
        FExecuteAction::CreateSP(this, &SIKRigSkeleton::HandleRemoveBoneSettings),
        FCanExecuteAction::CreateSP(this, &SIKRigSkeleton::CanRemoveBoneSettings));
}

void SIKRigSkeleton::FillContextMenu(FMenuBuilder& MenuBuilder)
{
	const FIKRigSkeletonCommands& Actions = FIKRigSkeletonCommands::Get();

	const TArray<TSharedPtr<FIKRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
	if (SelectedItems.IsEmpty())
	{
		return;
	}

	if (SelectedItems.Num() > 1)
	{
		return; // TODO create limb definitions from multiple selected bones
	}

	MenuBuilder.BeginSection("AddRemoveGoals", LOCTEXT("AddRemoveGoalOperations", "Goals"));
	MenuBuilder.AddMenuEntry(Actions.NewGoal);
	MenuBuilder.AddMenuEntry(Actions.Delete);
	MenuBuilder.EndSection();
	
	MenuBuilder.BeginSection("ConnectGoals", LOCTEXT("ConnectGoalOperations", "Connect Goals To Solvers"));
	MenuBuilder.AddMenuEntry(Actions.ConnectGoalToSolvers);
	MenuBuilder.AddMenuEntry(Actions.DisconnectGoalFromSolvers);
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("BoneSettings", LOCTEXT("BoneSettingsOperations", "Bone Settings"));
	MenuBuilder.AddMenuEntry(Actions.AddBoneSettings);
	MenuBuilder.AddMenuEntry(Actions.RemoveBoneSettings);
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("RootBone", LOCTEXT("RootBoneOperations", "Root Bone"));
	MenuBuilder.AddMenuEntry(Actions.SetRootBoneOnSolvers);
	MenuBuilder.EndSection();
}

void SIKRigSkeleton::HandleNewGoal()
{
	const TArray<TSharedPtr<FIKRigTreeElement>> SelectedItems = TreeView.Get()->GetSelectedItems();
	if (SelectedItems.IsEmpty())
	{
		return; // have to select something
	}

	TSharedPtr<FIKRigTreeElement> SelectedItem = SelectedItems[0];
	if (SelectedItem->ElementType != IKRigTreeElementType::BONE)
	{
		return; // can only add goals to bones
	}

	UIKRigController* Controller = EditorController.Pin()->AssetController;

	// build default name for the new goal
	const FName BoneName = SelectedItem->Key;
	const FName NewGoalName = FName(BoneName.ToString() + "_Goal");

	// create a new goal
	UIKRigEffectorGoal* NewGoal = Controller->AddNewGoal(NewGoalName, BoneName);

	if (NewGoal)
	{
		// get selected solvers
		TArray<TSharedPtr<FSolverStackElement>> SelectedSolvers;
		EditorController.Pin()->GetSelectedSolvers(SelectedSolvers);
		// connect the new goal to all the selected solvers
		for (const TSharedPtr<FSolverStackElement>& SolverElement : SelectedSolvers)
		{
			Controller->ConnectGoalToSolver(*NewGoal, SolverElement->IndexInStack);	
		}
	}
	
	// create new goal tree element and update view
	RefreshTreeView();

	// show goal in details view
	EditorController.Pin()->ShowDetailsForGoal(NewGoalName);
}

bool SIKRigSkeleton::CanAddNewGoal()
{
	// is anything selected?
	TArray<TSharedPtr<FIKRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
	if (SelectedItems.IsEmpty())
	{
		return false;
	}

	// can only add goals to selected bones
	for (const auto&Item : SelectedItems)
	{
		if (Item->ElementType != IKRigTreeElementType::BONE)
		{
			return false;
		}
	}

	return true;
}

void SIKRigSkeleton::HandleDeleteItem()
{
	TArray<TSharedPtr<FIKRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
	TSharedPtr<FIKRigTreeElement> ParentOfDeletedGoal;
	for (const auto&Item : SelectedItems)
	{
		if (Item->ElementType == IKRigTreeElementType::GOAL)
		{
			EditorController.Pin()->AssetController->RemoveGoal(Item->Key);
			
		}else if (Item->ElementType == IKRigTreeElementType::EFFECTOR)
		{
			EditorController.Pin()->AssetController->DisconnectGoalFromSolver(Item->EffectorGoalName, Item->EffectorSolverIndex);
		}
	}

	EditorController.Pin()->ShowEmptyDetails();
	RefreshTreeView();
}
bool SIKRigSkeleton::CanDeleteItem() const
{
	// is anything selected?
	TArray<TSharedPtr<FIKRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
	if (SelectedItems.IsEmpty())
	{
		return false;
	}

	// are all selected items goals or effectors?
	for (const auto&Item : SelectedItems)
	{
		const bool bIsGoal = Item->ElementType == IKRigTreeElementType::GOAL;
		const bool bIsEffector = Item->ElementType == IKRigTreeElementType::EFFECTOR;
		if (!(bIsGoal || bIsEffector))
		{
			return false;
		}
	}

	return true;
}

void SIKRigSkeleton::HandleConnectGoalToSolver()
{
	const bool bConnect = true; //connect
	ConnectSelectedGoalsToSelectedSolvers(bConnect);
}

void SIKRigSkeleton::HandleDisconnectGoalFromSolver()
{
	const bool bConnect = false; //disconnect
	ConnectSelectedGoalsToSelectedSolvers(bConnect);
}

bool SIKRigSkeleton::CanConnectGoalToSolvers() const
{
	const bool bCountOnlyConnected = false;
	const int32 NumDisconnected = GetNumSelectedGoalToSolverConnections(bCountOnlyConnected);
	return NumDisconnected > 0;
}

bool SIKRigSkeleton::CanDisconnectGoalFromSolvers() const
{
	const bool bCountOnlyConnected = true;
	const int32 NumConnected = GetNumSelectedGoalToSolverConnections(bCountOnlyConnected);
	return NumConnected > 0;
}

void SIKRigSkeleton::ConnectSelectedGoalsToSelectedSolvers(bool bConnect)
{
	TArray<TSharedPtr<FIKRigTreeElement>> SelectedGoals;
	GetSelectedGoals(SelectedGoals);
	TArray<TSharedPtr<FSolverStackElement>> SelectedSolvers;
	EditorController.Pin()->GetSelectedSolvers(SelectedSolvers);

	UIKRigController* Controller = EditorController.Pin()->AssetController;
	for (const TSharedPtr<FIKRigTreeElement>& GoalElement : SelectedGoals)
	{
		const FName GoalName = GoalElement->Key;
		const int32 GoalIndex = Controller->GetGoalIndex(GoalName);
		check(GoalIndex != INDEX_NONE);
		const UIKRigEffectorGoal& EffectorGoal = *Controller->GetGoal(GoalIndex);
		for (const TSharedPtr<FSolverStackElement>& SolverElement : SelectedSolvers)
		{
			if (bConnect)
			{
				Controller->ConnectGoalToSolver(EffectorGoal, SolverElement->IndexInStack);	
			}else
			{
				Controller->DisconnectGoalFromSolver(EffectorGoal.GoalName, SolverElement->IndexInStack);	
			}
		}
	}

	// add/remove new effector under goal in skeleton view
	RefreshTreeView();
}

int32 SIKRigSkeleton::GetNumSelectedGoalToSolverConnections(bool bCountOnlyConnected) const
{
	TArray<TSharedPtr<FIKRigTreeElement>> SelectedGoals;
	GetSelectedGoals(SelectedGoals);
	TArray<TSharedPtr<FSolverStackElement>> SelectedSolvers;
	EditorController.Pin()->GetSelectedSolvers(SelectedSolvers);

	int32 NumMatched = 0;
	for (const TSharedPtr<FIKRigTreeElement>& Goal : SelectedGoals)
	{
		for (const TSharedPtr<FSolverStackElement>& Solver : SelectedSolvers)
		{
			const bool bIsConnected = EditorController.Pin()->AssetController->IsGoalConnectedToSolver(Goal->Key, Solver->IndexInStack);
			if (bIsConnected == bCountOnlyConnected)
			{
				++NumMatched;
			}
		}
	}

	return NumMatched;
}

void SIKRigSkeleton::HandleSetRootBoneOnSolvers()
{
    // get name of selected root bone
    TArray<TSharedPtr<FIKRigTreeElement>> SelectedBones;
	GetSelectedBones(SelectedBones);
	const FName RootBoneName = SelectedBones[0]->Key;

	// apply to all selected solvers (ignored on solvers that don't accept a root bone)
	UIKRigController* Controller = EditorController.Pin()->AssetController;
	TArray<TSharedPtr<FSolverStackElement>> SelectedSolvers;
	EditorController.Pin()->GetSelectedSolvers(SelectedSolvers);
	int32 SolverToShow = 0;
	for (const auto& Solver : SelectedSolvers)
	{
		Controller->SetRootBone(RootBoneName, Solver->IndexInStack);
		SolverToShow = Solver->IndexInStack;
	}

	// show solver that had it's root bone updated
	EditorController.Pin()->ShowDetailsForSolver(SolverToShow);
	
	// show new icon when bone has settings applied
	RefreshTreeView();
}

bool SIKRigSkeleton::CanSetRootBoneOnSolvers()
{
	// must have at least 1 bone selected
	TArray<TSharedPtr<FIKRigTreeElement>> SelectedBones;
	GetSelectedBones(SelectedBones);
	if (SelectedBones.Num() != 1)
	{
		return false;
	}

	// must have at least 1 solver selected that accepts root bones
	UIKRigController* Controller = EditorController.Pin()->AssetController;
	TArray<TSharedPtr<FSolverStackElement>> SelectedSolvers;
	EditorController.Pin()->GetSelectedSolvers(SelectedSolvers);
	for (const auto& Solver : SelectedSolvers)
	{
		if (Controller->GetSolver(Solver->IndexInStack)->CanSetRootBone())
		{
			return true;
		}
	}
	
	return false;
}

void SIKRigSkeleton::HandleAddBoneSettings()
{
	// get selected bones
	TArray<TSharedPtr<FIKRigTreeElement>> SelectedBones;
	GetSelectedBones(SelectedBones);
	
	// add settings for bone on all selected solvers (ignored if already present)
	UIKRigController* Controller = EditorController.Pin()->AssetController;
	TArray<TSharedPtr<FSolverStackElement>> SelectedSolvers;
	EditorController.Pin()->GetSelectedSolvers(SelectedSolvers);
	FName BoneNameForSettings;
	int32 SolverIndex = INDEX_NONE;
	for (const TSharedPtr<FIKRigTreeElement>& BoneItem : SelectedBones)
	{
		for (const TSharedPtr<FSolverStackElement>& Solver : SelectedSolvers)
		{
			Controller->AddBoneSetting(BoneItem->Key, Solver->IndexInStack);
			BoneNameForSettings = BoneItem->Key;
			SolverIndex = Solver->IndexInStack;
        }
	}

	EditorController.Pin()->ShowDetailsForBoneSettings(BoneNameForSettings, SolverIndex);

	// show new icon when bone has settings applied
	RefreshTreeView();
}

bool SIKRigSkeleton::CanAddBoneSettings()
{
    // must have at least 1 bone selected
    TArray<TSharedPtr<FIKRigTreeElement>> SelectedBones;
	GetSelectedBones(SelectedBones);
	if (SelectedBones.IsEmpty())
	{
		return false;
	}

	// must have at least 1 solver selected that does not already have a bone setting for the selected bones
	UIKRigController* Controller = EditorController.Pin()->AssetController;
	TArray<TSharedPtr<FSolverStackElement>> SelectedSolvers;
	EditorController.Pin()->GetSelectedSolvers(SelectedSolvers);
	for (const TSharedPtr<FIKRigTreeElement>& BoneItem : SelectedBones)
	{
		for (const TSharedPtr<FSolverStackElement>& Solver : SelectedSolvers)
		{
			if (Controller->CanAddBoneSetting(BoneItem->Key, Solver->IndexInStack))
			{
				return true;
			}
        }
	}
	
	return false;
}

void SIKRigSkeleton::HandleRemoveBoneSettings()
{
	// get selected bones
	TArray<TSharedPtr<FIKRigTreeElement>> SelectedBones;
	GetSelectedBones(SelectedBones);
	
	// add settings for bone on all selected solvers (ignored if already present)
	UIKRigController* Controller = EditorController.Pin()->AssetController;
	TArray<TSharedPtr<FSolverStackElement>> SelectedSolvers;
	EditorController.Pin()->GetSelectedSolvers(SelectedSolvers);
	FName BoneToShowInDetailsView;
	for (const TSharedPtr<FIKRigTreeElement>& BoneItem : SelectedBones)
	{
		for (const TSharedPtr<FSolverStackElement>& Solver : SelectedSolvers)
		{
			Controller->RemoveBoneSetting(BoneItem->Key, Solver->IndexInStack);
			BoneToShowInDetailsView = BoneItem->Key;
		}
	}

	EditorController.Pin()->ShowDetailsForBone(BoneToShowInDetailsView);

	// show new icon when bone has settings applied
	RefreshTreeView();
}

bool SIKRigSkeleton::CanRemoveBoneSettings()
{
    // must have at least 1 bone selected
    TArray<TSharedPtr<FIKRigTreeElement>> SelectedBones;
	GetSelectedBones(SelectedBones);
	if (SelectedBones.IsEmpty())
	{
		return false;
	}

	// must have at least 1 solver selected that has a bone setting for 1 of the selected bones
	UIKRigController* Controller = EditorController.Pin()->AssetController;
	TArray<TSharedPtr<FSolverStackElement>> SelectedSolvers;
	EditorController.Pin()->GetSelectedSolvers(SelectedSolvers);
	for (const TSharedPtr<FIKRigTreeElement>& BoneItem : SelectedBones)
	{
		for (const TSharedPtr<FSolverStackElement>& Solver : SelectedSolvers)
		{
			if (Controller->CanRemoveBoneSetting(BoneItem->Key, Solver->IndexInStack))
			{
				return true;
			}
		}
	}
	
	return false;
}

void SIKRigSkeleton::GetSelectedBones(TArray<TSharedPtr<FIKRigTreeElement>>& OutBoneItems)
{
	const TArray<TSharedPtr<FIKRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
	for (const auto& Item : SelectedItems)
	{
		if (Item->ElementType == IKRigTreeElementType::BONE)
		{
			OutBoneItems.Add(Item);
        }
	}
}

void SIKRigSkeleton::GetSelectedGoals(TArray<TSharedPtr<FIKRigTreeElement>>& OutSelectedGoals) const
{
	OutSelectedGoals.Reset();
	TArray<TSharedPtr<FIKRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
	for (const auto& Item : SelectedItems)
	{
		if (Item->ElementType == IKRigTreeElementType::GOAL)
		{
			OutSelectedGoals.Add(Item);
		}
	}
}

void SIKRigSkeleton::HandleRenameElement() const
{
	TArray<TSharedPtr<FIKRigTreeElement>> SelectedGoals;
	GetSelectedGoals(SelectedGoals);
	if (SelectedGoals.Num() != 1)
	{
		return;
	}
	
	SelectedGoals[0]->RequestRename();
}

FReply SIKRigSkeleton::OnImportSkeletonClicked()
{
	FIKRigSkeletonImportSettings Settings;
	const TSharedPtr<FStructOnScope> StructToDisplay = MakeShareable(new FStructOnScope(FIKRigSkeletonImportSettings::StaticStruct(), (uint8*)&Settings));
	TSharedRef<SKismetInspector> KismetInspector = SNew(SKismetInspector);
	KismetInspector->ShowSingleStruct(StructToDisplay);

	SGenericDialogWidget::OpenDialog(LOCTEXT("IKRigSkeletonImport", "Import Skeleton"), KismetInspector, SGenericDialogWidget::FArguments(), true);

	if (Settings.Mesh != nullptr)
	{
		ImportSkeleton(FAssetData(Settings.Mesh));
		EditorController.Pin()->PromptToAddSolverAtStartup();
	}
	
	return FReply::Handled();
}

EVisibility SIKRigSkeleton::IsImportButtonVisible() const
{
	if (UIKRigController* Controller = EditorController.Pin()->AssetController)
	{
		if (Controller->GetSkeleton().BoneNames.Num() > 0)
		{
			return EVisibility::Collapsed;
		}
	}
	
	return EVisibility::Visible;
}

void SIKRigSkeleton::RefreshTreeView(bool IsInitialSetup)
{
	// save expansion state
	TreeView->SaveAndClearSparseItemInfos();

	// reset all tree items
	RootElements.Reset();
	AllElements.Reset();

	// validate we have a skeleton to load
	UIKRigController* Controller = EditorController.Pin()->AssetController;
	FIKRigSkeleton& Skeleton = Controller->GetSkeleton();
	if (Skeleton.BoneNames.IsEmpty())
	{
		TreeView->RequestTreeRefresh();
		return;
	}

	// create all bone elements
	for (const FName BoneName : Skeleton.BoneNames)
	{
		TSharedPtr<FIKRigTreeElement> NewItem = MakeShared<FIKRigTreeElement>(BoneName, IKRigTreeElementType::BONE);
		AllElements.Add(NewItem);
	}

	// store children/parent pointers on all bone elements
	for (int32 BoneIndex=0; BoneIndex<Skeleton.BoneNames.Num(); ++BoneIndex)
	{
		const int32 ParentIndex = Skeleton.ParentIndices[BoneIndex];
		if (ParentIndex < 0)
		{
			// store root element
			RootElements.Add(AllElements[BoneIndex]);
		}else
		{
			// store pointer to child on parent
			AllElements[ParentIndex]->Children.Add(AllElements[BoneIndex]);
			// store pointer to parent on child
			AllElements[BoneIndex]->Parent = AllElements[ParentIndex];
		}
	}

	// create all bone settings elements
	for (int32 SolverIndex=0; SolverIndex<Controller->GetNumSolvers(); ++SolverIndex)
	{
		UIKRigSolver* Solver = Controller->GetSolver(SolverIndex);
		if (!Solver->UsesBoneSettings())
		{
			continue;
		}
		for (int32 BoneIndex=0; BoneIndex<Skeleton.BoneNames.Num(); ++BoneIndex)
		{
			if (Solver->GetBoneSetting(Skeleton.BoneNames[BoneIndex]))
			{
				const FName DisplayName = FName("Bone Settings for: " + Solver->GetName());
				TSharedPtr<FIKRigTreeElement> SettingsItem = MakeShared<FIKRigTreeElement>(DisplayName, IKRigTreeElementType::BONE_SETTINGS);
				SettingsItem->BoneSettingBoneName = Skeleton.BoneNames[BoneIndex];
				SettingsItem->BoneSettingsSolverIndex = SolverIndex;
				AllElements.Add(SettingsItem);
				// store hierarchy pointers for item
				AllElements[BoneIndex]->Children.Add(SettingsItem);
				SettingsItem->Parent = AllElements[BoneIndex];
			}
		}
    }
	
	// create all goal and effector elements
	TArray<UIKRigEffectorGoal*> Goals = Controller->GetAllGoals();
	for (const UIKRigEffectorGoal* Goal : Goals)
	{
		// make new element for goal
		const int32 BoneIndex = Skeleton.GetBoneIndexFromName(Goal->BoneName);
		check(BoneIndex!=INDEX_NONE);
		TSharedPtr<FIKRigTreeElement> GoalItem = MakeShared<FIKRigTreeElement>(Goal->GoalName, IKRigTreeElementType::GOAL);
		const int32 GoalItemIndex = AllElements.Add(GoalItem);

		// store hierarchy pointers for goal
		AllElements[BoneIndex]->Children.Add(GoalItem);
		GoalItem->Parent = AllElements[BoneIndex];

		// add all effectors connected to this goal
		for (int32 SolverIndex=0; SolverIndex<Controller->GetNumSolvers(); ++SolverIndex)
		{
			if (UObject* Effector = Controller->GetEffectorForGoal(Goal->GoalName, SolverIndex))
			{
				// make new element for effector
				const UIKRigSolver* Solver = Controller->GetSolver(SolverIndex);
				const FName DisplayName = FName("Effector for: " + Solver->GetName());
				TSharedPtr<FIKRigTreeElement> EffectorItem = MakeShared<FIKRigTreeElement>(DisplayName, IKRigTreeElementType::EFFECTOR);
				EffectorItem->EffectorSolverIndex = SolverIndex;
				EffectorItem->EffectorGoalName = Goal->GoalName;
				AllElements.Add(EffectorItem);
				EffectorItem->Parent = GoalItem;
				GoalItem->Children.Add(EffectorItem);
			}
		}
	}

	// restore expansion state
	for (const TSharedPtr<FIKRigTreeElement>& Element : AllElements)
	{
		TreeView->RestoreSparseItemInfos(Element);
	}

	// expand all elements upon the initial construction of the tree
	if (IsInitialSetup)
	{
		for (TSharedPtr<FIKRigTreeElement> RootElement : RootElements)
		{
			SetExpansionRecursive(RootElement, false, true);
		}
	}
	
	TreeView->RequestTreeRefresh();
}

TSharedRef<ITableRow> SIKRigSkeleton::MakeTableRowWidget(
	TSharedPtr<FIKRigTreeElement> InItem,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	return InItem->MakeTreeRowWidget(EditorController.Pin().ToSharedRef(), OwnerTable, InItem.ToSharedRef(), CommandList.ToSharedRef(), SharedThis(this));
}

void SIKRigSkeleton::HandleGetChildrenForTree(
	TSharedPtr<FIKRigTreeElement> InItem,
	TArray<TSharedPtr<FIKRigTreeElement>>& OutChildren)
{
	OutChildren = InItem.Get()->Children;
}

void SIKRigSkeleton::OnSelectionChanged(TSharedPtr<FIKRigTreeElement> Selection, ESelectInfo::Type SelectInfo)
{
	// gate any selection changes NOT made by user clicking mouse
	if (SelectInfo == ESelectInfo::OnMouseClick)
	{
		TArray<TSharedPtr<FIKRigTreeElement>> SelectedGoals;
		GetSelectedGoals(SelectedGoals);
		TArray<FName> SelectedGoalNames;
		for (const auto& Goal : SelectedGoals)
		{
			SelectedGoalNames.Add(Goal->Key);
		}
		EditorController.Pin()->HandleGoalsSelectedInTreeView(SelectedGoalNames);
	}
}

TSharedPtr<SWidget> SIKRigSkeleton::CreateContextMenu()
{
	const bool CloseAfterSelection = true;
	FMenuBuilder MenuBuilder(CloseAfterSelection, CommandList);
	FillContextMenu(MenuBuilder);
	return MenuBuilder.MakeWidget();
}

void SIKRigSkeleton::OnItemClicked(TSharedPtr<FIKRigTreeElement> InItem)
{
	// update details view
	if (InItem->ElementType == IKRigTreeElementType::BONE)
	{
		EditorController.Pin()->ShowDetailsForBone(InItem->Key);
	}else if (InItem->ElementType == IKRigTreeElementType::GOAL)
	{
		EditorController.Pin()->ShowDetailsForGoal(InItem->Key);
	}else if (InItem->ElementType == IKRigTreeElementType::EFFECTOR)
	{
		EditorController.Pin()->ShowDetailsForEffector(InItem->EffectorGoalName, InItem->EffectorSolverIndex);
	}else if (InItem->ElementType == IKRigTreeElementType::BONE_SETTINGS)
	{
		EditorController.Pin()->ShowDetailsForBoneSettings(InItem->BoneSettingBoneName, InItem->BoneSettingsSolverIndex);
	}

	// to rename an item, you have to select it first, then click on it again within a time limit (slow double click)
	const bool ClickedOnSameItem = TreeView->LastSelected.Pin().Get() == InItem.Get();
	const uint32 CurrentCycles = FPlatformTime::Cycles();
	const double SecondsPassed = static_cast<double>(CurrentCycles - TreeView->LastClickCycles) * FPlatformTime::GetSecondsPerCycle();
	if (ClickedOnSameItem && SecondsPassed > 0.25f)
	{
		RegisterActiveTimer(0.f,
            FWidgetActiveTimerDelegate::CreateLambda([this](double, float) {
            HandleRenameElement();
            return EActiveTimerReturnType::Stop;
        }));
	}

	TreeView->LastClickCycles = CurrentCycles;
	TreeView->LastSelected = InItem;
}

void SIKRigSkeleton::OnItemDoubleClicked(TSharedPtr<FIKRigTreeElement> InItem)
{
	if (TreeView->IsItemExpanded(InItem))
	{
		SetExpansionRecursive(InItem, false, false);
	}
	else
	{
		SetExpansionRecursive(InItem, false, true);
	}
}

void SIKRigSkeleton::OnSetExpansionRecursive(TSharedPtr<FIKRigTreeElement> InItem, bool bShouldBeExpanded)
{
	SetExpansionRecursive(InItem, false, bShouldBeExpanded);
}

void SIKRigSkeleton::SetExpansionRecursive(
	TSharedPtr<FIKRigTreeElement> InElement,
	bool bTowardsParent,
    bool bShouldBeExpanded)
{
	TreeView->SetItemExpansion(InElement, bShouldBeExpanded);
    
    if (bTowardsParent)
    {
    	if (InElement->Parent.Get())
    	{
    		SetExpansionRecursive(InElement->Parent, bTowardsParent, bShouldBeExpanded);
    	}
    }
    else
    {
    	for (int32 ChildIndex = 0; ChildIndex < InElement->Children.Num(); ++ChildIndex)
    	{
    		SetExpansionRecursive(InElement->Children[ChildIndex], bTowardsParent, bShouldBeExpanded);
    	}
    }
}

void SIKRigSkeleton::ImportSkeleton(const FAssetData& InAssetData)
{
	USkeletalMesh* Mesh = Cast<USkeletalMesh>(InAssetData.GetAsset());
	if (!Mesh)
	{
		return;
	}
	
	// load the skeletal mesh in the viewport
	TSharedRef<IPersonaToolkit> Persona = EditorController.Pin()->EditorToolkit.Pin()->GetPersonaToolkit();
	Persona->SetPreviewMesh(Mesh, true);
	Persona->GetPreviewScene()->FocusViews();

	// import the skeleton data into the IKRig asset
	EditorController.Pin()->AssetController->SetSourceSkeletalMesh(Mesh, true);

	// update the bone hierarchy view
	RefreshTreeView(true);
}

FReply SIKRigSkeleton::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const TArray<TSharedPtr<FIKRigTreeElement>> SelectedItems = TreeView.Get()->GetSelectedItems();
	if (SelectedItems.Num() != 1)
	{
		return FReply::Unhandled();
	}

	const TSharedPtr<FIKRigTreeElement> DraggedElement = SelectedItems[0];
	if (DraggedElement->ElementType != IKRigTreeElementType::GOAL)
	{
		return FReply::Unhandled();
	}
	
	if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
	{
		const TSharedRef<FIKRigSkeletonDragDropOp> DragDropOp = FIKRigSkeletonDragDropOp::New(DraggedElement);
		return FReply::Handled().BeginDragDrop(DragDropOp);
	}

	return FReply::Unhandled();
}

TOptional<EItemDropZone> SIKRigSkeleton::OnCanAcceptDrop(
	const FDragDropEvent& DragDropEvent,
	EItemDropZone DropZone,
    TSharedPtr<FIKRigTreeElement> TargetItem)
{
	TOptional<EItemDropZone> ReturnedDropZone;
	
	const TSharedPtr<FIKRigSkeletonDragDropOp> DragDropOp = DragDropEvent.GetOperationAs<FIKRigSkeletonDragDropOp>();
	if (DragDropOp.IsValid())
	{
		if (TargetItem.Get()->ElementType == IKRigTreeElementType::BONE)
        {
        	ReturnedDropZone = EItemDropZone::BelowItem;	
        }
	}
	
	return ReturnedDropZone;
}

FReply SIKRigSkeleton::OnAcceptDrop(
	const FDragDropEvent& DragDropEvent,
	EItemDropZone DropZone,
    TSharedPtr<FIKRigTreeElement> TargetItem)
{
	const TSharedPtr<FIKRigSkeletonDragDropOp> DragDropOp = DragDropEvent.GetOperationAs<FIKRigSkeletonDragDropOp>();
	if (!DragDropOp.IsValid())
	{
		return FReply::Unhandled();
	}

	const FIKRigTreeElement& DraggedElement = *DragDropOp.Get()->Element.Pin().Get();
	UIKRigController* Controller = EditorController.Pin()->AssetController;
	const bool bWasReparented = Controller->SetGoalBone(DraggedElement.Key, TargetItem.Get()->Key);
	if (bWasReparented)
	{
		RefreshTreeView();
	}
	
	return FReply::Handled();
}

FReply SIKRigSkeleton::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();

	// handle deleting selected goals
	if (Key == EKeys::Delete)
	{
		TArray<TSharedPtr<FIKRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
		for (const auto& SelectedItem : SelectedItems)
		{
			if (SelectedItem->ElementType == IKRigTreeElementType::GOAL)
			{
				UIKRigController* Controller = EditorController.Pin()->AssetController;
				Controller->RemoveGoal(SelectedItem->Key);
			}

			if (SelectedItem->ElementType == IKRigTreeElementType::EFFECTOR)
			{
				UIKRigController* Controller = EditorController.Pin()->AssetController;
				Controller->DisconnectGoalFromSolver(SelectedItem->EffectorGoalName, SelectedItem->EffectorSolverIndex);
			}
			
			if (SelectedItem->ElementType == IKRigTreeElementType::BONE_SETTINGS)
			{
				UIKRigController* Controller = EditorController.Pin()->AssetController;
				Controller->RemoveBoneSetting(SelectedItem->BoneSettingBoneName, SelectedItem->BoneSettingsSolverIndex);
			}
		}

		RefreshTreeView();
		
		return FReply::Handled();
	}
	
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE