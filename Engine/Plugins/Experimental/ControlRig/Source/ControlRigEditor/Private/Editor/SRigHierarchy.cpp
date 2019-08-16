// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SRigHierarchy.h"
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

#define LOCTEXT_NAMESPACE "SRigHierarchy"

//////////////////////////////////////////////////////////////
/// FRigTreeBone
///////////////////////////////////////////////////////////
FRigTreeBone::FRigTreeBone(const FName& InBone, TWeakPtr<SRigHierarchy> InHierarchyHandler)
{
	CachedBone = InBone;
}

TSharedRef<ITableRow> FRigTreeBone::MakeTreeRowWidget(TSharedPtr<FControlRigEditor> InControlRigEditor, const TSharedRef<STableViewBase>& InOwnerTable, TSharedRef<FRigTreeBone> InRigTreeBone, TSharedRef<FUICommandList> InCommandList, TSharedPtr<SRigHierarchy> InHierarchy)
{
	return SNew(SRigHierarchyItem, InControlRigEditor, InOwnerTable, InRigTreeBone, InCommandList, InHierarchy)
		.OnRenameBone(InHierarchy.Get(), &SRigHierarchy::RenameBone)
		.OnVerifyBoneNameChanged(InHierarchy.Get(), &SRigHierarchy::OnVerifyNameChanged);
}

void FRigTreeBone::RequestRename()
{
	OnRenameRequested.ExecuteIfBound();
}

//////////////////////////////////////////////////////////////
/// FRigHierarchyDragDropOp
///////////////////////////////////////////////////////////
TSharedRef<FRigHierarchyDragDropOp> FRigHierarchyDragDropOp::New(TArray<FName> InBoneNames)
{
	TSharedRef<FRigHierarchyDragDropOp> Operation = MakeShared<FRigHierarchyDragDropOp>();
	Operation->BoneNames = InBoneNames;
	Operation->Construct();
	return Operation;
}

TSharedPtr<SWidget> FRigHierarchyDragDropOp::GetDefaultDecorator() const
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

FString FRigHierarchyDragDropOp::GetJoinedBoneNames() const
{
	TArray<FString> BoneNameStrings;
	for (const FName& BoneName : BoneNames)
	{
		BoneNameStrings.Add(BoneName.ToString());
	}
	return FString::Join(BoneNameStrings, TEXT(","));
}


//////////////////////////////////////////////////////////////
/// SRigHierarchyItem
///////////////////////////////////////////////////////////
void SRigHierarchyItem::Construct(const FArguments& InArgs, TSharedPtr<FControlRigEditor> InControlRigEditor, const TSharedRef<STableViewBase>& OwnerTable, TSharedRef<FRigTreeBone> InRigTreeBone, TSharedRef<FUICommandList> InCommandList, TSharedPtr<SRigHierarchy> InHierarchy)
{
	WeakRigTreeBone = InRigTreeBone;
	WeakCommandList = InCommandList;
	ControlRigEditor = InControlRigEditor;

	OnVerifyBoneNameChanged = InArgs._OnVerifyBoneNameChanged;
	OnRenameBone = InArgs._OnRenameBone;

	TSharedPtr< SInlineEditableTextBlock > InlineWidget;

	STableRow<TSharedPtr<FRigTreeBone>>::Construct(
		STableRow<TSharedPtr<FRigTreeBone>>::FArguments()
		.OnDragDetected(InHierarchy.Get(), &SRigHierarchy::OnDragDetected)
		.Content()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SAssignNew(InlineWidget, SInlineEditableTextBlock)
				.Text(this, &SRigHierarchyItem::GetName)
				//.HighlightText(FilterText)
				.OnVerifyTextChanged(this, &SRigHierarchyItem::OnVerifyNameChanged)
				.OnTextCommitted(this, &SRigHierarchyItem::OnNameCommitted)
				.MultiLine(false)
			]
		], OwnerTable);

	InRigTreeBone->OnRenameRequested.BindSP(InlineWidget.Get(), &SInlineEditableTextBlock::EnterEditingMode);
}

