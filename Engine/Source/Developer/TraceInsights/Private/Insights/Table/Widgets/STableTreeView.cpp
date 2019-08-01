// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "STableTreeView.h"

#include "EditorStyleSet.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "SlateOptMacros.h"
#include "TraceServices/AnalysisService.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Views/STableViewBase.h"

// Insights
#include "Insights/InsightsManager.h"
#include "Insights/Table/ViewModels/Table.h"
#include "Insights/Table/ViewModels/TableColumn.h"
#include "Insights/Table/ViewModels/TableTreeNode.h"
#include "Insights/Table/ViewModels/TreeNodeGrouping.h"
#include "Insights/Table/ViewModels/TreeNodeSorting.h"
#include "Insights/Table/Widgets/STableTreeViewTooltip.h"
#include "Insights/Table/Widgets/STableTreeViewRow.h"

#define LOCTEXT_NAMESPACE "STableTreeView"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

const FName STableTreeView::DefaultColumnBeingSorted;
const EColumnSortMode::Type STableTreeView::DefaultColumnSortMode(EColumnSortMode::Descending);

////////////////////////////////////////////////////////////////////////////////////////////////////

STableTreeView::STableTreeView()
	: Table()
	, Session(FInsightsManager::Get()->GetSession())
	, TreeView(nullptr)
	, TreeViewHeaderColumnArgs()
	, TreeViewHeaderRow(nullptr)
	, ExternalScrollbar(nullptr)
	, HoveredColumnId()
	, HoveredNodePtr(nullptr)
	, HighlightedNodeName()
	, TableTreeNodes()
	, GroupNodes()
	, FilteredGroupNodes()
	, TableTreeNodesIdMap()
	, ExpandedNodes()
	, bExpansionSaved(false)
	, SearchBox(nullptr)
	, TextFilter(nullptr)
	, Filters(nullptr)
	, AvailableGroupings()
	, GroupByComboBox(nullptr)
	, CurrentGrouping(nullptr)
	, AvailableSortings()
	, ColumnToSortingMap()
	, CurrentSorting(nullptr)
	, ColumnBeingSorted(DefaultColumnBeingSorted)
	, ColumnSortMode(DefaultColumnSortMode)
	, StatsStartTime(0.0)
	, StatsEndTime(0.0)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

STableTreeView::~STableTreeView()
{
	// Remove ourselves from the Insights manager.
	if (FInsightsManager::Get().IsValid())
	{
		FInsightsManager::Get()->GetSessionChangedEvent().RemoveAll(this);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void STableTreeView::Construct(const FArguments& InArgs, TSharedPtr<FTable> InTablePtr)
{
	check(InTablePtr.IsValid());
	Table = InTablePtr;

	SAssignNew(ExternalScrollbar, SScrollBar)
	.AlwaysShowScrollbar(true);

	ChildSlot
	[
		SNew(SVerticalBox)

		+SVerticalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(2.0f)
			[
				SNew(SVerticalBox)

				// Search box
				+SVerticalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(2.0f)
				.AutoHeight()
				[
					SAssignNew(SearchBox, SSearchBox)
					.HintText(LOCTEXT("SearchBoxHint", "Search timers or groups"))
					.OnTextChanged(this, &STableTreeView::SearchBox_OnTextChanged)
					.IsEnabled(this, &STableTreeView::SearchBox_IsEnabled)
					.ToolTipText(LOCTEXT("FilterSearchHint", "Type here to search timer or group"))
				]

				// Group by
				+SVerticalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(2.0f)
				.AutoHeight()
				[
					SNew(SHorizontalBox)

					+SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("GroupByText", "Group by"))
					]

					+SHorizontalBox::Slot()
					.FillWidth(2.0f)
					.VAlign(VAlign_Center)
					[
						SAssignNew(GroupByComboBox, SComboBox<TSharedPtr<FTreeNodeGrouping>>)
						.ToolTipText(this, &STableTreeView::GroupBy_GetSelectedTooltipText)
						.OptionsSource(&AvailableGroupings)
						.OnSelectionChanged(this, &STableTreeView::GroupBy_OnSelectionChanged)
						.OnGenerateWidget(this, &STableTreeView::GroupBy_OnGenerateWidget)
						[
							SNew(STextBlock)
							.Text(this, &STableTreeView::GroupBy_GetSelectedText)
						]
					]
				]
			]
		]

		// Tree view
		+SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0.0f)
			[
				SNew(SScrollBox)
				.Orientation(Orient_Horizontal)

				+ SScrollBox::Slot()
				[
					SNew(SBorder)
					.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(0.0f)
					[
						SAssignNew(TreeView, STreeView<FTableTreeNodePtr>)
						.ExternalScrollbar(ExternalScrollbar)
						.SelectionMode(ESelectionMode::Multi)
						.TreeItemsSource(&FilteredGroupNodes)
						.OnGetChildren(this, &STableTreeView::TreeView_OnGetChildren)
						.OnGenerateRow(this, &STableTreeView::TreeView_OnGenerateRow)
						.OnSelectionChanged(this, &STableTreeView::TreeView_OnSelectionChanged)
						.OnMouseButtonDoubleClick(this, &STableTreeView::TreeView_OnMouseButtonDoubleClick)
						.OnContextMenuOpening(FOnContextMenuOpening::CreateSP(this, &STableTreeView::TreeView_GetMenuContent))
						.ItemHeight(12.0f)
						.HeaderRow
						(
							SAssignNew(TreeViewHeaderRow, SHeaderRow)
							.Visibility(EVisibility::Visible)
						)
					]
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f)
			[
				SNew(SBox)
				.WidthOverride(FOptionalSize(13.0f))
				[
					ExternalScrollbar.ToSharedRef()
				]
			]
		]
	];

	InitializeAndShowHeaderColumns();
	//BindCommands();

	// Create the search filters: text based, type based etc.
	TextFilter = MakeShareable(new FTableTreeNodeTextFilter(FTableTreeNodeTextFilter::FItemToStringArray::CreateSP(this, &STableTreeView::HandleItemToStringArray)));
	Filters = MakeShareable(new FTableTreeNodeFilterCollection());
	Filters->Add(TextFilter);

	CreateGroupings();
	CreateSortings();

	// Register ourselves with the Insights manager.
	FInsightsManager::Get()->GetSessionChangedEvent().AddSP(this, &STableTreeView::InsightsManager_OnSessionChanged);
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<SWidget> STableTreeView::TreeView_GetMenuContent()
{
	const TArray<FTableTreeNodePtr> SelectedNodes = TreeView->GetSelectedItems();
	const int32 NumSelectedNodes = SelectedNodes.Num();
	FTableTreeNodePtr SelectedNode = NumSelectedNodes ? SelectedNodes[0] : nullptr;

	const TSharedPtr<FTableColumn> HoveredColumnPtr = Table->FindColumn(HoveredColumnId);

	FText SelectionStr;
	FText PropertyName;
	FText PropertyValue;

	if (NumSelectedNodes == 0)
	{
		SelectionStr = LOCTEXT("NothingSelected", "Nothing selected");
	}
	else if (NumSelectedNodes == 1)
	{
		if (HoveredColumnPtr != nullptr)
		{
			PropertyName = HoveredColumnPtr->GetShortName();
			PropertyValue = HoveredColumnPtr->GetValueAsText(SelectedNode->GetRowId());
		}
		SelectionStr = FText::FromName(SelectedNode->GetName());
	}
	else
	{
		SelectionStr = LOCTEXT("MultipleSelection", "Multiple selection");
	}

	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, NULL);

	// Selection menu
	MenuBuilder.BeginSection("Selection", LOCTEXT("ContextMenu_Header_Selection", "Selection"));
	{
		struct FLocal
		{
			static bool ReturnFalse()
			{
				return false;
			}
		};

		FUIAction DummyUIAction;
		DummyUIAction.CanExecuteAction = FCanExecuteAction::CreateStatic(&FLocal::ReturnFalse);
		MenuBuilder.AddMenuEntry
		(
			SelectionStr,
			LOCTEXT("ContextMenu_Selection", "Currently selected items"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "@missing.icon"), DummyUIAction, NAME_None, EUserInterfaceActionType::Button
		);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("Misc", LOCTEXT("ContextMenu_Header_Misc", "Miscellaneous"));
	{
		/*TODO
		FUIAction Action_CopySelectedToClipboard
		(
			FExecuteAction::CreateSP(this, &STableTreeView::ContextMenu_CopySelectedToClipboard_Execute),
			FCanExecuteAction::CreateSP(this, &STableTreeView::ContextMenu_CopySelectedToClipboard_CanExecute)
		);
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_Header_Misc_CopySelectedToClipboard", "Copy To Clipboard"),
			LOCTEXT("ContextMenu_Header_Misc_CopySelectedToClipboard_Desc", "Copies selection to clipboard"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Profiler.Misc.CopyToClipboard"), Action_CopySelectedToClipboard, NAME_None, EUserInterfaceActionType::Button
		);
		*/

		MenuBuilder.AddSubMenu
		(
			LOCTEXT("ContextMenu_Header_Misc_Sort", "Sort By"),
			LOCTEXT("ContextMenu_Header_Misc_Sort_Desc", "Sort by column"),
			FNewMenuDelegate::CreateSP(this, &STableTreeView::TreeView_BuildSortByMenu),
			false,
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Profiler.Misc.SortBy")
		);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("Columns", LOCTEXT("ContextMenu_Header_Columns", "Columns"));
	{
		MenuBuilder.AddSubMenu
		(
			LOCTEXT("ContextMenu_Header_Columns_View", "View Column"),
			LOCTEXT("ContextMenu_Header_Columns_View_Desc", "Hides or shows columns"),
			FNewMenuDelegate::CreateSP(this, &STableTreeView::TreeView_BuildViewColumnMenu),
			false,
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Profiler.EventGraph.ViewColumn")
		);

		FUIAction Action_ShowAllColumns
		(
			FExecuteAction::CreateSP(this, &STableTreeView::ContextMenu_ShowAllColumns_Execute),
			FCanExecuteAction::CreateSP(this, &STableTreeView::ContextMenu_ShowAllColumns_CanExecute)
		);
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_Header_Columns_ShowAllColumns", "Show All Columns"),
			LOCTEXT("ContextMenu_Header_Columns_ShowAllColumns_Desc", "Resets tree view to show all columns"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Profiler.EventGraph.ResetColumn"), Action_ShowAllColumns, NAME_None, EUserInterfaceActionType::Button
		);

		FUIAction Action_ResetColumns
		(
			FExecuteAction::CreateSP(this, &STableTreeView::ContextMenu_ResetColumns_Execute),
			FCanExecuteAction::CreateSP(this, &STableTreeView::ContextMenu_ResetColumns_CanExecute)
		);
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_Header_Columns_ResetColumns", "Reset Columns to Default"),
			LOCTEXT("ContextMenu_Header_Columns_ResetColumns_Desc", "Resets columns to default"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Profiler.EventGraph.ResetColumn"), Action_ResetColumns, NAME_None, EUserInterfaceActionType::Button
		);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeView::TreeView_BuildSortByMenu(FMenuBuilder& MenuBuilder)
{
	// TODO: Refactor later @see TSharedPtr<SWidget> SCascadePreviewViewportToolBar::GenerateViewMenu() const

	MenuBuilder.BeginSection("ColumnName", LOCTEXT("ContextMenu_Header_Misc_ColumnName", "Column Name"));

	//TODO: for (Sorting : AvailableSortings)
	for (const TSharedPtr<FTableColumn>& ColumnPtr : Table->GetColumns())
	{
		const FTableColumn& Column = *ColumnPtr;

		if (Column.IsVisible() && Column.CanBeSorted())
		{
			FUIAction Action_SortByColumn
			(
				FExecuteAction::CreateSP(this, &STableTreeView::ContextMenu_SortByColumn_Execute, Column.GetId()),
				FCanExecuteAction::CreateSP(this, &STableTreeView::ContextMenu_SortByColumn_CanExecute, Column.GetId()),
				FIsActionChecked::CreateSP(this, &STableTreeView::ContextMenu_SortByColumn_IsChecked, Column.GetId())
			);
			MenuBuilder.AddMenuEntry
			(
				Column.GetTitleName(),
				Column.GetDescription(),
				FSlateIcon(), Action_SortByColumn, NAME_None, EUserInterfaceActionType::RadioButton
			);
		}
	}

	MenuBuilder.EndSection();

	//-----------------------------------------------------------------------------

	MenuBuilder.BeginSection("SortMode", LOCTEXT("ContextMenu_Header_Misc_Sort_SortMode", "Sort Mode"));
	{
		FUIAction Action_SortAscending
		(
			FExecuteAction::CreateSP(this, &STableTreeView::ContextMenu_SortMode_Execute, EColumnSortMode::Ascending),
			FCanExecuteAction::CreateSP(this, &STableTreeView::ContextMenu_SortMode_CanExecute, EColumnSortMode::Ascending),
			FIsActionChecked::CreateSP(this, &STableTreeView::ContextMenu_SortMode_IsChecked, EColumnSortMode::Ascending)
		);
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_Header_Misc_Sort_SortAscending", "Sort Ascending"),
			LOCTEXT("ContextMenu_Header_Misc_Sort_SortAscending_Desc", "Sorts ascending"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Profiler.Misc.SortAscending"), Action_SortAscending, NAME_None, EUserInterfaceActionType::RadioButton
		);

		FUIAction Action_SortDescending
		(
			FExecuteAction::CreateSP(this, &STableTreeView::ContextMenu_SortMode_Execute, EColumnSortMode::Descending),
			FCanExecuteAction::CreateSP(this, &STableTreeView::ContextMenu_SortMode_CanExecute, EColumnSortMode::Descending),
			FIsActionChecked::CreateSP(this, &STableTreeView::ContextMenu_SortMode_IsChecked, EColumnSortMode::Descending)
		);
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_Header_Misc_Sort_SortDescending", "Sort Descending"),
			LOCTEXT("ContextMenu_Header_Misc_Sort_SortDescending_Desc", "Sorts descending"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Profiler.Misc.SortDescending"), Action_SortDescending, NAME_None, EUserInterfaceActionType::RadioButton
		);
	}
	MenuBuilder.EndSection();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeView::TreeView_BuildViewColumnMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection("ViewColumn", LOCTEXT("ContextMenu_Header_Columns_View", "View Column"));

	for (const TSharedPtr<FTableColumn>& ColumnPtr : Table->GetColumns())
	{
		const FTableColumn& Column = *ColumnPtr;

		FUIAction Action_ToggleColumn
		(
			FExecuteAction::CreateSP(this, &STableTreeView::ToggleColumnVisibility, Column.GetId()),
			FCanExecuteAction::CreateSP(this, &STableTreeView::CanToggleColumnVisibility, Column.GetId()),
			FIsActionChecked::CreateSP(this, &STableTreeView::IsColumnVisible, Column.GetId())
		);
		MenuBuilder.AddMenuEntry
		(
			Column.GetTitleName(),
			Column.GetDescription(),
			FSlateIcon(), Action_ToggleColumn, NAME_None, EUserInterfaceActionType::ToggleButton
		);
	}

	MenuBuilder.EndSection();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeView::InitializeAndShowHeaderColumns()
{
	for (const TSharedPtr<FTableColumn>& ColumnPtr : Table->GetColumns())
	{
		TreeViewHeaderRow_CreateColumnArgs(*ColumnPtr);

		if (ColumnPtr->ShouldBeVisible())
		{
			ShowColumn(ColumnPtr->GetId());
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeView::TreeViewHeaderRow_CreateColumnArgs(const FTableColumn& Column)
{
	SHeaderRow::FColumn::FArguments ColumnArgs;

	ColumnArgs
		.ColumnId(Column.GetId())
		.DefaultLabel(Column.GetShortName())
		.SortMode(EColumnSortMode::None)
		.HAlignHeader(HAlign_Fill)
		.VAlignHeader(VAlign_Fill)
		.HeaderContentPadding(TOptional<FMargin>(2.0f))
		.HAlignCell(HAlign_Fill)
		.VAlignCell(VAlign_Fill)
		.SortMode(this, &STableTreeView::GetSortModeForColumn, Column.GetId())
		.OnSort(this, &STableTreeView::OnSortModeChanged)
		.ManualWidth(Column.GetInitialWidth())
		.FixedWidth(Column.IsFixedWidth() ? Column.GetInitialWidth() : TOptional<float>())
		.HeaderContent()
		[
			SNew(SHorizontalBox)
			.ToolTip(STableTreeViewTooltip::GetColumnTooltip(Column))

			+ SHorizontalBox::Slot()
			.HAlign(Column.GetHorizontalAlignment())
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Column.GetShortName())
			]
		]
		.MenuContent()
		[
			TreeViewHeaderRow_GenerateColumnMenu(Column)
		];

	TreeViewHeaderColumnArgs.Add(Column.GetId(), ColumnArgs);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> STableTreeView::TreeViewHeaderRow_GenerateColumnMenu(const FTableColumn& Column)
{
	bool bIsMenuVisible = false;

	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, NULL);
	{
		if (Column.CanBeHidden())
		{
			MenuBuilder.BeginSection("Column", LOCTEXT("TreeViewHeaderRow_Header_Column", "Column"));

			FUIAction Action_HideColumn
			(
				FExecuteAction::CreateSP(this, &STableTreeView::HideColumn, Column.GetId()),
				FCanExecuteAction::CreateSP(this, &STableTreeView::CanHideColumn, Column.GetId())
			);

			MenuBuilder.AddMenuEntry
			(
				LOCTEXT("TreeViewHeaderRow_HideColumn", "Hide"),
				LOCTEXT("TreeViewHeaderRow_HideColumn_Desc", "Hides the selected column"),
				FSlateIcon(), Action_HideColumn, NAME_None, EUserInterfaceActionType::Button
			);
			bIsMenuVisible = true;

			MenuBuilder.EndSection();
		}

		if (Column.CanBeSorted())
		{
			MenuBuilder.BeginSection("SortMode", LOCTEXT("ContextMenu_Header_Misc_Sort_SortMode", "Sort Mode"));

			FUIAction Action_SortAscending
			(
				FExecuteAction::CreateSP(this, &STableTreeView::HeaderMenu_SortMode_Execute, Column.GetId(), EColumnSortMode::Ascending),
				FCanExecuteAction::CreateSP(this, &STableTreeView::HeaderMenu_SortMode_CanExecute, Column.GetId(), EColumnSortMode::Ascending),
				FIsActionChecked::CreateSP(this, &STableTreeView::HeaderMenu_SortMode_IsChecked, Column.GetId(), EColumnSortMode::Ascending)
			);
			MenuBuilder.AddMenuEntry
			(
				LOCTEXT("ContextMenu_Header_Misc_Sort_SortAscending", "Sort Ascending"),
				LOCTEXT("ContextMenu_Header_Misc_Sort_SortAscending_Desc", "Sorts ascending"),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "Profiler.Misc.SortAscending"), Action_SortAscending, NAME_None, EUserInterfaceActionType::RadioButton
			);

			FUIAction Action_SortDescending
			(
				FExecuteAction::CreateSP(this, &STableTreeView::HeaderMenu_SortMode_Execute, Column.GetId(), EColumnSortMode::Descending),
				FCanExecuteAction::CreateSP(this, &STableTreeView::HeaderMenu_SortMode_CanExecute, Column.GetId(), EColumnSortMode::Descending),
				FIsActionChecked::CreateSP(this, &STableTreeView::HeaderMenu_SortMode_IsChecked, Column.GetId(), EColumnSortMode::Descending)
			);
			MenuBuilder.AddMenuEntry
			(
				LOCTEXT("ContextMenu_Header_Misc_Sort_SortDescending", "Sort Descending"),
				LOCTEXT("ContextMenu_Header_Misc_Sort_SortDescending_Desc", "Sorts descending"),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "Profiler.Misc.SortDescending"), Action_SortDescending, NAME_None, EUserInterfaceActionType::RadioButton
			);
			bIsMenuVisible = true;

			MenuBuilder.EndSection();
		}

		if (Column.CanBeFiltered())
		{
			MenuBuilder.BeginSection("FilterMode", LOCTEXT("ContextMenu_Header_Misc_Filter_FilterMode", "Filter Mode"));
			bIsMenuVisible = true;
			MenuBuilder.EndSection();
		}
	}

	return bIsMenuVisible ? MenuBuilder.MakeWidget() : (TSharedRef<SWidget>)SNullWidget::NullWidget;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeView::InsightsManager_OnSessionChanged()
{
	TSharedPtr<const Trace::IAnalysisSession> NewSession = FInsightsManager::Get()->GetSession();

	if (NewSession != Session)
	{
		Session = NewSession;
		RebuildTree();
	}
	else
	{
		UpdateTree();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeView::UpdateTree()
{
	CreateGroups();
	SortTreeNodes();
	ApplyFiltering();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeView::ApplyFiltering()
{
	FilteredGroupNodes.Reset();

	// Apply filter to all groups and its children.
	const int32 NumGroups = GroupNodes.Num();
	for (int32 GroupIndex = 0; GroupIndex < NumGroups; ++GroupIndex)
	{
		FTableTreeNodePtr& GroupPtr = GroupNodes[GroupIndex];
		GroupPtr->ClearFilteredChildren();
		const bool bIsGroupVisible = Filters->PassesAllFilters(GroupPtr);

		const TArray<FBaseTreeNodePtr>& GroupChildren = GroupPtr->GetChildren();
		const int32 NumChildren = GroupChildren.Num();
		int32 NumVisibleChildren = 0;
		for (int32 Cx = 0; Cx < NumChildren; ++Cx)
		{
			// Add a child.
			const FTableTreeNodePtr& NodePtr = StaticCastSharedPtr<FTableTreeNode>(GroupChildren[Cx]);
			const bool bIsChildVisible = Filters->PassesAllFilters(NodePtr);
			if (bIsChildVisible)
			{
				GroupPtr->AddFilteredChild(NodePtr);
				NumVisibleChildren++;
			}
		}

		if (bIsGroupVisible || NumVisibleChildren > 0)
		{
			// Add a group.
			FilteredGroupNodes.Add(GroupPtr);
			GroupPtr->SetExpansion(true);
		}
		else
		{
			GroupPtr->SetExpansion(false);
		}
	}

	// Only expand timer nodes if we have a text filter.
	const bool bNonEmptyTextFilter = !TextFilter->GetRawFilterText().IsEmpty();
	if (bNonEmptyTextFilter)
	{
		if (!bExpansionSaved)
		{
			ExpandedNodes.Empty();
			TreeView->GetExpandedItems(ExpandedNodes);
			bExpansionSaved = true;
		}

		for (int32 Fx = 0; Fx < FilteredGroupNodes.Num(); Fx++)
		{
			const FTableTreeNodePtr& GroupPtr = FilteredGroupNodes[Fx];
			TreeView->SetItemExpansion(GroupPtr, GroupPtr->IsExpanded());
		}
	}
	else
	{
		if (bExpansionSaved)
		{
			// Restore previously expanded nodes when the text filter is disabled.
			TreeView->ClearExpandedItems();
			for (auto It = ExpandedNodes.CreateConstIterator(); It; ++It)
			{
				TreeView->SetItemExpansion(*It, true);
			}
			bExpansionSaved = false;
		}
	}

	// Request tree refresh
	TreeView->RequestTreeRefresh();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeView::HandleItemToStringArray(const FTableTreeNodePtr& FTableTreeNodePtr, TArray<FString>& OutSearchStrings) const
{
	OutSearchStrings.Add(FTableTreeNodePtr->GetName().GetPlainNameString());
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// TreeView
////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeView::TreeView_Refresh()
{
	if (TreeView.IsValid())
	{
		TreeView->RequestTreeRefresh();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeView::TreeView_OnSelectionChanged(FTableTreeNodePtr SelectedItem, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo != ESelectInfo::Direct)
	{
		TArray<FTableTreeNodePtr> SelectedItems = TreeView->GetSelectedItems();
		if (SelectedItems.Num() == 1)
		{
			//HighlightedNodeName = SelectedItems[0]->GetName();
		}
		else
		{
			//HighlightedNodeName = NAME_None;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeView::TreeView_OnGetChildren(FTableTreeNodePtr InParent, TArray<FTableTreeNodePtr>& OutChildren)
{
	const TArray<FBaseTreeNodePtr>& FilteredChildren = InParent->GetFilteredChildren();
	for (const FBaseTreeNodePtr& NodePtr : FilteredChildren)
	{
		OutChildren.Add(StaticCastSharedPtr<FTableTreeNode>(NodePtr));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeView::TreeView_OnMouseButtonDoubleClick(FTableTreeNodePtr TimerNode)
{
	if (!TimerNode->IsGroup())
	{
		//im:TODO: const bool bIsStatTracked = FTimingProfilerManager::Get()->IsStatTracked(TimerNode->GetId());
	//	if (!bIsStatTracked)
	//	{
	//		// Add a new graph.
	//		FTimingProfilerManager::Get()->TrackStat(TimerNode->GetId());
	//	}
	//	else
	//	{
	//		// Remove a graph
	//		FTimingProfilerManager::Get()->UntrackStat(TimerNode->GetId());
	//	}
	}
	else
	{
		const bool bIsGroupExpanded = TreeView->IsItemExpanded(TimerNode);
		TreeView->SetItemExpansion(TimerNode, !bIsGroupExpanded);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Tree View's Table Row
////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<ITableRow> STableTreeView::TreeView_OnGenerateRow(FTableTreeNodePtr NodePtr, const TSharedRef<STableViewBase>& OwnerTable)
{
	TSharedRef<ITableRow> TableRow =
		SNew(STableTreeViewRow, OwnerTable)
		.OnShouldBeEnabled(this, &STableTreeView::TableRow_ShouldBeEnabled)
		.OnIsColumnVisible(this, &STableTreeView::IsColumnVisible)
		.OnSetHoveredCell(this, &STableTreeView::TableRow_SetHoveredCell)
		.OnGetColumnOutlineHAlignmentDelegate(this, &STableTreeView::TableRow_GetColumnOutlineHAlignment)
		.HighlightText(this, &STableTreeView::TableRow_GetHighlightText)
		.HighlightedNodeName(this, &STableTreeView::TableRow_GetHighlightedNodeName)
		.TablePtr(Table)
		.TableTreeNodePtr(NodePtr);

	return TableRow;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeView::TableRow_SetHoveredCell(TSharedPtr<FTable> TablePtr, TSharedPtr<FTableColumn> ColumnPtr, const FTableTreeNodePtr NodePtr)
{
	HoveredColumnId = ColumnPtr ? ColumnPtr->GetId() : FName();

	const bool bIsAnyMenusVisible = FSlateApplication::Get().AnyMenusVisible();
	if (!HasMouseCapture() && !bIsAnyMenusVisible)
	{
		HoveredNodePtr = NodePtr;
	}

	//UE_LOG(TimingProfiler, Log, TEXT("%s -> %s"), *HoveredColumnId.GetPlainNameString(), NodePtr.IsValid() ? *NodePtr->GetName().GetPlainNameString() : TEXT("nullptr"));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EHorizontalAlignment STableTreeView::TableRow_GetColumnOutlineHAlignment(const FName ColumnId) const
{
	const TIndirectArray<SHeaderRow::FColumn>& Columns = TreeViewHeaderRow->GetColumns();
	const int32 LastColumnIdx = Columns.Num() - 1;

	// First column
	if (Columns[0].ColumnId == ColumnId)
	{
		return HAlign_Left;
	}
	// Last column
	else if (Columns[LastColumnIdx].ColumnId == ColumnId)
	{
		return HAlign_Right;
	}
	// Middle columns
	{
		return HAlign_Center;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText STableTreeView::TableRow_GetHighlightText() const
{
	return SearchBox->GetText();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FName STableTreeView::TableRow_GetHighlightedNodeName() const
{
	return HighlightedNodeName;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool STableTreeView::TableRow_ShouldBeEnabled(const uint32 TimerId) const
{
	return true;//im:TODO: Session->GetAggregatedStat(TimerId) != nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// SearchBox
////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeView::SearchBox_OnTextChanged(const FText& InFilterText)
{
	TextFilter->SetRawFilterText(InFilterText);
	SearchBox->SetError(TextFilter->GetFilterErrorText());
	ApplyFiltering();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool STableTreeView::SearchBox_IsEnabled() const
{
	return TableTreeNodes.Num() > 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Grouping
////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeView::CreateGroups()
{
	GroupNodes.Reset();
	TMap<FName, FTableTreeNodePtr> GroupMap;

	for (const FTableTreeNodePtr& NodePtr : TableTreeNodes)
	{
		FTreeNodeGroupInfo GroupInfo = CurrentGrouping->GetGroupForNode(NodePtr);

		FTableTreeNodePtr GroupPtr = nullptr;
		FTableTreeNodePtr* GroupPtrPtr = GroupMap.Find(GroupInfo.Name);
		if (!GroupPtrPtr)
		{
			GroupPtr = MakeShareable(new FTableTreeNode(GroupInfo.Name, Table));

			GroupMap.Add(GroupInfo.Name, GroupPtr);
			GroupNodes.Add(GroupPtr);

			GroupPtr->SetExpansion(GroupInfo.IsExpanded);
			TreeView->SetItemExpansion(GroupPtr, GroupInfo.IsExpanded);
		}
		else
		{
			GroupPtr = *GroupPtrPtr;
		}

		GroupPtr->AddChildAndSetGroupPtr(NodePtr);
	}

	// Sort groups by name.
	GroupNodes.Sort([](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) { return A->GetName().LexicalLess(B->GetName()); });
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeView::CreateGroupings()
{
	AvailableGroupings.Reset(3);

	AvailableGroupings.Add(MakeShareable(new FTreeNodeGroupingFlat()));
	AvailableGroupings.Add(MakeShareable(new FTreeNodeGroupingByNameFirstLetter()));
	AvailableGroupings.Add(MakeShareable(new FTreeNodeGroupingByType()));

	CurrentGrouping = AvailableGroupings[0];

	GroupByComboBox->SetSelectedItem(CurrentGrouping);
	GroupByComboBox->RefreshOptions();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeView::GroupBy_OnSelectionChanged(TSharedPtr<FTreeNodeGrouping> NewGrouping, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo != ESelectInfo::Direct)
	{
		CurrentGrouping = NewGrouping;

		CreateGroups();
		SortTreeNodes();
		ApplyFiltering();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> STableTreeView::GroupBy_OnGenerateWidget(TSharedPtr<FTreeNodeGrouping> InGrouping) const
{
	return SNew(STextBlock)
		.Text(InGrouping->GetName())
		.ToolTipText(InGrouping->GetDescription());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText STableTreeView::GroupBy_GetSelectedText() const
{
	return CurrentGrouping.IsValid() ? CurrentGrouping->GetName() : FText::GetEmpty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText STableTreeView::GroupBy_GetSelectedTooltipText() const
{
	return CurrentGrouping.IsValid() ? CurrentGrouping->GetDescription() : FText::GetEmpty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Sorting
////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeView::CreateSortings()
{
	AvailableSortings.Reset();
	ColumnToSortingMap.Reset();
	CurrentSorting = nullptr;

	TSharedPtr<FTreeNodeSorting> SortingByName = MakeShareable(new FTreeNodeSortingByName());
	AvailableSortings.Add(SortingByName);

	TSharedPtr<FTreeNodeSorting> SortingByType = MakeShareable(new FTreeNodeSortingByType());
	AvailableSortings.Add(SortingByType);

	const Trace::ITableLayout& TableLayout = Table->GetSourceTable()->GetLayout();

	for (const TSharedPtr<FTableColumn> ColumnPtr : Table->GetColumns())
	{
		if (ColumnPtr->CanBeSorted())
		{
			if (ColumnPtr->IsHierarchy())
			{
				ColumnToSortingMap.Add(ColumnPtr->GetId(), SortingByName);
			}
			else
			{
				TSharedPtr<FTreeNodeSorting> Sorting = nullptr;

				switch (TableLayout.GetColumnType(ColumnPtr->GetIndex()))
				{
				case Trace::TableColumnType_Bool:
					Sorting = MakeShareable(new FTreeNodeSortingByBoolValue(ColumnPtr));
					break;
				case Trace::TableColumnType_Int:
					Sorting = MakeShareable(new FTreeNodeSortingByIntValue(ColumnPtr));
					break;
				case Trace::TableColumnType_Float:
					Sorting = MakeShareable(new FTreeNodeSortingByFloatValue(ColumnPtr));
					break;
				case Trace::TableColumnType_Double:
					Sorting = MakeShareable(new FTreeNodeSortingByDoubleValue(ColumnPtr));
					break;
				case Trace::TableColumnType_CString:
					Sorting = MakeShareable(new FTreeNodeSortingByCStringValue(ColumnPtr));
					break;
				}

				if (Sorting)
				{
					AvailableSortings.Add(Sorting);
					ColumnToSortingMap.Add(ColumnPtr->GetId(), Sorting);
				}
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeView::UpdateCurrentSortingByColumn()
{
	TSharedPtr<FTreeNodeSorting>* SorterPtrPtr = ColumnToSortingMap.Find(ColumnBeingSorted);
	CurrentSorting = SorterPtrPtr ? *SorterPtrPtr : nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeView::SortTreeNodes()
{
	if (CurrentSorting.IsValid())
	{
		const Insights::ITreeNodeSorting* Sorter = CurrentSorting.Get();

		const int32 NumGroups = GroupNodes.Num();

		if (ColumnSortMode == EColumnSortMode::Type::Descending)
		{
			for (int32 GroupIndex = 0; GroupIndex < NumGroups; ++GroupIndex)
			{
				GroupNodes[GroupIndex]->SortChildrenDescending(Sorter);
			}
		}
		else
		{
			for (int32 GroupIndex = 0; GroupIndex < NumGroups; ++GroupIndex)
			{
				GroupNodes[GroupIndex]->SortChildrenAscending(Sorter);
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EColumnSortMode::Type STableTreeView::GetSortModeForColumn(const FName ColumnId) const
{
	if (ColumnBeingSorted != ColumnId)
	{
		return EColumnSortMode::None;
	}

	return ColumnSortMode;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeView::SetSortModeForColumn(const FName& ColumnId, const EColumnSortMode::Type SortMode)
{
	ColumnBeingSorted = ColumnId;
	ColumnSortMode = SortMode;
	UpdateCurrentSortingByColumn();

	SortTreeNodes();
	ApplyFiltering();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeView::OnSortModeChanged(const EColumnSortPriority::Type SortPriority, const FName& ColumnId, const EColumnSortMode::Type SortMode)
{
	SetSortModeForColumn(ColumnId, SortMode);
	TreeView_Refresh();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// SortMode action (HeaderMenu)
////////////////////////////////////////////////////////////////////////////////////////////////////

bool STableTreeView::HeaderMenu_SortMode_IsChecked(const FName ColumnId, const EColumnSortMode::Type InSortMode)
{
	return ColumnBeingSorted == ColumnId && ColumnSortMode == InSortMode;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool STableTreeView::HeaderMenu_SortMode_CanExecute(const FName ColumnId, const EColumnSortMode::Type InSortMode) const
{
	FTableColumn& Column = *Table->FindColumnChecked(ColumnId);
	const bool bIsValid = Column.CanBeSorted();

	bool bCanExecute = ColumnBeingSorted != ColumnId ? true : ColumnSortMode != InSortMode;
	bCanExecute = bCanExecute && bIsValid;

	return bCanExecute;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeView::HeaderMenu_SortMode_Execute(const FName ColumnId, const EColumnSortMode::Type InSortMode)
{
	SetSortModeForColumn(ColumnId, InSortMode);
	TreeView_Refresh();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// SortMode action (ContextMenu)
////////////////////////////////////////////////////////////////////////////////////////////////////

bool STableTreeView::ContextMenu_SortMode_IsChecked(const EColumnSortMode::Type InSortMode)
{
	return ColumnSortMode == InSortMode;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool STableTreeView::ContextMenu_SortMode_CanExecute(const EColumnSortMode::Type InSortMode) const
{
	return true; //ColumnSortMode != InSortMode;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeView::ContextMenu_SortMode_Execute(const EColumnSortMode::Type InSortMode)
{
	SetSortModeForColumn(ColumnBeingSorted, InSortMode);
	TreeView_Refresh();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// SortByColumn action (ContextMenu)
////////////////////////////////////////////////////////////////////////////////////////////////////

bool STableTreeView::ContextMenu_SortByColumn_IsChecked(const FName ColumnId)
{
	return ColumnId == ColumnBeingSorted;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool STableTreeView::ContextMenu_SortByColumn_CanExecute(const FName ColumnId) const
{
	return true; //ColumnId != ColumnBeingSorted;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeView::ContextMenu_SortByColumn_Execute(const FName ColumnId)
{
	SetSortModeForColumn(ColumnId, EColumnSortMode::Descending);
	TreeView_Refresh();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// ShowColumn action
////////////////////////////////////////////////////////////////////////////////////////////////////

bool STableTreeView::CanShowColumn(const FName ColumnId) const
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeView::ShowColumn(const FName ColumnId)
{
	FTableColumn& Column = *Table->FindColumnChecked(ColumnId);
	Column.Show();

	SHeaderRow::FColumn::FArguments& ColumnArgs = TreeViewHeaderColumnArgs.FindChecked(ColumnId);

	const int32 NumColumns = TreeViewHeaderRow->GetColumns().Num();
	int32 ColumnIndex = 0;
	for (; ColumnIndex < NumColumns; ColumnIndex++)
	{
		const SHeaderRow::FColumn& CurrentColumn = TreeViewHeaderRow->GetColumns()[ColumnIndex];
		const FTableColumn& CurrentTableColumn = *Table->FindColumnChecked(CurrentColumn.ColumnId);
		if (Column.GetOrder() < CurrentTableColumn.GetOrder())
		{
			break;
		}
	}

	TreeViewHeaderRow->InsertColumn(ColumnArgs, ColumnIndex);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// HideColumn action
////////////////////////////////////////////////////////////////////////////////////////////////////

bool STableTreeView::CanHideColumn(const FName ColumnId) const
{
	FTableColumn& Column = *Table->FindColumnChecked(ColumnId);
	return Column.CanBeHidden();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeView::HideColumn(const FName ColumnId)
{
	FTableColumn& Column = *Table->FindColumnChecked(ColumnId);
	Column.Hide();

	TreeViewHeaderRow->RemoveColumn(ColumnId);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// ToggleColumn action
////////////////////////////////////////////////////////////////////////////////////////////////////

bool STableTreeView::IsColumnVisible(const FName ColumnId)
{
	FTableColumn& Column = *Table->FindColumnChecked(ColumnId);
	return Column.IsVisible();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool STableTreeView::CanToggleColumnVisibility(const FName ColumnId) const
{
	FTableColumn& Column = *Table->FindColumnChecked(ColumnId);
	return !Column.IsVisible() || Column.CanBeHidden();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeView::ToggleColumnVisibility(const FName ColumnId)
{
	FTableColumn& Column = *Table->FindColumnChecked(ColumnId);
	if (Column.IsVisible())
	{
		HideColumn(ColumnId);
	}
	else
	{
		ShowColumn(ColumnId);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// "Show All Columns" action (ContextMenu)
////////////////////////////////////////////////////////////////////////////////////////////////////

bool STableTreeView::ContextMenu_ShowAllColumns_CanExecute() const
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeView::ContextMenu_ShowAllColumns_Execute()
{
	ColumnBeingSorted = DefaultColumnBeingSorted;
	ColumnSortMode = DefaultColumnSortMode;
	UpdateCurrentSortingByColumn();

	for (const TSharedPtr<FTableColumn>& ColumnPtr : Table->GetColumns())
	{
		const FTableColumn& Column = *ColumnPtr;

		if (!Column.IsVisible())
		{
			ShowColumn(Column.GetId());
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// ResetColumns action (ContextMenu)
////////////////////////////////////////////////////////////////////////////////////////////////////

bool STableTreeView::ContextMenu_ResetColumns_CanExecute() const
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeView::ContextMenu_ResetColumns_Execute()
{
	ColumnBeingSorted = DefaultColumnBeingSorted;
	ColumnSortMode = DefaultColumnSortMode;
	UpdateCurrentSortingByColumn();

	for (const TSharedPtr<FTableColumn>& ColumnPtr : Table->GetColumns())
	{
		const FTableColumn& Column = *ColumnPtr;

		if (Column.ShouldBeVisible() && !Column.IsVisible())
		{
			ShowColumn(Column.GetId());
		}
		else if (!Column.ShouldBeVisible() && Column.IsVisible())
		{
			HideColumn(Column.GetId());
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeView::RebuildTree(bool bResync)
{
	bool bListHasChanged = false;

	if (bResync)
	{
		const int32 PreviousNodeCount = TableTreeNodes.Num();
		TableTreeNodes.Empty(PreviousNodeCount);
		//TableTreeNodesMap.Empty(PreviousNodeCount);
		TableTreeNodesIdMap.Empty(PreviousNodeCount);
		bListHasChanged = true;
	}

	TSharedPtr<Trace::IUntypedTable> SourceTable = Table->GetSourceTable();
	TSharedPtr<Trace::IUntypedTableReader> TableReader = Table->GetTableReader();

	if (Session.IsValid() && SourceTable.IsValid() && TableReader.IsValid())
	{
		int32 TotalRowCount = SourceTable->GetRowCount();

		if (TotalRowCount != TableTreeNodes.Num())
		{
			bResync = true;
		}

		if (bResync)
		{
			const int32 PreviousNodeCount = TableTreeNodes.Num();
			TableTreeNodes.Empty(PreviousNodeCount);
			//TableTreeNodesMap.Empty(PreviousNodeCount);
			TableTreeNodesIdMap.Empty(PreviousNodeCount);
			bListHasChanged = true;

			/*
			const Trace::ITableLayout& TableLayout = SourceTable->GetLayout();
			const int32 ColumnCount = TableLayout.GetColumnCount();

			for (; TableReader->IsValid(); TableReader->NextRow())
			{
				FString Line;
				for (int32 ColumnIndex = 0; ColumnIndex < ColumnCount; ++ColumnIndex)
				{
					switch (TableLayout.GetColumnType(ColumnIndex))
					{
					case Trace::TableColumnType_Bool:
						Line += TableReader->GetValueBool(ColumnIndex) ? "true" : "false";
						break;
					case Trace::TableColumnType_Int:
						Line += FString::Printf(TEXT("%lld"), TableReader->GetValueInt(ColumnIndex));
						break;
					case Trace::TableColumnType_Float:
						Line += FString::Printf(TEXT("%f"), TableReader->GetValueFloat(ColumnIndex));
						break;
					case Trace::TableColumnType_Double:
						Line += FString::Printf(TEXT("%f"), TableReader->GetValueDouble(ColumnIndex));
						break;
					case Trace::TableColumnType_CString:
						FString ValueString = TableReader->GetValueCString(ColumnIndex);
						ValueString.ReplaceInline(TEXT(","), TEXT(" "));
						Line += ValueString;
						break;
					}
					if (ColumnIndex < ColumnCount - 1)
					{
						Line += TEXT(",");
					}
					else
					{
						Line += TEXT("\n");
					}
				}
				//UE_LOG(TimingProfiler, Log, TEXT("%s"), *Line.ToString());
			}
			*/

			int32 NameColumnIndex = 0;

			for (int32 RowIndex = 0; RowIndex < TotalRowCount; ++RowIndex)
			{
				TableReader->SetRowIndex(RowIndex);
				uint64 NodeId = static_cast<uint64>(RowIndex);
				FName NodeName(TableReader->GetValueCString(NameColumnIndex));
				FTableTreeNodePtr NodePtr = MakeShareable(new FTableTreeNode(NodeId, NodeName, Table, RowIndex));
				TableTreeNodes.Add(NodePtr);
				//TableTreeNodesMap.Add(Name, NodePtr);
				TableTreeNodesIdMap.Add(NodeId, NodePtr);
			}
		}
	}

	if (bListHasChanged)
	{
		UpdateTree();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeView::SelectNodeByNodeId(uint64 Id)
{
	FTableTreeNodePtr* NodePtrPtr = TableTreeNodesIdMap.Find(Id);
	if (NodePtrPtr != nullptr)
	{
		FTableTreeNodePtr NodePtr = *NodePtrPtr;

		//UE_LOG(TimingProfiler, Log, TEXT("Select and RequestScrollIntoView %s"), *(*TimerNodePtrPtr)->GetName().ToString());
		TreeView->SetSelection(NodePtr);
		TreeView->RequestScrollIntoView(NodePtr);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeView::SelectNodeByTableRowIndex(int32 RowIndex)
{
	if (RowIndex >= 0 && RowIndex < TableTreeNodes.Num())
	{
		FTableTreeNodePtr NodePtr = TableTreeNodes[RowIndex];

		//UE_LOG(TimingProfiler, Log, TEXT("Select and RequestScrollIntoView %s"), *(*TimerNodePtrPtr)->GetName().ToString());
		TreeView->SetSelection(NodePtr);
		TreeView->RequestScrollIntoView(NodePtr);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
