// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/SDMXPixelMappingHierarchyView.h"

#include "Components/DMXPixelMappingFixtureGroupComponent.h"
#include "Components/DMXPixelMappingMatrixComponent.h"
#include "Components/DMXPixelMappingMatrixCellComponent.h"
#include "Components/DMXPixelMappingRendererComponent.h"
#include "Components/DMXPixelMappingRootComponent.h"
#include "DMXPixelMapping.h"
#include "DMXPixelMappingEditorUtils.h"
#include "Exporters/Exporter.h"
#include "Factories.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Views/TableViewMetadata.h"
#include "Framework/Views/TreeFilterHandler.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/TextFilter.h"
#include "ScopedTransaction.h"
#include "Settings/DMXPixelMappingEditorSettings.h"
#include "Toolkits/DMXPixelMappingToolkit.h"
#include "UnrealExporter.h"
#include "ViewModels/DMXPixelMappingHierarchyItem.h"
#include "Views/SDMXPixelMappingDesignerView.h"
#include "Views/SDMXPixelMappingPreviewView.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SScrollBorder.h"
#include "Widgets/SDMXPixelMappingHierarchyItem.h"
#include "Widgets/Views/STreeView.h"


#define LOCTEXT_NAMESPACE "SDMXPixelMappingHierarchyView"

namespace UE::DMX::PixelMappingEditor::SDMXPixelMappingHierarchyView::Private
{
	/** Helper to create new components from existing */
	class FBaseComponentTextFactory
		: public FCustomizableTextObjectFactory
	{
	public:

		FBaseComponentTextFactory()
			: FCustomizableTextObjectFactory(GWarn)
		{}

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


	/** Helper to restore expansion state after refreshing */
	struct FTreeExpansionSnapshot
	{
		static FTreeExpansionSnapshot TakeSnapshot(const FDMXPixelMappingHierarchyItemWidgetModelArr& InRootWidgets, const TSharedPtr<STreeView<FDMXPixelMappingHierarchyItemWidgetModelPtr>>& InTreeView)
		{
			FTreeExpansionSnapshot Result;
			for (FDMXPixelMappingHierarchyItemWidgetModelPtr Model : InRootWidgets)
			{
				Result.RecursiveTakeSnapshot(Model, InTreeView);
			}
			return Result;
		}

		void RestoreExpandedAndExpandNewModels(const FDMXPixelMappingHierarchyItemWidgetModelArr& InRootWidgets, const TSharedPtr<STreeView<FDMXPixelMappingHierarchyItemWidgetModelPtr>>& InTreeView)
		{
			for (FDMXPixelMappingHierarchyItemWidgetModelPtr Model : InRootWidgets)
			{
				RecursiveRestoreSnapshot(Model, InTreeView);
			}
		}

	private:

		FTreeExpansionSnapshot()
		{}

		void RecursiveTakeSnapshot(FDMXPixelMappingHierarchyItemWidgetModelPtr Model, const TSharedPtr<STreeView<FDMXPixelMappingHierarchyItemWidgetModelPtr>>& TreeView)
		{
			UDMXPixelMappingBaseComponent* Component = Model->GetComponent();
			if (IsValid(Component))
			{
				ComponentExpansionStates.Add(Component) = TreeView->IsItemExpanded(Model);

				for (FDMXPixelMappingHierarchyItemWidgetModelPtr& ChildModel : Model->GetChildren())
				{
					RecursiveTakeSnapshot(ChildModel, TreeView);
				}
			}
		}

		void RecursiveRestoreSnapshot(FDMXPixelMappingHierarchyItemWidgetModelPtr Model, const TSharedPtr<STreeView<FDMXPixelMappingHierarchyItemWidgetModelPtr>>& TreeView)
		{
			UDMXPixelMappingBaseComponent* Component = Model->GetComponent();
			if (IsValid(Component))
			{
				bool* pPreviousExpansionState = ComponentExpansionStates.Find(Component);
				if (pPreviousExpansionState == nullptr)
				{
					// Initially collapse matrix components
					if (Cast<UDMXPixelMappingMatrixComponent>(Component))
					{
						TreeView->SetItemExpansion(Model, false);
					}
					else
					{
						TreeView->SetItemExpansion(Model, true);
					}
				}
				else
				{
					TreeView->SetItemExpansion(Model, *pPreviousExpansionState);
				}

				for (FDMXPixelMappingHierarchyItemWidgetModelPtr& ChildModel : Model->GetChildren())
				{
					RecursiveRestoreSnapshot(ChildModel, TreeView);
				}
			}
		}

		TMap<UDMXPixelMappingBaseComponent*, bool> ComponentExpansionStates;
	};

}

const FName SDMXPixelMappingHierarchyView::FColumnIds::EditorColor = "EditorColor";
const FName SDMXPixelMappingHierarchyView::FColumnIds::ComponentName = "Name";
const FName SDMXPixelMappingHierarchyView::FColumnIds::FixtureID = "FixtureID";
const FName SDMXPixelMappingHierarchyView::FColumnIds::Patch = "Patch";

void SDMXPixelMappingHierarchyView::Construct(const FArguments& InArgs, const TSharedPtr<FDMXPixelMappingToolkit>& InToolkit)
{
	WeakToolkit = InToolkit;

	CommandList = MakeShareable(new FUICommandList);

	CommandList->MapAction(
		FGenericCommands::Get().Rename,
		FExecuteAction::CreateSP(this, &SDMXPixelMappingHierarchyView::RenameSelectedComponent),
		FCanExecuteAction::CreateSP(this, &SDMXPixelMappingHierarchyView::CanRenameSelectedComponent)
	);

	CommandList->MapAction(
		FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &SDMXPixelMappingHierarchyView::DeleteSelectedComponents)
	);

	using TextFilterType = TTextFilter<FDMXPixelMappingHierarchyItemWidgetModelPtr>;
	SearchFilter = MakeShared<TextFilterType>(TextFilterType::FItemToStringArray::CreateSP(this, &SDMXPixelMappingHierarchyView::GetWidgetFilterStrings));

	FilterHandler = MakeShared<TreeFilterHandler<TSharedPtr<FDMXPixelMappingHierarchyItem>>>();
	FilterHandler->SetFilter(SearchFilter.Get());
	FilterHandler->SetRootItems(&AllRootItems, &FilteredRootItems);
	FilterHandler->SetGetChildrenDelegate(TreeFilterHandler<TSharedPtr<FDMXPixelMappingHierarchyItem>>::FOnGetChildren::CreateSP(this, &SDMXPixelMappingHierarchyView::OnGetChildItems));

	InToolkit->GetOnSelectedComponentsChangedDelegate().AddSP(this, &SDMXPixelMappingHierarchyView::OnEditorSelectionChanged);
	UDMXPixelMappingBaseComponent::GetOnComponentAdded().AddSP(this, &SDMXPixelMappingHierarchyView::OnComponentAddedOrRemoved);
	UDMXPixelMappingBaseComponent::GetOnComponentRemoved().AddSP(this, &SDMXPixelMappingHierarchyView::OnComponentAddedOrRemoved);

	BuildChildSlotAndRefresh();
}

void SDMXPixelMappingHierarchyView::RequestRefresh()
{
	if (!RequestRefreshTimerHandle.IsValid())
	{
		RequestRefreshTimerHandle = GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateSP(this, &SDMXPixelMappingHierarchyView::ForceRefresh));
	}
}

void SDMXPixelMappingHierarchyView::ForceRefresh()
{
	RequestRefreshTimerHandle.Invalidate();

	using namespace UE::DMX::PixelMappingEditor::SDMXPixelMappingHierarchyView::Private;
	FTreeExpansionSnapshot ExpansionSnapshot = FTreeExpansionSnapshot::TakeSnapshot(AllRootItems, HierarchyTreeView);

	// Create the root, and let it construct new tree items
	AllRootItems.Reset();
	AllRootItems.Add(FDMXPixelMappingHierarchyItem::CreateNew(WeakToolkit.Pin()));

	// Refresh and filter the tree view
	FilterHandler->RefreshAndFilterTree();

	ExpansionSnapshot.RestoreExpandedAndExpandNewModels(AllRootItems, HierarchyTreeView);
	AdoptSelectionFromToolkit();
}

void SDMXPixelMappingHierarchyView::BuildChildSlotAndRefresh()
{
	using namespace UE::DMX::PixelMappingEditor::SDMXPixelMappingHierarchyView::Private;
	FTreeExpansionSnapshot ExpansionSnapshot = FTreeExpansionSnapshot::TakeSnapshot(AllRootItems, HierarchyTreeView);

	ChildSlot
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4.f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Right)
				.AutoWidth()
				.Padding(8.f, 0.f, 0.f, 0.f)
				[
					SNew(SComboButton)
					.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
					.HasDownArrow(true)
					.OnGetMenuContent(this, &SDMXPixelMappingHierarchyView::GenerateHeaderRowFilterMenu)
					.ButtonContent()
					[
						SNew(SImage)
						.Image(FAppStyle::Get().GetBrush("Icons.Filter"))
						.ColorAndOpacity(FSlateColor::UseForeground())
					]
				]
	
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				[	
					SAssignNew(SearchBox, SSearchBox)
					.OnTextChanged(this, &SDMXPixelMappingHierarchyView::SetFilterText)
				]
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			.Padding(4.f)
			[
				SAssignNew(HierarchyTreeView, STreeView<FDMXPixelMappingHierarchyItemWidgetModelPtr>)
				.ItemHeight(20.0f)
				.SelectionMode(ESelectionMode::Multi)
				.HeaderRow(GenerateHeaderRow())
				.OnGetChildren(FilterHandler.ToSharedRef(), &TreeFilterHandler<TSharedPtr<FDMXPixelMappingHierarchyItem>>::OnGetFilteredChildren)
				.OnGenerateRow(this, &SDMXPixelMappingHierarchyView::OnGenerateRow)
				.OnSelectionChanged(this, &SDMXPixelMappingHierarchyView::OnSelectionChanged)
				.OnContextMenuOpening(this, &SDMXPixelMappingHierarchyView::OnContextMenuOpening)
				.TreeItemsSource(&FilteredRootItems)
				.OnItemToString_Debug_Lambda([this](FDMXPixelMappingHierarchyItemWidgetModelPtr Item) { return Item->GetComponentNameText().ToString(); })
			]
		];

	FilterHandler->SetTreeView(HierarchyTreeView.Get());

	// Create the root, and let it construct new tree items
	AllRootItems.Reset();
	AllRootItems.Add(FDMXPixelMappingHierarchyItem::CreateNew(WeakToolkit.Pin()));

	// Refresh and filter the tree view
	FilterHandler->RefreshAndFilterTree();

	ExpansionSnapshot.RestoreExpandedAndExpandNewModels(AllRootItems, HierarchyTreeView);
	AdoptSelectionFromToolkit();
}

TSharedRef<SHeaderRow> SDMXPixelMappingHierarchyView::GenerateHeaderRow()
{
	const TSharedRef<SHeaderRow> HeaderRow = SNew(SHeaderRow);
	const FDMXPixelMappingHierarchySettings& HierarchySettings = GetDefault<UDMXPixelMappingEditorSettings>()->HierarchySettings;

	if (HierarchySettings.bShowEditorColorColumn)
	{
		HeaderRow->AddColumn(
			SHeaderRow::FColumn::FArguments()
			.ColumnId(FColumnIds::EditorColor)
			.DefaultLabel(LOCTEXT("EditorColorColumnLabel", ""))
			.FixedWidth(16.f)
			.VAlignHeader(VAlign_Center)
		);
	}
	
	HeaderRow->AddColumn(
		SHeaderRow::FColumn::FArguments()
		.ColumnId(FColumnIds::ComponentName)
		.SortMode(this, &SDMXPixelMappingHierarchyView::GetColumnSortMode, FColumnIds::ComponentName)
		.OnSort(this, &SDMXPixelMappingHierarchyView::SetSortAndRefresh)
		.FillWidth(0.68f)
		.HeaderContentPadding(FMargin(6.f))
		.VAlignHeader(VAlign_Center)
		.HeaderContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("FixturePatchNameColumnLabel", "Name"))
			.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
		]
	);

	if (HierarchySettings.bShowFixtureIDColumn)
	{
		HeaderRow->AddColumn(
			SHeaderRow::FColumn::FArguments()
			.ColumnId(FColumnIds::FixtureID)
			.SortMode(this, &SDMXPixelMappingHierarchyView::GetColumnSortMode, FColumnIds::FixtureID)
			.OnSort(this, &SDMXPixelMappingHierarchyView::SetSortAndRefresh)
			.FillWidth(0.16f)
			.HeaderContentPadding(FMargin(6.f))
			.VAlignHeader(VAlign_Center)
			.HeaderContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("FixtureIDColumnLabel", "FID"))
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
			]
		);
	}

	if (HierarchySettings.bShowPatchColumn)
	{
		HeaderRow->AddColumn(
			SHeaderRow::FColumn::FArguments()
			.ColumnId(FColumnIds::Patch)
			.SortMode(this, &SDMXPixelMappingHierarchyView::GetColumnSortMode, FColumnIds::Patch)
			.OnSort(this, &SDMXPixelMappingHierarchyView::SetSortAndRefresh)
			.FillWidth(0.16f)
			.HeaderContentPadding(FMargin(6.f))
			.VAlignHeader(VAlign_Center)
			.HeaderContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PatchColumnLabel", "Patch"))
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
			]
		);
	}

	return HeaderRow;
}

TSharedRef<SWidget> SDMXPixelMappingHierarchyView::GenerateHeaderRowFilterMenu()
{
	constexpr bool bShouldCloseMenuAfterSelection = false;
	FMenuBuilder MenuBuilder(bShouldCloseMenuAfterSelection, nullptr);

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("FilterSection", "Columns"));
	{
		auto AddMenuEntryLambda = [this, &MenuBuilder](const FText& Label, const FText& ToolTip, const FName& ColumnID)
		{
			MenuBuilder.AddMenuEntry(
				Label,
				ToolTip,
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &SDMXPixelMappingHierarchyView::ToggleColumnVisility, ColumnID),
					FCanExecuteAction(),
					FIsActionChecked::CreateSP(this, &SDMXPixelMappingHierarchyView::IsColumVisible, ColumnID)
				),
				NAME_None,
				EUserInterfaceActionType::ToggleButton
			);
		};

		AddMenuEntryLambda(
			LOCTEXT("FixturePatchNameColumn_Label", "Show Editor Color"),
			FText::GetEmpty(),
			FColumnIds::EditorColor
		);

		AddMenuEntryLambda(
			LOCTEXT("FixturePatchTypeColumn_Label", "Show Fixture ID"),
			FText::GetEmpty(),
			FColumnIds::FixtureID
		);

		AddMenuEntryLambda(
			LOCTEXT("FixturePatchModeColumn_Label", "Show Patch"),
			FText::GetEmpty(),
			FColumnIds::Patch
		);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

FReply SDMXPixelMappingHierarchyView::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (CommandList->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SDMXPixelMappingHierarchyView::PostUndo(bool bSuccess)
{
	RequestRefresh();
}

void SDMXPixelMappingHierarchyView::PostRedo(bool bSuccess)
{ 
	RequestRefresh();
}

void SDMXPixelMappingHierarchyView::OnGetChildItems(TSharedPtr<FDMXPixelMappingHierarchyItem> InParent, TArray<TSharedPtr<FDMXPixelMappingHierarchyItem>>& OutChildren)
{
	// If the parent is a fixture group, sort its children using the current sort order
	if (InParent.IsValid() && 
		InParent->GetComponent() && 
		InParent->GetComponent()->GetClass() == UDMXPixelMappingFixtureGroupComponent::StaticClass())
	{
		const FDMXPixelMappingHierarchySettings& HierarchySettings = GetDefault<UDMXPixelMappingEditorSettings>()->HierarchySettings;
		if (HierarchySettings.SortByColumnId == FColumnIds::ComponentName)
		{
			InParent->StableSortChildren([](const TSharedPtr<FDMXPixelMappingHierarchyItem>& Item)
				{
					return Item->GetComponentNameText().ToString();
				});
		}
		else if (HierarchySettings.SortByColumnId == FColumnIds::Patch)
		{
			InParent->StableSortChildren([](const TSharedPtr<FDMXPixelMappingHierarchyItem>& Item)
				{
					return Item->GetAbsoluteChannel();
				});
		}

		if (!HierarchySettings.bSortAscending)
		{
			InParent->ReverseChildren();
		}
	}

	OutChildren = InParent->GetChildren();
}

TSharedRef<ITableRow> SDMXPixelMappingHierarchyView::OnGenerateRow(FDMXPixelMappingHierarchyItemWidgetModelPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SDMXPixelMappingHierarchyItem, OwnerTable, WeakToolkit, Item.ToSharedRef());
}

TSharedPtr<SWidget> SDMXPixelMappingHierarchyView::OnContextMenuOpening()
{
	if (!WeakToolkit.IsValid())
	{
		return SNullWidget::NullWidget;
	}

	FMenuBuilder MenuBuilder(true, CommandList);

	MenuBuilder.BeginSection("Edit", LOCTEXT("Edit", "Edit"));
	{
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Rename);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void SDMXPixelMappingHierarchyView::OnSelectionChanged(FDMXPixelMappingHierarchyItemWidgetModelPtr SelectedItem, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo == ESelectInfo::Direct)
	{
		return;
	}

	const TGuardValue<bool> GuardIsUpdatingSelection(bIsUpdatingSelection, true);

	if (TSharedPtr<FDMXPixelMappingToolkit> Toolkit = WeakToolkit.Pin())
	{
		TSet<FDMXPixelMappingComponentReference> ComponentsToSelect;
		FDMXPixelMappingHierarchyItemWidgetModelArr SelectedItems = HierarchyTreeView->GetSelectedItems();
		for (FDMXPixelMappingHierarchyItemWidgetModelPtr& Item : SelectedItems)
		{
			ComponentsToSelect.Add(FDMXPixelMappingComponentReference(Toolkit, Item->GetComponent()));
		}

		Toolkit->SelectComponents(ComponentsToSelect);
	}
}

void SDMXPixelMappingHierarchyView::ToggleColumnVisility(FName ColumnId)
{
	UDMXPixelMappingEditorSettings* EditorSettings = GetMutableDefault<UDMXPixelMappingEditorSettings>();
	if (ColumnId == FColumnIds::EditorColor)
	{
		EditorSettings->HierarchySettings.bShowEditorColorColumn = !EditorSettings->HierarchySettings.bShowEditorColorColumn;
	}
	else if (ColumnId == FColumnIds::FixtureID)
	{
		EditorSettings->HierarchySettings.bShowFixtureIDColumn = !EditorSettings->HierarchySettings.bShowFixtureIDColumn;
	}
	else if (ColumnId == FColumnIds::Patch)
	{
		EditorSettings->HierarchySettings.bShowPatchColumn = !EditorSettings->HierarchySettings.bShowPatchColumn;
	}
	EditorSettings->SaveConfig();

	// To adopt column changes, the entire child slot needs to be updated
	BuildChildSlotAndRefresh();
}

bool SDMXPixelMappingHierarchyView::IsColumVisible(FName ColumnId) const
{
	UDMXPixelMappingEditorSettings* EditorSettings = GetMutableDefault<UDMXPixelMappingEditorSettings>();
	if (ColumnId == FColumnIds::EditorColor)
	{
		return EditorSettings->HierarchySettings.bShowEditorColorColumn;
	}
	else if (ColumnId == FColumnIds::FixtureID)
	{
		return EditorSettings->HierarchySettings.bShowFixtureIDColumn;
	}
	else if (ColumnId == FColumnIds::Patch)
	{
		return EditorSettings->HierarchySettings.bShowPatchColumn;
	}

	return false;
}

EColumnSortMode::Type SDMXPixelMappingHierarchyView::GetColumnSortMode(FName ColumnId) const
{
	const FDMXPixelMappingHierarchySettings& HierarchySettings = GetDefault<UDMXPixelMappingEditorSettings>()->HierarchySettings;
	if (HierarchySettings.SortByColumnId != ColumnId)
	{
		return EColumnSortMode::None;
	}

	return HierarchySettings.bSortAscending ? EColumnSortMode::Ascending : EColumnSortMode::Descending;
}

void SDMXPixelMappingHierarchyView::SetSortAndRefresh(const EColumnSortPriority::Type SortPriority, const FName& ColumnId, const EColumnSortMode::Type InSortMode)
{
	UDMXPixelMappingEditorSettings* EditorSettings = GetMutableDefault<UDMXPixelMappingEditorSettings>();

	EditorSettings->HierarchySettings.bSortAscending = InSortMode == EColumnSortMode::Ascending;
	EditorSettings->HierarchySettings.SortByColumnId = ColumnId;
	EditorSettings->SaveConfig();

	RequestRefresh();
}

void SDMXPixelMappingHierarchyView::OnComponentAddedOrRemoved(UDMXPixelMapping* PixelMapping, UDMXPixelMappingBaseComponent* Component)
{
	RequestRefresh();
}

void SDMXPixelMappingHierarchyView::OnEditorSelectionChanged()
{
	if (!bIsUpdatingSelection)
	{
		AdoptSelectionFromToolkit();
	}
}

void SDMXPixelMappingHierarchyView::AdoptSelectionFromToolkit()
{
	if (bIsUpdatingSelection)
	{
		return;
	}

	if (HierarchyTreeView.IsValid())
	{
		HierarchyTreeView->ClearSelection();
	}

	for (const TSharedPtr<FDMXPixelMappingHierarchyItem>& Item : FilteredRootItems)
	{
		RecursiveAdoptSelectionFromToolkit(Item.ToSharedRef());
	}
}

bool SDMXPixelMappingHierarchyView::RecursiveAdoptSelectionFromToolkit(const TSharedRef<FDMXPixelMappingHierarchyItem>& Item)
{
	if (bIsUpdatingSelection)
	{
		return false;
	}

	const TSharedPtr<FDMXPixelMappingToolkit> Toolkit = WeakToolkit.Pin();
	if (!Toolkit.IsValid())
	{
		return false;
	}

	bool bContainsSelection = false;
	for (const TSharedPtr<FDMXPixelMappingHierarchyItem>& ChildItem : Item->GetChildren())
	{
		bContainsSelection |= RecursiveAdoptSelectionFromToolkit(ChildItem.ToSharedRef());
	}

	const TSet<FDMXPixelMappingComponentReference> SelectedComponents = Toolkit->GetSelectedComponents();
	if (bContainsSelection)
	{
		HierarchyTreeView->SetItemExpansion(Item, true);
	}

	if (SelectedComponents.Contains(FDMXPixelMappingComponentReference(Toolkit, Item->GetComponent())))
	{
		HierarchyTreeView->SetItemSelection(Item, true, ESelectInfo::Direct);
		HierarchyTreeView->RequestScrollIntoView(Item);

		return true;
	}

	return bContainsSelection;
}

bool SDMXPixelMappingHierarchyView::CanRenameSelectedComponent() const
{
	return HierarchyTreeView.IsValid() ? HierarchyTreeView->GetSelectedItems().Num() == 1 : false;
}

void SDMXPixelMappingHierarchyView::RenameSelectedComponent()
{
	const TArray<TSharedPtr<FDMXPixelMappingHierarchyItem>> SelectedItems = HierarchyTreeView.IsValid() ? HierarchyTreeView->GetSelectedItems() : TArray<TSharedPtr<FDMXPixelMappingHierarchyItem>>();
	if (!ensureMsgf(SelectedItems.Num() == 1, TEXT("Cannot rename selected components. Please call CanRenameSelectedComponent() before calling RenameSelectedComponent().")))
	{
		return;
	}

	const TSharedPtr<ITableRow> TableRow = HierarchyTreeView->WidgetFromItem(SelectedItems[0]);
	if (!ensureMsgf(TableRow.IsValid(), TEXT("Cannot find widget for item. Cannot rename component")))
	{
		return;
	}

	StaticCastSharedPtr<SDMXPixelMappingHierarchyItem>(TableRow)->EnterRenameMode();
}

void SDMXPixelMappingHierarchyView::DeleteSelectedComponents()
{
	if (TSharedPtr<FDMXPixelMappingToolkit> Toolkit = WeakToolkit.Pin())
	{
		const int32 NumSelectedComponents = Toolkit->GetSelectedComponents().Num();
		const FScopedTransaction Transaction(FText::Format(LOCTEXT("DMXPixelMapping.RemoveComponents", "Remove {0}|plural(one=Component, other=Components)"), NumSelectedComponents));

		Toolkit->DeleteSelectedComponents();
	}
}

void SDMXPixelMappingHierarchyView::SetFilterText(const FText& Text)
{
	using namespace UE::DMX::PixelMappingEditor::SDMXPixelMappingHierarchyView::Private;
	FTreeExpansionSnapshot ExpansionSnapshot = FTreeExpansionSnapshot::TakeSnapshot(AllRootItems, HierarchyTreeView);

	FilterHandler->SetIsEnabled(!Text.IsEmpty());
	SearchFilter->SetRawFilterText(Text);
	FilterHandler->RefreshAndFilterTree();

	ExpansionSnapshot.RestoreExpandedAndExpandNewModels(AllRootItems, HierarchyTreeView);
}

void SDMXPixelMappingHierarchyView::GetWidgetFilterStrings(FDMXPixelMappingHierarchyItemWidgetModelPtr InModel, TArray<FString>& OutStrings) const
{
	OutStrings.Add(InModel->GetComponentNameText().ToString());
	OutStrings.Add(InModel->GetFixtureIDText().ToString());
	OutStrings.Add(InModel->GetPatchText().ToString());
}

#undef LOCTEXT_NAMESPACE
