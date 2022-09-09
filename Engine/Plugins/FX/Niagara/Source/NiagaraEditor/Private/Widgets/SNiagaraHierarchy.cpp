// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SNiagaraHierarchy.h"
#include "Framework/Commands/GenericCommands.h"
#include "Styling/StyleColors.h"
#include "Styling/AppStyle.h"
#include "ViewModels/HierarchyEditor/NiagaraHierarchyViewModelBase.h"
#include "Widgets/SNiagaraParameterName.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ScopedTransaction.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

#define LOCTEXT_NAMESPACE "NiagaraHierarchy"

void SNiagaraHierarchyCategory::Construct(const FArguments& InArgs,	TSharedPtr<FNiagaraHierarchyCategoryViewModel> InCategory)
{
	Category = InCategory;

	InCategory->GetOnRequestRename().BindSP(this, &SNiagaraHierarchyCategory::EnterEditingMode);
	
	ChildSlot
	[
		SAssignNew(InlineEditableTextBlock, SInlineEditableTextBlock)
		.Style(&FNiagaraEditorStyle::Get().GetWidgetStyle<FInlineEditableTextBlockStyle>("NiagaraEditor.HierarchyEditor.Category"))
		.Text(this, &SNiagaraHierarchyCategory::GetCategoryText)
		.OnTextCommitted(this, &SNiagaraHierarchyCategory::OnRenameCategory)
		.OnVerifyTextChanged(this, &SNiagaraHierarchyCategory::OnVerifyCategoryRename)
		.IsSelected(InArgs._IsSelected)
	];
}

void SNiagaraHierarchyCategory::EnterEditingMode() const
{
	InlineEditableTextBlock->EnterEditingMode();
}

bool SNiagaraHierarchyCategory::OnVerifyCategoryRename(const FText& NewName, FText& OutTooltip) const
{
	TArray<TSharedPtr<FNiagaraHierarchyCategoryViewModel>> Categories;
	Category.Pin()->GetParent().Pin()->GetChildrenViewModelsForType<UNiagaraHierarchyCategory, FNiagaraHierarchyCategoryViewModel>(Categories);

	if(GetCategoryText().ToString() != NewName.ToString())
	{
		TSet<FString> CategoryNames;
		for(const auto& CategoryViewModel : Categories)
		{
			CategoryNames.Add(Cast<UNiagaraHierarchyCategory>(CategoryViewModel->GetDataMutable())->GetCategoryName().ToString());
		}

		if(CategoryNames.Contains(NewName.ToString()))
		{
			OutTooltip = LOCTEXT("HierarchyCategoryCantRename_DuplicateOnLayer", "Another category of the same name already exists on the same hierarchy level!");
			return false;
		}
	}

	return true;
}

FText SNiagaraHierarchyCategory::GetCategoryText() const
{
	return FText::FromName(Cast<UNiagaraHierarchyCategory>(Category.Pin()->GetData())->GetCategoryName());
}

void SNiagaraHierarchyCategory::OnRenameCategory(const FText& NewText, ETextCommit::Type) const
{
	FScopedTransaction Transaction(LOCTEXT("Transaction_Rename_Category", "Renamed hierarchy category"));
	Category.Pin()->GetHierarchyViewModel()->GetHierarchyDataRoot()->Modify();
	
	Cast<UNiagaraHierarchyCategory>(Category.Pin()->GetDataMutable())->SetCategoryName(FName(NewText.ToString()));
}

void SNiagaraHierarchySection::Construct(const FArguments& InArgs, TSharedPtr<FNiagaraHierarchySectionViewModel> InSection, TWeakObjectPtr<UNiagaraHierarchyViewModelBase> InHierarchyViewModel)
{
	SectionViewModel = InSection;
	HierarchyViewModel = InHierarchyViewModel;

	if(SectionViewModel != nullptr)
	{
		SectionViewModel->GetOnRequestRename().BindSP(this, &SNiagaraHierarchySection::EnterEditingMode);

		SDropTarget::FArguments LeftDropTargetArgs;
		SDropTarget::FArguments OntoDropTargetArgs;
		SDropTarget::FArguments RightDropTargetArgs;

		LeftDropTargetArgs
			.OnAllowDrop(this, &SNiagaraHierarchySection::OnCanAcceptSectionDrop, EItemDropZone::AboveItem)
			.OnDropped(this, &SNiagaraHierarchySection::OnDroppedOn, EItemDropZone::AboveItem)
			.VerticalImage(FAppStyle::GetNoBrush())
			.HorizontalImage(FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.Stack.DropTarget.BorderHorizontal"));

		OntoDropTargetArgs
			.OnAllowDrop(this, &SNiagaraHierarchySection::OnCanAcceptDrop)
			.OnDropped(this, &SNiagaraHierarchySection::OnDroppedOn, EItemDropZone::OntoItem);
		
		RightDropTargetArgs
			.OnAllowDrop(this, &SNiagaraHierarchySection::OnCanAcceptSectionDrop, EItemDropZone::BelowItem)
			.OnDropped(this, &SNiagaraHierarchySection::OnDroppedOn, EItemDropZone::BelowItem)
			.VerticalImage(FAppStyle::GetNoBrush())
			.HorizontalImage(FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.Stack.DropTarget.BorderHorizontal"));
		
		ChildSlot
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SNiagaraSectionDragDropTarget)
				.DropTargetArgs(LeftDropTargetArgs)
			]
			+ SHorizontalBox::Slot()
			[
				SNew(SNiagaraSectionDragDropTarget)
				.DropTargetArgs(OntoDropTargetArgs
				.Content()
				[
					SAssignNew(MenuAnchor, SMenuAnchor)
					.OnGetMenuContent(this, &SNiagaraHierarchySection::OnGetMenuContent)
					[
						SNew(SCheckBox)
						.Visibility(EVisibility::HitTestInvisible)
						.Style(FAppStyle::Get(), "DetailsView.SectionButton")
						.OnCheckStateChanged(this, &SNiagaraHierarchySection::OnSectionCheckChanged)
						.IsChecked(this, &SNiagaraHierarchySection::GetSectionCheckState)
						[
							SAssignNew(InlineEditableTextBlock, SInlineEditableTextBlock)
							.IsSelected(InArgs._IsSelected)
							.Text(this, &SNiagaraHierarchySection::GetText)
							.OnTextCommitted(this, &SNiagaraHierarchySection::OnRenameSection)
							.OnVerifyTextChanged(this, &SNiagaraHierarchySection::OnVerifySectionRename)
						]
					]
				])
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SNiagaraSectionDragDropTarget)
				.DropTargetArgs(RightDropTargetArgs)				
			]
		];
	}
	// if this section doesn't represent data, it's the "All" widget
	else
	{
		ChildSlot
		[
			SNew(SDropTarget)
			.OnAllowDrop(this, &SNiagaraHierarchySection::OnCanAcceptDrop)
			.OnDropped(this, &SNiagaraHierarchySection::OnDroppedOn, EItemDropZone::OntoItem)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "DetailsView.SectionButton")
				.OnCheckStateChanged(this, &SNiagaraHierarchySection::OnSectionCheckChanged)
				.IsChecked(this, &SNiagaraHierarchySection::GetSectionCheckState)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AllSection", "All"))
				]
			]			
		];
	}
}

SNiagaraHierarchySection::~SNiagaraHierarchySection()
{
	SectionViewModel.Reset();
}

void SNiagaraHierarchySection::EnterEditingMode() const
{
	InlineEditableTextBlock->EnterEditingMode();
}

