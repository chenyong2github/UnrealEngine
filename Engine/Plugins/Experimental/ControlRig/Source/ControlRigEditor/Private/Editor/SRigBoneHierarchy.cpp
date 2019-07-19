// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SRigBoneHierarchy.h"
#include "Widgets/Input/SComboButton.h"
#include "EditorStyleSet.h"
#include "Widgets/Input/SSearchBox.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "ControlRigEditor.h"
#include "BlueprintActionDatabase.h"
#include "BlueprintVariableNodeSpawner.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "K2Node_VariableGet.h"
#include "ControlRigBlueprintUtils.h"
#include "NodeSpawners/ControlRigPropertyNodeSpawner.h"
#include "ControlRigHierarchyCommands.h"
#include "ControlRigBlueprint.h"
#include "Graph/ControlRigGraph.h"
#include "Graph/ControlRigGraphNode.h"
#include "Graph/ControlRigGraphSchema.h"
#include "GraphEditorModule.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "AnimationRuntime.h"
#include "PropertyCustomizationHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "Editor/EditorEngine.h"
#include "HelperUtil.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "ControlRig.h"

#define LOCTEXT_NAMESPACE "SRigBoneHierarchy"

//////////////////////////////////////////////////////////////
/// FRigTreeBone
///////////////////////////////////////////////////////////
FRigTreeBone::FRigTreeBone(const FName& InBone, TWeakPtr<SRigBoneHierarchy> InHierarchyHandler)
{
	CachedBone = InBone;
}

TSharedRef<ITableRow> FRigTreeBone::MakeTreeRowWidget(TSharedPtr<FControlRigEditor> InControlRigEditor, const TSharedRef<STableViewBase>& InOwnerTable, TSharedRef<FRigTreeBone> InRigTreeBone, TSharedRef<FUICommandList> InCommandList, TSharedPtr<SRigBoneHierarchy> InHierarchy)
{
	return SNew(SRigBoneHierarchyItem, InControlRigEditor, InOwnerTable, InRigTreeBone, InCommandList, InHierarchy)
		.OnRenameBone(InHierarchy.Get(), &SRigBoneHierarchy::RenameBone)
		.OnVerifyBoneNameChanged(InHierarchy.Get(), &SRigBoneHierarchy::OnVerifyNameChanged);
}

void FRigTreeBone::RequestRename()
{
	OnRenameRequested.ExecuteIfBound();
}

//////////////////////////////////////////////////////////////
/// FRigBoneHierarchyDragDropOp
///////////////////////////////////////////////////////////
TSharedRef<FRigBoneHierarchyDragDropOp> FRigBoneHierarchyDragDropOp::New(TArray<FName> InBoneNames)
{
	TSharedRef<FRigBoneHierarchyDragDropOp> Operation = MakeShared<FRigBoneHierarchyDragDropOp>();
	Operation->BoneNames = InBoneNames;
	Operation->Construct();
	return Operation;
}

TSharedPtr<SWidget> FRigBoneHierarchyDragDropOp::GetDefaultDecorator() const
{
	return SNew(SBorder)
		.Visibility(EVisibility::Visible)
		.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
		[
			SNew(STextBlock)
			.Text(FText::FromString(GetJoinedBoneNames()))
			.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
		];
}

FString FRigBoneHierarchyDragDropOp::GetJoinedBoneNames() const
{
	TArray<FString> BoneNameStrings;
	for (const FName& BoneName : BoneNames)
	{
		BoneNameStrings.Add(BoneName.ToString());
	}
	return FString::Join(BoneNameStrings, TEXT(","));
}


