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
#include "ControlRigEditorStyle.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Dialogs/Dialogs.h"

#define LOCTEXT_NAMESPACE "SRigHierarchy"

//////////////////////////////////////////////////////////////
/// FRigTreeElement
///////////////////////////////////////////////////////////
FRigTreeElement::FRigTreeElement(const FRigElementKey& InKey, TWeakPtr<SRigHierarchy> InHierarchyHandler)
{
	Key = InKey;
}

TSharedRef<ITableRow> FRigTreeElement::MakeTreeRowWidget(TSharedPtr<FControlRigEditor> InControlRigEditor, const TSharedRef<STableViewBase>& InOwnerTable, TSharedRef<FRigTreeElement> InRigTreeElement, TSharedRef<FUICommandList> InCommandList, TSharedPtr<SRigHierarchy> InHierarchy)
{
	return SNew(SRigHierarchyItem, InControlRigEditor, InOwnerTable, InRigTreeElement, InCommandList, InHierarchy)
		.OnRenameElement(InHierarchy.Get(), &SRigHierarchy::RenameElement)
		.OnVerifyElementNameChanged(InHierarchy.Get(), &SRigHierarchy::OnVerifyNameChanged);
}

void FRigTreeElement::RequestRename()
{
	OnRenameRequested.ExecuteIfBound();
}

//////////////////////////////////////////////////////////////
/// FRigElementHierarchyDragDropOp
///////////////////////////////////////////////////////////
TSharedRef<FRigElementHierarchyDragDropOp> FRigElementHierarchyDragDropOp::New(const TArray<FRigElementKey>& InElements)
{
	TSharedRef<FRigElementHierarchyDragDropOp> Operation = MakeShared<FRigElementHierarchyDragDropOp>();
	Operation->Elements = InElements;
	Operation->Construct();
	return Operation;
}

TSharedPtr<SWidget> FRigElementHierarchyDragDropOp::GetDefaultDecorator() const
{
	return SNew(SBorder)
		.Visibility(EVisibility::Visible)
		.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
		[
			SNew(STextBlock)
			.Text(FText::FromString(GetJoinedElementNames()))
			//.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
		];
}

FString FRigElementHierarchyDragDropOp::GetJoinedElementNames() const
{
	TArray<FString> ElementNameStrings;
	for (const FRigElementKey& Element: Elements)
	{
		ElementNameStrings.Add(Element.Name.ToString());
	}
	return FString::Join(ElementNameStrings, TEXT(","));
}

//////////////////////////////////////////////////////////////
/// SRigHierarchyItem
///////////////////////////////////////////////////////////
void SRigHierarchyItem::Construct(const FArguments& InArgs, TSharedPtr<FControlRigEditor> InControlRigEditor, const TSharedRef<STableViewBase>& OwnerTable, TSharedRef<FRigTreeElement> InRigTreeElement, TSharedRef<FUICommandList> InCommandList, TSharedPtr<SRigHierarchy> InHierarchy)
{
	WeakRigTreeElement = InRigTreeElement;
	WeakCommandList = InCommandList;
	ControlRigEditor = InControlRigEditor;

	OnVerifyElementNameChanged = InArgs._OnVerifyElementNameChanged;
	OnRenameElement = InArgs._OnRenameElement;

	TSharedPtr< SInlineEditableTextBlock > InlineWidget;

	const FSlateBrush* Brush = nullptr;
	switch (InRigTreeElement->Key.Type)
	{
		case ERigElementType::Control:
		{
			Brush = FControlRigEditorStyle::Get().GetBrush("ControlRig.Tree.Control");
			break;
		}
		case ERigElementType::Space:
		{
			Brush = FControlRigEditorStyle::Get().GetBrush("ControlRig.Tree.Space");
			break;
		}
		case ERigElementType::Bone:
		{
			ERigBoneType BoneType = ERigBoneType::User;

			int32 BoneIndex = InHierarchy->ControlRigBlueprint->HierarchyContainer.GetIndex(InRigTreeElement->Key);
			if (BoneIndex != INDEX_NONE)
			{
				BoneType = InHierarchy->ControlRigBlueprint->HierarchyContainer.BoneHierarchy[BoneIndex].Type;
			}

			switch (BoneType)
			{
				case ERigBoneType::Imported:
				{
					Brush = FControlRigEditorStyle::Get().GetBrush("ControlRig.Tree.BoneImported");
					break;
				}
				case ERigBoneType::User:
				default:
				{
					Brush = FControlRigEditorStyle::Get().GetBrush("ControlRig.Tree.BoneUser");
					break;
				}
			}

			break;
		}
		default:
		{
			Brush = FControlRigEditorStyle::Get().GetBrush("ControlRig.Tree.BoneUser");
			break;
		}
	}

	STableRow<TSharedPtr<FRigTreeElement>>::Construct(
		STableRow<TSharedPtr<FRigTreeElement>>::FArguments()
		.OnDragDetected(InHierarchy.Get(), &SRigHierarchy::OnDragDetected)
		.OnCanAcceptDrop(InHierarchy.Get(), &SRigHierarchy::OnCanAcceptDrop)
		.OnAcceptDrop(InHierarchy.Get(), &SRigHierarchy::OnAcceptDrop)
		.Content()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.MaxWidth(18)
			.FillWidth(1.0)
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.Image(Brush)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SAssignNew(InlineWidget, SInlineEditableTextBlock)
				.Text(this, &SRigHierarchyItem::GetName)
				.OnVerifyTextChanged(this, &SRigHierarchyItem::OnVerifyNameChanged)
				.OnTextCommitted(this, &SRigHierarchyItem::OnNameCommitted)
				.MultiLine(false)
			]
		], OwnerTable);

	InRigTreeElement->OnRenameRequested.BindSP(InlineWidget.Get(), &SInlineEditableTextBlock::EnterEditingMode);
}

FText SRigHierarchyItem::GetName() const
{
	return (FText::FromName(WeakRigTreeElement.Pin()->Key.Name));
}

bool SRigHierarchyItem::OnVerifyNameChanged(const FText& InText, FText& OutErrorMessage)
{
	const FName NewName = FName(*InText.ToString());
	if (OnVerifyElementNameChanged.IsBound())
	{
		return OnVerifyElementNameChanged.Execute(WeakRigTreeElement.Pin()->Key, NewName, OutErrorMessage);
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
		FRigElementKey OldKey = WeakRigTreeElement.Pin()->Key;

		if (!OnRenameElement.IsBound() || OnRenameElement.Execute(OldKey, NewName))
		{
			if (WeakRigTreeElement.IsValid())
			{
				WeakRigTreeElement.Pin()->Key.Name = NewName;
			}
		}
	}
}

///////////////////////////////////////////////////////////

SRigHierarchy::~SRigHierarchy()
{
	if (ControlRigEditor.IsValid())
	{
		ControlRigEditor.Pin()->GetKeyDownDelegate().Unbind();
		ControlRigEditor.Pin()->OnViewportContextMenu().Unbind();
		ControlRigEditor.Pin()->OnViewportContextMenuCommands().Unbind();

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

void SRigHierarchy::Construct(const FArguments& InArgs, TSharedRef<FControlRigEditor> InControlRigEditor)
{
	ControlRigEditor = InControlRigEditor;

	ControlRigBlueprint = ControlRigEditor.Pin()->GetControlRigBlueprint();
	ControlRigBlueprint->HierarchyContainer.Initialize();

	ControlRigBlueprint->HierarchyContainer.OnElementAdded.AddRaw(this, &SRigHierarchy::OnRigElementAdded);
	ControlRigBlueprint->HierarchyContainer.OnElementRemoved.AddRaw(this, &SRigHierarchy::OnRigElementRemoved);
	ControlRigBlueprint->HierarchyContainer.OnElementRenamed.AddRaw(this, &SRigHierarchy::OnRigElementRenamed);
	ControlRigBlueprint->HierarchyContainer.OnElementReparented.AddRaw(this, &SRigHierarchy::OnRigElementReparented);
	ControlRigBlueprint->HierarchyContainer.OnElementSelected.AddRaw(this, &SRigHierarchy::OnRigElementSelected);

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
				SAssignNew(TreeView, STreeView<TSharedPtr<FRigTreeElement>>)
				.TreeItemsSource(&RootElements)
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

	bIsChangingRigHierarchy = false;
	RefreshTreeView();

	if (ControlRigEditor.IsValid())
	{
		ControlRigEditor.Pin()->GetKeyDownDelegate().BindLambda([&](const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)->FReply {
			return OnKeyDown(MyGeometry, InKeyEvent);
		});
		ControlRigEditor.Pin()->OnViewportContextMenu().BindSP(this, &SRigHierarchy::FillContextMenu);
		ControlRigEditor.Pin()->OnViewportContextMenuCommands().BindSP(this, &SRigHierarchy::GetContextMenuCommands);
	}
}

void SRigHierarchy::BindCommands()
{
	// create new command
	const FControlRigHierarchyCommands& Commands = FControlRigHierarchyCommands::Get();

	CommandList->MapAction(Commands.AddBoneItem,
		FExecuteAction::CreateSP(this, &SRigHierarchy::HandleNewItem, ERigElementType::Bone));

	CommandList->MapAction(Commands.AddControlItem,
		FExecuteAction::CreateSP(this, &SRigHierarchy::HandleNewItem, ERigElementType::Control));

	CommandList->MapAction(Commands.AddSpaceItem,
		FExecuteAction::CreateSP(this, &SRigHierarchy::HandleNewItem, ERigElementType::Space));

	CommandList->MapAction(Commands.DuplicateItem,
		FExecuteAction::CreateSP(this, &SRigHierarchy::HandleDuplicateItem),
		FCanExecuteAction::CreateSP(this, &SRigHierarchy::CanDuplicateItem));

	CommandList->MapAction(Commands.DeleteItem,
		FExecuteAction::CreateSP(this, &SRigHierarchy::HandleDeleteItem),
		FCanExecuteAction::CreateSP(this, &SRigHierarchy::CanDeleteItem));

	CommandList->MapAction(Commands.RenameItem,
		FExecuteAction::CreateSP(this, &SRigHierarchy::HandleRenameItem),
		FCanExecuteAction::CreateSP(this, &SRigHierarchy::CanRenameItem));

	CommandList->MapAction(Commands.CopyItems,
		FExecuteAction::CreateSP(this, &SRigHierarchy::HandleCopyItems),
		FCanExecuteAction::CreateSP(this, &SRigHierarchy::CanCopyOrPasteItems));

	CommandList->MapAction(Commands.PasteItems,
		FExecuteAction::CreateSP(this, &SRigHierarchy::HandlePasteItems),
		FCanExecuteAction::CreateSP(this, &SRigHierarchy::CanPasteItems));

	CommandList->MapAction(Commands.PasteLocalTransforms,
		FExecuteAction::CreateSP(this, &SRigHierarchy::HandlePasteLocalTransforms),
		FCanExecuteAction::CreateSP(this, &SRigHierarchy::CanCopyOrPasteItems));

	CommandList->MapAction(Commands.PasteGlobalTransforms,
		FExecuteAction::CreateSP(this, &SRigHierarchy::HandlePasteGlobalTransforms),
		FCanExecuteAction::CreateSP(this, &SRigHierarchy::CanCopyOrPasteItems));

	CommandList->MapAction(
		Commands.ResetTransform,
		FExecuteAction::CreateSP(this, &SRigHierarchy::HandleResetTransform),
		FCanExecuteAction::CreateSP(this, &SRigHierarchy::IsMultiSelected));

	CommandList->MapAction(
		Commands.ResetInitialTransform,
		FExecuteAction::CreateSP(this, &SRigHierarchy::HandleResetInitialTransform),
		FCanExecuteAction::CreateSP(this, &SRigHierarchy::IsMultiSelected));

	CommandList->MapAction(
		Commands.ResetSpace,
		FExecuteAction::CreateSP(this, &SRigHierarchy::HandleResetSpace),
		FCanExecuteAction::CreateSP(this, &SRigHierarchy::IsControlSelected));

	CommandList->MapAction(
		Commands.SetInitialTransformFromClosestBone,
		FExecuteAction::CreateSP(this, &SRigHierarchy::HandleSetInitialTransformFromClosestBone),
		FCanExecuteAction::CreateSP(this, &SRigHierarchy::IsControlOrSpaceSelected));

	CommandList->MapAction(
		Commands.SetInitialTransformFromCurrentTransform,
		FExecuteAction::CreateSP(this, &SRigHierarchy::HandleSetInitialTransformFromCurrentTransform),
		FCanExecuteAction::CreateSP(this, &SRigHierarchy::IsControlOrSpaceSelected));

	CommandList->MapAction(
		Commands.FrameSelection,
		FExecuteAction::CreateSP(this, &SRigHierarchy::HandleFrameSelection),
		FCanExecuteAction::CreateSP(this, &SRigHierarchy::IsMultiSelected));
}

FReply SRigHierarchy::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (CommandList.IsValid() && CommandList->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SRigHierarchy::OnFilterTextChanged(const FText& SearchText)
{
	FilterText = SearchText;

	RefreshTreeView();
}

void SRigHierarchy::RefreshTreeView()
{
	TMap<FRigElementKey, bool> ExpansionState;
	for (TPair<FRigElementKey, TSharedPtr<FRigTreeElement>> Pair : ElementMap)
	{
		ExpansionState.FindOrAdd(Pair.Key) = TreeView->IsItemExpanded(Pair.Value);
	}

	RootElements.Reset();
	ElementMap.Reset();
	ParentMap.Reset();

	if (ControlRigBlueprint.IsValid())
	{
		FRigHierarchyContainer* Container = GetHierarchyContainer();
		FRigBoneHierarchy& BoneHierarchy = Container->BoneHierarchy;
		FRigControlHierarchy& ControlHierarchy = Container->ControlHierarchy;
		FRigSpaceHierarchy& SpaceHierarchy = Container->SpaceHierarchy;

		for (const FRigBone& Element : BoneHierarchy)
		{
			AddBoneElement(Element);
		}
		for (const FRigControl& Element : ControlHierarchy)
		{
			AddControlElement(Element);
		}
		for (const FRigSpace& Element : SpaceHierarchy)
		{
			AddSpaceElement(Element);
		}

		if (ExpansionState.Num() == 0)
		{
			for (TSharedPtr<FRigTreeElement> RootElement : RootElements)
			{
				SetExpansionRecursive(RootElement, false);
			}
		}
		else
		{
			for (TPair<FRigElementKey, bool> Pair : ExpansionState)
			{
				if (!Pair.Value)
				{
					continue;
				}

				if (TSharedPtr<FRigTreeElement>* ItemPtr = ElementMap.Find(Pair.Key))
				{
					TreeView->SetItemExpansion(*ItemPtr, true);
				}
			}
		}

		TreeView->RequestTreeRefresh();

		TArray<FRigElementKey> Selection = Container->CurrentSelection();
		for (const FRigElementKey& Key : Selection)
		{
			OnRigElementSelected(Container, Key, true);
		}
	}
}

void SRigHierarchy::SetExpansionRecursive(TSharedPtr<FRigTreeElement> InElement, bool bTowardsParent)
{
	TreeView->SetItemExpansion(InElement, true);

	if (bTowardsParent)
	{
		if (const FRigElementKey* ParentKey = ParentMap.Find(InElement->Key))
		{
			if (TSharedPtr<FRigTreeElement>* ParentItem = ElementMap.Find(*ParentKey))
			{
				SetExpansionRecursive(*ParentItem, bTowardsParent);
			}
		}
	}
	else
	{
		for (int32 ChildIndex = 0; ChildIndex < InElement->Children.Num(); ++ChildIndex)
		{
			SetExpansionRecursive(InElement->Children[ChildIndex], bTowardsParent);
		}
	}
}
TSharedRef<ITableRow> SRigHierarchy::MakeTableRowWidget(TSharedPtr<FRigTreeElement> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return InItem->MakeTreeRowWidget(ControlRigEditor.Pin(), OwnerTable, InItem.ToSharedRef(), CommandList.ToSharedRef(), SharedThis(this));
}

void SRigHierarchy::HandleGetChildrenForTree(TSharedPtr<FRigTreeElement> InItem, TArray<TSharedPtr<FRigTreeElement>>& OutChildren)
{
	OutChildren = InItem.Get()->Children;
}

void SRigHierarchy::OnSelectionChanged(TSharedPtr<FRigTreeElement> Selection, ESelectInfo::Type SelectInfo)
{
	if (bIsChangingRigHierarchy)
	{
		return;
	}

	FRigHierarchyContainer* Hierarchy = GetHierarchyContainer();

	if (Hierarchy)
	{
		TGuardValue<bool> GuardRigHierarchyChanges(bIsChangingRigHierarchy, true);

		TArray<FRigElementKey> OldSelection = Hierarchy->CurrentSelection();
		TArray<FRigElementKey> NewSelection;

		TArray<TSharedPtr<FRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
		for (const TSharedPtr<FRigTreeElement>& SelectedItem : SelectedItems)
		{
			NewSelection.Add(SelectedItem->Key);
		}

		for (const FRigElementKey& PreviouslySelected : OldSelection)
		{
			if (NewSelection.Contains(PreviouslySelected))
			{
				continue;
			}
			Hierarchy->Select(PreviouslySelected, false);
		}

		for (const FRigElementKey& NewlySelected : NewSelection)
		{
			Hierarchy->Select(NewlySelected, true);
		}
	}
}

TSharedPtr<FRigTreeElement> SRigHierarchy::FindElement(const FRigElementKey& InElementKey, TSharedPtr<FRigTreeElement> CurrentItem)
{
	if (CurrentItem->Key == InElementKey)
	{
		return CurrentItem;
	}

	for (int32 ChildIndex = 0; ChildIndex < CurrentItem->Children.Num(); ++ChildIndex)
	{
		TSharedPtr<FRigTreeElement> Found = FindElement(InElementKey, CurrentItem->Children[ChildIndex]);
		if (Found.IsValid())
		{
			return Found;
		}
	}

	return TSharedPtr<FRigTreeElement>();
}

void SRigHierarchy::AddElement(FRigElementKey InKey, FRigElementKey InParentKey)
{
	if(ElementMap.Contains(InKey))
	{
		return;
	}

	FString FilteredString = FilterText.ToString();
	if (FilteredString.IsEmpty())
	{
		TSharedPtr<FRigTreeElement> NewItem = MakeShared<FRigTreeElement>(InKey, SharedThis(this));
		ElementMap.Add(InKey, NewItem);
		if (InParentKey)
		{
			ParentMap.Add(InKey, InParentKey);
		}

		if (InParentKey)
		{
			TSharedPtr<FRigTreeElement>* FoundItem = ElementMap.Find(InParentKey);
			check(FoundItem);
			FoundItem->Get()->Children.Add(NewItem);
		}
		else
		{
			RootElements.Add(NewItem);
		}
	}
	else
	{
		FString FilteredStringUnderScores = FilteredString.Replace(TEXT(" "), TEXT("_"));
		if (InKey.Name.ToString().Contains(FilteredString) || InKey.Name.ToString().Contains(FilteredStringUnderScores))	
		{
			TSharedPtr<FRigTreeElement> NewItem = MakeShared<FRigTreeElement>(InKey, SharedThis(this));
			RootElements.Add(NewItem);
		}
	}
}

void SRigHierarchy::AddBoneElement(FRigBone InBone)
{
	const FRigHierarchyContainer* Container = GetHierarchyContainer();
	const FRigBoneHierarchy& BoneHierarchy = Container->BoneHierarchy;
	const FRigControlHierarchy& ControlHierarchy = Container->ControlHierarchy;
	const FRigSpaceHierarchy& SpaceHierarchy = Container->SpaceHierarchy;

	FRigElementKey ParentKey;
	if(InBone.ParentIndex != INDEX_NONE)
	{
		AddBoneElement(BoneHierarchy[InBone.ParentIndex]);
		ParentKey = BoneHierarchy[InBone.ParentIndex].GetElementKey();
	}
	AddElement(InBone.GetElementKey(), ParentKey);
}

void SRigHierarchy::AddControlElement(FRigControl InControl)
{
	const FRigHierarchyContainer* Container = GetHierarchyContainer();
	const FRigControlHierarchy& ControlHierarchy = Container->ControlHierarchy;
	const FRigSpaceHierarchy& SpaceHierarchy = Container->SpaceHierarchy;

	FRigElementKey ParentKey;
	if(InControl.SpaceIndex != INDEX_NONE)
	{
		AddSpaceElement(SpaceHierarchy[InControl.SpaceIndex]);
		ParentKey = SpaceHierarchy[InControl.SpaceIndex].GetElementKey();
	}
	else if(InControl.ParentIndex != INDEX_NONE)
	{
		AddControlElement(ControlHierarchy[InControl.ParentIndex]);
		ParentKey = ControlHierarchy[InControl.ParentIndex].GetElementKey();
	}
	AddElement(InControl.GetElementKey(), ParentKey);
}

void SRigHierarchy::AddSpaceElement(FRigSpace InSpace)
{
	const FRigHierarchyContainer* Container = GetHierarchyContainer();
	const FRigBoneHierarchy& BoneHierarchy = Container->BoneHierarchy;
	const FRigControlHierarchy& ControlHierarchy = Container->ControlHierarchy;
	const FRigSpaceHierarchy& SpaceHierarchy = Container->SpaceHierarchy;

	FRigElementKey ParentKey;
	if(InSpace.ParentIndex != INDEX_NONE)
	{
		switch(InSpace.SpaceType)
		{
			case ERigSpaceType::Bone:
			{
				AddBoneElement(BoneHierarchy[InSpace.ParentIndex]);
				ParentKey = BoneHierarchy[InSpace.ParentIndex].GetElementKey();
				break;
			}
			case ERigSpaceType::Control:
			{
				AddControlElement(ControlHierarchy[InSpace.ParentIndex]);
				ParentKey = ControlHierarchy[InSpace.ParentIndex].GetElementKey();
				break;
			}
			case ERigSpaceType::Space:
			{
				AddSpaceElement(SpaceHierarchy[InSpace.ParentIndex]);
				ParentKey = SpaceHierarchy[InSpace.ParentIndex].GetElementKey();
				break;
			}
			default:
			{
				break;
			}
		}
	}
	AddElement(InSpace.GetElementKey(), ParentKey);
}

void SRigHierarchy::OnRigElementAdded(FRigHierarchyContainer* Container, const FRigElementKey& InKey)
{
	if (bIsChangingRigHierarchy || InKey.Type == ERigElementType::Curve)
	{
		return;
	}
	RefreshTreeView();
}

void SRigHierarchy::OnRigElementRemoved(FRigHierarchyContainer* Container, const FRigElementKey& InKey)
{
	if (bIsChangingRigHierarchy || InKey.Type == ERigElementType::Curve)
	{
		return;
	}
	RefreshTreeView();
}

void SRigHierarchy::OnRigElementRenamed(FRigHierarchyContainer* Container, ERigElementType ElementType, const FName& InOldName, const FName& InNewName)
{
	if (bIsChangingRigHierarchy || ElementType == ERigElementType::Curve)
	{
		return;
	}
	RefreshTreeView();
}

void SRigHierarchy::OnRigElementReparented(FRigHierarchyContainer* Container, const FRigElementKey& InKey, const FName& InOldParentName, const FName& InNewParentName)
{
	if (bIsChangingRigHierarchy || InKey.Type == ERigElementType::Curve)
	{
		return;
	}
	RefreshTreeView();
}

void SRigHierarchy::OnRigElementSelected(FRigHierarchyContainer* Container, const FRigElementKey& InKey, bool bSelected)
{
	if (bIsChangingRigHierarchy || InKey.Type == ERigElementType::Curve)
	{
		return;
	}

	for (int32 RootIndex = 0; RootIndex < RootElements.Num(); ++RootIndex)
	{
		TSharedPtr<FRigTreeElement> Found = FindElement(InKey, RootElements[RootIndex]);
		if (Found.IsValid())
		{
			TreeView->SetItemSelection(Found, bSelected, ESelectInfo::OnNavigation);
			HandleFrameSelection();
		}
	}
}

void SRigHierarchy::ClearDetailPanel() const
{
	ControlRigEditor.Pin()->ClearDetailObject();
}

TSharedPtr< SWidget > SRigHierarchy::CreateContextMenu()
{
	const bool CloseAfterSelection = true;
	FMenuBuilder MenuBuilder(CloseAfterSelection, CommandList);
	FillContextMenu(MenuBuilder);
	return MenuBuilder.MakeWidget();
}

void SRigHierarchy::FillContextMenu(class FMenuBuilder& MenuBuilder)
{
	const FControlRigHierarchyCommands& Actions = FControlRigHierarchyCommands::Get();
	{
		struct FLocalMenuBuilder
		{
			static void FillNewMenu(FMenuBuilder& InSubMenuBuilder, TSharedPtr<STreeView<TSharedPtr<FRigTreeElement>>> InTreeView)
			{
				const FControlRigHierarchyCommands& Actions = FControlRigHierarchyCommands::Get();

				FRigElementKey SelectedKey;
				TArray<TSharedPtr<FRigTreeElement>> SelectedItems = InTreeView->GetSelectedItems();
				if (SelectedItems.Num() > 0)
				{
					SelectedKey = SelectedItems[0]->Key;
				}

				if (!SelectedKey || SelectedKey.Type == ERigElementType::Bone)
				{
					InSubMenuBuilder.AddMenuEntry(Actions.AddBoneItem);
				}

				InSubMenuBuilder.AddMenuEntry(Actions.AddControlItem);
				InSubMenuBuilder.AddMenuEntry(Actions.AddSpaceItem);
			}
		};

		MenuBuilder.BeginSection("Elements", LOCTEXT("ElementsHeader", "Elements"));
		MenuBuilder.AddSubMenu(LOCTEXT("New", "New"), LOCTEXT("New_ToolTip", "Create New Elements"),
			FNewMenuDelegate::CreateStatic(&FLocalMenuBuilder::FillNewMenu, TreeView));

		MenuBuilder.AddMenuEntry(Actions.DeleteItem);
		MenuBuilder.AddMenuEntry(Actions.DuplicateItem);
		MenuBuilder.AddMenuEntry(Actions.RenameItem);
		MenuBuilder.EndSection();

		MenuBuilder.BeginSection("Copy&Paste", LOCTEXT("Copy&PasteHeader", "Copy & Paste"));
		MenuBuilder.AddMenuEntry(Actions.CopyItems);
		MenuBuilder.AddMenuEntry(Actions.PasteItems);
		MenuBuilder.AddMenuEntry(Actions.PasteLocalTransforms);
		MenuBuilder.AddMenuEntry(Actions.PasteGlobalTransforms);
		MenuBuilder.EndSection();

		MenuBuilder.BeginSection("Transforms", LOCTEXT("TransformsHeader", "Transforms"));
		MenuBuilder.AddMenuEntry(Actions.ResetTransform);
		MenuBuilder.AddMenuEntry(Actions.ResetInitialTransform);
		MenuBuilder.AddMenuEntry(Actions.SetInitialTransformFromCurrentTransform);
		MenuBuilder.AddMenuEntry(Actions.SetInitialTransformFromClosestBone);
		MenuBuilder.AddMenuEntry(Actions.ResetSpace);
		MenuBuilder.EndSection();

		MenuBuilder.BeginSection("Assets", LOCTEXT("AssetsHeader", "Assets"));
		MenuBuilder.AddSubMenu(
			LOCTEXT("ImportSubMenu", "Import"),
			LOCTEXT("ImportSubMenu_ToolTip", "Import hierarchy to the current rig. This only imports non-existing node. For example, if there is hand_r, it won't import hand_r. \
				If you want to reimport whole new hiearchy, delete all nodes, and use import hierarchy."),
			FNewMenuDelegate::CreateSP(this, &SRigHierarchy::CreateImportMenu)
		);
		MenuBuilder.AddSubMenu(
			LOCTEXT("RefreshSubMenu", "Refresh"),
			LOCTEXT("RefreshSubMenu_ToolTip", "Refresh the existing initial transform from the selected mesh. This only updates if the node is found."),
			FNewMenuDelegate::CreateSP(this, &SRigHierarchy::CreateRefreshMenu)
		);
		MenuBuilder.EndSection();
	}
}

TSharedPtr<FUICommandList> SRigHierarchy::GetContextMenuCommands()
{
	return CommandList;
}

void SRigHierarchy::CreateRefreshMenu(FMenuBuilder& MenuBuilder)
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
			.OnObjectChanged(this, &SRigHierarchy::RefreshHierarchy)
		]
		,
		FText()
	);
}

