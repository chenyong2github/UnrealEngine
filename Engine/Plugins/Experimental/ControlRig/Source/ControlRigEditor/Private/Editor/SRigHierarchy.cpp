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
	bIsTransient = false;

	if(InHierarchyHandler.IsValid())
	{
		if(URigHierarchy* Hierarchy = InHierarchyHandler.Pin()->GetDebuggedHierarchy())
		{
			if(FRigControlElement* ControlElement = Hierarchy->Find<FRigControlElement>(Key))
			{
				bIsTransient = ControlElement->Settings.bIsTransientControl;
			}
		}
	}
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

			FRigBoneElement* BoneElement = InHierarchy->ControlRigBlueprint->Hierarchy->Find<FRigBoneElement>(InRigTreeElement->Key);
			if(BoneElement)
			{
				BoneType = BoneElement->BoneType;
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
		case ERigElementType::RigidBody:
		{
			Brush = FControlRigEditorStyle::Get().GetBrush("ControlRig.Tree.RigidBody");
			break;
		}
		case ERigElementType::Socket:
		{
			Brush = FControlRigEditorStyle::Get().GetBrush("ControlRig.Tree.Socket");
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
	if(WeakRigTreeElement.Pin()->bIsTransient)
	{
		static const FText TemporaryControl = FText::FromString(TEXT("Temporary Control"));
		return TemporaryControl;
	}
	return (FText::FromName(WeakRigTreeElement.Pin()->Key.Name));
}

bool SRigHierarchyItem::OnVerifyNameChanged(const FText& InText, FText& OutErrorMessage)
{
	FString NewName = InText.ToString();
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
		FString NewName = InText.ToString();
		FRigElementKey OldKey = WeakRigTreeElement.Pin()->Key;

		if (OnRenameElement.IsBound())
		{
			FName NewSanitizedName = OnRenameElement.Execute(OldKey, NewName);
			if (NewSanitizedName.IsNone())
			{
				return;
			}
			NewName = NewSanitizedName.ToString();
		}

		if (WeakRigTreeElement.IsValid())
		{
			WeakRigTreeElement.Pin()->Key.Name = *NewName;
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
		ControlRigBlueprint->Hierarchy->OnModified().RemoveAll(this);
		ControlRigBlueprint->OnRefreshEditor().RemoveAll(this);
	}
}

void SRigHierarchy::Construct(const FArguments& InArgs, TSharedRef<FControlRigEditor> InControlRigEditor)
{
	ControlRigEditor = InControlRigEditor;

	ControlRigBlueprint = ControlRigEditor.Pin()->GetControlRigBlueprint();

	ControlRigBlueprint->Hierarchy->OnModified().AddRaw(this, &SRigHierarchy::OnHierarchyModified);
	ControlRigBlueprint->OnRefreshEditor().AddRaw(this, &SRigHierarchy::HandleRefreshEditorFromBlueprint);
	ControlRigBlueprint->OnSetObjectBeingDebugged().AddRaw(this, &SRigHierarchy::HandleSetObjectBeingDebugged);

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

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 2.0f, 0.0f)
					[
						SNew(SComboButton)
						.Visibility(EVisibility::Visible)
						.ComboButtonStyle(FEditorStyle::Get(), "GenericFilters.ComboButtonStyle")
						.ForegroundColor(FLinearColor::White)
						.ContentPadding(0.0f)
						.OnGetMenuContent(this, &SRigHierarchy::CreateFilterMenu)
						.ButtonContent()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.TextStyle(FEditorStyle::Get(), "GenericFilters.TextStyle")
								.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.9"))
								.Text(FText::FromString(FString(TEXT("\xf0b0"))) /*fa-filter*/)
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(2, 0, 0, 0)
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.TextStyle(FEditorStyle::Get(), "GenericFilters.TextStyle")
								.Text(LOCTEXT("FilterMenuLabel", "Options"))
							]
						]
					]
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

	bFlattenHierarchyOnFilter = false;
	bHideParentsOnFilter = false;
	bShowImportedBones = true;
	bShowBones = true;
	bShowControls = true;
	bShowSpaces = true;
	bShowRigidBodies = true;
	bShowSockets = true;
	bIsChangingRigHierarchy = false;
	bShowDynamicHierarchy = false;
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
		Commands.SetGizmoTransformFromCurrent,
		FExecuteAction::CreateSP(this, &SRigHierarchy::HandleSetGizmoTransformFromCurrent),
		FCanExecuteAction::CreateSP(this, &SRigHierarchy::IsControlSelected));

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

	CommandList->MapAction(
		Commands.FilteringFlattensHierarchy,
		FExecuteAction::CreateLambda([this]() { bFlattenHierarchyOnFilter = !bFlattenHierarchyOnFilter; RefreshTreeView(); }),
		FCanExecuteAction(),
		FIsActionChecked::CreateLambda([this]() { return bFlattenHierarchyOnFilter; }));

	CommandList->MapAction(
		Commands.HideParentsWhenFiltering,
		FExecuteAction::CreateLambda([this]() { bHideParentsOnFilter = !bHideParentsOnFilter; RefreshTreeView(); }),
		FCanExecuteAction(),
		FIsActionChecked::CreateLambda([this]() { return bHideParentsOnFilter; }));

	CommandList->MapAction(
		Commands.ShowImportedBones,
		FExecuteAction::CreateLambda([this]() { bShowImportedBones = !bShowImportedBones; RefreshTreeView(); }),
		FCanExecuteAction(),
		FIsActionChecked::CreateLambda([this]() { return bShowImportedBones; }));
	
	CommandList->MapAction(
		Commands.ShowBones,
		FExecuteAction::CreateLambda([this]() { bShowBones = !bShowBones; RefreshTreeView(); }),
		FCanExecuteAction(),
		FIsActionChecked::CreateLambda([this]() { return bShowBones; }));

	CommandList->MapAction(
		Commands.ShowControls,
		FExecuteAction::CreateLambda([this]() { bShowControls = !bShowControls; RefreshTreeView(); }),
		FCanExecuteAction(),
		FIsActionChecked::CreateLambda([this]() { return bShowControls; }));
	
	CommandList->MapAction(
		Commands.ShowSpaces,
		FExecuteAction::CreateLambda([this]() { bShowSpaces = !bShowSpaces; RefreshTreeView(); }),
		FCanExecuteAction(),
		FIsActionChecked::CreateLambda([this]() { return bShowSpaces; }));

	CommandList->MapAction(
		Commands.ShowRigidBodies,
		FExecuteAction::CreateLambda([this]() { bShowRigidBodies = !bShowRigidBodies; RefreshTreeView(); }),
		FCanExecuteAction(),
		FIsActionChecked::CreateLambda([this]() { return bShowRigidBodies; }));

	CommandList->MapAction(
		Commands.ShowSockets,
		FExecuteAction::CreateLambda([this]() { bShowSockets = !bShowSockets; RefreshTreeView(); }),
		FCanExecuteAction(),
		FIsActionChecked::CreateLambda([this]() { return bShowSockets; }));

	CommandList->MapAction(
		Commands.ShowDynamicHierarchy,
		FExecuteAction::CreateLambda([this]() { bShowDynamicHierarchy = !bShowDynamicHierarchy; RefreshTreeView(); }),
		FCanExecuteAction(),
		FIsActionChecked::CreateLambda([this]() { return bShowDynamicHierarchy; }));
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
	if (URigHierarchy* Hierarchy = GetHierarchy())
	{
		if (Hierarchy->Num(ERigElementType::Bone) > 0)
		{
			return EVisibility::Collapsed;
		}
	}
	return EVisibility::Visible;
}

