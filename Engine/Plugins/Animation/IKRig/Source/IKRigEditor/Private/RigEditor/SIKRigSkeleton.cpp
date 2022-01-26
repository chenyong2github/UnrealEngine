// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigEditor/SIKRigSkeleton.h"

#include "IKRigSolver.h"
#include "Engine/SkeletalMesh.h"
#include "IPersonaToolkit.h"
#include "SKismetInspector.h"
#include "Dialogs/Dialogs.h"
#include "SPositiveActionButton.h"
#include "RigEditor/IKRigEditorController.h"
#include "RigEditor/IKRigEditorStyle.h"
#include "RigEditor/IKRigSkeletonCommands.h"
#include "RigEditor/IKRigToolkit.h"
#include "RigEditor/SIKRigSolverStack.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Commands/UICommandList.h"
#include "Preferences/PersonaOptions.h"

#define LOCTEXT_NAMESPACE "SIKRigSkeleton"

FIKRigTreeElement::FIKRigTreeElement(const FText& InKey, IKRigTreeElementType InType)
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
	SkeletonView = InSkeleton;

	// is this element affected by the selected solver?
	bool bIsConnectedToSelectedSolver;
	const int32 SelectedSolver = InEditorController->GetSelectedSolverIndex();
	if (SelectedSolver == INDEX_NONE)
	{
		bIsConnectedToSelectedSolver = InEditorController->IsElementConnectedToAnySolver(InRigTreeElement);
	}
	else
	{
		bIsConnectedToSelectedSolver = InEditorController->IsElementConnectedToSolver(InRigTreeElement, SelectedSolver);
	}

	// determine text style
	FTextBlockStyle NormalText = FIKRigEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("IKRig.Tree.NormalText");
	FTextBlockStyle ItalicText = FIKRigEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("IKRig.Tree.ItalicText");
	FSlateFontInfo TextFont;
	FSlateColor TextColor;
	if (bIsConnectedToSelectedSolver)
	{
		// elements connected to the selected solver are green
		TextFont = ItalicText.Font;
		TextColor = NormalText.ColorAndOpacity;
	}
	else
	{
		TextFont = NormalText.Font;
		TextColor = FLinearColor(0.2f, 0.2f, 0.2f, 0.5f);
	}

	// determine which icon to use for tree element
	const FSlateBrush* Brush = FAppStyle::Get().GetBrush("SkeletonTree.Bone");
	switch(InRigTreeElement->ElementType)
	{
		case IKRigTreeElementType::BONE:
			if (!InEditorController->IsElementExcludedBone(InRigTreeElement))
			{
				Brush = FAppStyle::Get().GetBrush("SkeletonTree.Bone");
			}
			else
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
		case IKRigTreeElementType::SOLVERGOAL:
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
			.Font(TextFont)
			.ColorAndOpacity(TextColor)
        ];

		if (InEditorController->AssetController->GetRetargetRoot() == InRigTreeElement->BoneName)
		{	
			HorizontalBox->AddSlot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("RetargetRootLabel", " (Retarget Root)"))
				.Font(ItalicText.Font)
				.ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.9f, 0.5f))
			];
		}
	}
	else
	{
		TSharedPtr< SInlineEditableTextBlock > InlineWidget;
		HorizontalBox->AddSlot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        [
	        SAssignNew(InlineWidget, SInlineEditableTextBlock)
		    .Text(this, &SIKRigSkeletonItem::GetName)
		    .Font(TextFont)
			.ColorAndOpacity(TextColor)
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

	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return; 
	}
	
	const FText OldText = WeakRigTreeElement.Pin()->Key;
	const FName OldName = WeakRigTreeElement.Pin()->GoalName;
	const FName PotentialNewName = FName(InText.ToString());
	const FName NewName = Controller->AssetController->RenameGoal(OldName, PotentialNewName);
	if (NewName != NAME_None)
	{
		WeakRigTreeElement.Pin()->Key = FText::FromName(NewName);
		WeakRigTreeElement.Pin()->GoalName = NewName;
	}
	
	Controller->RefreshAllViews();
	SkeletonView.Pin()->ReplaceItemInSelection(OldText, WeakRigTreeElement.Pin()->Key);
}

FText SIKRigSkeletonItem::GetName() const
{
	return WeakRigTreeElement.Pin()->Key;
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
	EditorController.Pin()->SetSkeletonsView(SharedThis(this));
	CommandList = MakeShared<FUICommandList>();
	BindCommands();
	
	ChildSlot
    [
        SNew(SVerticalBox)

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
                .HighlightParentNodesForSelection(false)
                .ItemHeight(24)
            ]
        ]
    ];

	const bool IsInitialSetup = true;
	RefreshTreeView(IsInitialSetup);
}

void SIKRigSkeleton::AddSelectedItemFromViewport(
	const FName& ItemName,
	IKRigTreeElementType ItemType,
	const bool bReplace)
{	
	// nothing to add
	if (ItemName == NAME_None)
	{
		return;
	}

	// record what was already selected
	TArray<TSharedPtr<FIKRigTreeElement>> PreviouslySelectedItems = TreeView->GetSelectedItems();
	// add/remove items as needed
	for (const TSharedPtr<FIKRigTreeElement>& Item : AllElements)
	{
		bool bIsBeingAdded = false;
		switch (ItemType)
		{
		case IKRigTreeElementType::GOAL:
			if (ItemName == Item->GoalName)
			{
				bIsBeingAdded = true;
			}
			break;
		case IKRigTreeElementType::BONE:
			if (ItemName == Item->BoneName)
			{
				bIsBeingAdded = true;
			}
			break;
		default:
			ensureMsgf(false, TEXT("IKRig cannot select anything but bones and goals in viewport."));
			return;
		}

		if (bReplace)
		{
			if (bIsBeingAdded)
			{
				TreeView->ClearSelection();
				AddItemToSelection(Item);
				return;
			}
			
			continue;
		}

		// remove if already selected (invert)
		if (bIsBeingAdded && PreviouslySelectedItems.Contains(Item))
		{
			RemoveItemFromSelection(Item);
			continue;
		}

		// add if being added
		if (bIsBeingAdded)
		{
			AddItemToSelection(Item);
			continue;
		}
	}
}

void SIKRigSkeleton::AddItemToSelection(const TSharedPtr<FIKRigTreeElement>& InItem)
{
	TreeView->SetItemSelection(InItem, true, ESelectInfo::Direct);
    
	if(GetDefault<UPersonaOptions>()->bExpandTreeOnSelection)
	{
		TSharedPtr<FIKRigTreeElement> ItemToExpand = InItem->Parent;
		while(ItemToExpand.IsValid())
		{
			TreeView->SetItemExpansion(ItemToExpand, true);
			ItemToExpand = ItemToExpand->Parent;
		}
	}
    
	TreeView->RequestScrollIntoView(InItem);
}

void SIKRigSkeleton::RemoveItemFromSelection(const TSharedPtr<FIKRigTreeElement>& InItem)
{
	TreeView->SetItemSelection(InItem, false, ESelectInfo::Direct);
}

void SIKRigSkeleton::ReplaceItemInSelection(const FText& OldName, const FText& NewName)
{
	for (const TSharedPtr<FIKRigTreeElement>& Item : AllElements)
	{
		// remove old selection
		if (Item->Key.EqualTo(OldName))
		{
			TreeView->SetItemSelection(Item, false, ESelectInfo::Direct);
		}
		// add new selection
		if (Item->Key.EqualTo(NewName))
		{
			TreeView->SetItemSelection(Item, true, ESelectInfo::Direct);
		}
	}
}

void SIKRigSkeleton::GetSelectedBoneChains(TArray<FIKRigSkeletonChain>& OutChains)
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return; 
	}

	// get selected bones
	TArray<TSharedPtr<FIKRigTreeElement>> SelectedBoneItems;
	GetSelectedBones(SelectedBoneItems);

	const FIKRigSkeleton& Skeleton = Controller->AssetController->GetIKRigSkeleton();

	// get selected bone indices
	TArray<int32> SelectedBones;
	for (const TSharedPtr<FIKRigTreeElement>& BoneItem : SelectedBoneItems)
	{
		const FName BoneName = BoneItem.Get()->BoneName;
		const int32 BoneIndex = Skeleton.GetBoneIndexFromName(BoneName);
		SelectedBones.Add(BoneIndex);
	}
	
	return Skeleton.GetChainsInList(SelectedBones, OutChains);
}

bool SIKRigSkeleton::HasSelectedItems() const
{
	return TreeView->GetNumItemsSelected() > 0;
}

void SIKRigSkeleton::BindCommands()
{
	const FIKRigSkeletonCommands& Commands = FIKRigSkeletonCommands::Get();

	CommandList->MapAction(Commands.NewGoal,
        FExecuteAction::CreateSP(this, &SIKRigSkeleton::HandleNewGoal),
        FCanExecuteAction::CreateSP(this, &SIKRigSkeleton::CanAddNewGoal));
	
	CommandList->MapAction(Commands.DeleteElement,
        FExecuteAction::CreateSP(this, &SIKRigSkeleton::HandleDeleteElement),
        FCanExecuteAction::CreateSP(this, &SIKRigSkeleton::CanDeleteElement));

	CommandList->MapAction(Commands.ConnectGoalToSolver,
        FExecuteAction::CreateSP(this, &SIKRigSkeleton::HandleConnectGoalToSolver),
        FCanExecuteAction::CreateSP(this, &SIKRigSkeleton::CanConnectGoalToSolvers));

	CommandList->MapAction(Commands.DisconnectGoalFromSolver,
        FExecuteAction::CreateSP(this, &SIKRigSkeleton::HandleDisconnectGoalFromSolver),
        FCanExecuteAction::CreateSP(this, &SIKRigSkeleton::CanDisconnectGoalFromSolvers));

	CommandList->MapAction(Commands.SetRootBoneOnSolver,
        FExecuteAction::CreateSP(this, &SIKRigSkeleton::HandleSetRootBoneOnSolvers),
        FCanExecuteAction::CreateSP(this, &SIKRigSkeleton::CanSetRootBoneOnSolvers));

	CommandList->MapAction(Commands.SetEndBoneOnSolver,
	FExecuteAction::CreateSP(this, &SIKRigSkeleton::HandleSetEndBoneOnSolvers),
	FCanExecuteAction::CreateSP(this, &SIKRigSkeleton::CanSetEndBoneOnSolvers),
	FIsActionChecked(),
	FIsActionButtonVisible::CreateSP(this, &SIKRigSkeleton::HasEndBoneCompatibleSolverSelected));

	CommandList->MapAction(Commands.AddBoneSettings,
        FExecuteAction::CreateSP(this, &SIKRigSkeleton::HandleAddBoneSettings),
        FCanExecuteAction::CreateSP(this, &SIKRigSkeleton::CanAddBoneSettings));

	CommandList->MapAction(Commands.RemoveBoneSettings,
        FExecuteAction::CreateSP(this, &SIKRigSkeleton::HandleRemoveBoneSettings),
        FCanExecuteAction::CreateSP(this, &SIKRigSkeleton::CanRemoveBoneSettings));

	CommandList->MapAction(Commands.ExcludeBone,
		FExecuteAction::CreateSP(this, &SIKRigSkeleton::HandleExcludeBone),
		FCanExecuteAction::CreateSP(this, &SIKRigSkeleton::CanExcludeBone));

	CommandList->MapAction(Commands.IncludeBone,
		FExecuteAction::CreateSP(this, &SIKRigSkeleton::HandleIncludeBone),
		FCanExecuteAction::CreateSP(this, &SIKRigSkeleton::CanIncludeBone));

	CommandList->MapAction(Commands.NewRetargetChain,
		FExecuteAction::CreateSP(this, &SIKRigSkeleton::HandleNewRetargetChain),
		FCanExecuteAction::CreateSP(this, &SIKRigSkeleton::CanAddNewRetargetChain));

	CommandList->MapAction(Commands.SetRetargetRoot,
		FExecuteAction::CreateSP(this, &SIKRigSkeleton::HandleSetRetargetRoot),
		FCanExecuteAction::CreateSP(this, &SIKRigSkeleton::CanSetRetargetRoot));

	CommandList->MapAction(Commands.ClearRetargetRoot,
		FExecuteAction::CreateSP(this, &SIKRigSkeleton::HandleClearRetargetRoot),
		FCanExecuteAction::CreateSP(this, &SIKRigSkeleton::CanClearRetargetRoot));

	CommandList->MapAction(Commands.RenameGoal,
	FExecuteAction::CreateSP(this, &SIKRigSkeleton::HandleRenameGoal),
		FCanExecuteAction::CreateSP(this, &SIKRigSkeleton::CanRenameGoal));
}