void SRigHierarchy::RefreshHierarchy(const FAssetData& InAssetData)
{
	FRigHierarchyContainer* Hierarchy = GetHierarchyContainer();
	USkeletalMesh* Mesh = Cast<USkeletalMesh>(InAssetData.GetAsset());
	if (Mesh && Hierarchy)
	{
		FScopedTransaction Transaction(LOCTEXT("HierarchyRefresh", "Refresh Transform"));
		ControlRigBlueprint->Modify();

		const FReferenceSkeleton& RefSkeleton = Mesh->RefSkeleton;
		Hierarchy->BoneHierarchy.ImportSkeleton(RefSkeleton, NAME_None, true, true, true);
	}
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
			.AllowedClass(USkeletalMesh::StaticClass())
			.OnObjectChanged(this, &SRigHierarchy::ImportHierarchy)
		]
		,
		FText()
	);
}

void SRigHierarchy::ImportHierarchy(const FAssetData& InAssetData)
{
	FRigHierarchyContainer* Hierarchy = GetHierarchyContainer();
	USkeletalMesh* Mesh = Cast<USkeletalMesh> (InAssetData.GetAsset());
	if (Mesh && Hierarchy)
	{
		FScopedTransaction Transaction(LOCTEXT("HierarchyImport", "Import Hierarchy"));
		ControlRigBlueprint->Modify();

		const FReferenceSkeleton& RefSkeleton = Mesh->RefSkeleton;
		Hierarchy->BoneHierarchy.ImportSkeleton(RefSkeleton, NAME_None, false, false, true);

		FSlateApplication::Get().DismissAllMenus();
		RefreshTreeView();
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

bool SRigHierarchy::IsControlSelected() const
{
	TArray<TSharedPtr<FRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
	for (TSharedPtr<FRigTreeElement> SelectedItem : SelectedItems)
	{
		if (SelectedItem->Key.Type == ERigElementType::Control)
		{
			return true;
		}
	}
	return false;
}

bool SRigHierarchy::IsControlOrSpaceSelected() const
{
	TArray<TSharedPtr<FRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
	for (TSharedPtr<FRigTreeElement> SelectedItem : SelectedItems)
	{
		if (SelectedItem->Key.Type == ERigElementType::Control)
		{
			return true;
		}
		if (SelectedItem->Key.Type == ERigElementType::Space)
		{
			return true;
		}
	}
	return false;
}

void SRigHierarchy::HandleDeleteItem()
{
 	FRigHierarchyContainer* Hierarchy = GetHierarchyContainer();
 	if (Hierarchy)
 	{
		ClearDetailPanel();
		FScopedTransaction Transaction(LOCTEXT("HierarchyTreeDeleteSelected", "Delete selected items from hierarchy"));
		ControlRigBlueprint->Modify();

		// clear detail view display
		ControlRigEditor.Pin()->ClearDetailObject();

		bool bConfirmedByUser = false;
		bool bDeleteImportedBones = false;

		TArray<TSharedPtr<FRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
		for (int32 ItemIndex = 0; ItemIndex < SelectedItems.Num(); ++ItemIndex)
		{
			TGuardValue<bool> GuardRigHierarchyChanges(bIsChangingRigHierarchy, true);

			FRigElementKey SelectedKey = SelectedItems[ItemIndex]->Key;
			switch (SelectedKey.Type)
			{
				case ERigElementType::Bone:
				{
					int32 BoneIndex = Hierarchy->BoneHierarchy.GetIndex(SelectedKey.Name);
					if (BoneIndex != INDEX_NONE)
					{
						const FRigBone& Bone = Hierarchy->BoneHierarchy[BoneIndex];
						if (Bone.Type == ERigBoneType::Imported && Bone.ParentIndex != INDEX_NONE)
						{
							if (!bConfirmedByUser)
							{
								FText ConfirmDelete = LOCTEXT("ConfirmDeleteBoneHierarchy",
									"Deleting imported(white) bones can cause issues with animation - are you sure ?");

								FSuppressableWarningDialog::FSetupInfo Info(ConfirmDelete, LOCTEXT("DeleteImportedBone", "Delete Imported Bone"), "DeleteImportedBoneHierarchy_Warning");
								Info.ConfirmText = LOCTEXT("DeleteImportedBoneHierarchy_Yes", "Yes");
								Info.CancelText = LOCTEXT("DeleteImportedBoneHierarchy_No", "No");

								FSuppressableWarningDialog DeleteImportedBonesInHierarchy(Info);
								bDeleteImportedBones = DeleteImportedBonesInHierarchy.ShowModal() != FSuppressableWarningDialog::Cancel;
								bConfirmedByUser = true;
							}

							if (!bDeleteImportedBones)
							{
								break;
							}
						}
						Hierarchy->BoneHierarchy.Remove(SelectedKey.Name);
					}
					break;
				}
				case ERigElementType::Control:
				{
					if (Hierarchy->ControlHierarchy.GetIndex(SelectedKey.Name) != INDEX_NONE)
					{
						Hierarchy->ControlHierarchy.Remove(SelectedKey.Name);
					}
					break;
				}
				case ERigElementType::Space:
				{
					if (Hierarchy->SpaceHierarchy.GetIndex(SelectedKey.Name) != INDEX_NONE)
					{
						Hierarchy->SpaceHierarchy.Remove(SelectedKey.Name);
					}
					break;
				}
				default:
				{
					return;
				}
			}
		}

		RefreshTreeView();
		FSlateApplication::Get().DismissAllMenus();
 	}
}

bool SRigHierarchy::CanDeleteItem() const
{
	return IsMultiSelected();
}

/** Delete Item */
void SRigHierarchy::HandleNewItem(ERigElementType InElementType)
{
	FRigHierarchyContainer* Hierarchy = GetHierarchyContainer();
	if (Hierarchy)
	{
		// unselect current selected item
		ClearDetailPanel();

		FScopedTransaction Transaction(LOCTEXT("HierarchyTreeAdded", "Add new item to hierarchy"));
		ControlRigBlueprint->Modify();

		FRigElementKey ParentKey;
		FTransform ParentTransform = FTransform::Identity;

		TArray<TSharedPtr<FRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
		if (SelectedItems.Num() > 0)
		{
			ParentKey = SelectedItems[0]->Key;
			ParentTransform = Hierarchy->GetGlobalTransform(ParentKey);
		}

		FString NewNameTemplate = FString::Printf(TEXT("New%s"), *StaticEnum<ERigElementType>()->GetNameStringByValue((int64)InElementType));
		const FName NewElementName = CreateUniqueName(*NewNameTemplate, InElementType);
		{
			TGuardValue<bool> GuardRigHierarchyChanges(bIsChangingRigHierarchy, true);
			switch (InElementType)
			{
				case ERigElementType::Bone:
				{
					FName ParentName = ParentKey.Type == ERigElementType::Bone ? ParentKey.Name : NAME_None;
					ParentTransform = ParentName == NAME_None ? FTransform::Identity : ParentTransform;
					Hierarchy->BoneHierarchy.Add(NewElementName, ParentName, ERigBoneType::User, ParentTransform);
					break;
				}
				case ERigElementType::Control:
				{
					if (ParentKey.Type == ERigElementType::Bone)
					{
						for (const FRigSpace& ExistingSpace : Hierarchy->SpaceHierarchy)
						{
							if (ExistingSpace.SpaceType == ERigSpaceType::Bone && ExistingSpace.ParentName == ParentKey.Name)
							{
								ParentKey = ExistingSpace.GetElementKey();
								break;
							}
						}

						if (ParentKey.Type != ERigElementType::Space)
						{
							FString SpaceName = FString::Printf(TEXT("%sSpace"), *NewElementName.ToString());
							FRigSpace& NewSpace = Hierarchy->SpaceHierarchy.Add(*SpaceName, ERigSpaceType::Bone, ParentKey.Name);
							ParentKey = NewSpace.GetElementKey();
						}
					}

					if(!ParentKey)
					{
						Hierarchy->ControlHierarchy.Add(NewElementName, ERigControlType::Transform);
					}
					else if (ParentKey.Type == ERigElementType::Space)
					{
						Hierarchy->ControlHierarchy.Add(NewElementName, ERigControlType::Transform, NAME_None, ParentKey.Name);
					}
					else if (ParentKey.Type == ERigElementType::Control)
					{
						Hierarchy->ControlHierarchy.Add(NewElementName, ERigControlType::Transform, ParentKey.Name);
					}
					break;
				}
				case ERigElementType::Space:
				{
					if (!ParentKey)
					{
						Hierarchy->SpaceHierarchy.Add(NewElementName, ERigSpaceType::Global);
					}
					else if (ParentKey.Type == ERigElementType::Bone)
					{
						FString SpaceName = FString::Printf(TEXT("%sSpace"), *NewElementName.ToString());
						Hierarchy->SpaceHierarchy.Add(*SpaceName, ERigSpaceType::Bone, ParentKey.Name);
					}
					else if (ParentKey.Type == ERigElementType::Control)
					{
						FString SpaceName = FString::Printf(TEXT("%sSpace"), *NewElementName.ToString());
						Hierarchy->SpaceHierarchy.Add(*SpaceName, ERigSpaceType::Control, ParentKey.Name);
					}
					else if (ParentKey.Type == ERigElementType::Space)
					{
						Hierarchy->SpaceHierarchy.Add(NewElementName, ERigSpaceType::Space, ParentKey.Name);
					}
					break;
				}
				default:
				{
					return;
				}
			}
		}
		Hierarchy->ClearSelection();
		Hierarchy->Select(FRigElementKey(NewElementName, InElementType));

		FSlateApplication::Get().DismissAllMenus();
		RefreshTreeView();
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
	FRigHierarchyContainer* Hierarchy = GetHierarchyContainer();
	if (Hierarchy)
	{
		ClearDetailPanel();

		FScopedTransaction Transaction(LOCTEXT("HierarchyTreeDuplicateSelected", "Duplicate selected items from hierarchy"));
		ControlRigBlueprint->Modify();

		TArray<TSharedPtr<FRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
		TArray<FRigElementKey> NewKeys;
		for (int32 Index = 0; Index < SelectedItems.Num(); ++Index)
		{
			TGuardValue<bool> GuardRigHierarchyChanges(bIsChangingRigHierarchy, true);

			FRigElementKey Key = SelectedItems[Index]->Key;

			switch (Key.Type)
			{
				case ERigElementType::Bone:
				{
					FTransform Transform = Hierarchy->GetGlobalTransform(Key);
					FName ParentName = Hierarchy->BoneHierarchy[Key.Name].ParentName;

					const FName NewName = CreateUniqueName(Key.Name, Key.Type);
					FRigBone& NewBone = Hierarchy->BoneHierarchy.Add(NewName, ParentName, ERigBoneType::User, Transform);
					NewKeys.Add(NewBone.GetElementKey());
					break;
				}
				case ERigElementType::Control:
				{
					FRigControl Control = Hierarchy->ControlHierarchy[Key.Name];
					const FName NewName = CreateUniqueName(Key.Name, Key.Type);
					FRigControl& NewControl = Hierarchy->ControlHierarchy.Add(NewName, Control.ControlType, Control.ParentName, Control.SpaceName, Control.InitialValue, Control.GizmoName, Control.GizmoTransform, Control.GizmoColor);
					NewKeys.Add(NewControl.GetElementKey());
					break;
				}
				case ERigElementType::Space:
				{
					FRigSpace Space = Hierarchy->SpaceHierarchy[Key.Name];
					const FName NewName = CreateUniqueName(Key.Name, Key.Type);
					FRigSpace& NewSpace = Hierarchy->SpaceHierarchy.Add(NewName, Space.SpaceType, Space.ParentName, Space.InitialTransform);
					NewKeys.Add(NewSpace.GetElementKey());
					break;
				}
				default:
				{
					return;
				}
			}
		}

		Hierarchy->ClearSelection();
		for (int32 Index = 0; Index < NewKeys.Num(); ++Index)
		{
			Hierarchy->Select(NewKeys[Index]);
		}

		FSlateApplication::Get().DismissAllMenus();
		RefreshTreeView();
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
	FRigHierarchyContainer* Hierarchy = GetHierarchyContainer();
	if (Hierarchy)
	{
		ClearDetailPanel();

		FScopedTransaction Transaction(LOCTEXT("HierarchyTreeRenameSelected", "Rename selected item from hierarchy"));
		ControlRigBlueprint->Modify();

		TArray<TSharedPtr<FRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
		if (SelectedItems.Num() == 1)
		{
			if (SelectedItems[0]->Key.Type == ERigElementType::Bone)
			{
				int32 BoneIndex = Hierarchy->GetIndex(SelectedItems[0]->Key);
				if (BoneIndex != INDEX_NONE)
				{
					const FRigBone& Bone = Hierarchy->BoneHierarchy[BoneIndex];
					if (Bone.Type == ERigBoneType::Imported)
					{
						FText ConfirmRename = LOCTEXT("RenameDeleteBoneHierarchy",
							"Renaming imported(white) bones can cause issues with animation - are you sure ?");

						FSuppressableWarningDialog::FSetupInfo Info(ConfirmRename, LOCTEXT("RenameImportedBone", "Rename Imported Bone"), "RenameImportedBoneHierarchy_Warning");
						Info.ConfirmText = LOCTEXT("RenameImportedBoneHierarchy_Yes", "Yes");
						Info.CancelText = LOCTEXT("RenameImportedBoneHierarchy_No", "No");

						FSuppressableWarningDialog RenameImportedBonesInHierarchy(Info);
						if (RenameImportedBonesInHierarchy.ShowModal() == FSuppressableWarningDialog::Cancel)
						{
							return;
						}
					}
				}
			}
			SelectedItems[0]->RequestRename();
		}
	}
}

bool SRigHierarchy::CanPasteItems() const
{
	return true;
}

bool SRigHierarchy::CanCopyOrPasteItems() const
{
	return TreeView->GetNumItemsSelected() > 0;
}

void SRigHierarchy::HandleCopyItems()
{
	if (FRigHierarchyContainer* Hierarchy = GetHierarchyContainer())
	{
		TArray<FRigElementKey> Selection = GetHierarchyContainer()->CurrentSelection();
		FString Content = Hierarchy->ExportToText(Selection);
		FPlatformApplicationMisc::ClipboardCopy(*Content);
	}
}

void SRigHierarchy::HandlePasteItems()
{
	if (FRigHierarchyContainer* Hierarchy = GetHierarchyContainer())
	{
		FString Content;
		FPlatformApplicationMisc::ClipboardPaste(Content);

		FScopedTransaction Transaction(LOCTEXT("HierarchyTreePaste", "Pasted rig elements."));
		ControlRigBlueprint->Modify();

		Hierarchy->ImportFromText(Content, ERigHierarchyImportMode::Append, true);
		ControlRigBlueprint->PropagateHierarchyFromBPToInstances(false);
	}
}

void SRigHierarchy::HandlePasteLocalTransforms()
{
	if (FRigHierarchyContainer* Hierarchy = GetHierarchyContainer())
	{
		FString Content;
		FPlatformApplicationMisc::ClipboardPaste(Content);

		FScopedTransaction Transaction(LOCTEXT("HierarchyTreePasteLocal", "Pasted local transforms."));
		ControlRigBlueprint->Modify();

		Hierarchy->ImportFromText(Content, ERigHierarchyImportMode::ReplaceLocalTransform, true);
		ControlRigBlueprint->PropagateHierarchyFromBPToInstances(false);
	}
}

void SRigHierarchy::HandlePasteGlobalTransforms()
{
	if (FRigHierarchyContainer* Hierarchy = GetHierarchyContainer())
	{
		FString Content;
		FPlatformApplicationMisc::ClipboardPaste(Content);

		FScopedTransaction Transaction(LOCTEXT("HierarchyTreePasteGlobal", "Pasted global transforms."));
		ControlRigBlueprint->Modify();

		Hierarchy->ImportFromText(Content, ERigHierarchyImportMode::ReplaceGlobalTransform, true);
		ControlRigBlueprint->PropagateHierarchyFromBPToInstances(false);
	}
}

FRigHierarchyContainer* SRigHierarchy::GetHierarchyContainer() const
{
	if (ControlRigBlueprint.IsValid())
	{
		return &ControlRigBlueprint->HierarchyContainer;
	}

	return nullptr;
}

FRigHierarchyContainer* SRigHierarchy::GetDebuggedHierarchyContainer() const
{
	if (UControlRig* CurrentRig = ControlRigEditor.Pin()->ControlRig)
	{
		return (FRigHierarchyContainer*)CurrentRig->GetHierarchy();
	}
	else if (UControlRig* DebuggedRig = Cast<UControlRig>(ControlRigBlueprint->GetObjectBeingDebugged()))
	{
		return (FRigHierarchyContainer*)DebuggedRig->GetHierarchy();
	}

	return GetHierarchyContainer();
}

FName SRigHierarchy::CreateUniqueName(const FName& InBaseName, ERigElementType InElementType) const
{
	switch (InElementType)
	{
		case ERigElementType::Bone:
		{
			FRigBoneHierarchy& Hierarchy = GetHierarchyContainer()->BoneHierarchy;
			return UtilityHelpers::CreateUniqueName(InBaseName, [&Hierarchy](const FName& CurName) { return Hierarchy.GetIndex(CurName) == INDEX_NONE; });
		}
		case ERigElementType::Control:
		{
			FRigControlHierarchy& Hierarchy = GetHierarchyContainer()->ControlHierarchy;
			return UtilityHelpers::CreateUniqueName(InBaseName, [&Hierarchy](const FName& CurName) { return Hierarchy.GetIndex(CurName) == INDEX_NONE; });
		}
		case ERigElementType::Space:
		{
			FRigSpaceHierarchy& Hierarchy = GetHierarchyContainer()->SpaceHierarchy;
			return UtilityHelpers::CreateUniqueName(InBaseName, [&Hierarchy](const FName& CurName) { return Hierarchy.GetIndex(CurName) == INDEX_NONE; });
		}
		default:
		{
			break;
		}
	}
	ensure(false);
	return NAME_None;
}

void SRigHierarchy::PostRedo(bool bSuccess) 
{
	if (bSuccess)
	{
		ControlRigBlueprint->PropagateHierarchyFromBPToInstances(false);
		RefreshTreeView();
	}
}

void SRigHierarchy::PostUndo(bool bSuccess) 
{
	if (bSuccess)
	{
		ControlRigBlueprint->PropagateHierarchyFromBPToInstances(false);
		RefreshTreeView();
	}
}

FReply SRigHierarchy::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	TArray<FRigElementKey> DraggedElements;
	TArray<TSharedPtr<FRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
	for (const TSharedPtr<FRigTreeElement>& SelectedItem : SelectedItems)
	{
		DraggedElements.Add(SelectedItem->Key);
	}

	if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) && DraggedElements.Num() > 0)
	{
		if (ControlRigEditor.IsValid())
		{
			TSharedRef<FRigElementHierarchyDragDropOp> DragDropOp = FRigElementHierarchyDragDropOp::New(MoveTemp(DraggedElements));
			DragDropOp->OnPerformDropToGraph.BindSP(ControlRigEditor.Pin().Get(), &FControlRigEditor::OnGraphNodeDropToPerform);
			return FReply::Handled().BeginDragDrop(DragDropOp);
		}
	}

	return FReply::Unhandled();
}

TOptional<EItemDropZone> SRigHierarchy::OnCanAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TSharedPtr<FRigTreeElement> TargetItem)
{
	TOptional<EItemDropZone> ReturnDropZone;

	TSharedPtr<FRigElementHierarchyDragDropOp> RigDragDropOp = DragDropEvent.GetOperationAs<FRigElementHierarchyDragDropOp>();
	if (RigDragDropOp.IsValid())
	{
		FRigHierarchyContainer* Container = GetHierarchyContainer();
		if (Container)
		{
			for (const FRigElementKey& DraggedKey : RigDragDropOp->GetElements())
			{
				if (Container->IsParentedTo(
					TargetItem->Key.Type,
					Container->GetIndex(TargetItem->Key),
					DraggedKey.Type,
					Container->GetIndex(DraggedKey)))
				{
					return ReturnDropZone;
				}
			}
		}

		switch (TargetItem->Key.Type)
		{
			case ERigElementType::Bone:
			{
				// bones can parent anything
				ReturnDropZone = EItemDropZone::OntoItem;
				break;
			}
			case ERigElementType::Control:
			case ERigElementType::Space:
			{
				for (const FRigElementKey& DraggedKey : RigDragDropOp->GetElements())
				{
					switch (DraggedKey.Type)
					{
						case ERigElementType::Control:
						case ERigElementType::Space:
						{
							break;
						}
						default:
						{
							return ReturnDropZone;
						}
					}
				}
				ReturnDropZone = EItemDropZone::OntoItem;
				break;
			}
			default:
			{
				break;
			}
		}
	}

	return ReturnDropZone;
}