EVisibility SRigHierarchy::IsSearchbarVisible() const
{
	if (URigHierarchy* Hierarchy = GetHierarchy())
	{
		if ((Hierarchy->Num(ERigElementType::Bone) +
			Hierarchy->Num(ERigElementType::Space) +
			Hierarchy->Num(ERigElementType::Control)) > 0)
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
		URigHierarchy* Hierarchy = GetHierarchy();

		if(bShowDynamicHierarchy && ControlRigBeingDebuggedPtr.IsValid())
		{
			Hierarchy = ControlRigBeingDebuggedPtr->GetHierarchy();
        }

		check(Hierarchy);

		Hierarchy->Traverse([&](FRigBaseElement* Element, bool& bContinue)
		{
			switch(Element->GetType())
			{
				case ERigElementType::Bone:
				{
					if(bShowBones)
					{
						FRigBoneElement* BoneElement = CastChecked<FRigBoneElement>(Element);
						if (bShowImportedBones || BoneElement->BoneType != ERigBoneType::Imported)
						{
                            AddElement(BoneElement);
                        }
                    }
					break;
				}
				case ERigElementType::Space:
				{
                    if(bShowSpaces)
                    {
                        FRigSpaceElement* SpaceElement = CastChecked<FRigSpaceElement>(Element);
                        AddElement(SpaceElement);
                    }
                    break;
                }
				case ERigElementType::Control:
				{
                    if(bShowControls)
                    {
                        FRigControlElement* ControlElement = CastChecked<FRigControlElement>(Element);
                        AddElement(ControlElement);
                    }
                    break;
                }
				case ERigElementType::RigidBody:
				{
                    if(bShowRigidBodies)
                    {
                        FRigRigidBodyElement* RigidBodyElement = CastChecked<FRigRigidBodyElement>(Element);
                        AddElement(RigidBodyElement);
                    }
                    break;
                }
				case ERigElementType::Socket:
				{
                    if(bShowSockets)
                    {
                        FRigSocketElement* Socket = CastChecked<FRigSocketElement>(Element);
                        AddElement(Socket);
                    }
                    break;
                }
				default:
				{
					break;
				}
            }
		});

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

		TArray<FRigElementKey> Selection = Hierarchy->GetSelectedKeys();
		for (const FRigElementKey& Key : Selection)
		{
			OnHierarchyModified(ERigHierarchyNotification::ElementSelected, Hierarchy, Hierarchy->FindChecked(Key));
		}
	}
}