void SIKRigSkeleton::FillContextMenu(FMenuBuilder& MenuBuilder)
{
	const FIKRigSkeletonCommands& Actions = FIKRigSkeletonCommands::Get();

	const TArray<TSharedPtr<FIKRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
	if (SelectedItems.IsEmpty())
	{
		return;
	}

	MenuBuilder.BeginSection("AddRemoveGoals", LOCTEXT("AddRemoveGoalOperations", "Goals"));
	MenuBuilder.AddMenuEntry(Actions.NewGoal);
	MenuBuilder.EndSection();
	
	MenuBuilder.BeginSection("ConnectGoals", LOCTEXT("ConnectGoalOperations", "Connect Goals To Solvers"));
	MenuBuilder.AddMenuEntry(Actions.ConnectGoalToSolver);
	MenuBuilder.AddMenuEntry(Actions.DisconnectGoalFromSolver);
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("BoneSettings", LOCTEXT("BoneSettingsOperations", "Bone Settings"));
	MenuBuilder.AddMenuEntry(Actions.SetRootBoneOnSolver);
	MenuBuilder.AddMenuEntry(Actions.SetEndBoneOnSolver);
	MenuBuilder.AddMenuEntry(Actions.AddBoneSettings);
	MenuBuilder.AddMenuEntry(Actions.RemoveBoneSettings);
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("IncludeExclude", LOCTEXT("IncludeExcludeOperations", "Exclude Bones"));
	MenuBuilder.AddMenuEntry(Actions.ExcludeBone);
	MenuBuilder.AddMenuEntry(Actions.IncludeBone);
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("Retargeting", LOCTEXT("RetargetingOperations", "Retargeting"));
	MenuBuilder.AddMenuEntry(Actions.SetRetargetRoot);
	MenuBuilder.AddMenuEntry(Actions.ClearRetargetRoot);
	MenuBuilder.AddMenuEntry(Actions.NewRetargetChain);
	MenuBuilder.EndSection();
}

void SIKRigSkeleton::HandleNewGoal() const
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return; 
	}

	// get names of selected bones and default goal names for them
	TArray<FName> GoalNames;
	TArray<FName> BoneNames;
	const TArray<TSharedPtr<FIKRigTreeElement>> SelectedItems = TreeView.Get()->GetSelectedItems();
	for (const TSharedPtr<FIKRigTreeElement>& Item : SelectedItems)
	{
		if (Item->ElementType != IKRigTreeElementType::BONE)
		{
			continue; // can only add goals to bones
		}

		// build default name for the new goal
		const FName BoneName = Item->BoneName;
		const FName NewGoalName = FName(BoneName.ToString() + "_Goal");
		
		GoalNames.Add(NewGoalName);
		BoneNames.Add(BoneName);
	}
	
	// add new goals
	Controller->AddNewGoals(GoalNames, BoneNames);
}

bool SIKRigSkeleton::CanAddNewGoal() const
{
	// is anything selected?
	TArray<TSharedPtr<FIKRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
	if (SelectedItems.IsEmpty())
	{
		return false;
	}

	// can only add goals to selected bones
	for (const TSharedPtr<FIKRigTreeElement>& Item : SelectedItems)
	{
		if (Item->ElementType != IKRigTreeElementType::BONE)
		{
			return false;
		}
	}

	return true;
}

