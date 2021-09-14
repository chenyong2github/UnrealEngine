// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRigHierarchy.h"
#include "Widgets/Input/SComboButton.h"
#include "EditorStyleSet.h"
#include "Widgets/Input/SButton.h"
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
#include "HAL/PlatformTime.h"
#include "Dialogs/Dialogs.h"
#include "IPersonaToolkit.h"
#include "SKismetInspector.h"
#include "Types/WidgetActiveTimerDelegate.h"

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
	if (InRigTreeElement->Key.IsValid())
	{
		return SNew(SRigHierarchyItem, InControlRigEditor, InOwnerTable, InRigTreeElement, InCommandList, InHierarchy)
			.OnRenameElement(InHierarchy.Get(), &SRigHierarchy::RenameElement)
			.OnVerifyElementNameChanged(InHierarchy.Get(), &SRigHierarchy::OnVerifyNameChanged);
	}

	return SNew(SRigHierarchyItem, InControlRigEditor, InOwnerTable, InRigTreeElement, InCommandList, InHierarchy);
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

	if (!InRigTreeElement->Key.IsValid())
	{
		STableRow<TSharedPtr<FRigTreeElement>>::Construct(
			STableRow<TSharedPtr<FRigTreeElement>>::FArguments()
			.ShowSelection(false)
			.OnCanAcceptDrop(InHierarchy.Get(), &SRigHierarchy::OnCanAcceptDrop)
			.OnAcceptDrop(InHierarchy.Get(), &SRigHierarchy::OnAcceptDrop)
			.Content()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.FillHeight(200.f)
				[
					SNew(SSpacer)
				]
			], OwnerTable);
		return;
	}

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
	}

	if (ControlRigBlueprint.IsValid())
	{
		ControlRigBlueprint->HierarchyContainer.OnElementAdded.RemoveAll(this);
		ControlRigBlueprint->HierarchyContainer.OnElementRemoved.RemoveAll(this);
		ControlRigBlueprint->HierarchyContainer.OnElementRenamed.RemoveAll(this);
		ControlRigBlueprint->HierarchyContainer.OnElementReparented.RemoveAll(this);
		ControlRigBlueprint->HierarchyContainer.OnElementSelected.RemoveAll(this);
		ControlRigBlueprint->OnRefreshEditor().RemoveAll(this);
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
	ControlRigBlueprint->OnRefreshEditor().AddRaw(this, &SRigHierarchy::HandleRefreshEditorFromBlueprint);

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

				+ SVerticalBox::Slot()
				.AutoHeight()
				.VAlign(VAlign_Top)
				[
					SNew(SHorizontalBox)
					.Visibility(this, &SRigHierarchy::IsToolbarVisible)

					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.MaxWidth(180.0f)
					.Padding(3.0f, 1.0f)
					[
						SNew(SButton)
						.ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
						.ForegroundColor(FLinearColor::White)
						.OnClicked(FOnClicked::CreateSP(this, &SRigHierarchy::OnImportSkeletonClicked))
						.Text(FText::FromString(TEXT("Import Hierarchy")))
					]
				]

				+SVerticalBox::Slot()
				.AutoHeight()
				.VAlign(VAlign_Top)
				[
					SNew(SHorizontalBox)
					.Visibility(this, &SRigHierarchy::IsSearchbarVisible)

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
				SAssignNew(TreeView, SRigHierarchyTreeView)
				.TreeItemsSource(&RootElements)
				.SelectionMode(ESelectionMode::Multi)
				.OnGenerateRow(this, &SRigHierarchy::MakeTableRowWidget)
				.OnGetChildren(this, &SRigHierarchy::HandleGetChildrenForTree)
				.OnSelectionChanged(this, &SRigHierarchy::OnSelectionChanged)
				.OnContextMenuOpening(this, &SRigHierarchy::CreateContextMenu)
				.OnMouseButtonClick(this, &SRigHierarchy::OnItemClicked)
				.OnMouseButtonDoubleClick(this, &SRigHierarchy::OnItemDoubleClicked)
				.OnSetExpansionRecursive(this, &SRigHierarchy::OnSetExpansionRecursive)
				.HighlightParentNodesForSelection(true)
				.ItemHeight(24)
			]
		]

		/*
		+ SVerticalBox::Slot()
		.Padding(0.0f, 0.0f)
		.FillHeight(0.1f)
		[
			SNew(SBorder)
			.Padding(2.0f)
			.BorderImage(FEditorStyle::GetBrush("SCSEditor.TreePanel"))
			[
				SNew(SSpacer)
			]
		]
		*/
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

	CommandList->MapAction(Commands.MirrorItem,
		FExecuteAction::CreateSP(this, &SRigHierarchy::HandleMirrorItem),
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
		FExecuteAction::CreateSP(this, &SRigHierarchy::HandleResetTransform, true),
		FCanExecuteAction::CreateSP(this, &SRigHierarchy::IsMultiSelected));

	CommandList->MapAction(
		Commands.ResetAllTransforms,
		FExecuteAction::CreateSP(this, &SRigHierarchy::HandleResetTransform, false),
		FCanExecuteAction::CreateSP(this, &SRigHierarchy::CanPasteItems));

	CommandList->MapAction(
		Commands.SetInitialTransformFromClosestBone,
		FExecuteAction::CreateSP(this, &SRigHierarchy::HandleSetInitialTransformFromClosestBone),
		FCanExecuteAction::CreateSP(this, &SRigHierarchy::IsControlOrSpaceSelected));

	CommandList->MapAction(
		Commands.SetInitialTransformFromCurrentTransform,
		FExecuteAction::CreateSP(this, &SRigHierarchy::HandleSetInitialTransformFromCurrentTransform),
		FCanExecuteAction::CreateSP(this, &SRigHierarchy::IsMultiSelected));

	CommandList->MapAction(
		Commands.FrameSelection,
		FExecuteAction::CreateSP(this, &SRigHierarchy::HandleFrameSelection),
		FCanExecuteAction::CreateSP(this, &SRigHierarchy::IsMultiSelected));

	CommandList->MapAction(
		Commands.ControlBoneTransform,
		FExecuteAction::CreateSP(this, &SRigHierarchy::HandleControlBoneOrSpaceTransform),
		FCanExecuteAction::CreateSP(this, &SRigHierarchy::IsSingleBoneSelected));

	CommandList->MapAction(
		Commands.Unparent,
		FExecuteAction::CreateSP(this, &SRigHierarchy::HandleUnparent),
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

EVisibility SRigHierarchy::IsToolbarVisible() const
{
	if (FRigHierarchyContainer* Container = GetHierarchyContainer())
	{
		if (Container->BoneHierarchy.Num() > 0)
		{
			return EVisibility::Collapsed;
		}
	}
	return EVisibility::Visible;
}

EVisibility SRigHierarchy::IsSearchbarVisible() const
{
	if (FRigHierarchyContainer* Container = GetHierarchyContainer())
	{
		if ((Container->BoneHierarchy.Num() + Container->SpaceHierarchy.Num() + Container->ControlHierarchy.Num()) > 0)
		{
			return EVisibility::Visible;
		}
	}
	return EVisibility::Collapsed;
}

FReply SRigHierarchy::OnImportSkeletonClicked()
{
	FRigHierarchyImportSettings Settings;
	TSharedPtr<FStructOnScope> StructToDisplay = MakeShareable(new FStructOnScope(FRigHierarchyImportSettings::StaticStruct(), (uint8*)&Settings));

	TSharedRef<SKismetInspector> KismetInspector = SNew(SKismetInspector);
	KismetInspector->ShowSingleStruct(StructToDisplay);

	SGenericDialogWidget::OpenDialog(LOCTEXT("ControlRigHierarchyImport", "Import Hierarchy"), KismetInspector, SGenericDialogWidget::FArguments(), true);

	if (Settings.Mesh != nullptr)
	{
		ImportHierarchy(FAssetData(Settings.Mesh));
	}

	return FReply::Handled();
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

	// internally save expansion states before rebuilding the tree, so the states can be restored later
	TreeView->SaveAndClearSparseItemInfos();

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

		for (const auto& Pair : ElementMap)
		{
			TreeView->RestoreSparseItemInfos(Pair.Value);
		}

		// expand all elements upon the initial construction of the tree
		if (ExpansionState.Num() == 0)
		{
			for (TSharedPtr<FRigTreeElement> RootElement : RootElements)
			{
				SetExpansionRecursive(RootElement, false, true);
			}
		}

		if (RootElements.Num() > 0)
		{
			AddSpacerElement();
		}

		TreeView->RequestTreeRefresh();

		TArray<FRigElementKey> Selection = Container->CurrentSelection();
		for (const FRigElementKey& Key : Selection)
		{
			OnRigElementSelected(Container, Key, true);
		}
	}
}