FText SRigHierarchyItem::GetName() const
{
	return (FText::FromName(WeakRigTreeBone.Pin()->CachedBone));
}

bool SRigHierarchyItem::OnVerifyNameChanged(const FText& InText, FText& OutErrorMessage)
{
	const FName NewName = FName(*InText.ToString());
	if (OnVerifyBoneNameChanged.IsBound())
	{
		return OnVerifyBoneNameChanged.Execute(WeakRigTreeBone.Pin()->CachedBone, NewName, OutErrorMessage);
	}

	// if not bound, just allow
	return true;
}

void SRigHierarchyItem::OnNameCommitted(const FText& InText, ETextCommit::Type InCommitType) const
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

SRigHierarchy::~SRigHierarchy()
{
}

void SRigHierarchy::Construct(const FArguments& InArgs, TSharedRef<FControlRigEditor> InControlRigEditor)
{
	ControlRigEditor = InControlRigEditor;

	ControlRigBlueprint = ControlRigEditor.Pin()->GetControlRigBlueprint();
	// @todo: find a better place to do it
	ControlRigBlueprint->Hierarchy.Initialize();
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
						.OnTextChanged(this, &SRigHierarchy::OnFilterTextChanged)
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
				.OnGenerateRow(this, &SRigHierarchy::MakeTableRowWidget)
				.OnGetChildren(this, &SRigHierarchy::HandleGetChildrenForTree)
				.OnSelectionChanged(this, &SRigHierarchy::OnSelectionChanged)
				.OnContextMenuOpening(this, &SRigHierarchy::CreateContextMenu)
				.HighlightParentNodesForSelection(true)
				.ItemHeight(24)
			]
		]
	];

	RefreshTreeView();
}

void SRigHierarchy::BindCommands()
{
	// create new command
	const FControlRigHierarchyCommands& Commands = FControlRigHierarchyCommands::Get();
	CommandList->MapAction(Commands.AddItem,
		FExecuteAction::CreateSP(this, &SRigHierarchy::HandleNewItem));

	CommandList->MapAction(Commands.DuplicateItem,
		FExecuteAction::CreateSP(this, &SRigHierarchy::HandleDuplicateItem),
		FCanExecuteAction::CreateSP(this, &SRigHierarchy::CanDuplicateItem));

	CommandList->MapAction(Commands.DeleteItem,
		FExecuteAction::CreateSP(this, &SRigHierarchy::HandleDeleteItem),
		FCanExecuteAction::CreateSP(this, &SRigHierarchy::CanDeleteItem));

	CommandList->MapAction(Commands.RenameItem,
		FExecuteAction::CreateSP(this, &SRigHierarchy::HandleRenameItem),
		FCanExecuteAction::CreateSP(this, &SRigHierarchy::CanRenameItem));
}

void SRigHierarchy::OnFilterTextChanged(const FText& SearchText)
{
	FilterText = SearchText;

	RefreshTreeView();
}

void SRigHierarchy::RefreshTreeView()
{
	RootBones.Reset();
	FilteredRootBones.Reset();

	if (ControlRigBlueprint.IsValid())
	{
		FRigHierarchy& Hierarchy = ControlRigBlueprint->Hierarchy;

		TMap<FName, TSharedPtr<FRigTreeBone>> SearchTable;

		FString FilteredString = FilterText.ToString();
		const bool bSearchOff = FilteredString.IsEmpty();
		const TArray<FRigBone>& Bones = Hierarchy.GetBones();
		for (int32 BoneIndex = 0; BoneIndex < Bones.Num(); ++BoneIndex)
		{
			const FRigBone& Bone = Bones[BoneIndex];

			// create new item
			if (bSearchOff)
			{
				TSharedPtr<FRigTreeBone> NewItem = MakeShared<FRigTreeBone>(Bones[BoneIndex].Name, SharedThis(this));
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
				TSharedPtr<FRigTreeBone> NewItem = MakeShared<FRigTreeBone>(Bones[BoneIndex].Name, SharedThis(this));
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
}

void SRigHierarchy::SetExpansionRecursive(TSharedPtr<FRigTreeBone> InBone)
{
	TreeView->SetItemExpansion(InBone, true);

	for (int32 ChildIndex = 0; ChildIndex < InBone->Children.Num(); ++ChildIndex)
	{
		SetExpansionRecursive(InBone->Children[ChildIndex]);
	}
}
TSharedRef<ITableRow> SRigHierarchy::MakeTableRowWidget(TSharedPtr<FRigTreeBone> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return InItem->MakeTreeRowWidget(ControlRigEditor.Pin(), OwnerTable, InItem.ToSharedRef(), CommandList.ToSharedRef(), SharedThis(this));
}

void SRigHierarchy::HandleGetChildrenForTree(TSharedPtr<FRigTreeBone> InItem, TArray<TSharedPtr<FRigTreeBone>>& OutChildren)
{
	OutChildren = InItem.Get()->Children;
}

void SRigHierarchy::OnSelectionChanged(TSharedPtr<FRigTreeBone> Selection, ESelectInfo::Type SelectInfo)
{
	// need dummy object
	if (Selection.IsValid())
	{
		FRigHierarchy* RigHierarchy = GetInstanceHierarchy();

		if (RigHierarchy)
		{
			const int32 BoneIndex = RigHierarchy->GetIndex(Selection->CachedBone);
			if (BoneIndex != INDEX_NONE)
			{
				ControlRigEditor.Pin()->SetDetailStruct(MakeShareable(new FStructOnScope(FRigBone::StaticStruct(), (uint8*)&RigHierarchy->GetBones()[BoneIndex])));
				ControlRigEditor.Pin()->SelectBone(Selection->CachedBone);
				return;
			}
			else
			{
				// clear the current selection
				ControlRigEditor.Pin()->ClearDetailObject();
				ControlRigEditor.Pin()->SelectBone(NAME_None);
			}
		}

		// if failed, try BP hierarhcy? Todo:
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

void SRigHierarchy::SelectBone(const FName& BoneName) const
{
	for (int32 RootIndex = 0; RootIndex < RootBones.Num(); ++RootIndex)
	{
		TSharedPtr<FRigTreeBone> Found = FindBone(BoneName, RootBones[RootIndex]);
		if (Found.IsValid())
		{
			TreeView->SetSelection(Found);
		}
	}
}

void SRigHierarchy::ClearDetailPanel() const
{
	ControlRigEditor.Pin()->ClearDetailObject();
}

TSharedPtr< SWidget > SRigHierarchy::CreateContextMenu()
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
			LOCTEXT("ImportSubMenu_ToolTip", "Import hierarchy to the current rig. This overrides the data if it contains the existing node."),
			FNewMenuDelegate::CreateSP(this, &SRigHierarchy::CreateImportMenu)
		);

		MenuBuilder.EndSection();
	}

	return MenuBuilder.MakeWidget();
}

void SRigHierarchy::CreateImportMenu(FMenuBuilder& MenuBuilder)
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
			.OnShouldFilterAsset(this, &SRigHierarchy::ShouldFilterOnImport)
			.OnObjectChanged(this, &SRigHierarchy::ImportHierarchy)
		]
		,
		FText()
	);
}

bool SRigHierarchy::ShouldFilterOnImport(const FAssetData& AssetData) const
{
	return (AssetData.AssetClass != USkeletalMesh::StaticClass()->GetFName() &&
		AssetData.AssetClass != USkeleton::StaticClass()->GetFName());
}

void SRigHierarchy::ImportHierarchy(const FAssetData& InAssetData)
{
	FRigHierarchy* Hier = GetHierarchy();
	if (Hier)
	{
		const FReferenceSkeleton* RefSkeleton = nullptr;
		if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(InAssetData.GetAsset()))
		{
			RefSkeleton = &Mesh->RefSkeleton;
			ControlRigBlueprint->SourceHierarchyImport = Mesh;
		}
		else if (USkeleton* Skeleton = Cast<USkeleton>(InAssetData.GetAsset()))
		{
			RefSkeleton = &Skeleton->GetReferenceSkeleton();
			ControlRigBlueprint->SourceHierarchyImport = Skeleton;
		}

		if (RefSkeleton)
		{
			FScopedTransaction Transaction(LOCTEXT("HierarchyImport", "Import Hierarchy"));
			ControlRigBlueprint->Modify();

			const TArray<FMeshBoneInfo>& BoneInfos = RefSkeleton->GetRefBoneInfo();
			const TArray<FTransform>& BonePoses = RefSkeleton->GetRefBonePose();

			for (int32 BoneIndex = 0; BoneIndex < RefSkeleton->GetNum(); ++BoneIndex)
			{
				// only add if you don't have it. This may change in the future
				int32 HierIndex = Hier->GetIndex(BoneInfos[BoneIndex].Name);
				FTransform InitialTransform = FAnimationRuntime::GetComponentSpaceTransform(*RefSkeleton, BonePoses, BoneIndex);
				// @todo: add optimized version without sorting, but if no sort, we should make sure not to use find index function
				FName ParentName = (BoneInfos[BoneIndex].ParentIndex != INDEX_NONE) ? BoneInfos[BoneInfos[BoneIndex].ParentIndex].Name : NAME_None;
				// if exists, see if we should change parents
				if (HierIndex != INDEX_NONE)
				{
					if (ParentName != Hier->GetParentName(BoneInfos[BoneIndex].Name))
					{
						// reparent
						Hier->Reparent(BoneInfos[BoneIndex].Name, ParentName);
					}

					Hier->SetInitialTransform(BoneInfos[BoneIndex].Name, InitialTransform);
				}
				else
				{
					Hier->AddBone(BoneInfos[BoneIndex].Name, ParentName, InitialTransform);
				}
			}

			ControlRigEditor.Pin()->OnHierarchyChanged();
			RefreshTreeView();
			FSlateApplication::Get().DismissAllMenus();
		}
	}
}

bool SRigHierarchy::IsMultiSelected() const
{
	return TreeView->GetNumItemsSelected() > 0;
}

bool SRigHierarchy::IsSingleSelected() const
{
	return TreeView->GetNumItemsSelected() == 1;
}

void SRigHierarchy::HandleDeleteItem()
{
 	FRigHierarchy* Hierarchy = GetHierarchy();
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
			// when you select whole Bones, you might not have them anymore
			if (Hierarchy->GetIndex(SelectedItems[ItemIndex]->CachedBone) != INDEX_NONE)
			{
				Hierarchy->DeleteBone(SelectedItems[ItemIndex]->CachedBone, true);
			}
		}

		ControlRigEditor.Pin()->OnHierarchyChanged();
		RefreshTreeView();
 	}
}

bool SRigHierarchy::CanDeleteItem() const
{
	return IsMultiSelected();
}

/** Delete Item */
void SRigHierarchy::HandleNewItem()
{
	FRigHierarchy* Hierarchy = GetHierarchy();
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
		Hierarchy->AddBone(NewBoneName, ParentName, ParentTransform);

		RefreshTreeView();
		ControlRigEditor.Pin()->OnHierarchyChanged();
		// reselect current selected item
		SelectBone(NewBoneName);
	}
}

/** Check whether we can deleting the selected item(s) */
bool SRigHierarchy::CanDuplicateItem() const
{
	return IsMultiSelected();
}

/** Duplicate Item */
void SRigHierarchy::HandleDuplicateItem()
{
	FRigHierarchy* Hierarchy = GetHierarchy();
	if (Hierarchy)
	{
		ClearDetailPanel();

		FScopedTransaction Transaction(LOCTEXT("HierarchyTreeDuplicateSelected", "Duplicate selected items from hierarchy"));
		ControlRigBlueprint->Modify();

		TArray<TSharedPtr<FRigTreeBone>> SelectedItems = TreeView->GetSelectedItems();
		TArray<FName> NewNames;
		for (int32 Index = 0; Index < SelectedItems.Num(); ++Index)
		{
			FName Name = SelectedItems[Index]->CachedBone;
			FTransform Transform = Hierarchy->GetGlobalTransform(Name);

			FName ParentName = Hierarchy->GetParentName(Name);

			const FName NewName = CreateUniqueName(Name);
			Hierarchy->AddBone(NewName, ParentName, Transform);
			NewNames.Add(NewName);
		}

		RefreshTreeView();
		ControlRigEditor.Pin()->OnHierarchyChanged();
		
		for (int32 Index = 0; Index < NewNames.Num(); ++Index)
		{
			SelectBone(NewNames[Index]);
		}
	}
}

/** Check whether we can deleting the selected item(s) */
bool SRigHierarchy::CanRenameItem() const
{
	return IsSingleSelected();
}

/** Delete Item */
void SRigHierarchy::HandleRenameItem()
{
	FRigHierarchy* Hierarchy = GetHierarchy();
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

FRigHierarchy* SRigHierarchy::GetHierarchy() const
{
	if (ControlRigBlueprint.IsValid())
	{
		return &ControlRigBlueprint->Hierarchy;
	}

	return nullptr;
}

FRigHierarchy* SRigHierarchy::GetInstanceHierarchy() const
{
	if (ControlRigEditor.IsValid())
	{
		UControlRig* ControlRig = ControlRigEditor.Pin()->GetInstanceRig();
		if (ControlRig)
		{
			return &ControlRig->Hierarchy.BaseHierarchy;
		}
	}

	return nullptr;
}
FName SRigHierarchy::CreateUniqueName(const FName& InBaseName) const
{
	return UtilityHelpers::CreateUniqueName(InBaseName, [this](const FName& CurName) { return GetHierarchy()->GetIndex(CurName) == INDEX_NONE; });
}

void SRigHierarchy::PostRedo(bool bSuccess) 
{
	if (bSuccess)
	{
		RefreshTreeView();
	}
}

void SRigHierarchy::PostUndo(bool bSuccess) 
{
	if (bSuccess)
	{
		RefreshTreeView();
	}
}

FReply SRigHierarchy::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	TArray<FName> DraggedBoneNames;
	TArray<TSharedPtr<FRigTreeBone>> SelectedItems =TreeView->GetSelectedItems();
	for (const TSharedPtr<FRigTreeBone>& SelectedItem : SelectedItems)
	{
		DraggedBoneNames.Add(SelectedItem->CachedBone);
	}

	if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
	{
		if (ControlRigEditor.IsValid())
		{
			TSharedRef<FRigHierarchyDragDropOp> DragDropOp = FRigHierarchyDragDropOp::New(MoveTemp(DraggedBoneNames));
			DragDropOp->OnPerformDropToGraph.BindSP(ControlRigEditor.Pin().Get(), &FControlRigEditor::OnGraphNodeDropToPerform);
			return FReply::Handled().BeginDragDrop(DragDropOp);
		}
	}

	return FReply::Unhandled();
}


bool SRigHierarchy::RenameBone(const FName& OldName, const FName& NewName)
{
	ClearDetailPanel();

	if (OldName == NewName)
	{
		return true;
	}

	// make sure there is no duplicate
	FRigHierarchy* Hierarchy = GetHierarchy();
	if (Hierarchy)
	{
		Hierarchy->Rename(OldName, NewName);
		SelectBone(NewName);

		ControlRigEditor.Pin()->OnHierarchyChanged();
		ControlRigEditor.Pin()->OnBoneRenamed(OldName, NewName);
		return true;
	}

	return false;
}

bool SRigHierarchy::OnVerifyNameChanged(const FName& OldName, const FName& NewName, FText& OutErrorMessage)
{
	if (OldName == NewName)
	{
		return true;
	}

	// make sure there is no duplicate
	FRigHierarchy* Hierarchy = GetHierarchy();
	if (Hierarchy)
	{
		const int32 Found = Hierarchy->GetIndex(OldName);
		if (Found != INDEX_NONE)
		{
			const int32 Duplicate = Hierarchy->GetIndex(NewName);
			if (Duplicate != INDEX_NONE)
			{
				OutErrorMessage = FText::FromString(TEXT("Duplicate name exists"));

				return false;
			}
		}
	}

	return true;
}
#undef LOCTEXT_NAMESPACE