FReply SRigHierarchy::OnAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TSharedPtr<FRigTreeElement> TargetItem)
{
	TSharedPtr<FRigElementHierarchyDragDropOp> RigDragDropOp = DragDropEvent.GetOperationAs<FRigElementHierarchyDragDropOp>();
	if (RigDragDropOp.IsValid())
	{
		FRigHierarchyContainer* Container = GetHierarchyContainer();
		FRigHierarchyContainer* DebuggedContainer = GetDebuggedHierarchyContainer();

		if (Container && ControlRigBlueprint.IsValid())
		{
			TGuardValue<bool> GuardRigHierarchyChanges(bIsChangingRigHierarchy, true);
			FScopedTransaction Transaction(LOCTEXT("HierarchyDragAndDrop", "Drag & Drop"));
			ControlRigBlueprint->Modify();

			for (const FRigElementKey& DraggedKey : RigDragDropOp->GetElements())
			{
				if (Container->IsParentedTo(
					TargetItem->Key.Type,
					Container->GetIndex(TargetItem->Key),
					DraggedKey.Type,
					Container->GetIndex(DraggedKey)))
				{
					return FReply::Unhandled();
				}

				if (DraggedKey.Type == ERigElementType::Bone)
				{
					int32 BoneIndex = Container->BoneHierarchy.GetIndex(DraggedKey.Name);
					if (BoneIndex != INDEX_NONE)
					{
						const FRigBone& Bone = Container->BoneHierarchy[BoneIndex];
						if (Bone.Type == ERigBoneType::Imported && Bone.ParentIndex != INDEX_NONE)
						{
							FText ConfirmReparent = LOCTEXT("ConfirmReparentBoneHierarchy",
								"Reparenting imported(white) bones can cause issues with animation - are you sure ?");

							FSuppressableWarningDialog::FSetupInfo Info(ConfirmReparent, LOCTEXT("ReparentImportedBone", "Reparent Imported Bone"), "ReparentImportedBoneHierarchy_Warning");
							Info.ConfirmText = LOCTEXT("ReparentImportedBoneHierarchy_Yes", "Yes");
							Info.CancelText = LOCTEXT("ReparentImportedBoneHierarchy_No", "No");

							FSuppressableWarningDialog ReparentImportedBonesInHierarchy(Info);
							if (ReparentImportedBonesInHierarchy.ShowModal() == FSuppressableWarningDialog::Cancel)
							{
								return FReply::Unhandled();
							}
						}
					}
				}
			}

			for (const FRigElementKey& DraggedKey : RigDragDropOp->GetElements())
			{
				FRigElementKey ParentKey = TargetItem->Key;

				FTransform Transform = DebuggedContainer->GetGlobalTransform(DraggedKey);
				FTransform ParentTransform = DebuggedContainer->GetGlobalTransform(ParentKey);

				switch (DraggedKey.Type)
				{
					case ERigElementType::Bone:
					{
						if (TargetItem->Key.Type == ERigElementType::Bone)
						{
							Container->BoneHierarchy.Reparent(DraggedKey.Name, TargetItem->Key.Name);
						}
						break;
					}
					case ERigElementType::Control:
					{
						if (ParentKey.Type == ERigElementType::Bone)
						{
							for (const FRigSpace& ExistingSpace : Container->SpaceHierarchy)
							{
								if (ExistingSpace.SpaceType == ERigSpaceType::Bone && ExistingSpace.ParentName == ParentKey.Name)
								{
									ParentKey = ExistingSpace.GetElementKey();
									break;
								}
							}

							if (ParentKey.Type != ERigElementType::Space)
							{
								FString SpaceName = FString::Printf(TEXT("%sSpace"), *DraggedKey.Name.ToString());
								FRigSpace& NewSpace = Container->SpaceHierarchy.Add(*SpaceName, ERigSpaceType::Bone, ParentKey.Name);
								if (DebuggedContainer != Container)
								{
									DebuggedContainer->SpaceHierarchy.Add(*SpaceName, ERigSpaceType::Bone, ParentKey.Name);
								}
								ParentKey = NewSpace.GetElementKey();
							}
						}

						if (ParentKey.Type == ERigElementType::Control)
						{
							Container->ControlHierarchy.SetSpace(DraggedKey.Name, NAME_None);
							Container->ControlHierarchy.Reparent(DraggedKey.Name, ParentKey.Name);
							if (DebuggedContainer != Container)
							{
								DebuggedContainer->ControlHierarchy.SetSpace(DraggedKey.Name, NAME_None);
								DebuggedContainer->ControlHierarchy.Reparent(DraggedKey.Name, ParentKey.Name);
							}
						}
						else if (ParentKey.Type == ERigElementType::Space)
						{
							Container->ControlHierarchy.Reparent(DraggedKey.Name, NAME_None);
							Container->ControlHierarchy.SetSpace(DraggedKey.Name, ParentKey.Name);
							if (DebuggedContainer != Container)
							{
								DebuggedContainer->ControlHierarchy.Reparent(DraggedKey.Name, NAME_None);
								DebuggedContainer->ControlHierarchy.SetSpace(DraggedKey.Name, ParentKey.Name);
							}
						}
						break;
					}
					case ERigElementType::Space:
					{
						if (TargetItem->Key.Type == ERigElementType::Bone)
						{
							Container->SpaceHierarchy.Reparent(DraggedKey.Name, ERigSpaceType::Bone, TargetItem->Key.Name);
							if (DebuggedContainer != Container)
							{
								DebuggedContainer->SpaceHierarchy.Reparent(DraggedKey.Name, ERigSpaceType::Bone, TargetItem->Key.Name);
							}
						}
						else if (TargetItem->Key.Type == ERigElementType::Control)
						{
							Container->SpaceHierarchy.Reparent(DraggedKey.Name, ERigSpaceType::Control, TargetItem->Key.Name);
							if (DebuggedContainer != Container)
							{
								DebuggedContainer->SpaceHierarchy.Reparent(DraggedKey.Name, ERigSpaceType::Control, TargetItem->Key.Name);
							}
						}
						else if (TargetItem->Key.Type == ERigElementType::Space)
						{
							Container->SpaceHierarchy.Reparent(DraggedKey.Name, ERigSpaceType::Space, TargetItem->Key.Name);
							if (DebuggedContainer != Container)
							{
								DebuggedContainer->SpaceHierarchy.Reparent(DraggedKey.Name, ERigSpaceType::Space, TargetItem->Key.Name);
							}
						}
						break;
					}
					case ERigElementType::Curve:
					{
						break;
					}
					default:
					{
						ensure(false);
						break;
					}
				}

				FTransform LocalTransform = Transform.GetRelativeTransform(ParentTransform);
				DebuggedContainer->SetInitialTransform(DraggedKey, LocalTransform);
				DebuggedContainer->SetLocalTransform(DraggedKey, LocalTransform);
				Container->SetInitialTransform(DraggedKey, LocalTransform);
				Container->SetLocalTransform(DraggedKey, LocalTransform);
			}
		}

		ControlRigBlueprint->PropagateHierarchyFromBPToInstances(true);
		RefreshTreeView();

		return FReply::Handled();
	}

	return FReply::Unhandled();
}

bool SRigHierarchy::RenameElement(const FRigElementKey& OldKey, const FName& NewName)
{
	ClearDetailPanel();

	if (OldKey.Name == NewName)
	{
		return true;
	}

	// make sure there is no duplicate
	if (ControlRigBlueprint.IsValid())
	{
		FScopedTransaction Transaction(LOCTEXT("HierarchyRename", "Rename Hierarchy Element"));
		ControlRigBlueprint->Modify();

		FRigHierarchyContainer* Container = GetHierarchyContainer();
		switch (OldKey.Type)
		{
			case ERigElementType::Bone:
			{
				Container->BoneHierarchy.Rename(OldKey.Name, NewName);
				return true;
			}
			case ERigElementType::Control:
			{
				Container->ControlHierarchy.Rename(OldKey.Name, NewName);
				return true;
			}
			case ERigElementType::Space:
			{
				Container->SpaceHierarchy.Rename(OldKey.Name, NewName);
				return true;
			}
			default:
			{
				break;
			}
		}
	}

	return false;
}

bool SRigHierarchy::OnVerifyNameChanged(const FRigElementKey& OldKey, const FName& NewName, FText& OutErrorMessage)
{
	if (OldKey.Name == NewName)
	{
		return true;
	}

	// make sure there is no duplicate
	if (ControlRigBlueprint.IsValid())
	{
		FRigHierarchyContainer* Container = GetHierarchyContainer();
		switch (OldKey.Type)
		{
		case ERigElementType::Bone:
		{
			return Container->BoneHierarchy.IsNameAvailable(NewName);
		}
		case ERigElementType::Control:
		{
			return Container->ControlHierarchy.IsNameAvailable(NewName);
		}
		case ERigElementType::Space:
		{
			return Container->SpaceHierarchy.IsNameAvailable(NewName);
		}
		default:
		{
			break;
		}
		}
	}
	return false;
}