void SIKRigSkeleton::HandleDeleteElement()
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return; 
	}

	TArray<TSharedPtr<FIKRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
	for (const TSharedPtr<FIKRigTreeElement>& SelectedItem : SelectedItems)
	{
		switch(SelectedItem->ElementType)
		{
			case IKRigTreeElementType::GOAL:
				Controller->AssetController->RemoveGoal(SelectedItem->GoalName);
				break;
			case IKRigTreeElementType::SOLVERGOAL:
				Controller->AssetController->DisconnectGoalFromSolver(SelectedItem->SolverGoalName, SelectedItem->SolverGoalIndex);
				break;
			case IKRigTreeElementType::BONE_SETTINGS:
				Controller->AssetController->RemoveBoneSetting(SelectedItem->BoneSettingBoneName, SelectedItem->BoneSettingsSolverIndex);
				break;
			default:
				break; // can't delete anything else
		}
	}
	
	RefreshTreeView();

	Controller->ShowEmptyDetails();
	// update all views
	Controller->RefreshAllViews();
}
bool SIKRigSkeleton::CanDeleteElement() const
{
	// is anything selected?
	TArray<TSharedPtr<FIKRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
	if (SelectedItems.IsEmpty())
	{
		return false;
	}

	// are all selected items goals, effectors or bone settings?
	for (const TSharedPtr<FIKRigTreeElement>& Item : SelectedItems)
	{
		const bool bIsBone = Item->ElementType == IKRigTreeElementType::BONE;
		if (bIsBone)
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
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return; 
	}
	
	TArray<TSharedPtr<FIKRigTreeElement>> SelectedGoals;
	GetSelectedGoals(SelectedGoals);
	TArray<TSharedPtr<FSolverStackElement>> SelectedSolvers;
	Controller->GetSelectedSolvers(SelectedSolvers);

	UIKRigController* AssetController = Controller->AssetController;
	for (const TSharedPtr<FIKRigTreeElement>& GoalElement : SelectedGoals)
	{
		const FName GoalName = GoalElement->GoalName;
		const int32 GoalIndex = AssetController->GetGoalIndex(GoalName);
		check(GoalIndex != INDEX_NONE);
		const UIKRigEffectorGoal& EffectorGoal = *AssetController->GetGoal(GoalIndex);
		for (const TSharedPtr<FSolverStackElement>& SolverElement : SelectedSolvers)
		{
			if (bConnect)
			{
				AssetController->ConnectGoalToSolver(EffectorGoal, SolverElement->IndexInStack);	
			}
			else
			{
				AssetController->DisconnectGoalFromSolver(EffectorGoal.GoalName, SolverElement->IndexInStack);	
			}
		}
	}

	// add/remove new effector under goal in skeleton view
	RefreshTreeView();
}

int32 SIKRigSkeleton::GetNumSelectedGoalToSolverConnections(bool bCountOnlyConnected) const
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return 0;
	}
	
	TArray<TSharedPtr<FIKRigTreeElement>> SelectedGoals;
	GetSelectedGoals(SelectedGoals);
	TArray<TSharedPtr<FSolverStackElement>> SelectedSolvers;
	Controller->GetSelectedSolvers(SelectedSolvers);

	int32 NumMatched = 0;
	for (const TSharedPtr<FIKRigTreeElement>& Goal : SelectedGoals)
	{
		for (const TSharedPtr<FSolverStackElement>& Solver : SelectedSolvers)
		{
			const bool bIsConnected = Controller->AssetController->IsGoalConnectedToSolver(Goal->GoalName, Solver->IndexInStack);
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
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return;
	}

    // get name of selected root bone
    TArray<TSharedPtr<FIKRigTreeElement>> SelectedBones;
	GetSelectedBones(SelectedBones);
	const FName RootBoneName = SelectedBones[0]->BoneName;

	// apply to all selected solvers (ignored on solvers that don't accept a root bone)
	UIKRigController* AssetController = Controller->AssetController;
	TArray<TSharedPtr<FSolverStackElement>> SelectedSolvers;
	Controller->GetSelectedSolvers(SelectedSolvers);
	int32 SolverToShow = 0;
	for (const TSharedPtr<FSolverStackElement>& Solver : SelectedSolvers)
	{
		AssetController->SetRootBone(RootBoneName, Solver->IndexInStack);
		SolverToShow = Solver->IndexInStack;
	}

	// show solver that had it's root bone updated
	Controller->ShowDetailsForSolver(SolverToShow);
	
	// show new icon when bone has settings applied
	RefreshTreeView();
}

bool SIKRigSkeleton::CanSetRootBoneOnSolvers()
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false;
	}
	
	// must have at least 1 bone selected
	TArray<TSharedPtr<FIKRigTreeElement>> SelectedBones;
	GetSelectedBones(SelectedBones);
	if (SelectedBones.Num() != 1)
	{
		return false;
	}

	// must have at least 1 solver selected that accepts root bones
	UIKRigController* AssetController = Controller->AssetController;
	TArray<TSharedPtr<FSolverStackElement>> SelectedSolvers;
	Controller->GetSelectedSolvers(SelectedSolvers);
	for (const TSharedPtr<FSolverStackElement>& Solver : SelectedSolvers)
	{
		if (AssetController->GetSolver(Solver->IndexInStack)->RequiresRootBone())
		{
			return true;
		}
	}
	
	return false;
}

void SIKRigSkeleton::HandleSetEndBoneOnSolvers()
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return;
	}

    // get name of selected root bone
    TArray<TSharedPtr<FIKRigTreeElement>> SelectedBones;
	GetSelectedBones(SelectedBones);
	const FName RootBoneName = SelectedBones[0]->BoneName;

	// apply to all selected solvers (ignored on solvers that don't accept a root bone)
	UIKRigController* AssetController = Controller->AssetController;
	TArray<TSharedPtr<FSolverStackElement>> SelectedSolvers;
	Controller->GetSelectedSolvers(SelectedSolvers);
	int32 SolverToShow = 0;
	for (const TSharedPtr<FSolverStackElement>& Solver : SelectedSolvers)
	{
		AssetController->SetEndBone(RootBoneName, Solver->IndexInStack);
		SolverToShow = Solver->IndexInStack;
	}

	// show solver that had it's root bone updated
	Controller->ShowDetailsForSolver(SolverToShow);
	
	// show new icon when bone has settings applied
	RefreshTreeView();
}

bool SIKRigSkeleton::CanSetEndBoneOnSolvers() const
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false;
	}
	
	// must have at least 1 bone selected
	TArray<TSharedPtr<FIKRigTreeElement>> SelectedBones;
	GetSelectedBones(SelectedBones);
	if (SelectedBones.Num() != 1)
	{
		return false;
	}

	// must have at least 1 solver selected that accepts end bones
	UIKRigController* AssetController = Controller->AssetController;
	TArray<TSharedPtr<FSolverStackElement>> SelectedSolvers;
	Controller->GetSelectedSolvers(SelectedSolvers);
	for (const TSharedPtr<FSolverStackElement>& Solver : SelectedSolvers)
	{
		if (AssetController->GetSolver(Solver->IndexInStack)->RequiresEndBone())
		{
			return true;
		}
	}
	
	return false;
}

bool SIKRigSkeleton::HasEndBoneCompatibleSolverSelected() const
{
	return CanSetEndBoneOnSolvers();
}

void SIKRigSkeleton::HandleAddBoneSettings()
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return;
	}
	
	// get selected bones
	TArray<TSharedPtr<FIKRigTreeElement>> SelectedBones;
	GetSelectedBones(SelectedBones);
	
	// add settings for bone on all selected solvers (ignored if already present)
	UIKRigController* AssetController = Controller->AssetController;
	TArray<TSharedPtr<FSolverStackElement>> SelectedSolvers;
	Controller->GetSelectedSolvers(SelectedSolvers);
	FName BoneNameForSettings;
	int32 SolverIndex = INDEX_NONE;
	for (const TSharedPtr<FIKRigTreeElement>& BoneItem : SelectedBones)
	{
		for (const TSharedPtr<FSolverStackElement>& Solver : SelectedSolvers)
		{
			AssetController->AddBoneSetting(BoneItem->BoneName, Solver->IndexInStack);
			BoneNameForSettings = BoneItem->BoneName;
			SolverIndex = Solver->IndexInStack;
        }
	}

	Controller->ShowDetailsForBoneSettings(BoneNameForSettings, SolverIndex);

	// show new icon when bone has settings applied
	RefreshTreeView();
}

bool SIKRigSkeleton::CanAddBoneSettings()
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false;
	}
	
    // must have at least 1 bone selected
    TArray<TSharedPtr<FIKRigTreeElement>> SelectedBones;
	GetSelectedBones(SelectedBones);
	if (SelectedBones.IsEmpty())
	{
		return false;
	}

	// must have at least 1 solver selected that does not already have a bone setting for the selected bones
	UIKRigController* AssetController = Controller->AssetController;
	TArray<TSharedPtr<FSolverStackElement>> SelectedSolvers;
	Controller->GetSelectedSolvers(SelectedSolvers);
	for (const TSharedPtr<FIKRigTreeElement>& BoneItem : SelectedBones)
	{
		for (const TSharedPtr<FSolverStackElement>& Solver : SelectedSolvers)
		{
			if (AssetController->CanAddBoneSetting(BoneItem->BoneName, Solver->IndexInStack))
			{
				return true;
			}
        }
	}
	
	return false;
}

void SIKRigSkeleton::HandleRemoveBoneSettings()
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return;
	}
	
	// get selected bones
	TArray<TSharedPtr<FIKRigTreeElement>> SelectedBones;
	GetSelectedBones(SelectedBones);
	
	// add settings for bone on all selected solvers (ignored if already present)
	UIKRigController* AssetController = Controller->AssetController;
	TArray<TSharedPtr<FSolverStackElement>> SelectedSolvers;
	Controller->GetSelectedSolvers(SelectedSolvers);
	FName BoneToShowInDetailsView;
	for (const TSharedPtr<FIKRigTreeElement>& BoneItem : SelectedBones)
	{
		for (const TSharedPtr<FSolverStackElement>& Solver : SelectedSolvers)
		{
			AssetController->RemoveBoneSetting(BoneItem->BoneName, Solver->IndexInStack);
			BoneToShowInDetailsView = BoneItem->BoneName;
		}
	}

	Controller->ShowDetailsForBone(BoneToShowInDetailsView);
	
	// show new icon when bone has settings applied
	RefreshTreeView();
}

bool SIKRigSkeleton::CanRemoveBoneSettings()
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false;
	}
	
    // must have at least 1 bone selected
    TArray<TSharedPtr<FIKRigTreeElement>> SelectedBones;
	GetSelectedBones(SelectedBones);
	if (SelectedBones.IsEmpty())
	{
		return false;
	}

	// must have at least 1 solver selected that has a bone setting for 1 of the selected bones
	UIKRigController* AssetController = Controller->AssetController;
	TArray<TSharedPtr<FSolverStackElement>> SelectedSolvers;
	Controller->GetSelectedSolvers(SelectedSolvers);
	for (const TSharedPtr<FIKRigTreeElement>& BoneItem : SelectedBones)
	{
		for (const TSharedPtr<FSolverStackElement>& Solver : SelectedSolvers)
		{
			if (AssetController->CanRemoveBoneSetting(BoneItem->BoneName, Solver->IndexInStack))
			{
				return true;
			}
		}
	}
	
	return false;
}

void SIKRigSkeleton::HandleExcludeBone()
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return;
	}
	
	// exclude selected bones
	TArray<TSharedPtr<FIKRigTreeElement>> SelectedBones;
	GetSelectedBones(SelectedBones);
	for (const TSharedPtr<FIKRigTreeElement>& BoneItem : SelectedBones)
	{
		Controller->AssetController->SetBoneExcluded(BoneItem->BoneName, true);
	}

	// show greyed out bone name after being excluded
	RefreshTreeView();
}

bool SIKRigSkeleton::CanExcludeBone()
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false;
	}
	
	// must have at least 1 bone selected that is INCLUDED
	TArray<TSharedPtr<FIKRigTreeElement>> SelectedBones;
	GetSelectedBones(SelectedBones);
	for (const TSharedPtr<FIKRigTreeElement>& BoneItem : SelectedBones)
	{
		if (!Controller->AssetController->GetBoneExcluded(BoneItem->BoneName))
		{
			return true;
		}
	}

	return false;
}

void SIKRigSkeleton::HandleIncludeBone()
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return;
	}
	
	// exclude selected bones
	TArray<TSharedPtr<FIKRigTreeElement>> SelectedBones;
	GetSelectedBones(SelectedBones);
	for (const TSharedPtr<FIKRigTreeElement>& BoneItem : SelectedBones)
	{
		Controller->AssetController->SetBoneExcluded(BoneItem->BoneName, false);
	}

	// show normal bone name after being included
	RefreshTreeView();
}

bool SIKRigSkeleton::CanIncludeBone()
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false;
	}
	
	// must have at least 1 bone selected that is EXCLUDED
	TArray<TSharedPtr<FIKRigTreeElement>> SelectedBones;
	GetSelectedBones(SelectedBones);
	for (const TSharedPtr<FIKRigTreeElement>& BoneItem : SelectedBones)
	{
		if (Controller->AssetController->GetBoneExcluded(BoneItem->BoneName))
		{
			return true;
		}
	}

	return false;
}

void SIKRigSkeleton::HandleNewRetargetChain()
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return;
	}
	
	TArray<FIKRigSkeletonChain> BoneChains;
	GetSelectedBoneChains(BoneChains);
	for (const FIKRigSkeletonChain& BoneChain : BoneChains)
	{
		Controller->AddNewRetargetChain(BoneChain.StartBone, BoneChain.StartBone, BoneChain.EndBone);
	}
	
	Controller->RefreshAllViews();
}

bool SIKRigSkeleton::CanAddNewRetargetChain()
{
	TArray<TSharedPtr<FIKRigTreeElement>> SelectedBones;
	GetSelectedBones(SelectedBones);
	return !SelectedBones.IsEmpty();
}

void SIKRigSkeleton::HandleSetRetargetRoot()
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return;
	}
	
	// get selected bones
	TArray<TSharedPtr<FIKRigTreeElement>> SelectedBones;
	GetSelectedBones(SelectedBones);

	// must have at least 1 bone selected
	if (SelectedBones.IsEmpty())
	{
		return;
	}

	// set the first selected bone as the retarget root
	Controller->AssetController->SetRetargetRoot(SelectedBones[0]->BoneName);

	// show root bone after being set
	Controller->RefreshAllViews();
}

bool SIKRigSkeleton::CanSetRetargetRoot()
{
	TArray<TSharedPtr<FIKRigTreeElement>> SelectedBones;
	GetSelectedBones(SelectedBones);
	return !SelectedBones.IsEmpty();
}

void SIKRigSkeleton::HandleClearRetargetRoot()
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return;
	}
	
	Controller->AssetController->SetRetargetRoot(NAME_None);
	Controller->RefreshAllViews();
}

bool SIKRigSkeleton::CanClearRetargetRoot()
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false;
	}

	return Controller->AssetController->GetRetargetRoot() != NAME_None;
}

bool SIKRigSkeleton::IsBoneInSelection(TArray<TSharedPtr<FIKRigTreeElement>>& SelectedBoneItems, const FName& BoneName)
{
	for (const TSharedPtr<FIKRigTreeElement>& Item : SelectedBoneItems)
	{
		if (Item->BoneName == BoneName)
		{
			return true;
		}
	}
	return false;
}

void SIKRigSkeleton::GetSelectedBones(TArray<TSharedPtr<FIKRigTreeElement>>& OutBoneItems) const
{
	const TArray<TSharedPtr<FIKRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
	for (const TSharedPtr<FIKRigTreeElement>& Item : SelectedItems)
	{
		if (Item->ElementType == IKRigTreeElementType::BONE)
		{
			OutBoneItems.Add(Item);
        }
	}
}