//////////////////////////////////////////////////////////////
/// SRigBoneHierarchyItem
///////////////////////////////////////////////////////////
void SRigBoneHierarchyItem::Construct(const FArguments& InArgs, TSharedPtr<FControlRigEditor> InControlRigEditor, const TSharedRef<STableViewBase>& OwnerTable, TSharedRef<FRigTreeBone> InRigTreeBone, TSharedRef<FUICommandList> InCommandList, TSharedPtr<SRigBoneHierarchy> InHierarchy)
{
	WeakRigTreeBone = InRigTreeBone;
	WeakCommandList = InCommandList;
	ControlRigEditor = InControlRigEditor;

	OnVerifyBoneNameChanged = InArgs._OnVerifyBoneNameChanged;
	OnRenameBone = InArgs._OnRenameBone;

	TSharedPtr< SInlineEditableTextBlock > InlineWidget;

	STableRow<TSharedPtr<FRigTreeBone>>::Construct(
		STableRow<TSharedPtr<FRigTreeBone>>::FArguments()
		.OnDragDetected(InHierarchy.Get(), &SRigBoneHierarchy::OnDragDetected)
		.Content()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SAssignNew(InlineWidget, SInlineEditableTextBlock)
				.Text(this, &SRigBoneHierarchyItem::GetName)
				//.HighlightText(FilterText)
				.OnVerifyTextChanged(this, &SRigBoneHierarchyItem::OnVerifyNameChanged)
				.OnTextCommitted(this, &SRigBoneHierarchyItem::OnNameCommitted)
				.MultiLine(false)
			]
		], OwnerTable);

	InRigTreeBone->OnRenameRequested.BindSP(InlineWidget.Get(), &SInlineEditableTextBlock::EnterEditingMode);
}

FText SRigBoneHierarchyItem::GetName() const
{
	return (FText::FromName(WeakRigTreeBone.Pin()->CachedBone));
}

bool SRigBoneHierarchyItem::OnVerifyNameChanged(const FText& InText, FText& OutErrorMessage)
{
	const FName NewName = FName(*InText.ToString());
	if (OnVerifyBoneNameChanged.IsBound())
	{
		return OnVerifyBoneNameChanged.Execute(WeakRigTreeBone.Pin()->CachedBone, NewName, OutErrorMessage);
	}

	// if not bound, just allow
	return true;
}

void SRigBoneHierarchyItem::OnNameCommitted(const FText& InText, ETextCommit::Type InCommitType) const
{
	// for now only allow enter
	// because it is important to keep the unique names per pose
	if (InCommitType == ETextCommit::OnEnter)
	{
		FName NewName = FName(*InText.ToString());
		FName OldName = WeakRigTreeBone.Pin()->CachedBone;

		if (!OnRenameBone.IsBound() || OnRenameBone.Execute(OldName, NewName))
		{
			if (WeakRigTreeBone.IsValid())
			{
				WeakRigTreeBone.Pin()->CachedBone = NewName;
			}
		}
	}
}

///////////////////////////////////////////////////////////

SRigBoneHierarchy::~SRigBoneHierarchy()
{
	if (ControlRigEditor.IsValid())
	{
		ControlRigBlueprint = ControlRigEditor.Pin()->GetControlRigBlueprint();
		if (ControlRigBlueprint.IsValid())
		{
			ControlRigBlueprint->HierarchyContainer.OnElementAdded.RemoveAll(this);
			ControlRigBlueprint->HierarchyContainer.OnElementRemoved.RemoveAll(this);
			ControlRigBlueprint->HierarchyContainer.OnElementRenamed.RemoveAll(this);
			ControlRigBlueprint->HierarchyContainer.OnElementReparented.RemoveAll(this);
			ControlRigBlueprint->HierarchyContainer.OnElementSelected.RemoveAll(this);
		}
	}
}

void SRigBoneHierarchy::Construct(const FArguments& InArgs, TSharedRef<FControlRigEditor> InControlRigEditor)
{
	ControlRigEditor = InControlRigEditor;

	ControlRigBlueprint = ControlRigEditor.Pin()->GetControlRigBlueprint();
	// @todo: find a better place to do it
	ControlRigBlueprint->HierarchyContainer.BoneHierarchy.Initialize();

	ControlRigBlueprint->HierarchyContainer.OnElementAdded.AddRaw(this, &SRigBoneHierarchy::OnRigElementAdded);
	ControlRigBlueprint->HierarchyContainer.OnElementRemoved.AddRaw(this, &SRigBoneHierarchy::OnRigElementRemoved);
	ControlRigBlueprint->HierarchyContainer.OnElementRenamed.AddRaw(this, &SRigBoneHierarchy::OnRigElementRenamed);
	ControlRigBlueprint->HierarchyContainer.OnElementReparented.AddRaw(this, &SRigBoneHierarchy::OnRigElementReparented);
	ControlRigBlueprint->HierarchyContainer.OnElementSelected.AddRaw(this, &SRigBoneHierarchy::OnRigElementSelected);

	// for deleting, renaming, dragging
	CommandList = MakeShared<FUICommandList>();

	UEditorEngine* Editor = Cast<UEditorEngine>(GEngine);
	if (Editor != nullptr)
	{
		Editor->RegisterForUndo(this);
	}

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
				+SVerticalBox::Slot()
				.AutoHeight()
				.VAlign(VAlign_Top)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.Padding(3.0f, 1.0f)
					[
						SAssignNew(FilterBox, SSearchBox)
						.OnTextChanged(this, &SRigBoneHierarchy::OnFilterTextChanged)
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
				SAssignNew(TreeView, STreeView<TSharedPtr<FRigTreeBone>>)
				.TreeItemsSource(&RootBones)
				.SelectionMode(ESelectionMode::Multi)
				.OnGenerateRow(this, &SRigBoneHierarchy::MakeTableRowWidget)
				.OnGetChildren(this, &SRigBoneHierarchy::HandleGetChildrenForTree)
				.OnSelectionChanged(this, &SRigBoneHierarchy::OnSelectionChanged)
				.OnContextMenuOpening(this, &SRigBoneHierarchy::CreateContextMenu)
				.HighlightParentNodesForSelection(true)
				.ItemHeight(24)
			]
		]
	];

	bIsChangingRigHierarchy = false;
	RefreshTreeView();
}

void SRigBoneHierarchy::BindCommands()
{
	// create new command
	const FControlRigHierarchyCommands& Commands = FControlRigHierarchyCommands::Get();
	CommandList->MapAction(Commands.AddItem,
		FExecuteAction::CreateSP(this, &SRigBoneHierarchy::HandleNewItem));

	CommandList->MapAction(Commands.DuplicateItem,
		FExecuteAction::CreateSP(this, &SRigBoneHierarchy::HandleDuplicateItem),
		FCanExecuteAction::CreateSP(this, &SRigBoneHierarchy::CanDuplicateItem));

	CommandList->MapAction(Commands.DeleteItem,
		FExecuteAction::CreateSP(this, &SRigBoneHierarchy::HandleDeleteItem),
		FCanExecuteAction::CreateSP(this, &SRigBoneHierarchy::CanDeleteItem));

	CommandList->MapAction(Commands.RenameItem,
		FExecuteAction::CreateSP(this, &SRigBoneHierarchy::HandleRenameItem),
		FCanExecuteAction::CreateSP(this, &SRigBoneHierarchy::CanRenameItem));
}

void SRigBoneHierarchy::OnFilterTextChanged(const FText& SearchText)
{
	FilterText = SearchText;

	RefreshTreeView();
}

void SRigBoneHierarchy::RefreshTreeView()
{
	RootBones.Reset();
	FilteredRootBones.Reset();

	if (ControlRigBlueprint.IsValid())
	{
		FRigBoneHierarchy& Hierarchy = ControlRigBlueprint->HierarchyContainer.BoneHierarchy;

		TMap<FName, TSharedPtr<FRigTreeBone>> SearchTable;

		FString FilteredString = FilterText.ToString();
		const bool bSearchOff = FilteredString.IsEmpty();
		for (int32 BoneIndex = 0; BoneIndex < Hierarchy.Num(); ++BoneIndex)
		{
			const FRigBone& Bone = Hierarchy[BoneIndex];

			// create new item
			if (bSearchOff)
			{
				TSharedPtr<FRigTreeBone> NewItem = MakeShared<FRigTreeBone>(Bone.Name, SharedThis(this));
				SearchTable.Add(Bone.Name, NewItem);

				if (Bone.ParentName == NAME_None)
				{
					RootBones.Add(NewItem);
				}
				else
				{
					// you have to find one
					TSharedPtr<FRigTreeBone>* FoundItem = SearchTable.Find(Bone.ParentName);
					check(FoundItem);
					// add to children list
					FoundItem->Get()->Children.Add(NewItem);
				}
			}
			else if (Bone.Name.ToString().Contains(FilteredString))
			{
				// if contains, just list out everything to root
				TSharedPtr<FRigTreeBone> NewItem = MakeShared<FRigTreeBone>(Bone.Name, SharedThis(this));
				// during search, everything is on root
				RootBones.Add(NewItem);
			}
		}

		if (bSearchOff)
		{
			for (int32 RootIndex = 0; RootIndex < RootBones.Num(); ++RootIndex)
			{
				SetExpansionRecursive(RootBones[RootIndex]);
			}
		}
	}

	TreeView->RequestTreeRefresh();

	if (ControlRigBlueprint.IsValid())
	{
		FRigBoneHierarchy& Hierarchy = ControlRigBlueprint->HierarchyContainer.BoneHierarchy;
		for (const FName& SelectedBone : Hierarchy.CurrentSelection())
		{
			OnRigElementSelected(&ControlRigBlueprint->HierarchyContainer, ERigElementType::Bone, SelectedBone, true);
		}
	}
}

void SRigBoneHierarchy::SetExpansionRecursive(TSharedPtr<FRigTreeBone> InBone)
{
	TreeView->SetItemExpansion(InBone, true);

	for (int32 ChildIndex = 0; ChildIndex < InBone->Children.Num(); ++ChildIndex)
	{
		SetExpansionRecursive(InBone->Children[ChildIndex]);
	}
}
TSharedRef<ITableRow> SRigBoneHierarchy::MakeTableRowWidget(TSharedPtr<FRigTreeBone> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return InItem->MakeTreeRowWidget(ControlRigEditor.Pin(), OwnerTable, InItem.ToSharedRef(), CommandList.ToSharedRef(), SharedThis(this));
}

void SRigBoneHierarchy::HandleGetChildrenForTree(TSharedPtr<FRigTreeBone> InItem, TArray<TSharedPtr<FRigTreeBone>>& OutChildren)
{
	OutChildren = InItem.Get()->Children;
}

void SRigBoneHierarchy::OnSelectionChanged(TSharedPtr<FRigTreeBone> Selection, ESelectInfo::Type SelectInfo)
{
	if (bIsChangingRigHierarchy)
	{
		return;
	}

	FRigBoneHierarchy* RigHierarchy = GetHierarchy();

	if (RigHierarchy)
	{
		TGuardValue<bool> GuardRigHierarchyChanges(bIsChangingRigHierarchy, true);

		TArray<FName> OldSelection = RigHierarchy->CurrentSelection();
		TArray<FName> NewSelection;

		TArray<TSharedPtr<FRigTreeBone>> SelectedItems = TreeView->GetSelectedItems();
		for (const TSharedPtr<FRigTreeBone>& SelectedItem : SelectedItems)
		{
			NewSelection.Add(SelectedItem->CachedBone);
		}

		for (const FName& PreviouslySelected : OldSelection)
		{
			if (NewSelection.Contains(PreviouslySelected))
			{
				continue;
			}
			RigHierarchy->Select(PreviouslySelected, false);
		}

		for (const FName& NewlySelected : NewSelection)
		{
			RigHierarchy->Select(NewlySelected, true);
		}
	}
}

TSharedPtr<FRigTreeBone> FindBone(const FName& InBoneName, TSharedPtr<FRigTreeBone> CurrentItem)
{
	if (CurrentItem->CachedBone == InBoneName)
	{
		return CurrentItem;
	}

	for (int32 ChildIndex = 0; ChildIndex < CurrentItem->Children.Num(); ++ChildIndex)
	{
		TSharedPtr<FRigTreeBone> Found = FindBone(InBoneName, CurrentItem->Children[ChildIndex]);
		if (Found.IsValid())
		{
			return Found;
		}
	}

	return TSharedPtr<FRigTreeBone>();
}

void SRigBoneHierarchy::OnRigElementAdded(FRigHierarchyContainer* Container, ERigElementType ElementType, const FName& InName)
{
	if (bIsChangingRigHierarchy || ElementType != ERigElementType::Bone)
	{
		return;
	}
	RefreshTreeView();
}

void SRigBoneHierarchy::OnRigElementRemoved(FRigHierarchyContainer* Container, ERigElementType ElementType, const FName& InName)
{
	if (bIsChangingRigHierarchy || ElementType != ERigElementType::Bone)
	{
		return;
	}
	RefreshTreeView();
}

void SRigBoneHierarchy::OnRigElementRenamed(FRigHierarchyContainer* Container, ERigElementType ElementType, const FName& InOldName, const FName& InNewName)
{
	if (bIsChangingRigHierarchy || ElementType != ERigElementType::Bone)
	{
		return;
	}
	RefreshTreeView();
}

void SRigBoneHierarchy::OnRigElementReparented(FRigHierarchyContainer* Container, ERigElementType ElementType, const FName& InName, const FName& InOldParentName, const FName& InNewParentName)
{
	if (bIsChangingRigHierarchy || ElementType != ERigElementType::Bone)
	{
		return;
	}
	RefreshTreeView();
}

void SRigBoneHierarchy::OnRigElementSelected(FRigHierarchyContainer* Container, ERigElementType ElementType, const FName& InName, bool bSelected)
{
	if (bIsChangingRigHierarchy || ElementType != ERigElementType::Bone)
	{
		return;
	}

	for (int32 RootIndex = 0; RootIndex < RootBones.Num(); ++RootIndex)
	{
		TSharedPtr<FRigTreeBone> Found = FindBone(InName, RootBones[RootIndex]);
		if (Found.IsValid())
		{
			TreeView->SetItemSelection(Found, bSelected);
		}
	}
}

void SRigBoneHierarchy::ClearDetailPanel() const
{
	ControlRigEditor.Pin()->ClearDetailObject();
}

TSharedPtr< SWidget > SRigBoneHierarchy::CreateContextMenu()
{
	const FControlRigHierarchyCommands& Actions = FControlRigHierarchyCommands::Get();

	const bool CloseAfterSelection = true;
	FMenuBuilder MenuBuilder(CloseAfterSelection, CommandList);
	{
		MenuBuilder.BeginSection("HierarchyEditAction", LOCTEXT("EditAction", "Edit"));
		MenuBuilder.AddMenuEntry(Actions.AddItem);
		MenuBuilder.AddMenuEntry(Actions.DeleteItem);
		MenuBuilder.AddMenuEntry(Actions.DuplicateItem);
		MenuBuilder.AddMenuEntry(Actions.RenameItem);

		MenuBuilder.AddMenuSeparator();
		MenuBuilder.AddSubMenu(
			LOCTEXT("ImportSubMenu", "Import"),
			LOCTEXT("ImportSubMenu_ToolTip", "Import hierarchy to the current rig. This only imports non-existing node. For example, if there is hand_r, it won't import hand_r. \
				If you want to reimport whole new hiearchy, delete all nodes, and use import hierarchy."),
			FNewMenuDelegate::CreateSP(this, &SRigBoneHierarchy::CreateImportMenu)
		);

		MenuBuilder.AddMenuSeparator();
		MenuBuilder.AddSubMenu(
			LOCTEXT("RefreshSubMenu", "Refresh"),
			LOCTEXT("RefreshSubMenu_ToolTip", "Refresh the existing initial transform from the selected mesh. This only updates if the node is found."),
			FNewMenuDelegate::CreateSP(this, &SRigBoneHierarchy::CreateRefreshMenu)
		);
	
		MenuBuilder.EndSection();
	}

	return MenuBuilder.MakeWidget();
}

void SRigBoneHierarchy::CreateRefreshMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddWidget(
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(3)
		[
			SNew(STextBlock)
			.Font(FEditorStyle::GetFontStyle("ControlRig.Hierarchy.Menu"))
			.Text(LOCTEXT("RefreshMesh_Title", "Select Mesh"))
			.ToolTipText(LOCTEXT("RefreshMesh_Tooltip", "Select Mesh to refresh transform from... It will refresh init transform from selected mesh. This doesn't change hierarchy. \
				If you want to reimport hierarchy, please delete all nodes, and use import hierarchy."))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(3)
		[
			SNew(SObjectPropertyEntryBox)
			.AllowedClass(USkeletalMesh::StaticClass())
			.OnObjectChanged(this, &SRigBoneHierarchy::RefreshHierarchy)
		]
		,
		FText()
	);
}

void SRigBoneHierarchy::RefreshHierarchy(const FAssetData& InAssetData)
{
	FRigBoneHierarchy* Hier = GetHierarchy();
	USkeletalMesh* Mesh = Cast<USkeletalMesh>(InAssetData.GetAsset());
	if (Mesh && Hier)
	{
		FScopedTransaction Transaction(LOCTEXT("HierarchyRefresh", "Refresh Transform"));
		ControlRigBlueprint->Modify();

		const FReferenceSkeleton& RefSkeleton = Mesh->RefSkeleton;
		const TArray<FMeshBoneInfo>& BoneInfos = RefSkeleton.GetRefBoneInfo();
		const TArray<FTransform>& BonePoses = RefSkeleton.GetRefBonePose();

		for (int32 BoneIndex = 0; BoneIndex < RefSkeleton.GetNum(); ++BoneIndex)
		{
			// only add if you don't have it. This may change in the future
			int32 RigIndex = Hier->GetIndex(BoneInfos[BoneIndex].Name);
			if (RigIndex != INDEX_NONE)
			{
				// @todo: add optimized version without sorting, but if no sort, we should make sure not to use find index function
				Hier->SetInitialTransform(RigIndex, FAnimationRuntime::GetComponentSpaceTransform(RefSkeleton, BonePoses, BoneIndex));
			}
		}
	}
}
void SRigBoneHierarchy::CreateImportMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddWidget(
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(3)
		[
			SNew(STextBlock)
			.Font(FEditorStyle::GetFontStyle("ControlRig.Hierarchy.Menu"))
			.Text(LOCTEXT("ImportMesh_Title", "Select Mesh"))
			.ToolTipText(LOCTEXT("ImportMesh_Tooltip", "Select Mesh to import hierarchy from... It will only import if the node doens't exists in the current hierarchy."))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(3)
		[
			SNew(SObjectPropertyEntryBox)
			.AllowedClass(USkeletalMesh::StaticClass())
			.OnObjectChanged(this, &SRigBoneHierarchy::ImportHierarchy)
		]
		,
		FText()
	);
}

void SRigBoneHierarchy::ImportHierarchy(const FAssetData& InAssetData)
{
	FRigBoneHierarchy* Hier = GetHierarchy();
	USkeletalMesh* Mesh = Cast<USkeletalMesh> (InAssetData.GetAsset());
	if (Mesh && Hier)
	{
		FScopedTransaction Transaction(LOCTEXT("HierarchyImport", "Import Hierarchy"));
		ControlRigBlueprint->Modify();

		const FReferenceSkeleton& RefSkeleton = Mesh->RefSkeleton;
		const TArray<FMeshBoneInfo>& BoneInfos = RefSkeleton.GetRefBoneInfo();
		const TArray<FTransform>& BonePoses = RefSkeleton.GetRefBonePose();

		Hier->ClearSelection();

		TArray<FName> AddedBones;
		for (int32 BoneIndex = 0; BoneIndex < RefSkeleton.GetNum(); ++BoneIndex)
		{
			// only add if you don't have it. This may change in the future
			if (Hier->GetIndex(BoneInfos[BoneIndex].Name) == INDEX_NONE)
			{
				// @todo: add optimized version without sorting, but if no sort, we should make sure not to use find index function
				FName ParentName = (BoneInfos[BoneIndex].ParentIndex != INDEX_NONE) ? BoneInfos[BoneInfos[BoneIndex].ParentIndex].Name : NAME_None;
				Hier->Add(BoneInfos[BoneIndex].Name, ParentName, FAnimationRuntime::GetComponentSpaceTransform(RefSkeleton, BonePoses, BoneIndex));
				AddedBones.Add(BoneInfos[BoneIndex].Name);
			}
		}

		for (const FName& AddedBone : AddedBones)
		{
			Hier->Select(AddedBone);
		}

		FSlateApplication::Get().DismissAllMenus();
		RefreshTreeView();
	}
}

bool SRigBoneHierarchy::IsMultiSelected() const
{
	return TreeView->GetNumItemsSelected() > 0;
}

bool SRigBoneHierarchy::IsSingleSelected() const
{
	return TreeView->GetNumItemsSelected() == 1;
}

void SRigBoneHierarchy::HandleDeleteItem()
{
 	FRigBoneHierarchy* Hierarchy = GetHierarchy();
 	if (Hierarchy)
 	{
		ClearDetailPanel();
		FScopedTransaction Transaction(LOCTEXT("HierarchyTreeDeleteSelected", "Delete selected items from hierarchy"));
		ControlRigBlueprint->Modify();

		// clear detail view display
		ControlRigEditor.Pin()->ClearDetailObject();

		TArray<TSharedPtr<FRigTreeBone>> SelectedItems = TreeView->GetSelectedItems();

		for (int32 ItemIndex = 0; ItemIndex < SelectedItems.Num(); ++ItemIndex)
		{
			TGuardValue<bool> GuardRigHierarchyChanges(bIsChangingRigHierarchy, true);

			// when you select whole Bones, you might not have them anymore
			if (Hierarchy->GetIndex(SelectedItems[ItemIndex]->CachedBone) != INDEX_NONE)
			{
				Hierarchy->Remove(SelectedItems[ItemIndex]->CachedBone);
			}
		}

		RefreshTreeView();
		FSlateApplication::Get().DismissAllMenus();
 	}
}

bool SRigBoneHierarchy::CanDeleteItem() const
{
	return IsMultiSelected();
}

/** Delete Item */
void SRigBoneHierarchy::HandleNewItem()
{
	FRigBoneHierarchy* Hierarchy = GetHierarchy();
	if (Hierarchy)
	{
		// unselect current selected item
		ClearDetailPanel();

		FScopedTransaction Transaction(LOCTEXT("HierarchyTreeAdded", "Add new item to hierarchy"));
		ControlRigBlueprint->Modify();

		FName ParentName = NAME_None;
		FTransform ParentTransform = FTransform::Identity;

		TArray<TSharedPtr<FRigTreeBone>> SelectedItems = TreeView->GetSelectedItems();
		if (SelectedItems.Num() > 0)
		{
			ParentName = SelectedItems[0]->CachedBone;
			ParentTransform = Hierarchy->GetGlobalTransform(ParentName);
		}

		const FName NewBoneName = CreateUniqueName(TEXT("NewBone"));
		{
			TGuardValue<bool> GuardRigHierarchyChanges(bIsChangingRigHierarchy, true);
			Hierarchy->Add(NewBoneName, ParentName, ParentTransform);
		}
		Hierarchy->ClearSelection();
		Hierarchy->Select(NewBoneName);

		FSlateApplication::Get().DismissAllMenus();
		RefreshTreeView();
	}
}

/** Check whether we can deleting the selected item(s) */
bool SRigBoneHierarchy::CanDuplicateItem() const
{
	return IsMultiSelected();
}

/** Duplicate Item */
void SRigBoneHierarchy::HandleDuplicateItem()
{
	FRigBoneHierarchy* Hierarchy = GetHierarchy();
	if (Hierarchy)
	{
		ClearDetailPanel();

		FScopedTransaction Transaction(LOCTEXT("HierarchyTreeDuplicateSelected", "Duplicate selected items from hierarchy"));
		ControlRigBlueprint->Modify();

		TArray<TSharedPtr<FRigTreeBone>> SelectedItems = TreeView->GetSelectedItems();
		TArray<FName> NewNames;
		for (int32 Index = 0; Index < SelectedItems.Num(); ++Index)
		{
			TGuardValue<bool> GuardRigHierarchyChanges(bIsChangingRigHierarchy, true);

			FName Name = SelectedItems[Index]->CachedBone;
			FTransform Transform = Hierarchy->GetGlobalTransform(Name);

			FName ParentName = (*Hierarchy)[Name].ParentName;

			const FName NewName = CreateUniqueName(Name);
			Hierarchy->Add(NewName, ParentName, Transform);
			NewNames.Add(NewName);
		}

		Hierarchy->ClearSelection();
		for (int32 Index = 0; Index < NewNames.Num(); ++Index)
		{
			Hierarchy->Select(NewNames[Index]);
		}

		FSlateApplication::Get().DismissAllMenus();
		RefreshTreeView();
	}
}

/** Check whether we can deleting the selected item(s) */
bool SRigBoneHierarchy::CanRenameItem() const
{
	return IsSingleSelected();
}

/** Delete Item */
void SRigBoneHierarchy::HandleRenameItem()
{
	FRigBoneHierarchy* Hierarchy = GetHierarchy();
	if (Hierarchy)
	{
		ClearDetailPanel();

		FScopedTransaction Transaction(LOCTEXT("HierarchyTreeRenameSelected", "Rename selected item from hierarchy"));
		ControlRigBlueprint->Modify();

		TArray<TSharedPtr<FRigTreeBone>> SelectedItems = TreeView->GetSelectedItems();
		if (SelectedItems.Num() > 0)
		{
			SelectedItems[0]->RequestRename();
		}
	}
}

FRigBoneHierarchy* SRigBoneHierarchy::GetHierarchy() const
{
	if (ControlRigBlueprint.IsValid())
	{
		return &ControlRigBlueprint->HierarchyContainer.BoneHierarchy;
	}

	return nullptr;
}

FRigBoneHierarchy* SRigBoneHierarchy::GetInstanceHierarchy() const
{
	if (ControlRigEditor.IsValid())
	{
		UControlRig* ControlRig = ControlRigEditor.Pin()->GetInstanceRig();
		if (ControlRig)
		{
			return &ControlRig->Hierarchy.BoneHierarchy;
		}
	}

	return nullptr;
}
FName SRigBoneHierarchy::CreateUniqueName(const FName& InBaseName) const
{
	return UtilityHelpers::CreateUniqueName(InBaseName, [this](const FName& CurName) { return GetHierarchy()->GetIndex(CurName) == INDEX_NONE; });
}

void SRigBoneHierarchy::PostRedo(bool bSuccess) 
{
	if (bSuccess)
	{
		RefreshTreeView();
	}
}

void SRigBoneHierarchy::PostUndo(bool bSuccess) 
{
	if (bSuccess)
	{
		RefreshTreeView();
	}
}

FReply SRigBoneHierarchy::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	TArray<FName> DraggedBoneNames;
	TArray<TSharedPtr<FRigTreeBone>> SelectedItems = TreeView->GetSelectedItems();
	for (const TSharedPtr<FRigTreeBone>& SelectedItem : SelectedItems)
	{
		DraggedBoneNames.Add(SelectedItem->CachedBone);
	}

	if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
	{
		if (ControlRigEditor.IsValid())
		{
			TSharedRef<FRigBoneHierarchyDragDropOp> DragDropOp = FRigBoneHierarchyDragDropOp::New(MoveTemp(DraggedBoneNames));
			DragDropOp->OnPerformDropToGraph.BindSP(ControlRigEditor.Pin().Get(), &FControlRigEditor::OnGraphNodeDropToPerform);
			return FReply::Handled().BeginDragDrop(DragDropOp);
		}
	}

	return FReply::Unhandled();
}


bool SRigBoneHierarchy::RenameBone(const FName& OldName, const FName& NewName)
{
	ClearDetailPanel();

	if (OldName == NewName)
	{
		return true;
	}

	// make sure there is no duplicate
	FRigBoneHierarchy* Hierarchy = GetHierarchy();
	if (Hierarchy)
	{
		Hierarchy->Rename(OldName, NewName);
		Hierarchy->ClearSelection();
		Hierarchy->Select(NewName);
		return true;
	}

	return false;
}

bool SRigBoneHierarchy::OnVerifyNameChanged(const FName& OldName, const FName& NewName, FText& OutErrorMessage)
{
	if (OldName == NewName)
	{
		return true;
	}

	// make sure there is no duplicate
	FRigBoneHierarchy* Hierarchy = GetHierarchy();
	if (Hierarchy)
	{
		if (!Hierarchy->IsNameAvailable(NewName))
		{
			OutErrorMessage = FText::FromString(TEXT("Duplicate name exists"));
			return false;
		}
	}

	return true;
}
#undef LOCTEXT_NAMESPACE