void SRigHierarchy::HandleResetTransform()
{
	if (IsMultiSelected() && ControlRigEditor.IsValid())
	{
		if (UControlRigBlueprint* Blueprint = ControlRigEditor.Pin()->GetControlRigBlueprint())
		{
			if (FRigHierarchyContainer* DebuggedContainer = GetDebuggedHierarchyContainer())
			{
				FScopedTransaction Transaction(LOCTEXT("HierarchyResetInitialTransforms", "Reset Initial Transforms"));
				Blueprint->Modify();

				TArray<TSharedPtr<FRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
				for (TSharedPtr<FRigTreeElement> SelectedItem : SelectedItems)
				{
					FRigElementKey Key = SelectedItem->Key;
					if (Key.Type == ERigElementType::Control || Key.Type == ERigElementType::Space)
					{
						FTransform InitialTransform = GetHierarchyContainer()->GetInitialTransform(Key);
						GetHierarchyContainer()->SetLocalTransform(Key, InitialTransform);
						DebuggedContainer->SetLocalTransform(Key, InitialTransform);
					}
				}
			}
		}
	}
}

void SRigHierarchy::HandleResetInitialTransform()
{
	if (IsMultiSelected() && ControlRigEditor.IsValid())
	{
		if (UControlRigBlueprint* Blueprint = ControlRigEditor.Pin()->GetControlRigBlueprint())
		{
			if (FRigHierarchyContainer* DebuggedContainer = GetDebuggedHierarchyContainer())
			{
				FScopedTransaction Transaction(LOCTEXT("HierarchyResetInitialTransforms", "Reset Initial Transforms"));
				Blueprint->Modify();

				TArray<TSharedPtr<FRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
				for (TSharedPtr<FRigTreeElement> SelectedItem : SelectedItems)
				{
					FRigElementKey Key = SelectedItem->Key;
					if (Key.Type == ERigElementType::Control || Key.Type == ERigElementType::Space)
					{
						GetHierarchyContainer()->SetInitialTransform(Key, FTransform::Identity);
						GetHierarchyContainer()->SetLocalTransform(Key, FTransform::Identity);
						DebuggedContainer->SetInitialTransform(Key, FTransform::Identity);
						DebuggedContainer->SetLocalTransform(Key, FTransform::Identity);
					}
				}
			}
		}
	}
}

void SRigHierarchy::HandleResetSpace()
{
	if (IsControlSelected())
	{
		if (UControlRigBlueprint* Blueprint = ControlRigEditor.Pin()->GetControlRigBlueprint())
		{
			if (FRigHierarchyContainer* DebuggedContainer = GetDebuggedHierarchyContainer())
			{
				FScopedTransaction Transaction(LOCTEXT("HierarchyResetSpace", "Reset Space"));
				Blueprint->Modify();

				TArray<TSharedPtr<FRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
				for (TSharedPtr<FRigTreeElement> SelectedItem : SelectedItems)
				{
					FRigElementKey Key = SelectedItem->Key;
					if (Key.Type == ERigElementType::Control)
					{
						FRigControl& Control = DebuggedContainer->ControlHierarchy[Key.Name];
						if (FRigElementKey SpaceKey = Control.GetSpaceElementKey())
						{
							FRigSpace& Space = DebuggedContainer->SpaceHierarchy[SpaceKey.Name];

							FTransform Transform = DebuggedContainer->GetGlobalTransform(Key);
							FTransform SpaceParentTransform = DebuggedContainer->GetGlobalTransform(Space.GetParentElementKey());
							FTransform LocalTransform = Transform.GetRelativeTransform(SpaceParentTransform);

							GetHierarchyContainer()->SetInitialTransform(SpaceKey, LocalTransform);
							GetHierarchyContainer()->SetLocalTransform(SpaceKey, LocalTransform);
							GetHierarchyContainer()->SetInitialTransform(Key, FTransform::Identity);
							GetHierarchyContainer()->SetLocalTransform(Key, FTransform::Identity);

							DebuggedContainer->SetInitialTransform(SpaceKey, LocalTransform);
							DebuggedContainer->SetLocalTransform(SpaceKey, LocalTransform);
							DebuggedContainer->SetInitialTransform(Key, FTransform::Identity);
							DebuggedContainer->SetLocalTransform(Key, FTransform::Identity);
						}
						else
						{
							FString SpaceName = FString::Printf(TEXT("%sSpace"), *Control.Name.ToString());
							if (FRigElementKey CurrentParentKey = Control.GetParentElementKey())
							{
								FRigSpace& NewSpace = GetHierarchyContainer()->SpaceHierarchy.Add(*SpaceName, ERigSpaceType::Control, CurrentParentKey.Name, Control.Value.Get<FTransform>());
								SpaceKey = NewSpace.GetElementKey();
							}
							else
							{
								FRigSpace& NewSpace = GetHierarchyContainer()->SpaceHierarchy.Add(*SpaceName, ERigSpaceType::Global, NAME_None, Control.Value.Get<FTransform>());
								SpaceKey = NewSpace.GetElementKey();
							}

							GetHierarchyContainer()->ControlHierarchy.Reparent(Control.Name, NAME_None);
							GetHierarchyContainer()->ControlHierarchy.SetSpace(Control.Name, SpaceKey.Name);
							GetHierarchyContainer()->ControlHierarchy.SetInitialValue<FTransform>(Control.Name, FTransform::Identity);
							GetHierarchyContainer()->ControlHierarchy.SetLocalTransform(Control.Name, FTransform::Identity);
							Blueprint->PropagateHierarchyFromBPToInstances();
						}
					}
				}
			}
		}
	}
}

void SRigHierarchy::HandleSetInitialTransformFromCurrentTransform()
{
	if (IsMultiSelected())
	{
		if (UControlRigBlueprint* Blueprint = ControlRigEditor.Pin()->GetControlRigBlueprint())
		{
			if (FRigHierarchyContainer* DebuggedContainer = GetDebuggedHierarchyContainer())
			{
				FScopedTransaction Transaction(LOCTEXT("HierarchySetInitialTransforms", "Set Initial Transforms"));
				Blueprint->Modify();

				TArray<TSharedPtr<FRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
				TMap<FRigElementKey, FTransform> GlobalTransforms;
				TMap<FRigElementKey, FTransform> ParentGlobalTransforms;

				for (TSharedPtr<FRigTreeElement> SelectedItem : SelectedItems)
				{
					FRigElementKey Key = SelectedItem->Key;
					if (Key.Type == ERigElementType::Control || Key.Type == ERigElementType::Space || Key.Type == ERigElementType::Bone)
					{
						GlobalTransforms.FindOrAdd(Key) = DebuggedContainer->GetGlobalTransform(Key);
						FRigElementKey ParentKey;
						switch (Key.Type)
						{
							case ERigElementType::Bone:
							{
								const FRigBone& Element = DebuggedContainer->BoneHierarchy[Key.Name];
								ParentKey = Element.GetParentElementKey();
								break;
							}
							case ERigElementType::Control:
							{
								const FRigControl& Element = DebuggedContainer->ControlHierarchy[Key.Name];
								if (Element.SpaceName != NAME_None)
								{
									ParentKey = Element.GetSpaceElementKey();
								}
								else
								{
									ParentKey = Element.GetParentElementKey();
								}
								break;
							}
							case ERigElementType::Space:
							{
								const FRigSpace& Element = DebuggedContainer->SpaceHierarchy[Key.Name];
								ParentKey = Element.GetParentElementKey();
								break;
							}
							case ERigElementType::Curve:
							{
								break;
							}
							default:
							{
								ensure(false);
								break;
							}
						}

						if (ParentKey)
						{
							ParentGlobalTransforms.FindOrAdd(Key) = DebuggedContainer->GetGlobalTransform(ParentKey);
						}
						else
						{
							ParentGlobalTransforms.FindOrAdd(Key) = FTransform::Identity;
						}
					}

				}

				for (TSharedPtr<FRigTreeElement> SelectedItem : SelectedItems)
				{
					FRigElementKey Key = SelectedItem->Key;
					if (Key.Type == ERigElementType::Control || Key.Type == ERigElementType::Space || Key.Type == ERigElementType::Bone)
					{
						FTransform LocalTransform = GlobalTransforms[Key].GetRelativeTransform(ParentGlobalTransforms[Key]);
						GetHierarchyContainer()->SetInitialTransform(Key, LocalTransform);
						GetHierarchyContainer()->SetLocalTransform(Key, LocalTransform);
						DebuggedContainer->SetInitialTransform(Key, LocalTransform);
						DebuggedContainer->SetLocalTransform(Key, LocalTransform);
					}
				}
			}
		}
	}
}

void SRigHierarchy::HandleFrameSelection()
{
	TArray<TSharedPtr<FRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
	for (TSharedPtr<FRigTreeElement> SelectedItem : SelectedItems)
	{
		SetExpansionRecursive(SelectedItem, true);
	}

	if (SelectedItems.Num() > 0)
	{
		TreeView->RequestScrollIntoView(SelectedItems.Last());
	}
}

bool SRigHierarchy::FindClosestBone(const FVector& Point, FName& OutRigElementName, FTransform& OutGlobalTransform) const
{
	if (FRigHierarchyContainer* DebuggedContainer = GetDebuggedHierarchyContainer())
	{
		const FRigBoneHierarchy& RigHierarchy = DebuggedContainer->BoneHierarchy;
		float NearestDistance = BIG_NUMBER;

		for (int32 Index = 0; Index < RigHierarchy.Num(); ++Index)
		{
			FTransform CurTransform = RigHierarchy.GetGlobalTransform(Index);
			float CurDistance = FVector::Distance(CurTransform.GetLocation(), Point);
			if (CurDistance < NearestDistance)
			{
				NearestDistance = CurDistance;
				OutGlobalTransform = CurTransform;
				OutRigElementName = RigHierarchy[Index].Name;
			}
		}

		return (OutRigElementName != NAME_None);
	}
	return false;
}

void SRigHierarchy::HandleSetInitialTransformFromClosestBone()
{
	if (IsControlOrSpaceSelected())
	{
		if (UControlRigBlueprint* Blueprint = ControlRigEditor.Pin()->GetControlRigBlueprint())
		{
			if (FRigHierarchyContainer* DebuggedContainer = GetDebuggedHierarchyContainer())
			{
				FScopedTransaction Transaction(LOCTEXT("HierarchySetInitialTransforms", "Set Initial Transforms"));
				Blueprint->Modify();

				TArray<TSharedPtr<FRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
				TMap<FRigElementKey, FTransform> ClosestTransforms;
				TMap<FRigElementKey, FTransform> ParentGlobalTransforms;

				for (TSharedPtr<FRigTreeElement> SelectedItem : SelectedItems)
				{
					FRigElementKey Key = SelectedItem->Key;
					if (Key.Type == ERigElementType::Control || Key.Type == ERigElementType::Space)
					{
						FTransform GlobalTransform = DebuggedContainer->GetGlobalTransform(Key);
						FTransform ClosestTransform;
						FName ClosestRigElement;

						if (!FindClosestBone(GlobalTransform.GetLocation(), ClosestRigElement, ClosestTransform))
						{
							continue;
						}

						ClosestTransforms.FindOrAdd(Key) = ClosestTransform;

						FRigElementKey ParentKey;
						switch (Key.Type)
						{
							case ERigElementType::Control:
							{
								const FRigControl& Element = DebuggedContainer->ControlHierarchy[Key.Name];
								if (Element.SpaceName != NAME_None)
								{
									ParentKey = Element.GetSpaceElementKey();
								}
								else
								{
									ParentKey = Element.GetParentElementKey();
								}
								break;
							}
							case ERigElementType::Space:
							{
								const FRigSpace& Element = DebuggedContainer->SpaceHierarchy[Key.Name];
								ParentKey = Element.GetParentElementKey();
								break;
							}
							case ERigElementType::Bone:
							case ERigElementType::Curve:
							{
								break;
							}
							default:
							{
								ensure(false);
								break;
							}
						}

						if (ParentKey)
						{
							ParentGlobalTransforms.FindOrAdd(Key) = DebuggedContainer->GetGlobalTransform(ParentKey);
						}
						else
						{
							ParentGlobalTransforms.FindOrAdd(Key) = FTransform::Identity;
						}
					}

				}

				for (TSharedPtr<FRigTreeElement> SelectedItem : SelectedItems)
				{
					FRigElementKey Key = SelectedItem->Key;
					if (Key.Type == ERigElementType::Control || Key.Type == ERigElementType::Space || Key.Type == ERigElementType::Bone)
					{
						if (!ClosestTransforms.Contains(Key))
						{
							continue;
						}

						FTransform LocalTransform = ClosestTransforms[Key].GetRelativeTransform(ParentGlobalTransforms[Key]);
						GetHierarchyContainer()->SetInitialTransform(Key, LocalTransform);
						GetHierarchyContainer()->SetLocalTransform(Key, LocalTransform);
						DebuggedContainer->SetInitialTransform(Key, LocalTransform);
						DebuggedContainer->SetLocalTransform(Key, LocalTransform);
					}
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE