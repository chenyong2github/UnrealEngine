// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/SDMXPixelMappingHierarchyView.h"
#include "Widgets/SDMXPixelMappingHierarchyItem.h"
#include "Toolkits/DMXPixelMappingToolkit.h"
#include "DMXPixelMappingComponentReference.h"
#include "DMXPixelMappingEditorUtils.h"
#include "ViewModels/DMXPixelMappingHierarchyViewModel.h"

#include "Framework/Commands/GenericCommands.h"
#include "Widgets/Layout/SBorder.h"
#include "Framework/Views/TreeFilterHandler.h"
#include "Widgets/Layout/SScrollBorder.h"
#include "Misc/IFilter.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "UnrealExporter.h"
#include "Exporters/Exporter.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Factories.h"
#include "ScopedTransaction.h"
#include "DMXPixelMappingRuntime/Public/Components/DMXPixelMappingOutputComponent.h"
#include "DMXPixelMappingRuntime/Public/Components/DMXPixelMappingRendererComponent.h"
#include "DMXPixelMappingRuntime/Public/Components/DMXPixelMappingRootComponent.h"

#define LOCTEXT_NAMESPACE "SDMXPixelMappingHierarchyView"

class FDMXPixelMappingBaseComponentTextFactory : public FCustomizableTextObjectFactory
{
public:

	FDMXPixelMappingBaseComponentTextFactory() : FCustomizableTextObjectFactory(GWarn)
	{

	}

	// FCustomizableTextObjectFactory implementation
	virtual bool CanCreateClass(UClass* InObjectClass, bool& bOmitSubObjs) const override
	{
		return InObjectClass->IsChildOf<UDMXPixelMappingBaseComponent>();
	}

	virtual void ProcessConstructedObject(UObject* NewObject) override
	{
		check(NewObject);

		if (NewObject->IsA<UDMXPixelMappingBaseComponent>())
		{
			UDMXPixelMappingBaseComponent* DMXPixelMappingBaseComponent = Cast<UDMXPixelMappingBaseComponent>(NewObject);
			DMXPixelMappingBaseComponents.Add(DMXPixelMappingBaseComponent);
		}
	}

public:
	TArray<UDMXPixelMappingBaseComponent*> DMXPixelMappingBaseComponents;
};

void SDMXPixelMappingHierarchyView::Construct(const FArguments& InArgs, const TSharedPtr<FDMXPixelMappingToolkit>& InToolkit)
{
	Toolkit = InToolkit;

	DelegateHandleAddComponent = InToolkit->GetOnComponenetAddedOrDeletedDelegate().AddSP(this, &SDMXPixelMappingHierarchyView::HandleAddComponent);

	bRebuildTreeRequested = false;
	bIsUpdatingSelection = false;

	SearchBoxWidgetFilter = MakeShared<WidgetTextFilter>(WidgetTextFilter::FItemToStringArray::CreateSP(this, &SDMXPixelMappingHierarchyView::GetWidgetFilterStrings));

	FilterHandler = MakeShared<TreeFilterHandler<TSharedPtr<FDMXPixelMappingHierarchyItemWidgetModel>>>();
	FilterHandler->SetFilter(SearchBoxWidgetFilter.Get());
	FilterHandler->SetRootItems(&RootWidgets, &TreeRootWidgets);
	FilterHandler->SetGetChildrenDelegate(TreeFilterHandler<TSharedPtr<FDMXPixelMappingHierarchyItemWidgetModel>>::FOnGetChildren::CreateRaw(this, &SDMXPixelMappingHierarchyView::WidgetHierarchy_OnGetChildren));

	CommandList = MakeShareable(new FUICommandList);

	CommandList->MapAction(
		FGenericCommands::Get().Rename,
		FExecuteAction::CreateSP(this, &SDMXPixelMappingHierarchyView::BeginRename)
	);
	CommandList->MapAction(
		FGenericCommands::Get().Cut,
		FExecuteAction::CreateSP(this, &SDMXPixelMappingHierarchyView::BeginCut),
		FCanExecuteAction::CreateSP(this, &SDMXPixelMappingHierarchyView::CanBeginCut)
	);
	CommandList->MapAction(
		FGenericCommands::Get().Copy,
		FExecuteAction::CreateSP(this, &SDMXPixelMappingHierarchyView::BeginCopy),
		FCanExecuteAction::CreateSP(this, &SDMXPixelMappingHierarchyView::CanBeginCopy)
	);
	CommandList->MapAction(
		FGenericCommands::Get().Paste,
		FExecuteAction::CreateSP(this, &SDMXPixelMappingHierarchyView::BeginPaste),
		FCanExecuteAction::CreateSP(this, &SDMXPixelMappingHierarchyView::CanBeginPaste)
	);
	CommandList->MapAction(
		FGenericCommands::Get().Duplicate,
		FExecuteAction::CreateSP(this, &SDMXPixelMappingHierarchyView::BeginDuplicate),
		FCanExecuteAction::CreateSP(this, &SDMXPixelMappingHierarchyView::CanBeginDuplicate)
	);
	CommandList->MapAction(
		FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &SDMXPixelMappingHierarchyView::BeginDelete)
	);


	ChildSlot
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SAssignNew(TreeViewArea, SBorder)
					.Padding(0)
					.BorderImage(FEditorStyle::GetBrush("NoBrush"))
				]
			]
		];

	RebuildTreeView();

	bRefreshRequested = true;

	OnSelectedComponenetChangedHandle = InToolkit->GetOnSelectedComponenetChangedDelegate().AddRaw(this, &SDMXPixelMappingHierarchyView::OnEditorSelectionChanged);

	GEditor->RegisterForUndo(this);
}

SDMXPixelMappingHierarchyView::~SDMXPixelMappingHierarchyView()
{
	if (DelegateHandleAddComponent.IsValid())
	{
		if (TSharedPtr<FDMXPixelMappingToolkit> ToolkitPtr = Toolkit.Pin())
		{
			ToolkitPtr->GetOnComponenetAddedOrDeletedDelegate().Remove(DelegateHandleAddComponent);
			ToolkitPtr->GetOnSelectedComponenetChangedDelegate().Remove(OnSelectedComponenetChangedHandle);
		}
	}
}

void SDMXPixelMappingHierarchyView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (bRebuildTreeRequested || bRefreshRequested)
	{
		if (bRebuildTreeRequested)
		{
			RebuildTreeView();
		}

		RefreshTree();

		UpdateItemsExpansionFromModel();

		for (FDMXPixelMappingHierarchyItemWidgetModelPtr& Item : CurrentSelectedItems)
		{
			Item->OnSelection();
		}
		CurrentSelectedItems.Empty();

		bRefreshRequested = false;
		bRebuildTreeRequested = false;
	}
}


FReply SDMXPixelMappingHierarchyView::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (CommandList->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

TSharedPtr<SWidget> SDMXPixelMappingHierarchyView::WidgetHierarchy_OnContextMenuOpening()
{
	FMenuBuilder MenuBuilder(true, CommandList);

	FDMXPixelMappingEditorUtils::CreateComponentContextMenu(MenuBuilder, Toolkit.Pin().ToSharedRef());

	return MenuBuilder.MakeWidget();
}

void SDMXPixelMappingHierarchyView::WidgetHierarchy_OnGetChildren(FDMXPixelMappingHierarchyItemWidgetModelPtr InParent, FDMXPixelMappingHierarchyItemWidgetModelArr& OutChildren)
{
	InParent->GatherChildren(OutChildren);
}

TSharedRef<ITableRow> SDMXPixelMappingHierarchyView::WidgetHierarchy_OnGenerateRow(FDMXPixelMappingHierarchyItemWidgetModelPtr InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SDMXPixelMappingHierarchyItem, OwnerTable, InItem, StaticCastSharedRef<SDMXPixelMappingHierarchyView>(AsShared()));
}

void SDMXPixelMappingHierarchyView::WidgetHierarchy_OnSelectionChanged(FDMXPixelMappingHierarchyItemWidgetModelPtr SelectedItem, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo != ESelectInfo::Direct)
	{
		bIsUpdatingSelection = true;

		if (TSharedPtr<FDMXPixelMappingToolkit> ToolkitPtr = Toolkit.Pin())
		{
			TSet<FDMXPixelMappingComponentReference> ComponentsToSelect;
			FDMXPixelMappingHierarchyItemWidgetModelArr SelectedItems = WidgetTreeView->GetSelectedItems();
			for (FDMXPixelMappingHierarchyItemWidgetModelPtr& Item : SelectedItems)
			{
				ComponentsToSelect.Add(Item->GetReference());
			}

			ToolkitPtr->SelectComponents(ComponentsToSelect);
		}

		bIsUpdatingSelection = false;
	}
}

void SDMXPixelMappingHierarchyView::GetWidgetFilterStrings(FDMXPixelMappingHierarchyItemWidgetModelPtr InModelPtr, TArray<FString>& OutStrings)
{
	InModelPtr->GetFilterStrings(OutStrings);
}

void SDMXPixelMappingHierarchyView::RebuildTreeView()
{
	float OldScrollOffset = 0.0f;

	if (WidgetTreeView.IsValid())
	{
		OldScrollOffset = WidgetTreeView->GetScrollOffset();
	}

	SAssignNew(WidgetTreeView, HierarchTreeView)
		.ItemHeight(20.0f)
		.SelectionMode(ESelectionMode::Multi)
		.OnGetChildren(FilterHandler.ToSharedRef(), &TreeFilterHandler<FDMXPixelMappingHierarchyItemWidgetModelPtr>::OnGetFilteredChildren)
		.OnGenerateRow(this, &SDMXPixelMappingHierarchyView::WidgetHierarchy_OnGenerateRow)
		.OnSelectionChanged(this, &SDMXPixelMappingHierarchyView::WidgetHierarchy_OnSelectionChanged)
		.OnContextMenuOpening(this, &SDMXPixelMappingHierarchyView::WidgetHierarchy_OnContextMenuOpening)
		.TreeItemsSource(&TreeRootWidgets);

	FilterHandler->SetTreeView(WidgetTreeView.Get());

	TreeViewArea->SetContent(
		SNew(SScrollBorder, WidgetTreeView.ToSharedRef())
		[
			WidgetTreeView.ToSharedRef()
		]);


	WidgetTreeView->SetScrollOffset(OldScrollOffset);
}

void SDMXPixelMappingHierarchyView::RecursiveExpand(FDMXPixelMappingHierarchyItemWidgetModelPtr Model, EExpandBehavior ExpandBehavior)
{
	bool bShouldExpandItem = true;

	WidgetTreeView->SetItemExpansion(Model, bShouldExpandItem);

	FDMXPixelMappingHierarchyItemWidgetModelArr Children;
	Model->GatherChildren(Children);

	for (FDMXPixelMappingHierarchyItemWidgetModelPtr& ChildModel : Children)
	{
		RecursiveExpand(ChildModel, ExpandBehavior);
	}
}

void SDMXPixelMappingHierarchyView::RestoreSelectedItems()
{
	for (FDMXPixelMappingHierarchyItemWidgetModelPtr& Model : RootWidgets)
	{
		RecursiveSelection(Model);
	}
}

void SDMXPixelMappingHierarchyView::RecursiveSelection(FDMXPixelMappingHierarchyItemWidgetModelPtr& Model)
{
	if (Model->ContainsSelection())
	{
		// Expand items that contain selection.
		WidgetTreeView->SetItemExpansion(Model, true);

		TArray<FDMXPixelMappingHierarchyItemWidgetModelPtr> Children;
		Model->GatherChildren(Children);

		for (FDMXPixelMappingHierarchyItemWidgetModelPtr& ChildModel : Children)
		{
			RecursiveSelection(ChildModel);
		}
	}

	if (Model->IsSelected())
	{
		WidgetTreeView->SetItemSelection(Model, true, ESelectInfo::Direct);
		WidgetTreeView->RequestScrollIntoView(Model);
	}
}

void SDMXPixelMappingHierarchyView::RefreshTree()
{
	RootWidgets.Empty();
	RootWidgets.Add(MakeShared<FDMXPixelMappingHierarchyItemWidgetModel>(Toolkit.Pin()));

	FilterHandler->RefreshAndFilterTree();
}

void SDMXPixelMappingHierarchyView::UpdateItemsExpansionFromModel()
{
	for (FDMXPixelMappingHierarchyItemWidgetModelPtr& Model : RootWidgets)
	{
		RecursiveExpand(Model, EExpandBehavior::FromModel);
	}
}

void SDMXPixelMappingHierarchyView::HandleAddComponent(bool bIsSuccess)
{
	RequestRebuildTree();
}

void SDMXPixelMappingHierarchyView::OnEditorSelectionChanged()
{
	if (!bIsUpdatingSelection)
	{
		WidgetTreeView->ClearSelection();

		if (RootWidgets.Num() > 0)
		{
			RootWidgets[0]->RefreshSelection();
		}

		RestoreSelectedItems();
	}
}

void SDMXPixelMappingHierarchyView::BeginRename()
{
	FDMXPixelMappingHierarchyItemWidgetModelArr SelectedItems = WidgetTreeView->GetSelectedItems();
	SelectedItems[0]->RequestBeginRename();
}

bool SDMXPixelMappingHierarchyView::CanBeginCut() const
{
	return CanBeginCopy();
}

void SDMXPixelMappingHierarchyView::BeginCut()
{
	BeginCopy();
	BeginDelete();
}

bool SDMXPixelMappingHierarchyView::CanBeginCopy() const
{
	FDMXPixelMappingHierarchyItemWidgetModelArr SelectedItems = WidgetTreeView->GetSelectedItems();
	int32 NumberOfValidItems = 0;
	for (FDMXPixelMappingHierarchyItemWidgetModelPtr SelectedItem : SelectedItems)
	{
		UDMXPixelMappingBaseComponent* SelectedComponent = SelectedItem->GetReference().GetComponent();
		// root component cannot be copied
		if (!SelectedComponent->IsA<UDMXPixelMappingRootComponent>())
		{
			NumberOfValidItems++;
		}
	}

	return NumberOfValidItems > 0;
}

void SDMXPixelMappingHierarchyView::BeginCopy()
{
	const FExportObjectInnerContext Context;
	FStringOutputDevice Archive;

	FDMXPixelMappingHierarchyItemWidgetModelArr SelectedItems = WidgetTreeView->GetSelectedItems();

	for (FDMXPixelMappingHierarchyItemWidgetModelPtr SelectedItem : SelectedItems)
	{
		UDMXPixelMappingBaseComponent* SelectedComponent = SelectedItem->GetReference().GetComponent();
		if (!SelectedComponent->IsA<UDMXPixelMappingRootComponent>())
		{
			UExporter::ExportToOutputDevice(&Context, SelectedComponent, nullptr, Archive, TEXT("copy"), 0, PPF_ExportsNotFullyQualified | PPF_Copy | PPF_Delimited, false, GetTransientPackage());
		}
	}

	// Copy text to clipboard
	FString ExportedText = Archive;
	FPlatformApplicationMisc::ClipboardCopy(*ExportedText);
}

bool SDMXPixelMappingHierarchyView::CanBeginPaste() const
{
	FDMXPixelMappingHierarchyItemWidgetModelArr SelectedItems = WidgetTreeView->GetSelectedItems();

	TSet<TSubclassOf<UDMXPixelMappingBaseComponent>> FoundComponentsType;
	for (FDMXPixelMappingHierarchyItemWidgetModelPtr SelectedItem : SelectedItems)
	{
		UDMXPixelMappingBaseComponent* SelectedComponent = SelectedItem->GetReference().GetComponent();
		FoundComponentsType.Add(SelectedComponent->GetClass());
	}

	// only one destination type is allowed (read: two renderers as well as two matrix are allowed)
	return FoundComponentsType.Num() < 2;
}

void SDMXPixelMappingHierarchyView::RecursivePaste(UDMXPixelMappingBaseComponent* InComponent)
{
	for (int32 ChildIndex = 0; ChildIndex < InComponent->Children.Num(); ChildIndex++)
	{
		UDMXPixelMappingBaseComponent* ChildComponent = InComponent->Children[ChildIndex];
		if (ChildComponent)
		{
			const FName UniqueName = MakeUniqueObjectName(ChildComponent->GetOuter(), ChildComponent->GetClass(), ChildComponent->GetNamePrefix());
			UDMXPixelMappingBaseComponent* ChildCopy = DuplicateObject(ChildComponent, ChildComponent->GetOuter(), UniqueName);
			if (ChildCopy)
			{
				ChildCopy->Parent = InComponent;
				InComponent->Children[ChildIndex] = ChildCopy;
				RecursivePaste(InComponent->Children[ChildIndex]);
			}
		}
	}
}

void SDMXPixelMappingHierarchyView::RequestRebuildTree()
{
	bRebuildTreeRequested = true;
}

bool SDMXPixelMappingHierarchyView::MoveComponentToComponent(UDMXPixelMappingBaseComponent* Source, UDMXPixelMappingBaseComponent* Destination)
{
	UDMXPixelMappingBaseComponent* NewParent = nullptr;
	// first check if we can directly move the component
	if (Source->CanBeMovedTo(Destination))
	{
		NewParent = Destination;
	}
	// try parent
	else
	{
		if (Source->CanBeMovedTo(Destination->Parent))
		{
			NewParent = Destination->Parent;
		}
	}

	if (NewParent)
	{
		NewParent->Modify();
		Source->Modify();

		RecursivePaste(Source);

		const FName UniqueName = MakeUniqueObjectName(NewParent->GetOuter(), Source->GetClass(), Source->GetNamePrefix());
		const FString NewNameStr = UniqueName.ToString();

		Source->Rename(*NewNameStr, NewParent->GetOuter());

		NewParent->AddChild(Source);

		RequestComponentRedraw(NewParent);

		RequestRebuildTree();

		return true;
	}

	return false;
}

void SDMXPixelMappingHierarchyView::BeginPaste()
{
	FString TextToImport;
	FPlatformApplicationMisc::ClipboardPaste(TextToImport);

	FDMXPixelMappingBaseComponentTextFactory Factory;
	Factory.ProcessBuffer(GetTransientPackage(), RF_Transactional, TextToImport);
	TArray<UDMXPixelMappingBaseComponent*> NewComponents = Factory.DMXPixelMappingBaseComponents;

	FDMXPixelMappingHierarchyItemWidgetModelArr SelectedItems = WidgetTreeView->GetSelectedItems();
	const FScopedTransaction Transaction(FText::Format(LOCTEXT("PasteComponent", "Paste {0}|plural(one=Component, other=Components)"), SelectedItems.Num()));

	for (FDMXPixelMappingHierarchyItemWidgetModelPtr SelectedItem : SelectedItems)
	{
		UDMXPixelMappingBaseComponent* SelectedComponent = SelectedItem->GetReference().GetComponent();
		for (UDMXPixelMappingBaseComponent* Component : NewComponents)
		{
			MoveComponentToComponent(Component, SelectedComponent);
		}
	}

	if (bRebuildTreeRequested)
	{
		CurrentSelectedItems = WidgetTreeView->GetSelectedItems();
	}
}

bool SDMXPixelMappingHierarchyView::CanBeginDuplicate() const
{
	return CanBeginCopy();
}

void SDMXPixelMappingHierarchyView::BeginDuplicate()
{
	FDMXPixelMappingHierarchyItemWidgetModelArr SelectedItems = WidgetTreeView->GetSelectedItems();

	const FScopedTransaction Transaction(FText::Format(LOCTEXT("RemoveComponents", "Duplicate {0}|plural(one=Component, other=Components)"), SelectedItems.Num()));

	for (FDMXPixelMappingHierarchyItemWidgetModelPtr SelectedItem : SelectedItems)
	{
		UDMXPixelMappingBaseComponent* SelectedComponent = SelectedItem->GetReference().GetComponent();
		const FName UniqueName = MakeUniqueObjectName(SelectedComponent->GetOuter(), SelectedComponent->GetClass(), SelectedComponent->GetNamePrefix());
		UDMXPixelMappingBaseComponent* NewComponent = DuplicateObject(SelectedComponent, SelectedComponent->GetOuter(), UniqueName);
		if (NewComponent)
		{
			MoveComponentToComponent(NewComponent, SelectedComponent);
		}
	}
}

void SDMXPixelMappingHierarchyView::RequestComponentRedraw(UDMXPixelMappingBaseComponent* Component)
{
	if (!Component)
	{
		return;
	}

	if (TSharedPtr<FDMXPixelMappingToolkit> ToolkitPtr = Toolkit.Pin())
	{
		TSet<FDMXPixelMappingComponentReference> ComponentsToUpdate;
		FDMXPixelMappingComponentReference ComponentReference(ToolkitPtr, Component);
		ComponentsToUpdate.Add(ComponentReference);
		//ToolkitPtr->SelectComponents(ComponentsToUpdate);
	}
}

void SDMXPixelMappingHierarchyView::BeginDelete()
{
	FDMXPixelMappingHierarchyItemWidgetModelArr SelectedItems = WidgetTreeView->GetSelectedItems();

	const FScopedTransaction Transaction(FText::Format(LOCTEXT("RemoveComponents", "Remove {0}|plural(one=Component, other=Components)"), SelectedItems.Num()));

	for (FDMXPixelMappingHierarchyItemWidgetModelPtr SelectedItem : SelectedItems)
	{
		UDMXPixelMappingBaseComponent* SelectedComponent = SelectedItem->GetReference().GetComponent();
		UDMXPixelMappingBaseComponent* ParentComponent = SelectedComponent->Parent;

		if (ParentComponent)
		{
			SelectedComponent->Modify();
			ParentComponent->Modify();
			ParentComponent->RemoveChild(SelectedComponent);

			RequestComponentRedraw(ParentComponent);

			RequestRebuildTree();
		}
	}
	
}

void SDMXPixelMappingHierarchyView::PostUndo(bool bSuccess)
{
	RequestRebuildTree();
}

#undef LOCTEXT_NAMESPACE