bool SNiagaraHierarchySection::OnCanAcceptDrop(TSharedPtr<FDragDropOperation> DragDropOperation) const
{
	if(DragDropOperation->IsOfType<FNiagaraHierarchyDragDropOp>())
	{
		TSharedPtr<FNiagaraHierarchyDragDropOp> HierarchyDragDropOp = StaticCastSharedPtr<FNiagaraHierarchyDragDropOp>(DragDropOperation);
		TWeakPtr<FNiagaraHierarchyItemViewModelBase> DraggedItem = HierarchyDragDropOp->GetDraggedItem();

		if(const UNiagaraHierarchyCategory* Category = Cast<UNiagaraHierarchyCategory>(DraggedItem.Pin()->GetData()))
		{
			return TryGetSectionData() != Category->GetSection();
		}
	}

	return false;
}

bool SNiagaraHierarchySection::OnCanAcceptSectionDrop(TSharedPtr<FDragDropOperation> DragDropOperation, EItemDropZone ItemDropZone) const
{
	if(DragDropOperation->IsOfType<FNiagaraSectionDragDropOp>() && ItemDropZone != EItemDropZone::OntoItem)
	{
		TSharedPtr<FNiagaraSectionDragDropOp> SectionDragDropOp = StaticCastSharedPtr<FNiagaraSectionDragDropOp>(DragDropOperation);
		TWeakPtr<FNiagaraHierarchyItemViewModelBase> DraggedItem = SectionDragDropOp->GetDraggedItem();
		if(UNiagaraHierarchySection* DraggedSection = Cast<UNiagaraHierarchySection>(DraggedItem.Pin()->GetDataMutable()))
		{
			int32 DraggedItemIndex = SectionViewModel->GetHierarchyViewModel()->GetHierarchyDataRoot()->GetSectionIndex(DraggedSection);
			bool bSameSection = TryGetSectionData() == DraggedSection;

			int32 InsertionIndex = INDEX_NONE;
			if(SectionViewModel.IsValid())
			{
				InsertionIndex = SectionViewModel->GetHierarchyViewModel()->GetHierarchyDataRoot()->GetSectionIndex(TryGetSectionData());

				// we add 1 to the insertion index if it's below an item because we either want to insert at the current index to place the item above, or at current+1 for below
				InsertionIndex += ItemDropZone == EItemDropZone::AboveItem ? -1 : 1;
			}

			bool bCanDrop = !bSameSection && DraggedItemIndex != InsertionIndex;			
			return bCanDrop;
		}
	}

	return false;
}

FReply SNiagaraHierarchySection::OnDroppedOn(const FGeometry&, const FDragDropEvent& DragDropEvent, EItemDropZone DropZone) const
{
	if(SectionViewModel.IsValid())
	{
		return SectionViewModel->OnDroppedOn(DragDropEvent, DropZone, SectionViewModel);
	}
	else
	{
		if(TSharedPtr<FNiagaraHierarchyDragDropOp> HierarchyDragDropOp = DragDropEvent.GetOperationAs<FNiagaraHierarchyDragDropOp>())
		{
			if(UNiagaraHierarchyCategory* Category = Cast<UNiagaraHierarchyCategory>(HierarchyDragDropOp->GetDraggedItem().Pin()->GetDataMutable()))
			{
				Category->SetSection(nullptr);
				TArray<UNiagaraHierarchyCategory*> ChildrenCategories;
				Category->GetChildrenOfType<>(ChildrenCategories, true);

				for(UNiagaraHierarchyCategory* ChildCategory : ChildrenCategories)
				{
					ChildCategory->SetSection(nullptr);
				}

				// we only need to reparent if the parent isn't already the root. This stops unnecessary reordering
				if(HierarchyDragDropOp->GetDraggedItem().Pin()->GetParent() != HierarchyViewModel->GetHierarchyViewModelRoot())
				{
					HierarchyViewModel->GetHierarchyViewModelRoot()->ReparentToThis(HierarchyDragDropOp->GetDraggedItem().Pin());
				}
			
				HierarchyViewModel->RefreshHierarchyView();

				return FReply::Handled();
			}
		}
	}

	return FReply::Unhandled();
}

FReply SNiagaraHierarchySection::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if(SectionViewModel.IsValid())
	{
		if(MouseEvent.IsMouseButtonDown(EKeys::RightMouseButton))
		{
			MenuAnchor->SetIsOpen(true);
			OnSectionCheckChanged(ECheckBoxState::Checked);
			return FReply::Handled();
		}
		else if(MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
		{
			OnSectionCheckChanged(ECheckBoxState::Checked);
			return FReply::Handled().DetectDrag(AsShared(), EKeys::LeftMouseButton);	
		}
	}

	return FReply::Unhandled();
}

FReply SNiagaraHierarchySection::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if(MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) && SectionViewModel.IsValid())
	{
		TSharedRef<FNiagaraSectionDragDropOp> SectionDragDropOp = MakeShared<FNiagaraSectionDragDropOp>(SectionViewModel);
		SectionDragDropOp->Construct();
		return FReply::Handled().BeginDragDrop(SectionDragDropOp);
	}
	
	return FReply::Unhandled();
}

void SNiagaraHierarchySection::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	if(TSharedPtr<FNiagaraHierarchyDragDropOp> HierarchyDragDropOp = DragDropEvent.GetOperationAs<FNiagaraHierarchyDragDropOp>())
	{
		HierarchyDragDropOp->SetDescription(FText::GetEmpty());
	}
}

TSharedRef<SWidget> SNiagaraHierarchySection::OnGetMenuContent() const
{
	FMenuBuilder MenuBuilder(true, HierarchyViewModel->GetCommands());
	
	MenuBuilder.AddMenuEntry(FGenericCommands::Get().Rename);
	MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete);

	return MenuBuilder.MakeWidget();
}

UNiagaraHierarchySection* SNiagaraHierarchySection::TryGetSectionData() const
{
	return SectionViewModel.IsValid() ? Cast<UNiagaraHierarchySection>(SectionViewModel->GetDataMutable()) : nullptr;
}

FText SNiagaraHierarchySection::GetText() const
{
	return SectionViewModel->GetSectionNameAsText();
}

void SNiagaraHierarchySection::OnRenameSection(const FText& Text, ETextCommit::Type CommitType) const
{
	FScopedTransaction Transaction(LOCTEXT("Transaction_Rename_Section", "Renamed hierarchy section"));
	HierarchyViewModel->GetHierarchyDataRoot()->Modify();
	
	SectionViewModel->SetSectionNameAsText(Text);
}

bool SNiagaraHierarchySection::OnVerifySectionRename(const FText& NewName, FText& OutTooltip) const
{
	// this function shouldn't be used in case the section isn't valid but we'll make sure regardless
	if(!SectionViewModel.IsValid())
	{
		return false;
	}

	if(SectionViewModel->GetSectionName().ToString() != NewName.ToString())
	{
		TArray<FString> SectionNames;

		SectionNames.Add("All");
		for(auto& Section : HierarchyViewModel->GetHierarchyViewModelRoot()->GetSectionViewModels())
		{
			SectionNames.Add(Section->GetSectionName().ToString());
		}

		if(SectionNames.Contains(NewName.ToString()))
		{
			OutTooltip = LOCTEXT("HierarchySectionCantRename_Duplicate", "A section with that name already exists!");
			return false;
		}
	}

	return true;
}

ECheckBoxState SNiagaraHierarchySection::GetSectionCheckState() const
{
	return HierarchyViewModel->GetActiveSection() == SectionViewModel ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SNiagaraHierarchySection::OnSectionCheckChanged(ECheckBoxState NewState)
{
	if(NewState == ECheckBoxState::Checked)
	{
		HierarchyViewModel->SetActiveSection(SectionViewModel);
	}

	if(SectionViewModel.IsValid())
	{
		HierarchyViewModel->SelectItemInDetailsPanel(SectionViewModel->GetDataMutable());
	}
}

void SNiagaraHierarchy::Construct(const FArguments& InArgs, TObjectPtr<UNiagaraHierarchyViewModelBase> InHierarchyViewModel)
{
	HierarchyViewModel = InHierarchyViewModel;
	
	HierarchyViewModel->OnRefreshSourceView().BindSP(this, &SNiagaraHierarchy::RefreshSourceView);
	HierarchyViewModel->OnRefreshHierarchyView().BindSP(this, &SNiagaraHierarchy::RefreshHierarchyView);
	HierarchyViewModel->OnRefreshSections().BindSP(this, &SNiagaraHierarchy::RefreshSectionsWidget);
	HierarchyViewModel->OnSelectObjectInDetailsPanel().BindSP(this, &SNiagaraHierarchy::SelectObjectInDetailsPanel);
	
	SHorizontalBox::FSlot* DetailsPanelSlot = nullptr;
	
	ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		[
			SAssignNew(SourceTreeView, STreeView<TSharedPtr<FNiagaraHierarchyItemViewModelBase>>)
			.TreeItemsSource(&InHierarchyViewModel->GetSourceItems())
			.OnSelectionChanged(this, &SNiagaraHierarchy::OnSelectionChanged)
			.OnGenerateRow(this, &SNiagaraHierarchy::GenerateSourceItemRow)
			.OnGetChildren_UObject(InHierarchyViewModel.Get(), &UNiagaraHierarchyViewModelBase::OnGetChildren)
			.OnItemToString_Debug_UObject(InHierarchyViewModel.Get(), &UNiagaraHierarchyViewModelBase::OnItemToStringDebug)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SSeparator).Orientation(EOrientation::Orient_Vertical)
		]
		+ SHorizontalBox::Slot()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBox)
					[
						CreateHierarchyButtonWidgets()
					]
				]
				+ SVerticalBox::Slot().Expose(SectionsSlot)
				.AutoHeight()
			]
			+ SVerticalBox::Slot()
			[
				SAssignNew(HierarchyTreeView, STreeView<TSharedPtr<FNiagaraHierarchyItemViewModelBase>>)
				.TreeItemsSource(&InHierarchyViewModel->GetHierarchyItems())
				.OnSelectionChanged(this, &SNiagaraHierarchy::OnSelectionChanged)
				.OnGenerateRow(this, &SNiagaraHierarchy::GenerateHierarchyItemRow)
				.OnGetChildren_UObject(InHierarchyViewModel.Get(), &UNiagaraHierarchyViewModelBase::OnGetChildren)
				.OnItemToString_Debug_UObject(InHierarchyViewModel.Get(), &UNiagaraHierarchyViewModelBase::OnItemToStringDebug)
			]
		]
		+ SHorizontalBox::Slot().Expose(DetailsPanelSlot)		
	];

	if(InHierarchyViewModel->SupportsDetailsPanel())
	{
		FPropertyEditorModule& PropertyEditorModule = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

		FDetailsViewArgs DetailsViewArgs;
		DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::ENameAreaSettings::ObjectsUseNameArea;
		DetailsViewArgs.bShowObjectLabel = true;
		DetailsPanel = PropertyEditorModule.CreateDetailView(DetailsViewArgs);

		for(auto& Customizations : HierarchyViewModel->GetInstanceCustomizations())
		{
			DetailsPanel->RegisterInstancedCustomPropertyLayout(Customizations.Key, Customizations.Value);
		}
		
		DetailsPanelSlot->AttachWidget(DetailsPanel.ToSharedRef());
	}
	
	RefreshSectionsWidget();
	HierarchyViewModel->GetHierarchyViewModelRoot()->GetOnSynced().AddSP(this, &SNiagaraHierarchy::RefreshSectionsWidget);
	
	HierarchyViewModel->GetCommands()->MapAction(FGenericCommands::Get().Rename,
		FExecuteAction::CreateSP(this, &SNiagaraHierarchy::RequestRenameSelectedItem),
		FCanExecuteAction::CreateSP(this, &SNiagaraHierarchy::CanRequestRenameSelectedItem));

	HierarchyViewModel->GetCommands()->MapAction(FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &SNiagaraHierarchy::DeleteSelectedItems),
		FCanExecuteAction::CreateSP(this, &SNiagaraHierarchy::CanDeleteSelectedItems));
}

SNiagaraHierarchy::~SNiagaraHierarchy()
{
	HierarchyViewModel->GetHierarchyViewModelRoot()->GetOnSynced().RemoveAll(this);

	HierarchyViewModel->OnRefreshSourceView().Unbind();
	HierarchyViewModel->OnRefreshHierarchyView().Unbind();
	HierarchyViewModel->OnRefreshSections().Unbind();
	HierarchyViewModel->OnSelectObjectInDetailsPanel().Unbind();

	HierarchyViewModel->GetCommands()->UnmapAction(FGenericCommands::Get().Delete);
	HierarchyViewModel->GetCommands()->UnmapAction(FGenericCommands::Get().Rename);
}

FReply SNiagaraHierarchy::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	return HierarchyViewModel->GetCommands()->ProcessCommandBindings(InKeyEvent) ? FReply::Handled() : FReply::Unhandled();
}

void SNiagaraHierarchy::RefreshSourceView(bool bFullRefresh) const
{
	if(bFullRefresh)
	{
		SourceTreeView->RebuildList();
	}
	else
	{
		SourceTreeView->RequestTreeRefresh();
	}
}

void SNiagaraHierarchy::RefreshHierarchyView(bool bFullRefresh) const
{
	// the top layer objects might have changed due to filtering. We need to refresh these too.
	HierarchyTreeView->SetTreeItemsSource(&HierarchyViewModel->GetHierarchyItems());
	if(bFullRefresh)
	{
		HierarchyTreeView->RebuildList();
	}
	else
	{
		HierarchyTreeView->RequestTreeRefresh();
	}
}

void SNiagaraHierarchy::RefreshSectionsWidget()
{
	SectionsSlot->AttachWidget(CreateSectionWidgets());
}

bool SNiagaraHierarchy::IsItemSelected(TSharedPtr<FNiagaraHierarchyItemViewModelBase> Item) const
{
	return HierarchyTreeView->IsItemSelected(Item);
}

void SNiagaraHierarchy::SelectObjectInDetailsPanel(UObject* Object) const
{
	if(DetailsPanel.IsValid())
	{
		DetailsPanel->SetObject(Object);
	}
}

TSharedRef<SWidget> SNiagaraHierarchy::CreateHierarchyButtonWidgets() const
{
	FSlimHorizontalToolBarBuilder ToolBarBuilder(nullptr, FMultiBoxCustomization::None);

	// Add Category
	{
		FUIAction AddCategoryAction;
		AddCategoryAction.ExecuteAction = FExecuteAction::CreateUObject(HierarchyViewModel.Get(), &UNiagaraHierarchyViewModelBase::AddCategory);
		ToolBarBuilder.AddToolBarButton(AddCategoryAction, NAME_None,
			FText::FromString("Add Category")
			);
	}

	// Add Section
	{
		FUIAction AddSectionAction;
		AddSectionAction.ExecuteAction = FExecuteAction::CreateUObject(HierarchyViewModel.Get(), &UNiagaraHierarchyViewModelBase::AddSection);
		ToolBarBuilder.AddToolBarButton(AddSectionAction, NAME_None,
			FText::FromString("Add Section")
			);
	}

	return ToolBarBuilder.MakeWidget();
}

TSharedRef<SWidget> SNiagaraHierarchy::CreateSectionWidgets()
{
	SectionsWidgetMap.Empty();
	
	TSharedRef<SWrapBox> SectionsBox = SNew(SWrapBox)
		.UseAllottedWidth(true);
	
	for (TSharedPtr<FNiagaraHierarchySectionViewModel>& Section : HierarchyViewModel->GetHierarchyViewModelRoot()->GetSectionViewModels())
	{
		TSharedPtr<SNiagaraHierarchySection> SectionWidget = SNew(SNiagaraHierarchySection, Section, HierarchyViewModel);
		SectionsWidgetMap.Add(Section, SectionWidget);
		
		SectionsBox->AddSlot()
		[
			SectionWidget.ToSharedRef()
		];
	}
	
	TSharedPtr<SNiagaraHierarchySection> DefaultSectionWidget = SNew(SNiagaraHierarchySection, nullptr, HierarchyViewModel);
	SectionsWidgetMap.Add(nullptr, DefaultSectionWidget);

	SectionsBox->AddSlot()
	[
		DefaultSectionWidget.ToSharedRef()
	];
	
	return SNew(SBorder).BorderBackgroundColor(FStyleColors::Recessed)
	[
		SectionsBox
	];
}

TSharedRef<ITableRow> SNiagaraHierarchy::GenerateSourceItemRow(TSharedPtr<FNiagaraHierarchyItemViewModelBase> HierarchyItem, const TSharedRef<STableViewBase>& TableViewBase)
{
	return SNew(STableRow<TSharedPtr<FNiagaraHierarchyItemViewModelBase>>, TableViewBase)
	.OnDragDetected(HierarchyItem.ToSharedRef(), &FNiagaraHierarchyItemViewModelBase::OnDragDetected, true)
	[
		HierarchyViewModel->GenerateRowContentWidget(HierarchyItem.ToSharedRef())
	];
}

TSharedRef<ITableRow> SNiagaraHierarchy::GenerateHierarchyItemRow(TSharedPtr<FNiagaraHierarchyItemViewModelBase> HierarchyItem, const TSharedRef<STableViewBase>& TableViewBase)
{
	return SNew(STableRow<TSharedPtr<FNiagaraHierarchyItemViewModelBase>>, TableViewBase)
	.OnAcceptDrop(HierarchyItem.ToSharedRef(), &FNiagaraHierarchyItemViewModelBase::OnDroppedOn)
	.OnCanAcceptDrop(HierarchyItem.ToSharedRef(), &FNiagaraHierarchyItemViewModelBase::OnCanAcceptDrop)
	.OnDragDetected(HierarchyItem.ToSharedRef(), &FNiagaraHierarchyItemViewModelBase::OnDragDetected, false)
	[
		HierarchyViewModel->GenerateRowContentWidget(HierarchyItem.ToSharedRef())
	];
}

void SNiagaraHierarchy::RequestRenameSelectedItem()
{
	TArray<TSharedPtr<FNiagaraHierarchyItemViewModelBase>> SelectedItems = HierarchyTreeView->GetSelectedItems();

	if(SelectedItems.Num() == 0)
	{
		TSharedPtr<FNiagaraHierarchySectionViewModel> ActiveSection = HierarchyViewModel->GetActiveSection();
		if(ActiveSection)
		{
			SelectedItems = { ActiveSection };
		}
	}
	
	if(SelectedItems.Num() == 1)
	{
		SelectedItems[0]->RequestRename();
	}
}

bool SNiagaraHierarchy::CanRequestRenameSelectedItem() const
{
	TArray<TSharedPtr<FNiagaraHierarchyItemViewModelBase>> SelectedItems = HierarchyTreeView->GetSelectedItems();

	if(SelectedItems.Num() == 0)
	{
		TSharedPtr<FNiagaraHierarchySectionViewModel> ActiveSection = HierarchyViewModel->GetActiveSection();
		if(ActiveSection)
		{
			SelectedItems = { ActiveSection };
		}
	}
	
	if(SelectedItems.Num() == 1)
	{
		return SelectedItems[0]->CanRename();
	}

	return false;
}

void SNiagaraHierarchy::DeleteSelectedItems()
{
	FScopedTransaction Transaction(LOCTEXT("Transaction_DeleteSelectedItems", "Deleted selected hierarchy items"));
	HierarchyViewModel->GetHierarchyDataRoot()->Modify();
	
	TArray<TSharedPtr<FNiagaraHierarchyItemViewModelBase>> SelectedItems = HierarchyTreeView->GetSelectedItems();

	if(SelectedItems.Num() == 0)
	{
		TSharedPtr<FNiagaraHierarchySectionViewModel> ActiveSection = HierarchyViewModel->GetActiveSection();
		if(ActiveSection)
		{
			SelectedItems = { ActiveSection };
		}
	}
	
	for(TSharedPtr<FNiagaraHierarchyItemViewModelBase>& SelectedItem : SelectedItems)
	{
		SelectedItem->Finalize();
		if(SelectedItem->GetParent().IsValid())
		{
			SelectedItem->GetParent().Pin()->SyncToData();
		}
	}

	RefreshSourceView();
	RefreshHierarchyView();
	HierarchyViewModel->OnHierarchyChanged().Broadcast();
}

bool SNiagaraHierarchy::CanDeleteSelectedItems() const
{
	TArray<TSharedPtr<FNiagaraHierarchyItemViewModelBase>> SelectedItems = HierarchyTreeView->GetSelectedItems();

	if(SelectedItems.Num() == 0)
	{
		TSharedPtr<FNiagaraHierarchySectionViewModel> ActiveSection = HierarchyViewModel->GetActiveSection();
		if(ActiveSection)
		{
			SelectedItems = { ActiveSection };
		}
	}

	if(SelectedItems.Num() > 0)
	{
		bool bCanDelete = true;
		for(TSharedPtr<FNiagaraHierarchyItemViewModelBase> SelectedItem : SelectedItems)
		{
			bCanDelete &= SelectedItems[0]->CanDelete();
		}

		return bCanDelete;
	}

	return false;
}

void SNiagaraHierarchy::OnSelectionChanged(TSharedPtr<FNiagaraHierarchyItemViewModelBase> HierarchyItem, ESelectInfo::Type Type) const
{
	HierarchyViewModel->OnSelectionChanged(HierarchyItem);
}

void SNiagaraSectionDragDropTarget::Construct(const FArguments& InArgs)
{
	SDropTarget::Construct(InArgs._DropTargetArgs);
}

FReply SNiagaraSectionDragDropTarget::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	TSharedPtr<FNiagaraHierarchyDragDropOp> HierarchyDragDropOp = DragDropEvent.GetOperationAs<FNiagaraHierarchyDragDropOp>();

	if(HierarchyDragDropOp.IsValid())
	{
		if(AllowDrop(DragDropEvent.GetOperation()))
		{
			if(const UNiagaraHierarchySection* Section = Cast<UNiagaraHierarchySection>(HierarchyDragDropOp->GetDraggedItem().Pin()->GetData()))
			{
				FText Text = LOCTEXT("MoveSectionHere", "The section will be moved here.");
				HierarchyDragDropOp->SetDescription(Text);
			}

			if(const UNiagaraHierarchyCategory* Category = Cast<UNiagaraHierarchyCategory>(HierarchyDragDropOp->GetDraggedItem().Pin()->GetData()))
			{
				FText Text = LOCTEXT("DropCategoryOnSection", "Move category here");
				HierarchyDragDropOp->SetDescription(Text);
			}
		}
		else
		{
			if(const UNiagaraHierarchySection* Section = Cast<UNiagaraHierarchySection>(HierarchyDragDropOp->GetDraggedItem().Pin()->GetData()))
			{
				HierarchyDragDropOp->SetDescription(LOCTEXT("SectionOnSectionNotAllowed", "Can't drop section on a section."));
			}
		}
	}

	return SDropTarget::OnDragOver(MyGeometry, DragDropEvent);
}

void SNiagaraSectionDragDropTarget::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	TSharedPtr<FNiagaraHierarchyDragDropOp> HierarchyDragDropOp = DragDropEvent.GetOperationAs<FNiagaraHierarchyDragDropOp>();

	if(HierarchyDragDropOp.IsValid())
	{
		HierarchyDragDropOp->SetDescription(FText::GetEmpty());
	}
	
	SDropTarget::OnDragLeave(DragDropEvent);
}

#undef LOCTEXT_NAMESPACE