void SRigHierarchy::SetExpansionRecursive(TSharedPtr<FRigTreeElement> InElement, bool bTowardsParent, bool bShouldBeExpanded)
{
	TreeView->SetItemExpansion(InElement, bShouldBeExpanded);

	if (bTowardsParent)
	{
		if (const FRigElementKey* ParentKey = ParentMap.Find(InElement->Key))
		{
			if (TSharedPtr<FRigTreeElement>* ParentItem = ElementMap.Find(*ParentKey))
			{
				SetExpansionRecursive(*ParentItem, bTowardsParent, bShouldBeExpanded);
			}
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

		if (NewSelection.Num() > 0)
		{
			if (ControlRigEditor.IsValid())
			{
				if (ControlRigEditor.Pin()->GetEventQueue() == EControlRigEditorEventQueue::Setup)
				{
					HandleControlBoneOrSpaceTransform();
				}
			}
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
	if (FilteredString.IsEmpty() || !InKey.IsValid())
	{
		TSharedPtr<FRigTreeElement> NewItem = MakeShared<FRigTreeElement>(InKey, SharedThis(this));

		if (InKey.IsValid())
		{
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
			RootElements.Add(NewItem);
		}
	}
	else
	{
		FString FilteredStringUnderScores = FilteredString.Replace(TEXT(" "), TEXT("_"));
		if (InKey.Name.ToString().Contains(FilteredString) || InKey.Name.ToString().Contains(FilteredStringUnderScores))	
		{
			TSharedPtr<FRigTreeElement> NewItem = MakeShared<FRigTreeElement>(InKey, SharedThis(this));
			ElementMap.Add(InKey, NewItem);
			RootElements.Add(NewItem);
		}
	}
}

void SRigHierarchy::AddSpacerElement()
{
	AddElement(FRigElementKey(), FRigElementKey());
}

void SRigHierarchy::AddBoneElement(FRigBone InBone)
{
	if (ElementMap.Contains(InBone.GetElementKey()))
	{
		return;
	}

	const FRigHierarchyContainer* Container = GetHierarchyContainer();
	const FRigBoneHierarchy& BoneHierarchy = Container->BoneHierarchy;
	const FRigControlHierarchy& ControlHierarchy = Container->ControlHierarchy;
	const FRigSpaceHierarchy& SpaceHierarchy = Container->SpaceHierarchy;

	AddElement(InBone.GetElementKey());

	FRigElementKey ParentKey;
	if(InBone.ParentIndex != INDEX_NONE)
	{
		AddBoneElement(BoneHierarchy[InBone.ParentIndex]);
		ParentKey = BoneHierarchy[InBone.ParentIndex].GetElementKey();
	}

	if (ParentKey.IsValid())
	{
		ReparentElement(InBone.GetElementKey(), ParentKey);
	}
}

void SRigHierarchy::AddControlElement(FRigControl InControl)
{
	if (ElementMap.Contains(InControl.GetElementKey()))
	{
		return;
	}

	const FRigHierarchyContainer* Container = GetHierarchyContainer();
	const FRigControlHierarchy& ControlHierarchy = Container->ControlHierarchy;
	const FRigSpaceHierarchy& SpaceHierarchy = Container->SpaceHierarchy;

	AddElement(InControl.GetElementKey());

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

	if (ParentKey.IsValid())
	{
		ReparentElement(InControl.GetElementKey(), ParentKey);
	}
}

void SRigHierarchy::AddSpaceElement(FRigSpace InSpace)
{
	if (ElementMap.Contains(InSpace.GetElementKey()))
	{
		return;
	}

	const FRigHierarchyContainer* Container = GetHierarchyContainer();
	const FRigBoneHierarchy& BoneHierarchy = Container->BoneHierarchy;
	const FRigControlHierarchy& ControlHierarchy = Container->ControlHierarchy;
	const FRigSpaceHierarchy& SpaceHierarchy = Container->SpaceHierarchy;

	AddElement(InSpace.GetElementKey());

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

	if (ParentKey.IsValid())
	{
		ReparentElement(InSpace.GetElementKey(), ParentKey);
	}
}

void SRigHierarchy::ReparentElement(FRigElementKey InKey, FRigElementKey InParentKey)
{
	if (!InKey.IsValid() || InKey == InParentKey)
	{
		return;
	}

	TSharedPtr<FRigTreeElement>* FoundItem = ElementMap.Find(InKey);
	if (FoundItem == nullptr)
	{
		return;
	}

	if (!FilterText.IsEmpty())
	{
		return;
	}

	if (const FRigElementKey* ExistingParentKey = ParentMap.Find(InKey))
	{
		if (*ExistingParentKey == InParentKey)
		{
			return;
		}

		if (TSharedPtr<FRigTreeElement>* ExistingParent = ElementMap.Find(*ExistingParentKey))
		{
			(*ExistingParent)->Children.Remove(*FoundItem);
		}

		ParentMap.Remove(InKey);
	}
	else
	{
		if (!InParentKey.IsValid())
		{
			return;
		}

		RootElements.Remove(*FoundItem);
	}

	if (InParentKey)
	{
		ParentMap.Add(InKey, InParentKey);

		TSharedPtr<FRigTreeElement>* FoundParent = ElementMap.Find(InParentKey);
		check(FoundParent);
		FoundParent->Get()->Children.Add(*FoundItem);
	}
	else
	{
		RootElements.Add(*FoundItem);
	}

}

void SRigHierarchy::OnRigElementAdded(FRigHierarchyContainer* Container, const FRigElementKey& InKey)
{
	if (ControlRigBlueprint->bSuspendAllNotifications)
	{
		return;
	}

	if (bIsChangingRigHierarchy || InKey.Type == ERigElementType::Curve)
	{
		return;
	}

	if (InKey.Type == ERigElementType::Control)
	{
		if (Container->GetIndex(InKey) == INDEX_NONE)
		{
			return;
		}
	}

	RefreshTreeView();
}

void SRigHierarchy::OnRigElementRemoved(FRigHierarchyContainer* Container, const FRigElementKey& InKey)
{
	if (bIsChangingRigHierarchy || InKey.Type == ERigElementType::Curve)
	{
		return;
	}
	if (ControlRigBlueprint->bSuspendAllNotifications)
	{
		return;
	}

	if (InKey.Type == ERigElementType::Control)
	{
		if (Container->GetIndex(InKey) == INDEX_NONE)
		{
			return;
		}
	}

	RefreshTreeView();
}

void SRigHierarchy::OnRigElementRenamed(FRigHierarchyContainer* Container, ERigElementType ElementType, const FName& InOldName, const FName& InNewName)
{
	if (bIsChangingRigHierarchy || ElementType == ERigElementType::Curve)
	{
		return;
	}
	if (ControlRigBlueprint->bSuspendAllNotifications)
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
	if (ControlRigBlueprint->bSuspendAllNotifications)
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
	if (ControlRigBlueprint->bSuspendAllNotifications)
	{
		return;
	}

	if (InKey.Type == ERigElementType::Control)
	{
		if (Container->GetIndex(InKey) == INDEX_NONE)
		{
			return;
		}
	}

	for (int32 RootIndex = 0; RootIndex < RootElements.Num(); ++RootIndex)
	{
		TSharedPtr<FRigTreeElement> Found = FindElement(InKey, RootElements[RootIndex]);
		if (Found.IsValid())
		{
			TreeView->SetItemSelection(Found, bSelected, ESelectInfo::OnNavigation);
			HandleFrameSelection();

			if (ControlRigEditor.IsValid())
			{
				if (ControlRigEditor.Pin()->GetEventQueue() == EControlRigEditorEventQueue::Setup)
				{
					TGuardValue<bool> GuardRigHierarchyChanges(bIsChangingRigHierarchy, true);
					HandleControlBoneOrSpaceTransform();
				}
			}
		}
	}
}

void SRigHierarchy::HandleRefreshEditorFromBlueprint(UControlRigBlueprint* InBlueprint)
{
	RefreshTreeView();
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

void SRigHierarchy::OnItemClicked(TSharedPtr<FRigTreeElement> InItem)
{
	FRigHierarchyContainer* Hierarchy = GetHierarchyContainer();
	if (Hierarchy->IsSelected(InItem->Key))
	{
		if (ControlRigEditor.IsValid())
		{
			ControlRigEditor.Pin()->SetDetailStruct(InItem->Key);
		}

		if (InItem->Key.Type == ERigElementType::Bone)
		{
			const FRigBone& Bone = Hierarchy->BoneHierarchy[InItem->Key.Name];
			if (Bone.Type == ERigBoneType::Imported)
			{
				return;
			}
		}

		uint32 CurrentCycles = FPlatformTime::Cycles();
		double SecondsPassed = double(CurrentCycles - TreeView->LastClickCycles) * FPlatformTime::GetSecondsPerCycle();
		if (SecondsPassed > 0.5f)
		{
			RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([this](double, float) {
				HandleRenameItem();
				return EActiveTimerReturnType::Stop;
			}));
		}

		TreeView->LastClickCycles = CurrentCycles;
	}
}

void SRigHierarchy::OnItemDoubleClicked(TSharedPtr<FRigTreeElement> InItem)
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

void SRigHierarchy::OnSetExpansionRecursive(TSharedPtr<FRigTreeElement> InItem, bool bShouldBeExpanded)
{
	SetExpansionRecursive(InItem, false, bShouldBeExpanded);
}

void SRigHierarchy::FillContextMenu(class FMenuBuilder& MenuBuilder)
{
	const FControlRigHierarchyCommands& Actions = FControlRigHierarchyCommands::Get();
	{
		struct FLocalMenuBuilder
		{
			static void FillNewMenu(FMenuBuilder& InSubMenuBuilder, TSharedPtr<SRigHierarchyTreeView> InTreeView)
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
		MenuBuilder.AddMenuEntry(Actions.MirrorItem);
		MenuBuilder.EndSection();

		if (IsSingleBoneSelected())
		{
			MenuBuilder.BeginSection("Interaction", LOCTEXT("InteractionHeader", "Interaction"));
			MenuBuilder.AddMenuEntry(Actions.ControlBoneTransform);
			MenuBuilder.EndSection();
		}

		/*
		if (IsSingleSpaceSelected())
		{
			MenuBuilder.BeginSection("Interaction", LOCTEXT("InteractionHeader", "Interaction"));
			MenuBuilder.AddMenuEntry(Actions.ControlSpaceTransform);
			MenuBuilder.EndSection();
		}
		*/

		MenuBuilder.BeginSection("Copy&Paste", LOCTEXT("Copy&PasteHeader", "Copy & Paste"));
		MenuBuilder.AddMenuEntry(Actions.CopyItems);
		MenuBuilder.AddMenuEntry(Actions.PasteItems);
		MenuBuilder.AddMenuEntry(Actions.PasteLocalTransforms);
		MenuBuilder.AddMenuEntry(Actions.PasteGlobalTransforms);
		MenuBuilder.EndSection();

		MenuBuilder.BeginSection("Transforms", LOCTEXT("TransformsHeader", "Transforms"));
		MenuBuilder.AddMenuEntry(Actions.ResetTransform);
		MenuBuilder.AddMenuEntry(Actions.ResetAllTransforms);
		MenuBuilder.AddMenuEntry(Actions.SetInitialTransformFromCurrentTransform);
		MenuBuilder.AddMenuEntry(Actions.SetInitialTransformFromClosestBone);
		MenuBuilder.AddMenuEntry(Actions.Unparent);
		MenuBuilder.EndSection();

		MenuBuilder.BeginSection("Assets", LOCTEXT("AssetsHeader", "Assets"));
		MenuBuilder.AddSubMenu(
			LOCTEXT("ImportSubMenu", "Import"),
			LOCTEXT("ImportSubMenu_ToolTip", "Import hierarchy to the current rig. This only imports non-existing node. For example, if there is hand_r, it won't import hand_r. If you want to reimport whole new hiearchy, delete all nodes, and use import hierarchy."),
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
			.ToolTipText(LOCTEXT("RefreshMesh_Tooltip", "Select Mesh to refresh transform from... It will refresh init transform from selected mesh. This doesn't change hierarchy. If you want to reimport hierarchy, please delete all nodes, and use import hierarchy."))
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
		TGuardValue<bool> GuardRigHierarchyChanges(bIsChangingRigHierarchy, true);
		TGuardValue<bool> SuspendBlueprintNotifs(ControlRigBlueprint->bSuspendAllNotifications, true);

		FScopedTransaction Transaction(LOCTEXT("HierarchyRefresh", "Refresh Transform"));
		ControlRigBlueprint->Modify();

		// don't select bone if we are in setup mode.
		// we do this to avoid the editmode / viewport gizmos to refresh recursively,
		// which can add an extreme slowdown depending on the number of bones (n^(n-1))
		bool bSelectBones = true;
		if (UControlRig* CurrentRig = ControlRigEditor.Pin()->ControlRig)
		{
			bSelectBones = !CurrentRig->IsSetupModeEnabled();
		}

		const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();
		Hierarchy->BoneHierarchy.ImportSkeleton(RefSkeleton, NAME_None, true, true, bSelectBones, false /* notify */);
	}

	ControlRigBlueprint->PropagateHierarchyFromBPToInstances(true);
	ControlRigEditor.Pin()->OnHierarchyChanged();
	RefreshTreeView();
	FSlateApplication::Get().DismissAllMenus();
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
	USkeletalMesh* Mesh = Cast<USkeletalMesh>(InAssetData.GetAsset());
	if (Mesh && Hierarchy)
	{
		TGuardValue<bool> GuardRigHierarchyChanges(bIsChangingRigHierarchy, true);
		TGuardValue<bool> SuspendBlueprintNotifs(ControlRigBlueprint->bSuspendAllNotifications, true);

		FScopedTransaction Transaction(LOCTEXT("HierarchyImport", "Import Hierarchy"));
		ControlRigBlueprint->Modify();

		// don't select bone if we are in setup mode.
		// we do this to avoid the editmode / viewport gizmos to refresh recursively,
		// which can add an extreme slowdown depending on the number of bones (n^(n-1))
		bool bSelectBones = true;
		if (UControlRig* CurrentRig = ControlRigEditor.Pin()->ControlRig)
		{
			bSelectBones = !CurrentRig->IsSetupModeEnabled();
		}

		const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();
		Hierarchy->BoneHierarchy.ImportSkeleton(RefSkeleton, NAME_None, false, false, bSelectBones, false /* notify */);
		Hierarchy->CurveContainer.ImportCurvesFromSkeleton(Mesh->GetSkeleton(), NAME_None, true, false, false /* notify */);

		ControlRigBlueprint->SourceHierarchyImport = Mesh->GetSkeleton();
		ControlRigBlueprint->SourceCurveImport = Mesh->GetSkeleton();
	}

	ControlRigBlueprint->PropagateHierarchyFromBPToInstances(true);
	ControlRigEditor.Pin()->OnHierarchyChanged();
	RefreshTreeView();
	FSlateApplication::Get().DismissAllMenus();

	if (ControlRigBlueprint->GetPreviewMesh() == nullptr &&
		ControlRigEditor.IsValid() && 
		Mesh != nullptr)
	{
		ControlRigEditor.Pin()->GetPersonaToolkit()->SetPreviewMesh(Mesh, true);
	}

	if (ControlRigEditor.IsValid())
	{
		ControlRigEditor.Pin()->Compile();
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

bool SRigHierarchy::IsSingleBoneSelected() const
{
	if(!IsSingleSelected())
	{
		return false;
	}
	return TreeView->GetSelectedItems()[0]->Key.Type == ERigElementType::Bone;
}

bool SRigHierarchy::IsSingleSpaceSelected() const
{
	if(!IsSingleSelected())
	{
		return false;
	}
	return TreeView->GetSelectedItems()[0]->Key.Type == ERigElementType::Space;
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
		TArray<FRigElementKey> RemovedItems;

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
			TGuardValue<bool> SuspendBlueprintNotifs(ControlRigBlueprint->bSuspendAllNotifications, true);

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
								FText ConfirmDelete = LOCTEXT("ConfirmDeleteBoneHierarchy", "Deleting imported(white) bones can cause issues with animation - are you sure ?");

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
					break;
				}
			}

			RemovedItems.Add(SelectedKey);
		}

		for (const FRigElementKey& RemovedItem : RemovedItems)
		{
			Hierarchy->HandleOnElementRemoved(Hierarchy, RemovedItem);
		}
	}

	ControlRigBlueprint->PropagateHierarchyFromBPToInstances(true);
	ControlRigEditor.Pin()->OnHierarchyChanged();
	RefreshTreeView();
	FSlateApplication::Get().DismissAllMenus();
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
	}

	FSlateApplication::Get().DismissAllMenus();
	RefreshTreeView();
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
		{
			TGuardValue<bool> GuardRigHierarchyChanges(bIsChangingRigHierarchy, true);
			TGuardValue<bool> SuspendBlueprintNotifs(ControlRigBlueprint->bSuspendAllNotifications, true);

			FScopedTransaction Transaction(LOCTEXT("HierarchyTreeDuplicateSelected", "Duplicate selected items from hierarchy"));
			ControlRigBlueprint->Modify();

			TArray<TSharedPtr<FRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
			TArray<FRigElementKey> KeysToDuplicate;
			for (int32 Index = 0; Index < SelectedItems.Num(); ++Index)
			{
				KeysToDuplicate.Add(SelectedItems[Index]->Key);
			}

			Hierarchy->DuplicateItems(KeysToDuplicate, true);
		}
		ControlRigBlueprint->PropagateHierarchyFromBPToInstances(true);
	}

	FSlateApplication::Get().DismissAllMenus();
	ControlRigEditor.Pin()->OnHierarchyChanged();
	RefreshTreeView();
}

/** Mirror Item */
void SRigHierarchy::HandleMirrorItem()
{
	FRigHierarchyContainer* Hierarchy = GetHierarchyContainer();
	if (Hierarchy)
	{
		FRigMirrorSettings Settings;
		TSharedPtr<FStructOnScope> StructToDisplay = MakeShareable(new FStructOnScope(FRigMirrorSettings::StaticStruct(), (uint8*)&Settings));

		TSharedRef<SKismetInspector> KismetInspector = SNew(SKismetInspector);
		KismetInspector->ShowSingleStruct(StructToDisplay);

		SGenericDialogWidget::OpenDialog(LOCTEXT("ControlRigHierarchyMirror", "Mirror Hierarchy"), KismetInspector, SGenericDialogWidget::FArguments(), true);

		ClearDetailPanel();
		{
			TGuardValue<bool> GuardRigHierarchyChanges(bIsChangingRigHierarchy, true);
			TGuardValue<bool> SuspendBlueprintNotifs(ControlRigBlueprint->bSuspendAllNotifications, true);

			FScopedTransaction Transaction(LOCTEXT("HierarchyTreeMirrorSelected", "Mirror selected items from hierarchy"));
			ControlRigBlueprint->Modify();

			TArray<TSharedPtr<FRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
			TArray<FRigElementKey> KeysToMirror;
			for (int32 Index = 0; Index < SelectedItems.Num(); ++Index)
			{
				KeysToMirror.Add(SelectedItems[Index]->Key);
			}

			Hierarchy->MirrorItems(KeysToMirror, Settings, true);
		}
		ControlRigBlueprint->PropagateHierarchyFromBPToInstances(true);
	}

	FSlateApplication::Get().DismissAllMenus();
	ControlRigEditor.Pin()->OnHierarchyChanged();
	RefreshTreeView();
}

/** Check whether we can deleting the selected item(s) */
bool SRigHierarchy::CanRenameItem() const
{
	return IsSingleSelected();
}

/** Delete Item */
void SRigHierarchy::HandleRenameItem()
{
	if (!CanRenameItem())
	{
		return;
	}

	FRigHierarchyContainer* Hierarchy = GetHierarchyContainer();
	if (Hierarchy)
	{
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
						FText ConfirmRename = LOCTEXT("RenameDeleteBoneHierarchy", "Renaming imported(white) bones can cause issues with animation - are you sure ?");

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
	if (FRigHierarchyContainer* Hierarchy = GetDebuggedHierarchyContainer())
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
		TGuardValue<bool> GuardRigHierarchyChanges(bIsChangingRigHierarchy, true);
		TGuardValue<bool> SuspendBlueprintNotifs(ControlRigBlueprint->bSuspendAllNotifications, true);

		FString Content;
		FPlatformApplicationMisc::ClipboardPaste(Content);

		FScopedTransaction Transaction(LOCTEXT("HierarchyTreePaste", "Pasted rig elements."));
		ControlRigBlueprint->Modify();

		Hierarchy->ImportFromText(Content, ERigHierarchyImportMode::Append, true);
	}

	ControlRigBlueprint->PropagateHierarchyFromBPToInstances(true);
	ControlRigEditor.Pin()->OnHierarchyChanged();
	RefreshTreeView();
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
	}

	ControlRigBlueprint->PropagateHierarchyFromBPToInstances(false);
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
	}

	ControlRigBlueprint->PropagateHierarchyFromBPToInstances(false);
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
				if (DraggedKey == TargetItem->Key)
				{
					return ReturnDropZone;
				}

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
				ReturnDropZone = EItemDropZone::OntoItem;
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
			TGuardValue<bool> SuspendBlueprintNotifs(ControlRigBlueprint->bSuspendAllNotifications, true);
			FScopedTransaction Transaction(LOCTEXT("HierarchyDragAndDrop", "Drag & Drop"));
			ControlRigBlueprint->Modify();

			for (const FRigElementKey& DraggedKey : RigDragDropOp->GetElements())
			{
				if (DraggedKey == TargetItem->Key)
				{
					return FReply::Unhandled();
				}

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
							FText ConfirmReparent = LOCTEXT("ConfirmReparentBoneHierarchy", "Reparenting imported(white) bones can cause issues with animation - are you sure ?");

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

				FTransform InitialTransform = DebuggedContainer->GetInitialGlobalTransform(DraggedKey);
				FTransform GlobalTransform = DebuggedContainer->GetGlobalTransform(DraggedKey);

				switch (DraggedKey.Type)
				{
					case ERigElementType::Bone:
					{
						if (TargetItem->Key.Type == ERigElementType::Bone)
						{
							Container->BoneHierarchy.Reparent(DraggedKey.Name, TargetItem->Key.Name);
						}
						else if (!TargetItem->Key.IsValid())
						{
							Container->BoneHierarchy.Reparent(DraggedKey.Name, NAME_None);
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
						else if (!ParentKey.IsValid())
						{
							Container->ControlHierarchy.Reparent(DraggedKey.Name, NAME_None);
							Container->ControlHierarchy.SetSpace(DraggedKey.Name, NAME_None);
							if (DebuggedContainer != Container)
							{
								DebuggedContainer->ControlHierarchy.Reparent(DraggedKey.Name, NAME_None);
								DebuggedContainer->ControlHierarchy.SetSpace(DraggedKey.Name, NAME_None);
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
						else if (!ParentKey.IsValid())
						{
							Container->SpaceHierarchy.Reparent(DraggedKey.Name, ERigSpaceType::Global, NAME_None);
							if (DebuggedContainer != Container)
							{
								DebuggedContainer->SpaceHierarchy.Reparent(DraggedKey.Name, ERigSpaceType::Global, NAME_None);
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

				DebuggedContainer->SetInitialGlobalTransform(DraggedKey, InitialTransform);
				DebuggedContainer->SetGlobalTransform(DraggedKey, GlobalTransform);
				Container->SetInitialGlobalTransform(DraggedKey, InitialTransform);
				Container->SetGlobalTransform(DraggedKey, GlobalTransform);
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
				break;
			}
			case ERigElementType::Control:
			{
				Container->ControlHierarchy.Rename(OldKey.Name, NewName);
				break;
			}
			case ERigElementType::Space:
			{
				Container->SpaceHierarchy.Rename(OldKey.Name, NewName);
				break;
			}
			default:
			{
				return false;
			}
		}

		ControlRigBlueprint->PropagateHierarchyFromBPToInstances(true, true);
		return true;
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

void SRigHierarchy::HandleResetTransform(bool bSelectionOnly)
{
	if ((IsMultiSelected() || !bSelectionOnly) && ControlRigEditor.IsValid())
	{
		if (UControlRigBlueprint* Blueprint = ControlRigEditor.Pin()->GetControlRigBlueprint())
		{
			if (FRigHierarchyContainer* DebuggedContainer = GetDebuggedHierarchyContainer())
			{
				FScopedTransaction Transaction(LOCTEXT("HierarchyResetTransforms", "Reset Transforms"));
				Blueprint->Modify();

				TArray<TSharedPtr<FRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
				TArray<FRigElementKey> KeysToReset;
				for (TSharedPtr<FRigTreeElement> SelectedItem : SelectedItems)
				{
					KeysToReset.Add(SelectedItem->Key);
				}

				if (!bSelectionOnly)
				{
					for (const FRigControl& Control : DebuggedContainer->ControlHierarchy)
					{
						KeysToReset.AddUnique(Control.GetElementKey());
					}
				}

				for (FRigElementKey Key : KeysToReset)
				{
					if (Key.Type == ERigElementType::Control)
					{
						FTransform Transform = GetHierarchyContainer()->ControlHierarchy.GetLocalTransform(Key.Name, ERigControlValueType::Initial);
						GetHierarchyContainer()->SetLocalTransform(Key, Transform);
						DebuggedContainer->SetLocalTransform(Key, Transform);
					}

					else if (Key.Type == ERigElementType::Space)
					{
						FTransform InitialTransform = GetHierarchyContainer()->GetInitialTransform(Key);
						GetHierarchyContainer()->SetLocalTransform(Key, InitialTransform);
						DebuggedContainer->SetLocalTransform(Key, InitialTransform);
					}

					else if (Key.Type == ERigElementType::Bone)
					{
						FTransform InitialTransform = GetHierarchyContainer()->GetInitialGlobalTransform(Key);
						GetHierarchyContainer()->SetGlobalTransform(Key, InitialTransform);
						DebuggedContainer->SetGlobalTransform(Key, InitialTransform);

						Blueprint->RemoveTransientControl(Key);
						if (ControlRigEditor.Pin()->PreviewInstance)
						{
							ControlRigEditor.Pin()->PreviewInstance->RemoveBoneModification(Key.Name);
						}
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
							GetHierarchyContainer()->ControlHierarchy.SetLocalTransform(Control.Name, FTransform::Identity, ERigControlValueType::Initial);
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

					FTransform GlobalTransform = GlobalTransforms[Key];
					FTransform LocalTransform = GlobalTransform.GetRelativeTransform(ParentGlobalTransforms[Key]);

					if (Key.Type == ERigElementType::Control )
					{
						GetHierarchyContainer()->ControlHierarchy[Key.Name].OffsetTransform = LocalTransform;
						GetHierarchyContainer()->SetLocalTransform(Key, FTransform::Identity);
						GetHierarchyContainer()->SetInitialTransform(Key, FTransform::Identity);
						DebuggedContainer->ControlHierarchy[Key.Name].OffsetTransform = LocalTransform;
						DebuggedContainer->SetLocalTransform(Key, FTransform::Identity);
						DebuggedContainer->SetInitialTransform(Key, FTransform::Identity);
					}
					else if (Key.Type == ERigElementType::Space)
					{
						GetHierarchyContainer()->SetInitialTransform(Key, LocalTransform);
						GetHierarchyContainer()->SetLocalTransform(Key, LocalTransform);
						DebuggedContainer->SetInitialTransform(Key, LocalTransform);
						DebuggedContainer->SetLocalTransform(Key, LocalTransform);
					}
					else if (Key.Type == ERigElementType::Bone)
					{
						FTransform InitialTransform = GlobalTransform;
						if (ControlRigEditor.Pin()->PreviewInstance)
						{
							if (FAnimNode_ModifyBone* ModifyBone = ControlRigEditor.Pin()->PreviewInstance->FindModifiedBone(Key.Name))
							{
								InitialTransform.SetTranslation(ModifyBone->Translation);
								InitialTransform.SetRotation(FQuat(ModifyBone->Rotation));
								InitialTransform.SetScale3D(ModifyBone->Scale);
								InitialTransform = InitialTransform * ParentGlobalTransforms[Key];
							}
						}
						GetHierarchyContainer()->SetInitialGlobalTransform(Key, InitialTransform);
						Blueprint->PropagateHierarchyFromBPToInstances(false, false);
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
		SetExpansionRecursive(SelectedItem, true, true);
	}

	if (SelectedItems.Num() > 0)
	{
		TreeView->RequestScrollIntoView(SelectedItems.Last());
	}
}

void SRigHierarchy::HandleControlBoneOrSpaceTransform()
{
	UControlRigBlueprint* Blueprint = ControlRigEditor.Pin()->GetControlRigBlueprint();
	if (Blueprint == nullptr)
	{
		return;
	}

	TArray<TSharedPtr<FRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
	for (TSharedPtr<FRigTreeElement> SelectedItem : SelectedItems)
	{
		if (SelectedItem->Key.Type == ERigElementType::Bone ||
			SelectedItem->Key.Type == ERigElementType::Space)
		{
			Blueprint->AddTransientControl(SelectedItem->Key);
			Blueprint->HierarchyContainer.ClearSelection();
			Blueprint->HierarchyContainer.Select(SelectedItem->Key, true);
			return;
		}
	}
}

void SRigHierarchy::HandleUnparent()
{
	UControlRigBlueprint* Blueprint = ControlRigEditor.Pin()->GetControlRigBlueprint();
	if (Blueprint == nullptr)
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("HierarchyTreeUnparentSelected", "Unparent selected items from hierarchy"));
	ControlRigBlueprint->Modify();

	bool bUnparentImportedBones = false;
	bool bConfirmedByUser = false;

	TArray<TSharedPtr<FRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
	for (TSharedPtr<FRigTreeElement> SelectedItem : SelectedItems)
	{
		TGuardValue<bool> GuardRigHierarchyChanges(bIsChangingRigHierarchy, true);
		TGuardValue<bool> SuspendBlueprintNotifs(ControlRigBlueprint->bSuspendAllNotifications, true);

		FRigHierarchyContainer* Container = GetHierarchyContainer();
		FRigBoneHierarchy& BoneHierarchy = Container->BoneHierarchy;
		FRigControlHierarchy& ControlHierarchy = Container->ControlHierarchy;
		FRigSpaceHierarchy& SpaceHierarchy = Container->SpaceHierarchy;

		FTransform InitialTransform = Container->GetInitialGlobalTransform(SelectedItem->Key);
		FTransform GlobalTransform = Container->GetGlobalTransform(SelectedItem->Key);

		switch (SelectedItem->Key.Type)
		{
			case ERigElementType::Bone:
			{
				bool bIsImportedBone = BoneHierarchy[SelectedItem->Key.Name].Type == ERigBoneType::Imported;
				if (bIsImportedBone && !bConfirmedByUser)
				{
					FText ConfirmUnparent = LOCTEXT("ConfirmUnparentBoneHierarchy", "Unparenting imported(white) bones can cause issues with animation - are you sure ?");

					FSuppressableWarningDialog::FSetupInfo Info(ConfirmUnparent, LOCTEXT("UnparentImportedBone", "Unparent Imported Bone"), "UnparentImportedBoneHierarchy_Warning");
					Info.ConfirmText = LOCTEXT("UnparentImportedBoneHierarchy_Yes", "Yes");
					Info.CancelText = LOCTEXT("UnparentImportedBoneHierarchy_No", "No");

					FSuppressableWarningDialog UnparentImportedBonesInHierarchy(Info);
					bUnparentImportedBones = UnparentImportedBonesInHierarchy.ShowModal() != FSuppressableWarningDialog::Cancel;
					bConfirmedByUser = true;
				}

				if (bUnparentImportedBones || !bIsImportedBone)
				{
					BoneHierarchy.Reparent(SelectedItem->Key.Name, NAME_None);
				}
				break;
			}
			case ERigElementType::Space:
			{
				SpaceHierarchy.Reparent(SelectedItem->Key.Name, ERigSpaceType::Global, NAME_None);
				break;
			}
			case ERigElementType::Control:
			{
				ControlHierarchy.Reparent(SelectedItem->Key.Name, NAME_None);
				ControlHierarchy.SetSpace(SelectedItem->Key.Name, NAME_None);
			}
			default:
			{
				break;
			}
		}

		Container->SetInitialGlobalTransform(SelectedItem->Key, InitialTransform);
		Container->SetGlobalTransform(SelectedItem->Key, GlobalTransform);
	}

	ControlRigBlueprint->PropagateHierarchyFromBPToInstances(true);
	ControlRigEditor.Pin()->OnHierarchyChanged();
	RefreshTreeView();
	FSlateApplication::Get().DismissAllMenus();
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
					if (!ClosestTransforms.Contains(Key))
					{
						continue;
					}

					if (Key.Type == ERigElementType::Space || Key.Type == ERigElementType::Bone)
					{
						FTransform LocalTransform = ClosestTransforms[Key].GetRelativeTransform(ParentGlobalTransforms[Key]);
						GetHierarchyContainer()->SetInitialTransform(Key, LocalTransform);
						GetHierarchyContainer()->SetLocalTransform(Key, LocalTransform);
						DebuggedContainer->SetInitialTransform(Key, LocalTransform);
						DebuggedContainer->SetLocalTransform(Key, LocalTransform);
					}
					if (Key.Type == ERigElementType::Control)
					{
						FTransform LocalTransform = ClosestTransforms[Key].GetRelativeTransform(ParentGlobalTransforms[Key]);
						GetHierarchyContainer()->ControlHierarchy[Key.Name].OffsetTransform = LocalTransform;
						GetHierarchyContainer()->SetLocalTransform(Key, FTransform::Identity);
						GetHierarchyContainer()->SetInitialTransform(Key, FTransform::Identity);
						DebuggedContainer->ControlHierarchy[Key.Name].OffsetTransform = LocalTransform;
						DebuggedContainer->SetLocalTransform(Key, FTransform::Identity);
						DebuggedContainer->SetInitialTransform(Key, FTransform::Identity);
					}
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE