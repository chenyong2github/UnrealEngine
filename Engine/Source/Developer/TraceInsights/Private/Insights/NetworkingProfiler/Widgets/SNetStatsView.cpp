// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SNetStatsView.h"

#include "EditorStyleSet.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "SlateOptMacros.h"
#include "Templates/UniquePtr.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/Model/NetProfiler.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Views/STableViewBase.h"

// Insights
#include "Insights/Table/ViewModels/Table.h"
#include "Insights/Table/ViewModels/TableColumn.h"
#include "Insights/NetworkingProfiler/NetworkingProfilerManager.h"
#include "Insights/NetworkingProfiler/ViewModels/NetStatsViewColumnFactory.h"
#include "Insights/NetworkingProfiler/Widgets/SNetStatsViewTooltip.h"
#include "Insights/NetworkingProfiler/Widgets/SNetStatsTableRow.h"

#define LOCTEXT_NAMESPACE "SNetStatsView"

////////////////////////////////////////////////////////////////////////////////////////////////////

SNetStatsView::SNetStatsView()
	: Table(MakeShared<Insights::FTable>())
	, bExpansionSaved(false)
	, bFilterOutZeroCountEvents(false)
	, GroupingMode(ENetEventGroupingMode::Flat)
	, AvailableSorters()
	, CurrentSorter(nullptr)
	, ColumnBeingSorted(GetDefaultColumnBeingSorted())
	, ColumnSortMode(GetDefaultColumnSortMode())
	, NextTimestamp(0)
	, ObjectsChangeCount(0)
	, GameInstanceIndex(0)
	, ConnectionIndex(0)
	, ConnectionMode(Trace::ENetProfilerConnectionMode::Outgoing)
	, StatsPacketStartIndex(0)
	, StatsPacketEndIndex(0)
	, StatsStartPosition(0)
	, StatsEndPosition(0)
{
	FMemory::Memset(bNetEventTypeIsVisible, 1);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

SNetStatsView::~SNetStatsView()
{
	// Remove ourselves from the Insights manager.
	if (FInsightsManager::Get().IsValid())
	{
		FInsightsManager::Get()->GetSessionChangedEvent().RemoveAll(this);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SNetStatsView::Construct(const FArguments& InArgs)
{
	SAssignNew(ExternalScrollbar, SScrollBar)
	.AlwaysShowScrollbar(true);

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(2.0f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(2.0f)
				.AutoHeight()
				[
					SNew(SHorizontalBox)

					// Search box
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.Padding(2.0f)
					.FillWidth(1.0f)
					[
						SAssignNew(SearchBox, SSearchBox)
						.HintText(LOCTEXT("SearchBoxHint", "Search net events or groups"))
						.OnTextChanged(this, &SNetStatsView::SearchBox_OnTextChanged)
						.IsEnabled(this, &SNetStatsView::SearchBox_IsEnabled)
						.ToolTipText(LOCTEXT("FilterSearchHint", "Type here to search net events or groups"))
					]

					// Filter out net event types with zero instance count
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.Padding(2.0f)
					.AutoWidth()
					[
						SNew(SCheckBox)
						.Style(FEditorStyle::Get(), "ToggleButtonCheckbox")
						.HAlign(HAlign_Center)
						.Padding(2.0f)
						.OnCheckStateChanged(this, &SNetStatsView::FilterOutZeroCountEvents_OnCheckStateChanged)
						.IsChecked(this, &SNetStatsView::FilterOutZeroCountEvents_IsChecked)
						.ToolTipText(LOCTEXT("FilterOutZeroCountEvents_Tooltip", "Filter out the net event types having zero total instance count (aggregated stats)."))
						[
							//TODO: SNew(SImage)
							SNew(STextBlock)
							.Text(LOCTEXT("FilterOutZeroCountEvents_Button", " !0 "))
							.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Caption"))
						]
					]
				]

				// Group by
				+ SVerticalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(2.0f)
				.AutoHeight()
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("GroupByText", "Group by"))
					]

					+ SHorizontalBox::Slot()
					.FillWidth(2.0f)
					.VAlign(VAlign_Center)
					[
						SAssignNew(GroupByComboBox, SComboBox<TSharedPtr<ENetEventGroupingMode>>)
						.ToolTipText(this, &SNetStatsView::GroupBy_GetSelectedTooltipText)
						.OptionsSource(&GroupByOptionsSource)
						.OnSelectionChanged(this, &SNetStatsView::GroupBy_OnSelectionChanged)
						.OnGenerateWidget(this, &SNetStatsView::GroupBy_OnGenerateWidget)
						[
							SNew(STextBlock)
							.Text(this, &SNetStatsView::GroupBy_GetSelectedText)
						]
					]
				]

				// TODO: Check boxes for: NetEvent, ...
				/*
				+ SVerticalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(2.0f)
				.AutoHeight()
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.Padding(FMargin(0.0f,0.0f,1.0f,0.0f))
					.FillWidth(1.0f)
					[
						GetToggleButtonForNetEventType(ENetEventNodeType::NetEvent)
					]

					//+ SHorizontalBox::Slot()
					//.Padding(FMargin(1.0f,0.0f,1.0f,0.0f))
					//.FillWidth(1.0f)
					//[
					//	GetToggleButtonForNetEventType(ENetEventNodeType::NetEvent1)
					//]

					//+ SHorizontalBox::Slot()
					.//Padding(FMargin(1.0f,0.0f,1.0f,0.0f))
					//.FillWidth(1.0f)
					//[
					//	GetToggleButtonForNetEventType(ENetEventNodeType::NetEvent2)
					//]
				]
				*/
			]
		]

		// Tree view
		+ SVerticalBox::Slot()
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
						SAssignNew(TreeView, STreeView<FNetEventNodePtr>)
						.ExternalScrollbar(ExternalScrollbar)
						.SelectionMode(ESelectionMode::Multi)
						.TreeItemsSource(&FilteredGroupNodes)
						.OnGetChildren(this, &SNetStatsView::TreeView_OnGetChildren)
						.OnGenerateRow(this, &SNetStatsView::TreeView_OnGenerateRow)
						.OnSelectionChanged(this, &SNetStatsView::TreeView_OnSelectionChanged)
						.OnMouseButtonDoubleClick(this, &SNetStatsView::TreeView_OnMouseButtonDoubleClick)
						.OnContextMenuOpening(FOnContextMenuOpening::CreateSP(this, &SNetStatsView::TreeView_GetMenuContent))
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
	TextFilter = MakeShared<FNetEventNodeTextFilter>(FNetEventNodeTextFilter::FItemToStringArray::CreateSP(this, &SNetStatsView::HandleItemToStringArray));
	Filters = MakeShared<FNetEventNodeFilterCollection>();
	Filters->Add(TextFilter);

	CreateGroupByOptionsSources();
	CreateSortings();

	// Register ourselves with the Insights manager.
	FInsightsManager::Get()->GetSessionChangedEvent().AddSP(this, &SNetStatsView::InsightsManager_OnSessionChanged);

	// Update the Session (i.e. when analysis session was already started).
	InsightsManager_OnSessionChanged();
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	//SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	// Check if we need to update the lists of net events, but not too often.
	const uint64 Time = FPlatformTime::Cycles64();
	if (Time > NextTimestamp)
	{
		const uint64 WaitTime = static_cast<uint64>(0.5 / FPlatformTime::GetSecondsPerCycle64()); // 500ms
		NextTimestamp = Time + WaitTime;

		RebuildTree(false);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<SWidget> SNetStatsView::TreeView_GetMenuContent()
{
	const TArray<FNetEventNodePtr> SelectedNodes = TreeView->GetSelectedItems();
	const int32 NumSelectedNodes = SelectedNodes.Num();
	FNetEventNodePtr SelectedNode = NumSelectedNodes ? SelectedNodes[0] : nullptr;

	const TSharedPtr<Insights::FTableColumn> HoveredColumnPtr = Table->FindColumn(HoveredColumnId);

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
			PropertyValue = HoveredColumnPtr->GetValueAsTooltipText(*SelectedNode);
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
			FExecuteAction::CreateSP(this, &SNetStatsView::ContextMenu_CopySelectedToClipboard_Execute),
			FCanExecuteAction::CreateSP(this, &SNetStatsView::ContextMenu_CopySelectedToClipboard_CanExecute)
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
			FNewMenuDelegate::CreateSP(this, &SNetStatsView::TreeView_BuildSortByMenu),
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
			FNewMenuDelegate::CreateSP(this, &SNetStatsView::TreeView_BuildViewColumnMenu),
			false,
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Profiler.EventGraph.ViewColumn")
		);

		FUIAction Action_ShowAllColumns
		(
			FExecuteAction::CreateSP(this, &SNetStatsView::ContextMenu_ShowAllColumns_Execute),
			FCanExecuteAction::CreateSP(this, &SNetStatsView::ContextMenu_ShowAllColumns_CanExecute)
		);
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_Header_Columns_ShowAllColumns", "Show All Columns"),
			LOCTEXT("ContextMenu_Header_Columns_ShowAllColumns_Desc", "Resets tree view to show all columns"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Profiler.EventGraph.ResetColumn"), Action_ShowAllColumns, NAME_None, EUserInterfaceActionType::Button
		);

		FUIAction Action_ShowMinMaxMedColumns
		(
			FExecuteAction::CreateSP(this, &SNetStatsView::ContextMenu_ShowMinMaxMedColumns_Execute),
			FCanExecuteAction::CreateSP(this, &SNetStatsView::ContextMenu_ShowMinMaxMedColumns_CanExecute)
		);
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_Header_Columns_ShowMinMaxMedColumns", "Reset Columns to Min/Max/Median Preset"),
			LOCTEXT("ContextMenu_Header_Columns_ShowMinMaxMedColumns_Desc", "Resets columns to Min/Max/Median preset"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Profiler.EventGraph.ResetColumn"), Action_ShowMinMaxMedColumns, NAME_None, EUserInterfaceActionType::Button
		);

		FUIAction Action_ResetColumns
		(
			FExecuteAction::CreateSP(this, &SNetStatsView::ContextMenu_ResetColumns_Execute),
			FCanExecuteAction::CreateSP(this, &SNetStatsView::ContextMenu_ResetColumns_CanExecute)
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

void SNetStatsView::TreeView_BuildSortByMenu(FMenuBuilder& MenuBuilder)
{
	// TODO: Refactor later @see TSharedPtr<SWidget> SCascadePreviewViewportToolBar::GenerateViewMenu() const

	MenuBuilder.BeginSection("ColumnName", LOCTEXT("ContextMenu_Header_Misc_ColumnName", "Column Name"));

	for (const TSharedRef<Insights::FTableColumn>& ColumnRef : Table->GetColumns())
	{
		const Insights::FTableColumn& Column = *ColumnRef;

		if (Column.IsVisible() && Column.CanBeSorted())
		{
			FUIAction Action_SortByColumn
			(
				FExecuteAction::CreateSP(this, &SNetStatsView::ContextMenu_SortByColumn_Execute, Column.GetId()),
				FCanExecuteAction::CreateSP(this, &SNetStatsView::ContextMenu_SortByColumn_CanExecute, Column.GetId()),
				FIsActionChecked::CreateSP(this, &SNetStatsView::ContextMenu_SortByColumn_IsChecked, Column.GetId())
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
			FExecuteAction::CreateSP(this, &SNetStatsView::ContextMenu_SortMode_Execute, EColumnSortMode::Ascending),
			FCanExecuteAction::CreateSP(this, &SNetStatsView::ContextMenu_SortMode_CanExecute, EColumnSortMode::Ascending),
			FIsActionChecked::CreateSP(this, &SNetStatsView::ContextMenu_SortMode_IsChecked, EColumnSortMode::Ascending)
		);
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_Header_Misc_Sort_SortAscending", "Sort Ascending"),
			LOCTEXT("ContextMenu_Header_Misc_Sort_SortAscending_Desc", "Sorts ascending"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Profiler.Misc.SortAscending"), Action_SortAscending, NAME_None, EUserInterfaceActionType::RadioButton
		);

		FUIAction Action_SortDescending
		(
			FExecuteAction::CreateSP(this, &SNetStatsView::ContextMenu_SortMode_Execute, EColumnSortMode::Descending),
			FCanExecuteAction::CreateSP(this, &SNetStatsView::ContextMenu_SortMode_CanExecute, EColumnSortMode::Descending),
			FIsActionChecked::CreateSP(this, &SNetStatsView::ContextMenu_SortMode_IsChecked, EColumnSortMode::Descending)
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

void SNetStatsView::TreeView_BuildViewColumnMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection("ViewColumn", LOCTEXT("ContextMenu_Header_Columns_View", "View Column"));

	for (const TSharedRef<Insights::FTableColumn>& ColumnRef : Table->GetColumns())
	{
		const Insights::FTableColumn& Column = *ColumnRef;

		FUIAction Action_ToggleColumn
		(
			FExecuteAction::CreateSP(this, &SNetStatsView::ToggleColumnVisibility, Column.GetId()),
			FCanExecuteAction::CreateSP(this, &SNetStatsView::CanToggleColumnVisibility, Column.GetId()),
			FIsActionChecked::CreateSP(this, &SNetStatsView::IsColumnVisible, Column.GetId())
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

void SNetStatsView::InitializeAndShowHeaderColumns()
{
	// Create columns.
	TArray<TSharedRef<Insights::FTableColumn>> Columns;
	FNetStatsViewColumnFactory::CreateNetStatsViewColumns(Columns);
	Table->SetColumns(Columns);

	// Show columns.
	for (const TSharedRef<Insights::FTableColumn>& ColumnRef : Table->GetColumns())
	{
		if (ColumnRef->ShouldBeVisible())
		{
			ShowColumn(ColumnRef->GetId());
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SNetStatsView::GetColumnHeaderText(const FName ColumnId) const
{
	const Insights::FTableColumn& Column = *Table->FindColumnChecked(ColumnId);
	return Column.GetShortName();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SNetStatsView::TreeViewHeaderRow_GenerateColumnMenu(const Insights::FTableColumn& Column)
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
				FExecuteAction::CreateSP(this, &SNetStatsView::HideColumn, Column.GetId()),
				FCanExecuteAction::CreateSP(this, &SNetStatsView::CanHideColumn, Column.GetId())
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
				FExecuteAction::CreateSP(this, &SNetStatsView::HeaderMenu_SortMode_Execute, Column.GetId(), EColumnSortMode::Ascending),
				FCanExecuteAction::CreateSP(this, &SNetStatsView::HeaderMenu_SortMode_CanExecute, Column.GetId(), EColumnSortMode::Ascending),
				FIsActionChecked::CreateSP(this, &SNetStatsView::HeaderMenu_SortMode_IsChecked, Column.GetId(), EColumnSortMode::Ascending)
			);
			MenuBuilder.AddMenuEntry
			(
				LOCTEXT("ContextMenu_Header_Misc_Sort_SortAscending", "Sort Ascending"),
				LOCTEXT("ContextMenu_Header_Misc_Sort_SortAscending_Desc", "Sorts ascending"),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "Profiler.Misc.SortAscending"), Action_SortAscending, NAME_None, EUserInterfaceActionType::RadioButton
			);

			FUIAction Action_SortDescending
			(
				FExecuteAction::CreateSP(this, &SNetStatsView::HeaderMenu_SortMode_Execute, Column.GetId(), EColumnSortMode::Descending),
				FCanExecuteAction::CreateSP(this, &SNetStatsView::HeaderMenu_SortMode_CanExecute, Column.GetId(), EColumnSortMode::Descending),
				FIsActionChecked::CreateSP(this, &SNetStatsView::HeaderMenu_SortMode_IsChecked, Column.GetId(), EColumnSortMode::Descending)
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

		//if (Column.CanBeFiltered())
		//{
		//	MenuBuilder.BeginSection("FilterMode", LOCTEXT("ContextMenu_Header_Misc_Filter_FilterMode", "Filter Mode"));
		//	bIsMenuVisible = true;
		//	MenuBuilder.EndSection();
		//}
	}

	/*
	TODO:
	- Show top ten
	- Show top bottom
	- Filter by list (avg, median, 10%, 90%, etc.)
	- Text box for filtering for each column instead of one text box used for filtering
	- Grouping button for flat view modes (show at most X groups, show all groups for names)
	*/

	return bIsMenuVisible ? MenuBuilder.MakeWidget() : (TSharedRef<SWidget>)SNullWidget::NullWidget;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::InsightsManager_OnSessionChanged()
{
	TSharedPtr<const Trace::IAnalysisSession> NewSession = FInsightsManager::Get()->GetSession();

	if (NewSession != Session)
	{
		Session = NewSession;
		Reset();
	}
	else
	{
		UpdateTree();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::UpdateTree()
{
	CreateGroups();
	SortTreeNodes();
	ApplyFiltering();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::ApplyFiltering()
{
	FilteredGroupNodes.Reset();

	// Apply filter to all groups and its children.
	const int32 NumGroups = GroupNodes.Num();
	for (int32 GroupIndex = 0; GroupIndex < NumGroups; ++GroupIndex)
	{
		FNetEventNodePtr& GroupPtr = GroupNodes[GroupIndex];
		GroupPtr->ClearFilteredChildren();
		const bool bIsGroupVisible = Filters->PassesAllFilters(GroupPtr);

		const TArray<Insights::FBaseTreeNodePtr>& GroupChildren = GroupPtr->GetChildren();
		const int32 NumChildren = GroupChildren.Num();
		int32 NumVisibleChildren = 0;
		for (int32 Cx = 0; Cx < NumChildren; ++Cx)
		{
			// Add a child.
			const FNetEventNodePtr& NodePtr = StaticCastSharedPtr<FNetEventNode, Insights::FBaseTreeNode>(GroupChildren[Cx]);
			const bool bIsChildVisible = (!bFilterOutZeroCountEvents || NodePtr->GetAggregatedStats().InstanceCount > 0)
									  && bNetEventTypeIsVisible[static_cast<int>(NodePtr->GetType())]
									  && Filters->PassesAllFilters(NodePtr);
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

	// Only expand net event nodes if we have a text filter.
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
			const FNetEventNodePtr& GroupPtr = FilteredGroupNodes[Fx];
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

void SNetStatsView::HandleItemToStringArray(const FNetEventNodePtr& FNetEventNodePtr, TArray<FString>& OutSearchStrings) const
{
	OutSearchStrings.Add(FNetEventNodePtr->GetName().GetPlainNameString());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SNetStatsView::GetToggleButtonForNetEventType(const ENetEventNodeType NodeType)
{
	return SNew(SCheckBox)
		.Style(FEditorStyle::Get(), "ToggleButtonCheckbox")
		.HAlign(HAlign_Center)
		.Padding(2.0f)
		.OnCheckStateChanged(this, &SNetStatsView::FilterByNetEventType_OnCheckStateChanged, NodeType)
		.IsChecked(this, &SNetStatsView::FilterByNetEventType_IsChecked, NodeType)
		.ToolTipText(NetEventNodeTypeHelper::ToDescription(NodeType))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
						.Image(NetEventNodeTypeHelper::GetIconForNetEventNodeType(NodeType))
				]

			+ SHorizontalBox::Slot()
				.Padding(2.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
						.Text(NetEventNodeTypeHelper::ToText(NodeType))
						.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Caption"))
				]
		];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::FilterOutZeroCountEvents_OnCheckStateChanged(ECheckBoxState NewRadioState)
{
	bFilterOutZeroCountEvents = (NewRadioState == ECheckBoxState::Checked);
	ApplyFiltering();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

ECheckBoxState SNetStatsView::FilterOutZeroCountEvents_IsChecked() const
{
	return bFilterOutZeroCountEvents ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::FilterByNetEventType_OnCheckStateChanged(ECheckBoxState NewRadioState, const ENetEventNodeType InStatType)
{
	bNetEventTypeIsVisible[static_cast<int>(InStatType)] = (NewRadioState == ECheckBoxState::Checked);
	ApplyFiltering();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

ECheckBoxState SNetStatsView::FilterByNetEventType_IsChecked(const ENetEventNodeType InStatType) const
{
	return bNetEventTypeIsVisible[static_cast<int>(InStatType)] ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// TreeView
////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::TreeView_Refresh()
{
	if (TreeView.IsValid())
	{
		TreeView->RequestTreeRefresh();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::TreeView_OnSelectionChanged(FNetEventNodePtr SelectedItem, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo != ESelectInfo::Direct)
	{
		TArray<FNetEventNodePtr> SelectedItems = TreeView->GetSelectedItems();
		if (SelectedItems.Num() == 1 && !SelectedItems[0]->IsGroup())
		{
			//TODO: FNetworkingProfilerManager::Get()->SetSelectedNetEvent(SelectedItems[0]->GetId());
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::TreeView_OnGetChildren(FNetEventNodePtr InParent, TArray<FNetEventNodePtr>& OutChildren)
{
	const TArray<Insights::FBaseTreeNodePtr>& Children = InParent->GetFilteredChildren();
	OutChildren.Reset(Children.Num());
	for (const Insights::FBaseTreeNodePtr& Child : Children)
	{
		OutChildren.Add(StaticCastSharedPtr<FNetEventNode, Insights::FBaseTreeNode>(Child));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::TreeView_OnMouseButtonDoubleClick(FNetEventNodePtr NetEventNodePtr)
{
	if (NetEventNodePtr->IsGroup())
	{
		const bool bIsGroupExpanded = TreeView->IsItemExpanded(NetEventNodePtr);
		TreeView->SetItemExpansion(NetEventNodePtr, !bIsGroupExpanded);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Tree View's Table Row
////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<ITableRow> SNetStatsView::TreeView_OnGenerateRow(FNetEventNodePtr NetEventNodePtr, const TSharedRef<STableViewBase>& OwnerTable)
{
	TSharedRef<ITableRow> TableRow =
		SNew(SNetStatsTableRow, OwnerTable)
		.OnShouldBeEnabled(this, &SNetStatsView::TableRow_ShouldBeEnabled)
		.OnIsColumnVisible(this, &SNetStatsView::IsColumnVisible)
		.OnSetHoveredCell(this, &SNetStatsView::TableRow_SetHoveredCell)
		.OnGetColumnOutlineHAlignmentDelegate(this, &SNetStatsView::TableRow_GetColumnOutlineHAlignment)
		.HighlightText(this, &SNetStatsView::TableRow_GetHighlightText)
		.HighlightedNodeName(this, &SNetStatsView::TableRow_GetHighlightedNodeName)
		.TablePtr(Table)
		.NetEventNodePtr(NetEventNodePtr);

	return TableRow;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SNetStatsView::TableRow_ShouldBeEnabled(const uint32 NodeId) const
{
	return true;//im:TODO: Session->GetAggregatedStat(NodeId) != nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::TableRow_SetHoveredCell(TSharedPtr<Insights::FTable> InTablePtr, TSharedPtr<Insights::FTableColumn> InColumnPtr, const FNetEventNodePtr InNodePtr)
{
	HoveredColumnId = InColumnPtr ? InColumnPtr->GetId() : FName();

	const bool bIsAnyMenusVisible = FSlateApplication::Get().AnyMenusVisible();
	if (!HasMouseCapture() && !bIsAnyMenusVisible)
	{
		HoveredNodePtr = InNodePtr;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EHorizontalAlignment SNetStatsView::TableRow_GetColumnOutlineHAlignment(const FName ColumnId) const
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

FText SNetStatsView::TableRow_GetHighlightText() const
{
	return SearchBox->GetText();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FName SNetStatsView::TableRow_GetHighlightedNodeName() const
{
	return HighlightedNodeName;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// SearchBox
////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::SearchBox_OnTextChanged(const FText& InFilterText)
{
	TextFilter->SetRawFilterText(InFilterText);
	SearchBox->SetError(TextFilter->GetFilterErrorText());
	ApplyFiltering();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SNetStatsView::SearchBox_IsEnabled() const
{
	return NetEventNodes.Num() > 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Grouping
////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::CreateGroups()
{
	TMap<FName, FNetEventNodePtr> GroupNodeSet;

	if (GroupingMode == ENetEventGroupingMode::Flat)
	{
		const FName GroupName(TEXT("All"));
		FNetEventNodePtr* GroupPtr = GroupNodeSet.Find(GroupName);
		if (!GroupPtr)
		{
			GroupPtr = &GroupNodeSet.Add(GroupName, MakeShared<FNetEventNode>(GroupName));
		}

		for (const FNetEventNodePtr& NetEventNodePtr : NetEventNodes)
		{
			(*GroupPtr)->AddChildAndSetGroupPtr(NetEventNodePtr);
		}

		TreeView->SetItemExpansion(*GroupPtr, true);
	}
	// Creates one group for one letter.
	else if (GroupingMode == ENetEventGroupingMode::ByName)
	{
		for (const FNetEventNodePtr& NetEventNodePtr : NetEventNodes)
		{
			const FName GroupName = *NetEventNodePtr->GetName().GetPlainNameString().Left(1).ToUpper();

			FNetEventNodePtr* GroupPtr = GroupNodeSet.Find(GroupName);
			if (!GroupPtr)
			{
				GroupPtr = &GroupNodeSet.Add(GroupName, MakeShared<FNetEventNode>(GroupName));
			}

			(*GroupPtr)->AddChildAndSetGroupPtr(NetEventNodePtr);
		}
	}
	// Creates one group for each stat type.
	else if (GroupingMode == ENetEventGroupingMode::ByType)
	{
		for (const FNetEventNodePtr& NetEventNodePtr : NetEventNodes)
		{
			const FName GroupName = *NetEventNodeTypeHelper::ToText(NetEventNodePtr->GetType()).ToString();

			FNetEventNodePtr* GroupPtr = GroupNodeSet.Find(GroupName);
			if (!GroupPtr)
			{
				GroupPtr = &GroupNodeSet.Add(GroupName, MakeShared<FNetEventNode>(GroupName));
			}

			(*GroupPtr)->AddChildAndSetGroupPtr(NetEventNodePtr);
			TreeView->SetItemExpansion(*GroupPtr, true);
		}
	}
	// Creates one group for each stat type.
	else if (GroupingMode == ENetEventGroupingMode::ByLevel)
	{
		for (const FNetEventNodePtr& NetEventNodePtr : NetEventNodes)
		{
			const FName GroupName(*FString::Printf(TEXT("Level %d"), NetEventNodePtr->GetLevel()));

			FNetEventNodePtr* GroupPtr = GroupNodeSet.Find(GroupName);
			if (!GroupPtr)
			{
				GroupPtr = &GroupNodeSet.Add(GroupName, MakeShared<FNetEventNode>(GroupName));
			}

			(*GroupPtr)->AddChildAndSetGroupPtr(NetEventNodePtr);
			TreeView->SetItemExpansion(*GroupPtr, true);
		}
	}

	GroupNodeSet.GenerateValueArray(GroupNodes);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::CreateGroupByOptionsSources()
{
	GroupByOptionsSource.Reset(3);

	// Must be added in order of elements in the ENetEventGroupingMode.
	GroupByOptionsSource.Add(MakeShared<ENetEventGroupingMode>(ENetEventGroupingMode::Flat));
	GroupByOptionsSource.Add(MakeShared<ENetEventGroupingMode>(ENetEventGroupingMode::ByName));
	//GroupByOptionsSource.Add(MakeShared<ENetEventGroupingMode>(ENetEventGroupingMode::ByType));
	GroupByOptionsSource.Add(MakeShared<ENetEventGroupingMode>(ENetEventGroupingMode::ByLevel));

	ENetEventGroupingModePtr* GroupingModePtrPtr = GroupByOptionsSource.FindByPredicate([&](const ENetEventGroupingModePtr InGroupingModePtr) { return *InGroupingModePtr == GroupingMode; });
	if (GroupingModePtrPtr != nullptr)
	{
		GroupByComboBox->SetSelectedItem(*GroupingModePtrPtr);
	}

	GroupByComboBox->RefreshOptions();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::GroupBy_OnSelectionChanged(TSharedPtr<ENetEventGroupingMode> NewGroupingMode, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo != ESelectInfo::Direct)
	{
		GroupingMode = *NewGroupingMode;

		CreateGroups();
		SortTreeNodes();
		ApplyFiltering();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SNetStatsView::GroupBy_OnGenerateWidget(TSharedPtr<ENetEventGroupingMode> InGroupingMode) const
{
	return SNew(STextBlock)
		.Text(NetEventNodeGroupingHelper::ToText(*InGroupingMode))
		.ToolTipText(NetEventNodeGroupingHelper::ToDescription(*InGroupingMode));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SNetStatsView::GroupBy_GetSelectedText() const
{
	return NetEventNodeGroupingHelper::ToText(GroupingMode);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SNetStatsView::GroupBy_GetSelectedTooltipText() const
{
	return NetEventNodeGroupingHelper::ToDescription(GroupingMode);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Sorting
////////////////////////////////////////////////////////////////////////////////////////////////////

const FName SNetStatsView::GetDefaultColumnBeingSorted()
{
	return FNetStatsViewColumns::TotalInclusiveSizeColumnID;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const EColumnSortMode::Type SNetStatsView::GetDefaultColumnSortMode()
{
	return EColumnSortMode::Descending;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::CreateSortings()
{
	AvailableSorters.Reset();
	CurrentSorter = nullptr;

	for (const TSharedRef<Insights::FTableColumn> ColumnRef : Table->GetColumns())
	{
		if (ColumnRef->CanBeSorted())
		{
			TSharedPtr<Insights::ITableCellValueSorter> SorterPtr = ColumnRef->GetValueSorter();
			if (ensure(SorterPtr.IsValid()))
			{
				AvailableSorters.Add(SorterPtr);
			}
		}
	}

	UpdateCurrentSortingByColumn();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::UpdateCurrentSortingByColumn()
{
	TSharedPtr<Insights::FTableColumn> ColumnPtr = Table->FindColumn(ColumnBeingSorted);
	CurrentSorter = ColumnPtr.IsValid() ? ColumnPtr->GetValueSorter() : nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::SortTreeNodes()
{
	if (CurrentSorter.IsValid())
	{
		for (FNetEventNodePtr& Root : GroupNodes)
		{
			SortTreeNodesRec(*Root, *CurrentSorter);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::SortTreeNodesRec(FNetEventNode& Node, const Insights::ITableCellValueSorter& Sorter)
{
	if (ColumnSortMode == EColumnSortMode::Type::Descending)
	{
		Node.SortChildrenDescending(Sorter);
	}
	else // if (ColumnSortMode == EColumnSortMode::Type::Ascending)
	{
		Node.SortChildrenAscending(Sorter);
	}

	for (Insights::FBaseTreeNodePtr ChildPtr : Node.GetChildren())
	{
		//if (ChildPtr->IsGroup())
		if (ChildPtr->GetChildren().Num() > 0)
		{
			SortTreeNodesRec(*StaticCastSharedPtr<FNetEventNode>(ChildPtr), Sorter);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EColumnSortMode::Type SNetStatsView::GetSortModeForColumn(const FName ColumnId) const
{
	if (ColumnBeingSorted != ColumnId)
	{
		return EColumnSortMode::None;
	}

	return ColumnSortMode;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::SetSortModeForColumn(const FName& ColumnId, const EColumnSortMode::Type SortMode)
{
	ColumnBeingSorted = ColumnId;
	ColumnSortMode = SortMode;
	UpdateCurrentSortingByColumn();

	SortTreeNodes();
	ApplyFiltering();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::OnSortModeChanged(const EColumnSortPriority::Type SortPriority, const FName& ColumnId, const EColumnSortMode::Type SortMode)
{
	SetSortModeForColumn(ColumnId, SortMode);
	TreeView_Refresh();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// SortMode action (HeaderMenu)
////////////////////////////////////////////////////////////////////////////////////////////////////

bool SNetStatsView::HeaderMenu_SortMode_IsChecked(const FName ColumnId, const EColumnSortMode::Type InSortMode)
{
	return ColumnBeingSorted == ColumnId && ColumnSortMode == InSortMode;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SNetStatsView::HeaderMenu_SortMode_CanExecute(const FName ColumnId, const EColumnSortMode::Type InSortMode) const
{
	const Insights::FTableColumn& Column = *Table->FindColumnChecked(ColumnId);
	return Column.CanBeSorted();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::HeaderMenu_SortMode_Execute(const FName ColumnId, const EColumnSortMode::Type InSortMode)
{
	SetSortModeForColumn(ColumnId, InSortMode);
	TreeView_Refresh();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// SortMode action (ContextMenu)
////////////////////////////////////////////////////////////////////////////////////////////////////

bool SNetStatsView::ContextMenu_SortMode_IsChecked(const EColumnSortMode::Type InSortMode)
{
	return ColumnSortMode == InSortMode;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SNetStatsView::ContextMenu_SortMode_CanExecute(const EColumnSortMode::Type InSortMode) const
{
	return true; //ColumnSortMode != InSortMode;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::ContextMenu_SortMode_Execute(const EColumnSortMode::Type InSortMode)
{
	SetSortModeForColumn(ColumnBeingSorted, InSortMode);
	TreeView_Refresh();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// SortByColumn action (ContextMenu)
////////////////////////////////////////////////////////////////////////////////////////////////////

bool SNetStatsView::ContextMenu_SortByColumn_IsChecked(const FName ColumnId)
{
	return ColumnId == ColumnBeingSorted;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SNetStatsView::ContextMenu_SortByColumn_CanExecute(const FName ColumnId) const
{
	return true; //ColumnId != ColumnBeingSorted;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::ContextMenu_SortByColumn_Execute(const FName ColumnId)
{
	SetSortModeForColumn(ColumnId, EColumnSortMode::Descending);
	TreeView_Refresh();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// ShowColumn action
////////////////////////////////////////////////////////////////////////////////////////////////////

bool SNetStatsView::CanShowColumn(const FName ColumnId) const
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::ShowColumn(const FName ColumnId)
{
	Insights::FTableColumn& Column = *Table->FindColumnChecked(ColumnId);
	Column.Show();

	SHeaderRow::FColumn::FArguments ColumnArgs;
	ColumnArgs
		.ColumnId(Column.GetId())
		.DefaultLabel(Column.GetShortName())
		.HAlignHeader(HAlign_Fill)
		.VAlignHeader(VAlign_Fill)
		.HeaderContentPadding(FMargin(2.0f))
		.HAlignCell(HAlign_Fill)
		.VAlignCell(VAlign_Fill)
		.SortMode(this, &SNetStatsView::GetSortModeForColumn, Column.GetId())
		.OnSort(this, &SNetStatsView::OnSortModeChanged)
		.ManualWidth(Column.GetInitialWidth())
		.FixedWidth(Column.IsFixedWidth() ? Column.GetInitialWidth() : TOptional<float>())
		.HeaderContent()
		[
			SNew(SBox)
			.ToolTip(SNetStatsViewTooltip::GetColumnTooltip(Column))
			.HAlign(Column.GetHorizontalAlignment())
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SNetStatsView::GetColumnHeaderText, Column.GetId())
			]
		]
		.MenuContent()
		[
			TreeViewHeaderRow_GenerateColumnMenu(Column)
		];

	int32 ColumnIndex = 0;
	const int32 NewColumnPosition = Table->GetColumnPositionIndex(ColumnId);
	const int32 NumColumns = TreeViewHeaderRow->GetColumns().Num();
	for (; ColumnIndex < NumColumns; ColumnIndex++)
	{
		const SHeaderRow::FColumn& CurrentColumn = TreeViewHeaderRow->GetColumns()[ColumnIndex];
		const int32 CurrentColumnPosition = Table->GetColumnPositionIndex(CurrentColumn.ColumnId);
		if (NewColumnPosition < CurrentColumnPosition)
		{
			break;
		}
	}

	TreeViewHeaderRow->InsertColumn(ColumnArgs, ColumnIndex);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// HideColumn action
////////////////////////////////////////////////////////////////////////////////////////////////////

bool SNetStatsView::CanHideColumn(const FName ColumnId) const
{
	const Insights::FTableColumn& Column = *Table->FindColumnChecked(ColumnId);
	return Column.CanBeHidden();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::HideColumn(const FName ColumnId)
{
	Insights::FTableColumn& Column = *Table->FindColumnChecked(ColumnId);
	Column.Hide();

	TreeViewHeaderRow->RemoveColumn(ColumnId);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// ToggleColumn action
////////////////////////////////////////////////////////////////////////////////////////////////////

bool SNetStatsView::IsColumnVisible(const FName ColumnId)
{
	const Insights::FTableColumn& Column = *Table->FindColumnChecked(ColumnId);
	return Column.IsVisible();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SNetStatsView::CanToggleColumnVisibility(const FName ColumnId) const
{
	const Insights::FTableColumn& Column = *Table->FindColumnChecked(ColumnId);
	return !Column.IsVisible() || Column.CanBeHidden();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::ToggleColumnVisibility(const FName ColumnId)
{
	const Insights::FTableColumn& Column = *Table->FindColumnChecked(ColumnId);
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

bool SNetStatsView::ContextMenu_ShowAllColumns_CanExecute() const
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::ContextMenu_ShowAllColumns_Execute()
{
	ColumnSortMode = GetDefaultColumnSortMode();
	ColumnBeingSorted = GetDefaultColumnBeingSorted();

	for (const TSharedRef<Insights::FTableColumn>& ColumnRef : Table->GetColumns())
	{
		const Insights::FTableColumn& Column = *ColumnRef;

		if (!Column.IsVisible())
		{
			ShowColumn(Column.GetId());
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// "Show Min/Max/Median Columns" action (ContextMenu)
////////////////////////////////////////////////////////////////////////////////////////////////////

bool SNetStatsView::ContextMenu_ShowMinMaxMedColumns_CanExecute() const
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::ContextMenu_ShowMinMaxMedColumns_Execute()
{
	TSet<FName> Preset =
	{
		FNetStatsViewColumns::NameColumnID,
		//FNetStatsViewColumns::MetaGroupNameColumnID,
		//FNetStatsViewColumns::TypeColumnID,
		FNetStatsViewColumns::InstanceCountColumnID,

		FNetStatsViewColumns::TotalInclusiveSizeColumnID,
		FNetStatsViewColumns::MaxInclusiveSizeColumnID,
		FNetStatsViewColumns::AverageInclusiveSizeColumnID,

		FNetStatsViewColumns::TotalExclusiveSizeColumnID,
		FNetStatsViewColumns::MaxExclusiveSizeColumnID,
		//FNetStatsViewColumns::AverageExclusiveSizeColumnID,
	};

	ColumnBeingSorted = FNetStatsViewColumns::TotalInclusiveSizeColumnID;
	ColumnSortMode = EColumnSortMode::Descending;
	UpdateCurrentSortingByColumn();

	for (const TSharedRef<Insights::FTableColumn>& ColumnRef : Table->GetColumns())
	{
		const Insights::FTableColumn& Column = *ColumnRef;

		const bool bShouldBeVisible = Preset.Contains(Column.GetId());

		if (bShouldBeVisible && !Column.IsVisible())
		{
			ShowColumn(Column.GetId());
		}
		else if (!bShouldBeVisible && Column.IsVisible())
		{
			HideColumn(Column.GetId());
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// ResetColumns action (ContextMenu)
////////////////////////////////////////////////////////////////////////////////////////////////////

bool SNetStatsView::ContextMenu_ResetColumns_CanExecute() const
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::ContextMenu_ResetColumns_Execute()
{
	ColumnBeingSorted = GetDefaultColumnBeingSorted();
	ColumnSortMode = GetDefaultColumnSortMode();
	UpdateCurrentSortingByColumn();

	for (const TSharedRef<Insights::FTableColumn>& ColumnRef : Table->GetColumns())
	{
		const Insights::FTableColumn& Column = *ColumnRef;

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

void SNetStatsView::Reset()
{
	GameInstanceIndex = 0;
	ConnectionIndex = 0;
	ConnectionMode = Trace::ENetProfilerConnectionMode::Outgoing;

	StatsPacketStartIndex = 0;
	StatsPacketEndIndex = 0;
	StatsStartPosition = 0;
	StatsEndPosition = 0;

	RebuildTree(true);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::RebuildTree(bool bResync)
{
	TArray<FNetEventNodePtr> SelectedItems;
	bool bListHasChanged = false;

	if (bResync)
	{
		const int32 PreviousNodeCount = NetEventNodes.Num();
		NetEventNodes.Empty(PreviousNodeCount);
		NetEventNodesIdMap.Empty(PreviousNodeCount);
		bListHasChanged = true;
	}

	if (Session.IsValid())
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

		const Trace::INetProfilerProvider& NetProfilerProvider = Trace::ReadNetProfilerProvider(*Session.Get());

		NetProfilerProvider.ReadEventTypes([this, &bResync, &SelectedItems, &bListHasChanged, &NetProfilerProvider](const Trace::FNetProfilerEventType* NetEvents, uint64 NetEventCount)
		{
			if (NetEventCount != NetEventNodes.Num())
			{
				bResync = true;
			}

			if (bResync)
			{
				// Save selection.
				TreeView->GetSelectedItems(SelectedItems);

				const int32 PreviousNodeCount = NetEventNodes.Num();
				NetEventNodes.Empty(PreviousNodeCount);
				NetEventNodesIdMap.Empty(PreviousNodeCount);
				bListHasChanged = true;

				for (uint64 Index = 0; Index < NetEventCount; ++Index)
				{
					const Trace::FNetProfilerEventType& NetEvent = NetEvents[Index];

					const uint64 Id = NetEvent.EventTypeIndex;

					const TCHAR* NamePtr;
					NetProfilerProvider.ReadName(NetEvent.NameIndex, [&NamePtr](const Trace::FNetProfilerName& InName)
					{
						NamePtr = InName.Name;
					});
					const FName Name(NamePtr);
					const ENetEventNodeType Type = ENetEventNodeType::NetEvent;
					FNetEventNodePtr NetEventNodePtr = MakeShared<FNetEventNode>(Id, Name, Type, NetEvent.Level);
					NetEventNodes.Add(NetEventNodePtr);
					NetEventNodesIdMap.Add(Id, NetEventNodePtr);
				}
			}
		});
	}

	if (bListHasChanged)
	{
		UpdateTree();
		UpdateStatsInternal();

		TreeView->RebuildList();

		// Restore selection.
		if (SelectedItems.Num() > 0)
		{
			TreeView->ClearSelection();
			TArray<FNetEventNodePtr> NewSelectedItems;
			for (const FNetEventNodePtr& NetEventNode : SelectedItems)
			{
				FNetEventNodePtr* NetEventNodePtrPtr = NetEventNodesIdMap.Find(NetEventNode->GetId());
				if (NetEventNodePtrPtr != nullptr)
				{
					NewSelectedItems.Add(*NetEventNodePtrPtr);
				}
			}
			if (NewSelectedItems.Num() > 0)
			{
				TreeView->SetItemSelection(NewSelectedItems, true);
				TreeView->RequestScrollIntoView(NewSelectedItems[0]);
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::ResetStats()
{
	GameInstanceIndex = 0;
	ConnectionIndex = 0;
	ConnectionMode = Trace::ENetProfilerConnectionMode::Outgoing;

	StatsPacketStartIndex = 0;
	StatsPacketEndIndex = 0;
	StatsStartPosition = 0;
	StatsEndPosition = 0;

	UpdateStatsInternal();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::UpdateStats(uint32 InGameInstanceIndex, uint32 InConnectionIndex, Trace::ENetProfilerConnectionMode InConnectionMode, uint32 InStatsPacketStartIndex, uint32 InStatsPacketEndIndex, uint32 InStatsStartPosition, uint32 InStatsEndPosition)
{
	GameInstanceIndex = InGameInstanceIndex;
	ConnectionIndex = InConnectionIndex;
	ConnectionMode = InConnectionMode;

	StatsPacketStartIndex = InStatsPacketStartIndex;
	StatsPacketEndIndex = InStatsPacketEndIndex;
	StatsStartPosition = InStatsStartPosition;
	StatsEndPosition = InStatsEndPosition;

	UpdateStatsInternal();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::UpdateStatsInternal()
{
	if (StatsPacketStartIndex >= StatsPacketEndIndex ||
		StatsStartPosition >= StatsEndPosition)
	{
		// keep previous aggregated stats
		return;
	}

	for (const FNetEventNodePtr& NetEventNodePtr : NetEventNodes)
	{
		NetEventNodePtr->ResetAggregatedStats();
	}

	if (Session.IsValid())
	{
		TUniquePtr<Trace::ITable<Trace::FNetProfilerAggregatedStats>> AggregationResultTable;

		{
			Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
			const Trace::INetProfilerProvider& NetProfilerProvider = Trace::ReadNetProfilerProvider(*Session.Get());
			AggregationResultTable.Reset(NetProfilerProvider.CreateAggregation(ConnectionIndex, ConnectionMode, StatsPacketStartIndex, StatsPacketEndIndex - 1, StatsStartPosition, StatsEndPosition));
		}

		if (AggregationResultTable.IsValid())
		{
			TUniquePtr<Trace::ITableReader<Trace::FNetProfilerAggregatedStats>> TableReader(AggregationResultTable->CreateReader());
			while (TableReader->IsValid())
			{
				const Trace::FNetProfilerAggregatedStats* Row = TableReader->GetCurrentRow();
				FNetEventNodePtr* NetEventNodePtrPtr = NetEventNodesIdMap.Find(static_cast<uint64>(Row->EventTypeIndex));
				if (NetEventNodePtrPtr != nullptr)
				{
					FNetEventNodePtr NetEventNodePtr = *NetEventNodePtrPtr;
					NetEventNodePtr->SetAggregatedStats(*Row);

					TSharedPtr<ITableRow> TableRowPtr = TreeView->WidgetFromItem(NetEventNodePtr);
					if (TableRowPtr.IsValid())
					{
						TSharedPtr<SNetStatsTableRow> RowPtr = StaticCastSharedPtr<SNetStatsTableRow, ITableRow>(TableRowPtr);
						RowPtr->InvalidateContent();
					}
				}
				TableReader->NextRow();
			}
		}
	}

	UpdateTree();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsView::SelectNetEventNode(uint64 Id)
{
	FNetEventNodePtr* NodePtrPtr = NetEventNodesIdMap.Find(Id);
	if (NodePtrPtr != nullptr)
	{
		FNetEventNodePtr NodePtr = *NodePtrPtr;

		TreeView->SetSelection(NodePtr);
		TreeView->RequestScrollIntoView(NodePtr);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