TArray<FRigElementKey> SRigHierarchy::GetSelectedKeys() const
{
	TArray<TSharedPtr<FRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
	
	TArray<FRigElementKey> SelectedKeys;
	for (const TSharedPtr<FRigTreeElement>& SelectedItem : SelectedItems)
	{
		SelectedKeys.AddUnique(SelectedItem->Key);
	}

	return SelectedKeys;
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

	URigHierarchy* Hierarchy = GetHierarchy();
	if (Hierarchy)
	{
		URigHierarchyController* Controller = Hierarchy->GetController(true);
		check(Controller);
		
		TGuardValue<bool> GuardRigHierarchyChanges(bIsChangingRigHierarchy, true);

		const TArray<FRigElementKey> NewSelection = GetSelectedKeys();
		Controller->SetSelection(NewSelection);

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

void SRigHierarchy::AddElement(FRigElementKey InKey, FRigElementKey InParentKey, const bool bIgnoreTextFilter)
{
	if(ElementMap.Contains(InKey))
	{
		return;
	}

	FString FilteredString = FilterText.ToString();
	if (bIgnoreTextFilter || FilteredString.IsEmpty() || !InKey.IsValid())
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

void SRigHierarchy::AddElement(FRigBaseElement* InElement, const bool bIgnoreTextFilter)
{
	check(InElement);
	
	if (ElementMap.Contains(InElement->GetKey()))
	{
		return;
	}

	const URigHierarchy* Hierarchy = GetHierarchy();
	check(Hierarchy);

	AddElement(InElement->GetKey(), FRigElementKey(), bIgnoreTextFilter);

	if (ElementMap.Contains(InElement->GetKey()))
	{
		const FRigElementKey ParentKey = Hierarchy->GetFirstParent(InElement->GetKey());
		if (ParentKey.IsValid())
		{
			if(ElementMap.Contains(ParentKey))
			{
				ReparentElement(InElement->GetKey(), ParentKey);
			}
		}
	}
}

void SRigHierarchy::AddSpacerElement()
{
	AddElement(FRigElementKey(), FRigElementKey());
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

	if (!FilterText.IsEmpty() && bFlattenHierarchyOnFilter)
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

void SRigHierarchy::OnHierarchyModified(ERigHierarchyNotification InNotif, URigHierarchy* InHierarchy, const FRigBaseElement* InElement)
{
	if (ControlRigBlueprint->bSuspendAllNotifications)
	{
		return;
	}

	if (bIsChangingRigHierarchy)
	{
		return;
	}

	if(InElement)
	{
		if(InElement->IsTypeOf(ERigElementType::Curve))
		{
			return;
		}

		/*
		if(const FRigControlElement* ControlElement = Cast<FRigControlElement>(InElement))
		{
			if(ControlElement->Settings.bIsTransientControl)
			{
				return;
			}
		}
		*/
	}

	switch(InNotif)
	{
		case ERigHierarchyNotification::ElementAdded:
		case ERigHierarchyNotification::ElementRemoved:
		case ERigHierarchyNotification::ElementRenamed:
		case ERigHierarchyNotification::ParentChanged:
		case ERigHierarchyNotification::HierarchyReset:
		{
			RefreshTreeView();
			break;
		}
		case ERigHierarchyNotification::ElementSelected:
		case ERigHierarchyNotification::ElementDeselected:
		{
			check(InElement);

			const bool bSelected = (InNotif == ERigHierarchyNotification::ElementSelected); 
				
			for (int32 RootIndex = 0; RootIndex < RootElements.Num(); ++RootIndex)
			{
				TSharedPtr<FRigTreeElement> Found = FindElement(InElement->GetKey(), RootElements[RootIndex]);
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
		default:
		{
			break;
		}
	}
}

void SRigHierarchy::HandleRefreshEditorFromBlueprint(UControlRigBlueprint* InBlueprint)
{
	if (bIsChangingRigHierarchy)
	{
		return;
	}
	RefreshTreeView();
}

void SRigHierarchy::HandleSetObjectBeingDebugged(UObject* InObject)
{
	if(ControlRigBeingDebuggedPtr.Get() == InObject)
	{
		return;
	}

	if(ControlRigBeingDebuggedPtr.IsValid())
	{
		if(UControlRig* ControlRigBeingDebugged = ControlRigBeingDebuggedPtr.Get())
		{
			if(!ControlRigBeingDebugged->HasAnyFlags(RF_BeginDestroyed))
			{
				ControlRigBeingDebugged->GetHierarchy()->OnModified().RemoveAll(this);
			}
		}
	}

	ControlRigBeingDebuggedPtr.Reset();
	
	if(UControlRig* ControlRig = Cast<UControlRig>(InObject))
	{
		ControlRigBeingDebuggedPtr = ControlRig;
		ControlRig->GetHierarchy()->OnModified().AddSP(this, &SRigHierarchy::OnHierarchyModified);
	}
}

void SRigHierarchy::ClearDetailPanel() const
{
	ControlRigEditor.Pin()->ClearDetailObject();
}

TSharedRef< SWidget > SRigHierarchy::CreateFilterMenu()
{
	const FControlRigHierarchyCommands& Actions = FControlRigHierarchyCommands::Get();

	const bool CloseAfterSelection = true;
	FMenuBuilder MenuBuilder(CloseAfterSelection, CommandList);

	MenuBuilder.BeginSection("FilterOptions", LOCTEXT("OptionsMenuHeading", "Options"));
	{
		MenuBuilder.AddMenuEntry(Actions.FilteringFlattensHierarchy);
		MenuBuilder.AddMenuEntry(Actions.ShowDynamicHierarchy);
		//MenuBuilder.AddMenuEntry(Actions.HideParentsWhenFiltering);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("FilterBones", LOCTEXT("BonesMenuHeading", "Bones"));
	{
		MenuBuilder.AddMenuEntry(Actions.ShowImportedBones);
		MenuBuilder.AddMenuEntry(Actions.ShowBones);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("FilterControls", LOCTEXT("ControlsMenuHeading", "Controls"));
	{
		MenuBuilder.AddMenuEntry(Actions.ShowControls);
		MenuBuilder.AddMenuEntry(Actions.ShowSpaces);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
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
	URigHierarchy* Hierarchy = GetHierarchy();
	check(Hierarchy);

	if (Hierarchy->IsSelected(InItem->Key))
	{
		if (ControlRigEditor.IsValid())
		{
			ControlRigEditor.Pin()->SetDetailStruct(InItem->Key);
		}

		if (InItem->Key.Type == ERigElementType::Bone)
		{
			if(FRigBoneElement* BoneElement = Hierarchy->Find<FRigBoneElement>(InItem->Key))
			{
				if (BoneElement->BoneType == ERigBoneType::Imported)
				{
					return;
				}
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
		MenuBuilder.AddMenuEntry(Actions.SetGizmoTransformFromCurrent);
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
	if (bIsChangingRigHierarchy)
	{
		return;
	}
	TGuardValue<bool> GuardRigHierarchyChanges(bIsChangingRigHierarchy, true);

	URigHierarchy* Hierarchy = GetHierarchy();
	USkeletalMesh* Mesh = Cast<USkeletalMesh>(InAssetData.GetAsset());
	if (Mesh && Hierarchy)
	{
		TGuardValue<bool> SuspendBlueprintNotifs(ControlRigBlueprint->bSuspendAllNotifications, true);

		FScopedTransaction Transaction(LOCTEXT("HierarchyRefresh", "Refresh Transform"));

		// don't select bone if we are in setup mode.
		// we do this to avoid the editmode / viewport gizmos to refresh recursively,
		// which can add an extreme slowdown depending on the number of bones (n^(n-1))
		bool bSelectBones = true;
		if (UControlRig* CurrentRig = ControlRigEditor.Pin()->ControlRig)
		{
			bSelectBones = !CurrentRig->IsSetupModeEnabled();
		}

		const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();

		URigHierarchyController* Controller = Hierarchy->GetController(true);
		check(Controller);
		
		Controller->ImportBones(RefSkeleton, NAME_None, true, true, bSelectBones, true);
	}

	ControlRigBlueprint->PropagateHierarchyFromBPToInstances();
	ControlRigEditor.Pin()->OnHierarchyChanged();
	ControlRigBlueprint->BroadcastRefreshEditor();
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
	if (bIsChangingRigHierarchy)
	{
		return;
	}
	TGuardValue<bool> GuardRigHierarchyChanges(bIsChangingRigHierarchy, true);

	URigHierarchy* Hierarchy = GetHierarchy();
	USkeletalMesh* Mesh = Cast<USkeletalMesh>(InAssetData.GetAsset());
	if (Mesh && Hierarchy)
	{
		TGuardValue<bool> SuspendBlueprintNotifs(ControlRigBlueprint->bSuspendAllNotifications, true);

		FScopedTransaction Transaction(LOCTEXT("HierarchyImport", "Import Hierarchy"));

		// don't select bone if we are in setup mode.
		// we do this to avoid the editmode / viewport gizmos to refresh recursively,
		// which can add an extreme slowdown depending on the number of bones (n^(n-1))
		bool bSelectBones = true;
		if (UControlRig* CurrentRig = ControlRigEditor.Pin()->ControlRig)
		{
			bSelectBones = !CurrentRig->IsSetupModeEnabled();
		}

		const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();

		URigHierarchyController* Controller = Hierarchy->GetController(true);
		check(Controller);

		Controller->ImportBones(RefSkeleton, NAME_None, false, false, bSelectBones, true);
		Controller->ImportCurves(Mesh->GetSkeleton(), NAME_None, true, true);

		ControlRigBlueprint->SourceHierarchyImport = Mesh->GetSkeleton();
		ControlRigBlueprint->SourceCurveImport = Mesh->GetSkeleton();
	}

	ControlRigBlueprint->PropagateHierarchyFromBPToInstances();
	ControlRigEditor.Pin()->OnHierarchyChanged();
	ControlRigBlueprint->BroadcastRefreshEditor();
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
	return GetSelectedKeys().Num() > 0;
}

bool SRigHierarchy::IsSingleSelected() const
{
	return GetSelectedKeys().Num() == 1;
}

bool SRigHierarchy::IsSingleBoneSelected() const
{
	if(!IsSingleSelected())
	{
		return false;
	}
	return GetSelectedKeys()[0].Type == ERigElementType::Bone;
}

bool SRigHierarchy::IsSingleSpaceSelected() const
{
	if(!IsSingleSelected())
	{
		return false;
	}
	return GetSelectedKeys()[0].Type == ERigElementType::Space;
}

bool SRigHierarchy::IsControlSelected() const
{
	TArray<FRigElementKey> SelectedKeys = GetSelectedKeys();
	for (const FRigElementKey& SelectedKey : SelectedKeys)
	{
		if (SelectedKey.Type == ERigElementType::Control)
		{
			return true;
		}
	}
	return false;
}

bool SRigHierarchy::IsControlOrSpaceSelected() const
{
	TArray<FRigElementKey> SelectedKeys = GetSelectedKeys();
	for (const FRigElementKey& SelectedKey : SelectedKeys)
	{
		if (SelectedKey.Type == ERigElementType::Control)
		{
			return true;
		}
		if (SelectedKey.Type == ERigElementType::Space)
		{
			return true;
		}
	}
	return false;
}

void SRigHierarchy::HandleDeleteItem()
{
	URigHierarchy* Hierarchy = GetHierarchy();
 	if (Hierarchy)
 	{
		TArray<FRigElementKey> RemovedItems;

		ClearDetailPanel();
		FScopedTransaction Transaction(LOCTEXT("HierarchyTreeDeleteSelected", "Delete selected items from hierarchy"));

		// clear detail view display
		ControlRigEditor.Pin()->ClearDetailObject();

		bool bConfirmedByUser = false;
		bool bDeleteImportedBones = false;

 		URigHierarchyController* Controller = Hierarchy->GetController(true);
 		check(Controller);

 		TArray<FRigElementKey> SelectedKeys = GetSelectedKeys();
		for (const FRigElementKey& SelectedKey : SelectedKeys)
		{
			TGuardValue<bool> GuardRigHierarchyChanges(bIsChangingRigHierarchy, true);
			TGuardValue<bool> SuspendBlueprintNotifs(ControlRigBlueprint->bSuspendAllNotifications, true);

			if (SelectedKey.Type == ERigElementType::Bone)
			{
				if (FRigBoneElement* BoneElement = Hierarchy->Find<FRigBoneElement>(SelectedKey))
				{
					if (BoneElement->BoneType == ERigBoneType::Imported && BoneElement->ParentElement != nullptr)
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
				}
			}

			Controller->RemoveElement(SelectedKey, true);
			RemovedItems.Add(SelectedKey);
		}
	}

	ControlRigBlueprint->PropagateHierarchyFromBPToInstances();
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
	URigHierarchy* Hierarchy = GetHierarchy();
	if (Hierarchy)
	{
		// unselect current selected item
		ClearDetailPanel();

		URigHierarchyController* Controller = Hierarchy->GetController(true);
		check(Controller);

		FScopedTransaction Transaction(LOCTEXT("HierarchyTreeAdded", "Add new item to hierarchy"));

		FRigElementKey NewItemKey;
		FRigElementKey ParentKey;
		FTransform ParentTransform = FTransform::Identity;

		TArray<FRigElementKey> SelectedKeys = GetSelectedKeys();
		if (SelectedKeys.Num() > 0)
		{
			ParentKey = SelectedKeys[0];
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
					NewItemKey = Controller->AddBone(NewElementName, ParentKey, ParentTransform, true, ERigBoneType::User, true);
					break;
				}
				case ERigElementType::Control:
				{
					FRigControlSettings Settings;
					Settings.ControlType = ERigControlType::EulerTransform;
						
					NewItemKey = Controller->AddControl(NewElementName, ParentKey, Settings, Settings.GetIdentityValue(), FTransform::Identity, FTransform::Identity, true);
					break;
				}
				case ERigElementType::Space:
				{
					NewItemKey = Controller->AddSpace(NewElementName, ParentKey, ParentTransform, true, true);
					break;
				}
				default:
				{
					return;
				}
			}
		}

		Controller->ClearSelection();
		Controller->SelectElement(NewItemKey);
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
	URigHierarchy* Hierarchy = GetHierarchy();
	if (Hierarchy)
	{
		ClearDetailPanel();
		{
			TGuardValue<bool> GuardRigHierarchyChanges(bIsChangingRigHierarchy, true);
			TGuardValue<bool> SuspendBlueprintNotifs(ControlRigBlueprint->bSuspendAllNotifications, true);

			FScopedTransaction Transaction(LOCTEXT("HierarchyTreeDuplicateSelected", "Duplicate selected items from hierarchy"));

			URigHierarchyController* Controller = Hierarchy->GetController(true);
			check(Controller);

			const TArray<FRigElementKey> KeysToDuplicate = GetSelectedKeys();
			Controller->DuplicateElements(KeysToDuplicate);
		}
		ControlRigBlueprint->PropagateHierarchyFromBPToInstances();
	}

	FSlateApplication::Get().DismissAllMenus();
	ControlRigEditor.Pin()->OnHierarchyChanged();
	{
		TGuardValue<bool> GuardRigHierarchyChanges(bIsChangingRigHierarchy, true);
		ControlRigBlueprint->BroadcastRefreshEditor();
	}
	RefreshTreeView();
}

/** Mirror Item */
void SRigHierarchy::HandleMirrorItem()
{
	URigHierarchy* Hierarchy = GetHierarchy();
	if (Hierarchy)
	{
		URigHierarchyController* Controller = Hierarchy->GetController(true);
		check(Controller);
		
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

			const TArray<FRigElementKey> KeysToMirror = GetSelectedKeys();
			const TArray<FRigElementKey> KeysToDuplicate = GetSelectedKeys();
			Controller->MirrorElements(KeysToDuplicate, Settings, true);
		}
		ControlRigBlueprint->PropagateHierarchyFromBPToInstances();
	}

	FSlateApplication::Get().DismissAllMenus();
	ControlRigEditor.Pin()->OnHierarchyChanged();
	RefreshTreeView();
}

/** Check whether we can deleting the selected item(s) */
bool SRigHierarchy::CanRenameItem() const
{
	if(IsSingleSelected())
	{
		const FRigElementKey Key = GetSelectedKeys()[0];
		if(Key.Type == ERigElementType::RigidBody ||
			Key.Type == ERigElementType::Socket)
		{
			return false;
		}
		if(Key.Type == ERigElementType::Control)
		{
			if(URigHierarchy* DebuggedHierarchy = GetDebuggedHierarchy())
			{
				if(FRigControlElement* ControlElement = DebuggedHierarchy->Find<FRigControlElement>(Key))
				{
					if(ControlElement->Settings.bIsTransientControl)
					{
						return false;
					}
				}
			}
		}
		return true;
	}
	return false;
}

/** Delete Item */
void SRigHierarchy::HandleRenameItem()
{
	if (!CanRenameItem())
	{
		return;
	}

	URigHierarchy* Hierarchy = GetHierarchy();
	if (Hierarchy)
	{
		FScopedTransaction Transaction(LOCTEXT("HierarchyTreeRenameSelected", "Rename selected item from hierarchy"));

		TArray<TSharedPtr<FRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
		if (SelectedItems.Num() == 1)
		{
			if (SelectedItems[0]->Key.Type == ERigElementType::Bone)
			{
				if(FRigBoneElement* BoneElement = Hierarchy->Find<FRigBoneElement>(SelectedItems[0]->Key))
				{
					if (BoneElement->BoneType == ERigBoneType::Imported)
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
	if (URigHierarchy* Hierarchy = GetDebuggedHierarchy())
	{
		URigHierarchyController* Controller = Hierarchy->GetController(true);
		check(Controller);

		const TArray<FRigElementKey> Selection = GetHierarchy()->GetSelectedKeys();
		const FString Content = Controller->ExportToText(Selection);
		FPlatformApplicationMisc::ClipboardCopy(*Content);
	}
}

void SRigHierarchy::HandlePasteItems()
{
	if (URigHierarchy* Hierarchy = GetHierarchy())
	{
		TGuardValue<bool> GuardRigHierarchyChanges(bIsChangingRigHierarchy, true);
		TGuardValue<bool> SuspendBlueprintNotifs(ControlRigBlueprint->bSuspendAllNotifications, true);

		FString Content;
		FPlatformApplicationMisc::ClipboardPaste(Content);

		FScopedTransaction Transaction(LOCTEXT("HierarchyTreePaste", "Pasted rig elements."));

		URigHierarchyController* Controller = Hierarchy->GetController(true);
		check(Controller);

		Controller->ImportFromText(Content, false, true, true);
	}

	//ControlRigBlueprint->PropagateHierarchyFromBPToInstances();
	ControlRigEditor.Pin()->OnHierarchyChanged();
	{
		TGuardValue<bool> GuardRigHierarchyChanges(bIsChangingRigHierarchy, true);
		ControlRigBlueprint->BroadcastRefreshEditor();
	}
	RefreshTreeView();
}

void SRigHierarchy::HandlePasteLocalTransforms()
{
	HandlePasteTransforms(ERigTransformType::CurrentLocal, true);
}

void SRigHierarchy::HandlePasteGlobalTransforms()
{
	HandlePasteTransforms(ERigTransformType::CurrentGlobal, false);
}

void SRigHierarchy::HandlePasteTransforms(ERigTransformType::Type InTransformType, bool bAffectChildren)
{
	if (URigHierarchy* Hierarchy = GetHierarchy())
	{
		FString Content;
		FPlatformApplicationMisc::ClipboardPaste(Content);

		FScopedTransaction Transaction(LOCTEXT("HierarchyTreePaste", "Pasted transforms."));

		FRigHierarchyCopyPasteContent Data;
		FRigHierarchyCopyPasteContent::StaticStruct()->ImportText(*Content, &Data, nullptr, EPropertyPortFlags::PPF_None, nullptr, FRigHierarchyCopyPasteContent::StaticStruct()->GetName(), true);

		URigHierarchy* DebuggedHierarchy = GetDebuggedHierarchy();

		const TArray<FRigElementKey> CurrentSelection = Hierarchy->GetSelectedKeys();
		const int32 Count = FMath::Min<int32>(CurrentSelection.Num(), Data.Elements.Num());
		for(int32 Index = 0; Index < Count; Index++)
		{
			const FRigHierarchyCopyPasteContentPerElement& PerElementData = Data.Elements[Index];
			const FTransform Transform =  PerElementData.Pose.Get(InTransformType);

			if(FRigTransformElement* TransformElement = Hierarchy->Find<FRigTransformElement>(CurrentSelection[Index]))
			{
				Hierarchy->SetTransform(TransformElement, Transform, InTransformType, bAffectChildren, true);
			}
			if(FRigBoneElement* BoneElement = Hierarchy->Find<FRigBoneElement>(CurrentSelection[Index]))
			{
				Hierarchy->SetTransform(BoneElement, Transform, ERigTransformType::MakeInitial(InTransformType), bAffectChildren, true);
			}
			
            if(DebuggedHierarchy && DebuggedHierarchy != Hierarchy)
            {
            	if(FRigTransformElement* TransformElement = DebuggedHierarchy->Find<FRigTransformElement>(CurrentSelection[Index]))
            	{
            		DebuggedHierarchy->SetTransform(TransformElement, Transform, InTransformType, bAffectChildren, true);
            	}
            	if(FRigBoneElement* BoneElement = DebuggedHierarchy->Find<FRigBoneElement>(CurrentSelection[Index]))
            	{
            		DebuggedHierarchy->SetTransform(BoneElement, Transform, ERigTransformType::MakeInitial(InTransformType), bAffectChildren, true);
            	}
            }
		}
	}
}

URigHierarchy* SRigHierarchy::GetHierarchy() const
{
	if (ControlRigBlueprint.IsValid())
	{
		return ControlRigBlueprint->Hierarchy;
	}
	return nullptr;
}

URigHierarchy* SRigHierarchy::GetDebuggedHierarchy() const
{
	if (ControlRigBlueprint.IsValid())
	{
		if (UControlRig* DebuggedRig = ControlRigBeingDebuggedPtr.Get())
		{
			return DebuggedRig->GetHierarchy();
		}
	}
	if (ControlRigEditor.IsValid())
	{
		if (UControlRig* CurrentRig = ControlRigEditor.Pin()->ControlRig)
		{
			return CurrentRig->GetHierarchy();
		}
	}
	return GetHierarchy();
}

FName SRigHierarchy::CreateUniqueName(const FName& InBaseName, ERigElementType InElementType) const
{
	return GetHierarchy()->GetSafeNewName(InBaseName.ToString(), InElementType);
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
	TArray<FRigElementKey> DraggedElements = GetSelectedKeys();
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
		URigHierarchy* Hierarchy = GetHierarchy();
		if (Hierarchy)
		{
			for (const FRigElementKey& DraggedKey : RigDragDropOp->GetElements())
			{
				if (DraggedKey == TargetItem->Key)
				{
					return ReturnDropZone;
				}

				if(Hierarchy->IsParentedTo(TargetItem->Key, DraggedKey))
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
			case ERigElementType::RigidBody:
			case ERigElementType::Socket:
			{
				for (const FRigElementKey& DraggedKey : RigDragDropOp->GetElements())
				{
					switch (DraggedKey.Type)
					{
						case ERigElementType::Control:
						case ERigElementType::Space:
						case ERigElementType::RigidBody:
						case ERigElementType::Socket:
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
	bool bMatchTransforms = DragDropEvent.GetModifierKeys().IsAltDown();
	bool bReparentItems = !bMatchTransforms;

	TSharedPtr<FRigElementHierarchyDragDropOp> RigDragDropOp = DragDropEvent.GetOperationAs<FRigElementHierarchyDragDropOp>();
	if (RigDragDropOp.IsValid())
	{
		URigHierarchy* Hierarchy = GetHierarchy();
		check(Hierarchy);
		URigHierarchy* DebuggedHierarchy = GetDebuggedHierarchy();
		check(DebuggedHierarchy);
		URigHierarchyController* Controller = Hierarchy->GetController(true);
		check(Controller);

		if (Hierarchy && ControlRigBlueprint.IsValid())
		{
			TGuardValue<bool> GuardRigHierarchyChanges(bIsChangingRigHierarchy, true);
			TGuardValue<bool> SuspendBlueprintNotifs(ControlRigBlueprint->bSuspendAllNotifications, true);
			FScopedTransaction Transaction(LOCTEXT("HierarchyDragAndDrop", "Drag & Drop"));

			FTransform TargetGlobalTransform = DebuggedHierarchy->GetGlobalTransform(TargetItem->Key);

			for (const FRigElementKey& DraggedKey : RigDragDropOp->GetElements())
			{
				if (DraggedKey == TargetItem->Key)
				{
					return FReply::Unhandled();
				}

				if (bReparentItems)
				{
					if(Hierarchy->IsParentedTo(TargetItem->Key, DraggedKey))
					{
						return FReply::Unhandled();
					}
				}

				if (DraggedKey.Type == ERigElementType::Bone)
				{
					if(FRigBoneElement* BoneElement = Hierarchy->Find<FRigBoneElement>(DraggedKey))
					{
						if (BoneElement->BoneType == ERigBoneType::Imported && BoneElement->ParentElement != nullptr)
						{
							FText ConfirmText = bMatchTransforms ?
								LOCTEXT("ConfirmMatchTransform", "Matching transforms of imported(white) bones can cause issues with animation - are you sure ?") :
								LOCTEXT("ConfirmReparentBoneHierarchy", "Reparenting imported(white) bones can cause issues with animation - are you sure ?");

							FText TitleText = bMatchTransforms ?
								LOCTEXT("MatchTransformImportedBone", "Match Transform on Imported Bone") :
								LOCTEXT("ReparentImportedBone", "Reparent Imported Bone");

							FSuppressableWarningDialog::FSetupInfo Info(ConfirmText, TitleText, "SRigHierarchy_Warning");
							Info.ConfirmText = LOCTEXT("SRigHierarchy_Warning_Yes", "Yes");
							Info.CancelText = LOCTEXT("SRigHierarchy_Warning_No", "No");

							FSuppressableWarningDialog ChangeImportedBonesInHierarchy(Info);
							if (ChangeImportedBonesInHierarchy.ShowModal() == FSuppressableWarningDialog::Cancel)
							{
								return FReply::Unhandled();
							}
						}
					}
				}
			}

			for (const FRigElementKey& DraggedKey : RigDragDropOp->GetElements())
			{
				if (bMatchTransforms)
				{
					if (DraggedKey.Type == ERigElementType::Control)
					{
						int32 ControlIndex = DebuggedHierarchy->GetIndex(DraggedKey);
						if (ControlIndex == INDEX_NONE)
						{
							continue;
						}

						FTransform ParentTransform = DebuggedHierarchy->GetParentTransformByIndex(ControlIndex, false);
						FTransform OffsetTransform = TargetGlobalTransform.GetRelativeTransform(ParentTransform);

						Hierarchy->SetControlOffsetTransformByIndex(ControlIndex, OffsetTransform, ERigTransformType::InitialLocal, true, true);
						Hierarchy->SetLocalTransform(DraggedKey, FTransform::Identity, true, true, true);
						Hierarchy->SetInitialLocalTransform(DraggedKey, FTransform::Identity, true, true);
						DebuggedHierarchy->SetControlOffsetTransformByIndex(ControlIndex, OffsetTransform, ERigTransformType::InitialLocal, true, true);
						DebuggedHierarchy->SetLocalTransform(DraggedKey, FTransform::Identity, true, true, true);
						DebuggedHierarchy->SetInitialLocalTransform(DraggedKey, FTransform::Identity, true, true);
					}
					else
					{
						Hierarchy->SetInitialGlobalTransform(DraggedKey, TargetGlobalTransform, true, true);
						Hierarchy->SetGlobalTransform(DraggedKey, TargetGlobalTransform, false, true, true);
						DebuggedHierarchy->SetInitialGlobalTransform(DraggedKey, TargetGlobalTransform, true, true);
						DebuggedHierarchy->SetGlobalTransform(DraggedKey, TargetGlobalTransform, false, true, true);
					}
					continue;
				}

				FRigElementKey ParentKey = TargetItem->Key;

				FTransform InitialTransform = DebuggedHierarchy->GetInitialGlobalTransform(DraggedKey);
				FTransform GlobalTransform = DebuggedHierarchy->GetGlobalTransform(DraggedKey);

				if(ParentKey.IsValid())
				{
					Controller->SetParent(DraggedKey, ParentKey, true, true);
				}
				else
				{
					Controller->RemoveAllParents(DraggedKey, true, true);
				}

				DebuggedHierarchy->SetInitialGlobalTransform(DraggedKey, InitialTransform, true, true);
				DebuggedHierarchy->SetGlobalTransform(DraggedKey, GlobalTransform, false, true, true);
				Hierarchy->SetInitialGlobalTransform(DraggedKey, InitialTransform, true, true);
				Hierarchy->SetGlobalTransform(DraggedKey, GlobalTransform, false, true, true);
			}
		}

		ControlRigBlueprint->PropagateHierarchyFromBPToInstances();

		if(bReparentItems)
		{
			TGuardValue<bool> GuardRigHierarchyChanges(bIsChangingRigHierarchy, true);
			ControlRigBlueprint->BroadcastRefreshEditor();
			RefreshTreeView();
		}

		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FName SRigHierarchy::RenameElement(const FRigElementKey& OldKey, const FString& NewName)
{
	ClearDetailPanel();

	if (OldKey.Name.ToString() == NewName)
	{
		return NAME_None;
	}

	// make sure there is no duplicate
	if (ControlRigBlueprint.IsValid())
	{
		FScopedTransaction Transaction(LOCTEXT("HierarchyRename", "Rename Hierarchy Element"));

		URigHierarchy* Hierarchy = GetHierarchy();
		URigHierarchyController* Controller = Hierarchy->GetController(true);
		check(Controller);

		FString SanitizedNameStr = NewName;
		Hierarchy->SanitizeName(SanitizedNameStr);
		const FName SanitizedName = *SanitizedNameStr;
		FName ResultingName = NAME_None;

		ResultingName = Controller->RenameElement(OldKey, SanitizedName, true).Name;
		ControlRigBlueprint->PropagateHierarchyFromBPToInstances();
		return ResultingName;
	}

	return NAME_None;
}

bool SRigHierarchy::OnVerifyNameChanged(const FRigElementKey& OldKey, const FString& NewName, FText& OutErrorMessage)
{
	if (OldKey.Name.ToString() == NewName)
	{
		return true;
	}

	// make sure there is no duplicate
	if (ControlRigBlueprint.IsValid())
	{
		URigHierarchy* Hierarchy = GetHierarchy();

		FString OutErrorString;
		if (!Hierarchy->IsNameAvailable(NewName, OldKey.Type, &OutErrorString))
		{
			OutErrorMessage = FText::FromString(OutErrorString);
			return false;
		}
	}
	return true;
}

void SRigHierarchy::HandleResetTransform(bool bSelectionOnly)
{
	if ((IsMultiSelected() || !bSelectionOnly) && ControlRigEditor.IsValid())
	{
		if (UControlRigBlueprint* Blueprint = ControlRigEditor.Pin()->GetControlRigBlueprint())
		{
			if (URigHierarchy* DebuggedHierarchy = GetDebuggedHierarchy())
			{
				FScopedTransaction Transaction(LOCTEXT("HierarchyResetTransforms", "Reset Transforms"));

				TArray<FRigElementKey> KeysToReset = GetSelectedKeys();
				if (!bSelectionOnly)
				{
					KeysToReset = DebuggedHierarchy->GetAllKeys(true, ERigElementType::Control);
				}

				for (FRigElementKey Key : KeysToReset)
				{
					const FTransform InitialTransform = GetHierarchy()->GetInitialLocalTransform(Key);
					GetHierarchy()->SetLocalTransform(Key, InitialTransform, false, true, true);
					DebuggedHierarchy->SetLocalTransform(Key, InitialTransform, false, true, true);
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
			if (URigHierarchy* DebuggedHierarchy = GetDebuggedHierarchy())
			{
				FScopedTransaction Transaction(LOCTEXT("HierarchySetInitialTransforms", "Set Initial Transforms"));

				TArray<FRigElementKey> SelectedKeys = GetSelectedKeys();
				TMap<FRigElementKey, FTransform> GlobalTransforms;
				TMap<FRigElementKey, FTransform> ParentGlobalTransforms;

				for (const FRigElementKey& SelectedKey : SelectedKeys)
				{
					GlobalTransforms.FindOrAdd(SelectedKey) = DebuggedHierarchy->GetGlobalTransform(SelectedKey);
					ParentGlobalTransforms.FindOrAdd(SelectedKey) = DebuggedHierarchy->GetParentTransform(SelectedKey);
				}

				for (const FRigElementKey& SelectedKey : SelectedKeys)
				{
					FTransform GlobalTransform = GlobalTransforms[SelectedKey];
					FTransform LocalTransform = GlobalTransform.GetRelativeTransform(ParentGlobalTransforms[SelectedKey]);

					if (SelectedKey.Type == ERigElementType::Control)
					{
						if(FRigControlElement* ControlElement = GetHierarchy()->Find<FRigControlElement>(SelectedKey))
						{
							GetHierarchy()->SetControlOffsetTransform(ControlElement, LocalTransform, ERigTransformType::InitialLocal, true, true);
							GetHierarchy()->SetControlOffsetTransform(ControlElement, LocalTransform, ERigTransformType::CurrentLocal, true, true);
							GetHierarchy()->SetTransform(ControlElement, FTransform::Identity, ERigTransformType::InitialLocal, true, true);
							GetHierarchy()->SetTransform(ControlElement, FTransform::Identity, ERigTransformType::CurrentLocal, true, true);
						}
						if(FRigControlElement* ControlElement = DebuggedHierarchy->Find<FRigControlElement>(SelectedKey))
						{
							DebuggedHierarchy->SetControlOffsetTransform(ControlElement, LocalTransform, ERigTransformType::InitialLocal, true, true);
							DebuggedHierarchy->SetControlOffsetTransform(ControlElement, LocalTransform, ERigTransformType::CurrentLocal, true, true);
							DebuggedHierarchy->SetTransform(ControlElement, FTransform::Identity, ERigTransformType::InitialLocal, true, true);
							DebuggedHierarchy->SetTransform(ControlElement, FTransform::Identity, ERigTransformType::CurrentLocal, true, true);
						}
					}
					else if (SelectedKey.Type == ERigElementType::Space ||
						SelectedKey.Type == ERigElementType::Bone)
					{
						FTransform InitialTransform = LocalTransform;
						if (ControlRigEditor.Pin()->PreviewInstance)
						{
							if (FAnimNode_ModifyBone* ModifyBone = ControlRigEditor.Pin()->PreviewInstance->FindModifiedBone(SelectedKey.Name))
							{
								InitialTransform.SetTranslation(ModifyBone->Translation);
								InitialTransform.SetRotation(FQuat(ModifyBone->Rotation));
								InitialTransform.SetScale3D(ModifyBone->Scale);
							}
						}

						if(FRigTransformElement* TransformElement = GetHierarchy()->Find<FRigTransformElement>(SelectedKey))
						{
							GetHierarchy()->SetTransform(TransformElement, LocalTransform, ERigTransformType::InitialLocal, true, true);
							GetHierarchy()->SetTransform(TransformElement, LocalTransform, ERigTransformType::CurrentLocal, true, true);
						}
						if(FRigTransformElement* TransformElement = DebuggedHierarchy->Find<FRigTransformElement>(SelectedKey))
						{
							DebuggedHierarchy->SetTransform(TransformElement, LocalTransform, ERigTransformType::InitialLocal, true, true);
							DebuggedHierarchy->SetTransform(TransformElement, LocalTransform, ERigTransformType::CurrentLocal, true, true);
						}
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

	TArray<FRigElementKey> SelectedKeys = GetSelectedKeys();
	for (const FRigElementKey& SelectedKey : SelectedKeys)
	{
		if (SelectedKey.Type == ERigElementType::Bone ||
			SelectedKey.Type == ERigElementType::Space)
		{
			Blueprint->AddTransientControl(SelectedKey);
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

	bool bUnparentImportedBones = false;
	bool bConfirmedByUser = false;

	TArray<FRigElementKey> SelectedKeys = GetSelectedKeys();
	TMap<FRigElementKey, FTransform> InitialTransforms;
	TMap<FRigElementKey, FTransform> GlobalTransforms;

	for (const FRigElementKey& SelectedKey : SelectedKeys)
	{
		URigHierarchy* Hierarchy = GetHierarchy();
		InitialTransforms.Add(SelectedKey, Hierarchy->GetInitialGlobalTransform(SelectedKey));
		GlobalTransforms.Add(SelectedKey, Hierarchy->GetGlobalTransform(SelectedKey));
	}

	for (const FRigElementKey& SelectedKey : SelectedKeys)
	{
		TGuardValue<bool> GuardRigHierarchyChanges(bIsChangingRigHierarchy, true);
		TGuardValue<bool> SuspendBlueprintNotifs(ControlRigBlueprint->bSuspendAllNotifications, true);

		URigHierarchy* Hierarchy = GetHierarchy();
		check(Hierarchy);

		URigHierarchyController* Controller = Hierarchy->GetController(true);
		check(Controller);

		const FTransform& InitialTransform = InitialTransforms.FindChecked(SelectedKey);
		const FTransform& GlobalTransform = GlobalTransforms.FindChecked(SelectedKey);

		switch (SelectedKey.Type)
		{
			case ERigElementType::Bone:
			{
				
				bool bIsImportedBone = false;
				if(FRigBoneElement* BoneElement = Hierarchy->Find<FRigBoneElement>(SelectedKey))
				{
					bIsImportedBone = BoneElement->BoneType == ERigBoneType::Imported;
				}
					
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
					Controller->RemoveAllParents(SelectedKey, true, true);
				}
				break;
			}
			case ERigElementType::Space:
			case ERigElementType::Control:
			{
				Controller->RemoveAllParents(SelectedKey, true, true);
				break;
			}
			default:
			{
				break;
			}
		}

		Hierarchy->SetInitialGlobalTransform(SelectedKey, InitialTransform, true, true);
		Hierarchy->SetGlobalTransform(SelectedKey, GlobalTransform, false, true, true);
	}

	ControlRigBlueprint->PropagateHierarchyFromBPToInstances();
	ControlRigEditor.Pin()->OnHierarchyChanged();
	RefreshTreeView();
	FSlateApplication::Get().DismissAllMenus();
}

bool SRigHierarchy::FindClosestBone(const FVector& Point, FName& OutRigElementName, FTransform& OutGlobalTransform) const
{
	if (URigHierarchy* DebuggedHierarchy = GetDebuggedHierarchy())
	{
		float NearestDistance = BIG_NUMBER;

		DebuggedHierarchy->ForEach<FRigBoneElement>([&] (FRigBoneElement* Element) -> bool
		{
			const FTransform CurTransform = DebuggedHierarchy->GetTransform(Element, ERigTransformType::CurrentGlobal);
            const float CurDistance = FVector::Distance(CurTransform.GetLocation(), Point);
            if (CurDistance < NearestDistance)
            {
                NearestDistance = CurDistance;
                OutGlobalTransform = CurTransform;
                OutRigElementName = Element->GetName();
            }
            return true;
		});

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
			if (URigHierarchy* DebuggedHierarchy = GetDebuggedHierarchy())
			{
				FScopedTransaction Transaction(LOCTEXT("HierarchySetInitialTransforms", "Set Initial Transforms"));

				TArray<FRigElementKey> SelectedKeys = GetSelectedKeys();
				TMap<FRigElementKey, FTransform> ClosestTransforms;
				TMap<FRigElementKey, FTransform> ParentGlobalTransforms;

				for (const FRigElementKey& SelectedKey : SelectedKeys)
				{
					if (SelectedKey.Type == ERigElementType::Control || SelectedKey.Type == ERigElementType::Space)
					{
						const FTransform GlobalTransform = DebuggedHierarchy->GetGlobalTransform(SelectedKey);
						FTransform ClosestTransform;
						FName ClosestRigElement;

						if (!FindClosestBone(GlobalTransform.GetLocation(), ClosestRigElement, ClosestTransform))
						{
							continue;
						}

						ClosestTransforms.FindOrAdd(SelectedKey) = ClosestTransform;
						ParentGlobalTransforms.FindOrAdd(SelectedKey) = DebuggedHierarchy->GetParentTransform(SelectedKey);
					}
				}

				for (const FRigElementKey& SelectedKey : SelectedKeys)
				{
					if (!ClosestTransforms.Contains(SelectedKey))
					{
						continue;
					}
					FTransform GlobalTransform = ClosestTransforms[SelectedKey];
					FTransform LocalTransform = GlobalTransform.GetRelativeTransform(ParentGlobalTransforms[SelectedKey]);

					if (SelectedKey.Type == ERigElementType::Control)
					{
						if(FRigControlElement* ControlElement = GetHierarchy()->Find<FRigControlElement>(SelectedKey))
						{
							GetHierarchy()->SetControlOffsetTransform(ControlElement, LocalTransform, ERigTransformType::InitialLocal, true, true);
							GetHierarchy()->SetControlOffsetTransform(ControlElement, LocalTransform, ERigTransformType::CurrentLocal, true, true);
							GetHierarchy()->SetTransform(ControlElement, FTransform::Identity, ERigTransformType::InitialLocal, true, true);
							GetHierarchy()->SetTransform(ControlElement, FTransform::Identity, ERigTransformType::CurrentLocal, true, true);
						}
						if(FRigControlElement* ControlElement = DebuggedHierarchy->Find<FRigControlElement>(SelectedKey))
						{
							DebuggedHierarchy->SetControlOffsetTransform(ControlElement, LocalTransform, ERigTransformType::InitialLocal, true, true);
							DebuggedHierarchy->SetControlOffsetTransform(ControlElement, LocalTransform, ERigTransformType::CurrentLocal, true, true);
							DebuggedHierarchy->SetTransform(ControlElement, FTransform::Identity, ERigTransformType::InitialLocal, true, true);
							DebuggedHierarchy->SetTransform(ControlElement, FTransform::Identity, ERigTransformType::CurrentLocal, true, true);
						}
					}
					else if (SelectedKey.Type == ERigElementType::Space ||
                        SelectedKey.Type == ERigElementType::Bone)
					{
						FTransform InitialTransform = LocalTransform;
						if (ControlRigEditor.Pin()->PreviewInstance)
						{
							if (FAnimNode_ModifyBone* ModifyBone = ControlRigEditor.Pin()->PreviewInstance->FindModifiedBone(SelectedKey.Name))
							{
								InitialTransform.SetTranslation(ModifyBone->Translation);
								InitialTransform.SetRotation(FQuat(ModifyBone->Rotation));
								InitialTransform.SetScale3D(ModifyBone->Scale);
							}
						}

						if(FRigTransformElement* TransformElement = GetHierarchy()->Find<FRigTransformElement>(SelectedKey))
						{
							GetHierarchy()->SetTransform(TransformElement, LocalTransform, ERigTransformType::InitialLocal, true, true);
							GetHierarchy()->SetTransform(TransformElement, LocalTransform, ERigTransformType::CurrentLocal, true, true);
						}
						if(FRigTransformElement* TransformElement = DebuggedHierarchy->Find<FRigTransformElement>(SelectedKey))
						{
							DebuggedHierarchy->SetTransform(TransformElement, LocalTransform, ERigTransformType::InitialLocal, true, true);
							DebuggedHierarchy->SetTransform(TransformElement, LocalTransform, ERigTransformType::CurrentLocal, true, true);
						}
					}
				}
			}
		}
	}
}

void SRigHierarchy::HandleSetGizmoTransformFromCurrent()
{
	if (IsControlSelected())
	{
		if (UControlRigBlueprint* Blueprint = ControlRigEditor.Pin()->GetControlRigBlueprint())
		{
			if (URigHierarchy* DebuggedHierarchy = GetDebuggedHierarchy())
			{
				FScopedTransaction Transaction(LOCTEXT("HierarchySetGizmoTransforms", "Set Gizmo Transforms"));

				TArray<TSharedPtr<FRigTreeElement>> SelectedItems = TreeView->GetSelectedItems();
				for (const TSharedPtr<FRigTreeElement>& SelectedItem : SelectedItems)
				{
					if(FRigControlElement* ControlElement = DebuggedHierarchy->Find<FRigControlElement>(SelectedItem->Key))
					{
						const FRigElementKey Key = ControlElement->GetKey();
						
						if (ControlElement->Settings.bGizmoEnabled)
						{
							const FTransform OffsetGlobalTransform = DebuggedHierarchy->GetGlobalControlOffsetTransform(Key); 
							const FTransform GizmoGlobalTransform = DebuggedHierarchy->GetGlobalControlGizmoTransform(Key);
							const FTransform GizmoLocalTransform = GizmoGlobalTransform.GetRelativeTransform(OffsetGlobalTransform);
							
							DebuggedHierarchy->SetControlGizmoTransform(Key, GizmoLocalTransform, true, true);
							DebuggedHierarchy->SetControlGizmoTransform(Key, GizmoLocalTransform, false, true);
							GetHierarchy()->SetControlGizmoTransform(Key, GizmoLocalTransform, true, true);
							GetHierarchy()->SetControlGizmoTransform(Key, GizmoLocalTransform, false, true);

							DebuggedHierarchy->SetLocalTransform(Key, FTransform::Identity, false, true, true);
							DebuggedHierarchy->SetLocalTransform(Key, FTransform::Identity, true, true, true);
							GetHierarchy()->SetLocalTransform(Key, FTransform::Identity, false, true, true);
							GetHierarchy()->SetLocalTransform(Key, FTransform::Identity, true, true, true);
						}

						if (FControlRigEditorEditMode* EditMode = ControlRigEditor.Pin()->GetEditMode())
						{
							EditMode->RequestToRecreateGizmoActors();
						}
					}
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE