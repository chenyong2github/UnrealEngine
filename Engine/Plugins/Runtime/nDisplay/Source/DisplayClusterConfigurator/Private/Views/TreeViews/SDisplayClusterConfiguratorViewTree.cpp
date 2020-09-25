// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/TreeViews/SDisplayClusterConfiguratorViewTree.h"

#include "DisplayClusterConfiguratorToolkit.h"
#include "Interfaces/Views/TreeViews/IDisplayClusterConfiguratorTreeBuilder.h"
#include "Interfaces/Views/TreeViews/IDisplayClusterConfiguratorTreeItem.h"
#include "Interfaces/Views/TreeViews/IDisplayClusterConfiguratorViewTree.h"
#include "Views/TreeViews/DisplayClusterConfiguratorTreeViewCommands.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IPinnedCommandList.h"
#include "Modules/ModuleManager.h"
#include "Misc/TextFilterExpressionEvaluator.h"
#include "UICommandList_Pinnable.h"
#include "UObject/PackageReload.h"
#include "UObject/UObjectGlobals.h"
#include "Types/ISlateMetaData.h"
#include "Widgets/Views/STreeView.h"
#include "Widgets/Layout/SScrollBorder.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SComboButton.h"

#define LOCTEXT_NAMESPACE "SDisplayClusterConfiguratorViewTree"

void SDisplayClusterConfiguratorViewTree::Construct(const FArguments& InArgs,
	const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit,
	const TSharedRef<IDisplayClusterConfiguratorTreeBuilder>& InBuilder,
	const TSharedRef<IDisplayClusterConfiguratorViewTree>& InViewTree,
	const FDisplayClusterConfiguratorTreeArgs& InTreeArgs)
{
	// Set properties
	BuilderPtr = InBuilder;
	ToolkitPtr = InToolkit;
	ViewTreePtr = InViewTree;

	Mode = InTreeArgs.Mode;

	ContextName = InTreeArgs.ContextName;

	TextFilterPtr = MakeShared<FTextFilterExpressionEvaluator>(ETextFilterExpressionEvaluatorMode::BasicString);

	// Register delegates
	FCoreUObjectDelegates::OnPackageReloaded.AddSP(this, &SDisplayClusterConfiguratorViewTree::HandlePackageReloaded);

	// Create our pinned commands before we bind commands
	IPinnedCommandListModule& PinnedCommandListModule = FModuleManager::LoadModuleChecked<IPinnedCommandListModule>(TEXT("PinnedCommandList"));
	PinnedCommands = PinnedCommandListModule.CreatePinnedCommandList(ContextName);

	// Register and bind all our menu commands
	FDisplayClusterConfiguratorTreeViewCommands::Register();
	BindCommands();

	SDisplayClusterConfiguratorViewBase::Construct(
		SDisplayClusterConfiguratorViewBase::FArguments()
		.Padding(0.0f)
		.Content()
		[
			SNew( SOverlay )
			+SOverlay::Slot()
			[
				// Add a border if we are being used as a picker
				SNew(SBorder)
				.Visibility_Lambda([this](){ return Mode == EDisplayClusterConfiguratorTreeMode::Picker ? EVisibility::Visible: EVisibility::Collapsed; })
				.BorderImage(FEditorStyle::Get().GetBrush("Menu.Background"))
			]
			+SOverlay::Slot()
			[
				SNew( SVerticalBox )
		
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 0.0f, 0.0f, 2.0f))
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, InTreeArgs.bShowFilterMenu ? 2.0f : 0.0f, 0.0f)
					[
						SAssignNew(FilterComboButton, SComboButton)
						.Visibility(InTreeArgs.bShowFilterMenu ? EVisibility::Visible : EVisibility::Collapsed)
						.ComboButtonStyle(FEditorStyle::Get(), "GenericFilters.ComboButtonStyle")
						.ForegroundColor(FLinearColor::White)
						.ContentPadding(0.0f)
						.OnGetMenuContent( this, &SDisplayClusterConfiguratorViewTree::CreateFilterMenu )
						.ToolTipText( this, &SDisplayClusterConfiguratorViewTree::GetFilterMenuTooltip )
						.AddMetaData<FTagMetaData>(TEXT("ConfiguratorViewTree.Items"))
						.ButtonContent()
						[
							SNew(SHorizontalBox)
							+SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.TextStyle(FEditorStyle::Get(), "GenericFilters.TextStyle")
								.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.9"))
								.Text(FText::FromString(FString(TEXT("\xf0b0"))) /*fa-filter*/)
							]
							+SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(2, 0, 0, 0)
							.VAlign(VAlign_Center)
							[
								SNew( STextBlock )
								.TextStyle(FEditorStyle::Get(), "GenericFilters.TextStyle")
								.Text( LOCTEXT("FilterMenuLabel", "Options") )
							]
						]
					]
					+SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SAssignNew( NameFilterBox, SSearchBox )
						.SelectAllTextWhenFocused( true )
						.OnTextChanged( this, &SDisplayClusterConfiguratorViewTree::OnFilterTextChanged )
						.HintText( LOCTEXT( "SearchBoxHint", "Search Config Tree...") )
						.AddMetaData<FTagMetaData>(TEXT("ConfiguratorViewTree.Search"))
					]
				]

				+ SVerticalBox::Slot()
				.Padding( FMargin( 0.0f, 2.0f, 0.0f, 0.0f ) )
				.AutoHeight()
				[
					PinnedCommands.ToSharedRef()
				]

				+ SVerticalBox::Slot()
				.Padding( FMargin( 0.0f, 2.0f, 0.0f, 0.0f ) )
				[
					SAssignNew(TreeHolder, SOverlay)
				]
			]
		],
		InToolkit);

	CreateTreeColumns();
}

void SDisplayClusterConfiguratorViewTree::Refresh()
{
	RebuildTree();
}

void SDisplayClusterConfiguratorViewTree::CreateTreeColumns()
{
	TSharedRef<SHeaderRow> TreeHeaderRow =
		SNew(SHeaderRow)
		.Visibility(EVisibility::Collapsed)
		+ SHeaderRow::Column(IDisplayClusterConfiguratorViewTree::Columns::Item)
		.DefaultLabel(LOCTEXT("DisplayClusterConfiguratorNameLabel", "Items"))
		.FillWidth(0.5f);
	{
		ConfigTreeView = SNew(STreeView<TSharedPtr<IDisplayClusterConfiguratorTreeItem>>)
			.TreeItemsSource(&FilteredItems)
			.OnGenerateRow(this, &SDisplayClusterConfiguratorViewTree::MakeTreeRowWidget)
			.OnGetChildren(this, &SDisplayClusterConfiguratorViewTree::GetFilteredChildren)
			.OnContextMenuOpening(this, &SDisplayClusterConfiguratorViewTree::CreateContextMenu)
			.OnSelectionChanged(this, &SDisplayClusterConfiguratorViewTree::OnSelectionChanged)
			.OnItemScrolledIntoView(this, &SDisplayClusterConfiguratorViewTree::OnItemScrolledIntoView)
			.OnMouseButtonDoubleClick(this, &SDisplayClusterConfiguratorViewTree::OnTreeDoubleClick)
			.OnSetExpansionRecursive(this, &SDisplayClusterConfiguratorViewTree::SetTreeItemExpansionRecursive)
			.ItemHeight(24)
			.HighlightParentNodesForSelection(true)
			.HeaderRow
			(
				TreeHeaderRow
			);

		TreeHolder->ClearChildren();
		TreeHolder->AddSlot()
			[
				SNew(SScrollBorder, ConfigTreeView.ToSharedRef())
				[
					ConfigTreeView.ToSharedRef()
				]
			];
	}

	RebuildTree();
}

void SDisplayClusterConfiguratorViewTree::RebuildTree()
{
	// Save selected items
	Items.Empty();
	LinearItems.Empty();
	FilteredItems.Empty();

	FDisplayClusterConfiguratorTreeBuilderOutput Output(Items, LinearItems);
	BuilderPtr.Pin()->Build(Output);
	ApplyFilter();
}

void SDisplayClusterConfiguratorViewTree::ApplyFilter()
{
	TextFilterPtr->SetFilterText(FilterText);

	FilteredItems.Empty();

	FDisplayClusterConfiguratorTreeFilterArgs FilterArgs(!FilterText.IsEmpty() ? TextFilterPtr : nullptr);
	BuilderPtr.Pin()->Filter(FilterArgs, Items, FilteredItems);

	if (!FilterText.IsEmpty())
	{
		for (TSharedPtr<IDisplayClusterConfiguratorTreeItem>& Item : LinearItems)
		{
			if (Item->GetFilterResult() > EDisplayClusterConfiguratorTreeFilterResult::Hidden)
			{
				ConfigTreeView->SetItemExpansion(Item, true);
			}
		}
	}
	else
	{
		SetInitialExpansionState();
	}

	ConfigTreeView->RequestTreeRefresh();
}

void SDisplayClusterConfiguratorViewTree::SetInitialExpansionState()
{
	for (TSharedPtr<IDisplayClusterConfiguratorTreeItem>& Item : LinearItems)
	{
		ConfigTreeView->SetItemExpansion(Item, Item->IsInitiallyExpanded());

		if (Item->IsSelected())
		{
			ConfigTreeView->SetItemSelection(Item, true, ESelectInfo::Direct);
			ConfigTreeView->RequestScrollIntoView(Item);
		}
	}
}

TSharedRef<ITableRow> SDisplayClusterConfiguratorViewTree::MakeTreeRowWidget(TSharedPtr<IDisplayClusterConfiguratorTreeItem> InInfo, const TSharedRef<STableViewBase>& OwnerTable)
{
	check(InInfo.IsValid());

	return InInfo->MakeTreeRowWidget(OwnerTable, TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateLambda([this]() { return FilterText; })));
}

void SDisplayClusterConfiguratorViewTree::GetFilteredChildren(TSharedPtr<IDisplayClusterConfiguratorTreeItem> InInfo, TArray<TSharedPtr<IDisplayClusterConfiguratorTreeItem>>& OutChildren)
{
	check(InInfo.IsValid());
	OutChildren = InInfo->GetFilteredChildren();
}

TSharedPtr<SWidget> SDisplayClusterConfiguratorViewTree::CreateContextMenu()
{
	const bool CloseAfterSelection = true;
	FMenuBuilder MenuBuilder(CloseAfterSelection, UICommandList, Extenders);

	return MenuBuilder.MakeWidget();
}

void SDisplayClusterConfiguratorViewTree::OnSelectionChanged(TSharedPtr<IDisplayClusterConfiguratorTreeItem> Selection, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo != ESelectInfo::Direct)
	{
		ViewTreePtr.Pin()->ClearSelectedItem();

		TArray<TSharedPtr<IDisplayClusterConfiguratorTreeItem>> SelectedItems = ConfigTreeView->GetSelectedItems();
		for (TSharedPtr<IDisplayClusterConfiguratorTreeItem>& Item : SelectedItems)
		{
			Item->OnSelection();
			ViewTreePtr.Pin()->SetSelectedItem(Item.ToSharedRef());
		}
	}
}

void SDisplayClusterConfiguratorViewTree::SetTreeItemExpansionRecursive(TSharedPtr<IDisplayClusterConfiguratorTreeItem> TreeItem, bool bInExpansionState) const
{
	ConfigTreeView->SetItemExpansion(TreeItem, bInExpansionState);

	// Recursively go through the children.
	for (auto It = TreeItem->GetChildren().CreateIterator(); It; ++It)
	{
		SetTreeItemExpansionRecursive(*It, bInExpansionState);
	}
}

TSharedRef<SWidget> SDisplayClusterConfiguratorViewTree::CreateFilterMenu()
{
	const FDisplayClusterConfiguratorTreeViewCommands& Actions = FDisplayClusterConfiguratorTreeViewCommands::Get();

	const bool CloseAfterSelection = true;
	FMenuBuilder MenuBuilder(CloseAfterSelection, UICommandList, Extenders);

	return MenuBuilder.MakeWidget();
}

FText SDisplayClusterConfiguratorViewTree::GetFilterMenuTooltip() const
{
	return FText::GetEmpty();
}

void SDisplayClusterConfiguratorViewTree::OnConfigReloaded()
{
	RebuildTree();
}

void SDisplayClusterConfiguratorViewTree::OnObjectSelected()
{
	ConfigTreeView->ClearSelection();

	RebuildTree();
}

EDisplayClusterConfiguratorTreeFilterResult SDisplayClusterConfiguratorViewTree::HandleFilterConfiguratonTreeItem(const FDisplayClusterConfiguratorTreeFilterArgs& InArgs, const TSharedPtr<IDisplayClusterConfiguratorTreeItem>& InItem)
{
	EDisplayClusterConfiguratorTreeFilterResult Result = EDisplayClusterConfiguratorTreeFilterResult::Shown;

	return Result;
}

#undef LOCTEXT_NAMESPACE