void SIKRigSkeleton::GetSelectedBoneNames(TArray<FName>& OutSelectedBoneNames) const
{
	TArray<TSharedPtr<FIKRigTreeElement>> OutSelectedBones;
	GetSelectedBones(OutSelectedBones);
	OutSelectedBoneNames.Reset();
	for (TSharedPtr<FIKRigTreeElement> SelectedBoneItem : OutSelectedBones)
	{
		OutSelectedBoneNames.Add(SelectedBoneItem->BoneName);
	}
}

void SIKRigSkeleton::GetSelectedGoals(TArray<TSharedPtr<FIKRigTreeElement>>& OutSelectedGoals) const
{
	OutSelectedGoals.Reset();
	TArray<TSharedPtr<FIKRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
	for (const TSharedPtr<FIKRigTreeElement>& Item : SelectedItems)
	{
		if (Item->ElementType == IKRigTreeElementType::GOAL)
		{
			OutSelectedGoals.Add(Item);
		}
	}
}

int32 SIKRigSkeleton::GetNumSelectedGoals()
{
	TArray<TSharedPtr<FIKRigTreeElement>> OutSelectedGoals;
	GetSelectedGoals(OutSelectedGoals);
	return OutSelectedGoals.Num();
}

void SIKRigSkeleton::GetSelectedGoalNames(TArray<FName>& OutSelectedGoalNames) const
{
	TArray<TSharedPtr<FIKRigTreeElement>> OutSelectedGoals;
	GetSelectedGoals(OutSelectedGoals);
	OutSelectedGoalNames.Reset();
	for (TSharedPtr<FIKRigTreeElement> SelectedGoalItem : OutSelectedGoals)
	{
		OutSelectedGoalNames.Add(SelectedGoalItem->GoalName);
	}
}

bool SIKRigSkeleton::IsGoalSelected(const FName& GoalName)
{
	TArray<TSharedPtr<FIKRigTreeElement>> OutSelectedGoals;
	GetSelectedGoals(OutSelectedGoals);
	for (TSharedPtr<FIKRigTreeElement> SelectedGoalItem : OutSelectedGoals)
	{
		if (SelectedGoalItem->GoalName == GoalName)
		{
			return true;
		}
	}
	return false;
}

void SIKRigSkeleton::HandleRenameGoal() const
{
	TArray<TSharedPtr<FIKRigTreeElement>> SelectedGoals;
	GetSelectedGoals(SelectedGoals);
	if (SelectedGoals.Num() != 1)
	{
		return;
	}
	
	SelectedGoals[0]->RequestRename();
}

bool SIKRigSkeleton::CanRenameGoal() const
{
	TArray<TSharedPtr<FIKRigTreeElement>> SelectedGoals;
	GetSelectedGoals(SelectedGoals);
	return SelectedGoals.Num() == 1;
}

void SIKRigSkeleton::RefreshTreeView(bool IsInitialSetup)
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return;
	}
	
	// save expansion and selection state
	TreeView->SaveAndClearState();
	
	// reset all tree items
	RootElements.Reset();
	AllElements.Reset();

	// validate we have a skeleton to load
	UIKRigController* AssetController = Controller->AssetController;
	const FIKRigSkeleton& Skeleton = AssetController->GetIKRigSkeleton();
	if (Skeleton.BoneNames.IsEmpty())
	{
		TreeView->RequestTreeRefresh();
		return;
	}

	// get all goals
	const TArray<UIKRigEffectorGoal*>& Goals = AssetController->GetAllGoals();
	
	// get all solvers
	const TArray<UIKRigSolver*>& Solvers = AssetController->GetSolverArray();
	// record bone element indices
	TMap<FName, int32> BoneTreeElementIndices;

	// create all bone elements
	for (const FName BoneName : Skeleton.BoneNames)
	{
		// create "Bone" tree element for this bone
		const FText BoneDisplayName = FText::FromName(BoneName);
		TSharedPtr<FIKRigTreeElement> BoneElement = MakeShared<FIKRigTreeElement>(BoneDisplayName, IKRigTreeElementType::BONE);
		BoneElement.Get()->BoneName = BoneName;
		const int32 BoneElementIndex = AllElements.Add(BoneElement);
		BoneTreeElementIndices.Add(BoneName, BoneElementIndex);

		// create all "Bone Setting" tree elements for this bone
		for (int32 SolverIndex=0; SolverIndex<Solvers.Num(); ++SolverIndex)
		{
			if (Solvers[SolverIndex]->GetBoneSetting(BoneName))
			{
				const FText SolverDisplayName = FText::FromString(AssetController->GetSolverUniqueName(SolverIndex));
				const FText BoneSettingDisplayName = FText::Format(LOCTEXT("BoneSettings", "{0} settings for {1}"), BoneDisplayName, SolverDisplayName);
				TSharedPtr<FIKRigTreeElement> SettingsItem = MakeShared<FIKRigTreeElement>(BoneSettingDisplayName, IKRigTreeElementType::BONE_SETTINGS);
				SettingsItem->BoneSettingBoneName = BoneName;
				SettingsItem->BoneSettingsSolverIndex = SolverIndex;
				AllElements.Add(SettingsItem);
				// store hierarchy pointers for item
				BoneElement->Children.Add(SettingsItem);
				SettingsItem->Parent = BoneElement;
			}
		}

		// create all "Goal" and "Effector" tree elements for this bone
		for (const UIKRigEffectorGoal* Goal : Goals)
		{
			if (Goal->BoneName != BoneName)
			{
				continue;
			}
			
			// make new element for goal
			const FText GoalDisplayName = FText::FromName(Goal->GoalName);
			TSharedPtr<FIKRigTreeElement> GoalItem = MakeShared<FIKRigTreeElement>(GoalDisplayName, IKRigTreeElementType::GOAL);
			GoalItem->GoalName = Goal->GoalName;
			AllElements.Add(GoalItem);

			// store hierarchy pointers for goal
			BoneElement->Children.Add(GoalItem);
			GoalItem->Parent = BoneElement;

			// add all solver settings connected to this goal
			for (int32 SolverIndex=0; SolverIndex<Solvers.Num(); ++SolverIndex)
			{
				if (UObject* GoalSettings = AssetController->GetGoalSettingsForSolver(Goal->GoalName, SolverIndex))
				{
					// make new element for solver goal
					const FText SolverDisplayName = FText::FromString(AssetController->GetSolverUniqueName(SolverIndex));
					const FText SolverGoalDisplayName = FText::Format(LOCTEXT("GoalSettingsForSolver", "{0} settings for solver {1}"), FText::FromName(Goal->GoalName), SolverDisplayName);
					TSharedPtr<FIKRigTreeElement> SolverGoalItem = MakeShared<FIKRigTreeElement>(SolverGoalDisplayName, IKRigTreeElementType::SOLVERGOAL);
					SolverGoalItem->SolverGoalIndex = SolverIndex;
					SolverGoalItem->SolverGoalName = Goal->GoalName;
					AllElements.Add(SolverGoalItem);
					SolverGoalItem->Parent = GoalItem;
					GoalItem->Children.Add(SolverGoalItem);
				}
			}
		}
	}

	// store children/parent pointers on all bone elements
	for (int32 BoneIndex=0; BoneIndex<Skeleton.BoneNames.Num(); ++BoneIndex)
	{
		const FName BoneName = Skeleton.BoneNames[BoneIndex];
		const TSharedPtr<FIKRigTreeElement> BoneTreeElement = AllElements[BoneTreeElementIndices[BoneName]];
		const int32 ParentIndex = Skeleton.ParentIndices[BoneIndex];
		if (ParentIndex < 0)
		{
			// store the root element
			RootElements.Add(BoneTreeElement);
			// has no parent, so skip storing parent pointer
			continue;
		}

		// get parent tree element
		const FName ParentBoneName = Skeleton.BoneNames[ParentIndex];
		const TSharedPtr<FIKRigTreeElement> ParentBoneTreeElement = AllElements[BoneTreeElementIndices[ParentBoneName]];
		// store pointer to child on parent
		ParentBoneTreeElement->Children.Add(BoneTreeElement);
		// store pointer to parent on child
		BoneTreeElement->Parent = ParentBoneTreeElement;
	}

	// expand all elements upon the initial construction of the tree
	if (IsInitialSetup)
	{
		for (TSharedPtr<FIKRigTreeElement> RootElement : RootElements)
		{
			SetExpansionRecursive(RootElement, false, true);
		}
	}
	else
	{
		// restore expansion and selection state
		for (const TSharedPtr<FIKRigTreeElement>& Element : AllElements)
		{
			TreeView->RestoreState(Element);
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
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return;
	}

	// update details view
	const TArray<TSharedPtr<FIKRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
	Controller->ShowDetailsForElements(SelectedItems);

	// NOTE: we may want to set the last selected item here
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
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return;
	}
	
	// to rename an item, you have to select it first, then click on it again within a time limit (slow double click)
	const bool ClickedOnSameItem = TreeView->LastSelected.Pin().Get() == InItem.Get();
	const uint32 CurrentCycles = FPlatformTime::Cycles();
	const double SecondsPassed = static_cast<double>(CurrentCycles - TreeView->LastClickCycles) * FPlatformTime::GetSecondsPerCycle();
	if (ClickedOnSameItem && SecondsPassed > 0.25f)
	{
		RegisterActiveTimer(0.f,
            FWidgetActiveTimerDelegate::CreateLambda([this](double, float) {
            HandleRenameGoal();
            return EActiveTimerReturnType::Stop;
        }));
	}

	TreeView->LastClickCycles = CurrentCycles;
	TreeView->LastSelected = InItem;
	Controller->SetLastSelectedType(EIKRigSelectionType::Hierarchy);
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
	
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return FReply::Handled();
	}

	const FIKRigTreeElement& DraggedElement = *DragDropOp.Get()->Element.Pin().Get();
	ensure(DraggedElement.ElementType == IKRigTreeElementType::GOAL);		// drag only supported for goals
	ensure(TargetItem.Get()->ElementType == IKRigTreeElementType::BONE);	// drop only supported for bones

	// re-parent the goal to a different bone
	UIKRigController* AssetController = Controller->AssetController;
	const bool bWasReparented = AssetController->SetGoalBone(DraggedElement.GoalName, TargetItem.Get()->BoneName);
	if (bWasReparented)
	{
		Controller->RefreshAllViews();
	}
	
	return FReply::Handled();
}

FReply SIKRigSkeleton::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();

	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return FReply::Handled();
	}

	if (CommandList.IsValid() && CommandList->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}
	
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE