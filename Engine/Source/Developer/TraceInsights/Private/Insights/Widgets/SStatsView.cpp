// Copyright Epic Games, Inc. All Rights Reserved.

#include "SStatsView.h"

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
#include "Insights/Common/Stopwatch.h"
#include "Insights/TimingProfilerCommon.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/ViewModels/TimingGraphTrack.h"
#include "Insights/ViewModels/StatsNodeHelper.h"
#include "Insights/ViewModels/StatsViewColumnFactory.h"
#include "Insights/Widgets/SStatsViewTooltip.h"
#include "Insights/Widgets/SStatsTableRow.h"
#include "Insights/Widgets/STimingProfilerWindow.h"
#include "Insights/Widgets/STimingView.h"

#define LOCTEXT_NAMESPACE "SStatsView"

////////////////////////////////////////////////////////////////////////////////////////////////////

const EColumnSortMode::Type SStatsView::GetDefaultColumnSortMode()
{
	return EColumnSortMode::Ascending;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FName SStatsView::GetDefaultColumnBeingSorted()
{
	return FStatsViewColumns::NameColumnID;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

SStatsView::SStatsView()
	: bExpansionSaved(false)
	, bFilterOutZeroCountStats(false)
	, GroupingMode(EStatsGroupingMode::Flat)
	, ColumnSortMode(GetDefaultColumnSortMode())
	, ColumnBeingSorted(GetDefaultColumnBeingSorted())
{
	FMemory::Memset(bStatsNodeIsVisible, 1);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

SStatsView::~SStatsView()
{
	// Remove ourselves from the Insights manager.
	if (FInsightsManager::Get().IsValid())
	{
		FInsightsManager::Get()->GetSessionChangedEvent().RemoveAll(this);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SStatsView::Construct(const FArguments& InArgs)
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
						.HintText(LOCTEXT("SearchBoxHint", "Search stats counters or groups"))
						.OnTextChanged(this, &SStatsView::SearchBox_OnTextChanged)
						.IsEnabled(this, &SStatsView::SearchBox_IsEnabled)
						.ToolTipText(LOCTEXT("FilterSearchHint", "Type here to search stats counter or group"))
					]

					// Filter out timers with zero instance count
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.Padding(2.0f)
					.AutoWidth()
					[
						SNew(SCheckBox)
						.Style(FEditorStyle::Get(), "ToggleButtonCheckbox")
						.HAlign(HAlign_Center)
						.Padding(2.0f)
						.OnCheckStateChanged(this, &SStatsView::FilterOutZeroCountStats_OnCheckStateChanged)
						.IsChecked(this, &SStatsView::FilterOutZeroCountStats_IsChecked)
						.ToolTipText(LOCTEXT("FilterOutZeroCountStats_Tooltip", "Filter out the stats counters having zero total instance count (aggregated stats)."))
						[
							//TODO: SNew(SImage)
							SNew(STextBlock)
							.Text(LOCTEXT("FilterOutZeroCountStats_Button", " !0 "))
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
						SAssignNew(GroupByComboBox, SComboBox<TSharedPtr<EStatsGroupingMode>>)
						.ToolTipText(this, &SStatsView::GroupBy_GetSelectedTooltipText)
						.OptionsSource(&GroupByOptionsSource)
						.OnSelectionChanged(this, &SStatsView::GroupBy_OnSelectionChanged)
						.OnGenerateWidget(this, &SStatsView::GroupBy_OnGenerateWidget)
						[
							SNew(STextBlock)
							.Text(this, &SStatsView::GroupBy_GetSelectedText)
						]
					]
				]

				// Check boxes for: Int64, Float
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
						GetToggleButtonForStatsType(EStatsNodeType::Int64)
					]

					+ SHorizontalBox::Slot()
					.Padding(FMargin(1.0f,0.0f,1.0f,0.0f))
					.FillWidth(1.0f)
					[
						GetToggleButtonForStatsType(EStatsNodeType::Float)
					]
				]
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
						SAssignNew(TreeView, STreeView<FStatsNodePtr>)
						.ExternalScrollbar(ExternalScrollbar)
						.SelectionMode(ESelectionMode::Multi)
						.TreeItemsSource(&FilteredGroupNodes)
						.OnGetChildren(this, &SStatsView::TreeView_OnGetChildren)
						.OnGenerateRow(this, &SStatsView::TreeView_OnGenerateRow)
						.OnSelectionChanged(this, &SStatsView::TreeView_OnSelectionChanged)
						.OnMouseButtonDoubleClick(this, &SStatsView::TreeView_OnMouseButtonDoubleClick)
						.OnContextMenuOpening(FOnContextMenuOpening::CreateSP(this, &SStatsView::TreeView_GetMenuContent))
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
	TextFilter = MakeShared<FStatsNodeTextFilter>(FStatsNodeTextFilter::FItemToStringArray::CreateSP(this, &SStatsView::HandleItemToStringArray));
	Filters = MakeShared<FStatsNodeFilterCollection>();
	Filters->Add(TextFilter);

	CreateGroupByOptionsSources();

	// Register ourselves with the Insights manager.
	FInsightsManager::Get()->GetSessionChangedEvent().AddSP(this, &SStatsView::InsightsManager_OnSessionChanged);

	// Update the Session (i.e. when analysis session was already started).
	InsightsManager_OnSessionChanged();
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<SWidget> SStatsView::TreeView_GetMenuContent()
{
	const TArray<FStatsNodePtr> SelectedStatsNodes = TreeView->GetSelectedItems();
	const int32 NumSelectedStatsNodes = SelectedStatsNodes.Num();
	FStatsNodePtr SelectedStatsNode = NumSelectedStatsNodes ? SelectedStatsNodes[0] : nullptr;

	const FStatsViewColumn* const * ColumnPtrPtr = FStatsViewColumnFactory::Get().ColumnIdToPtrMapping.Find(HoveredColumnId);
	const FStatsViewColumn* const ColumnPtr = (ColumnPtrPtr != nullptr) ? *ColumnPtrPtr : nullptr;

	FText SelectionStr;
	FText PropertyName;
	FText PropertyValue;

	if (NumSelectedStatsNodes == 0)
	{
		SelectionStr = LOCTEXT("NothingSelected", "Nothing selected");
	}
	else if (NumSelectedStatsNodes == 1)
	{
		if (ColumnPtr != nullptr)
		{
			PropertyName = ColumnPtr->ShortName;
			PropertyValue = ColumnPtr->GetFormattedValue(*SelectedStatsNode);
		}
		SelectionStr = FText::FromName(SelectedStatsNode->GetName());
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
			FExecuteAction::CreateSP(this, &SStatsView::ContextMenu_CopySelectedToClipboard_Execute),
			FCanExecuteAction::CreateSP(this, &SStatsView::ContextMenu_CopySelectedToClipboard_CanExecute)
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
			FNewMenuDelegate::CreateSP(this, &SStatsView::TreeView_BuildSortByMenu),
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
			FNewMenuDelegate::CreateSP(this, &SStatsView::TreeView_BuildViewColumnMenu),
			false,
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Profiler.EventGraph.ViewColumn")
		);

		FUIAction Action_ShowAllColumns
		(
			FExecuteAction::CreateSP(this, &SStatsView::ContextMenu_ShowAllColumns_Execute),
			FCanExecuteAction::CreateSP(this, &SStatsView::ContextMenu_ShowAllColumns_CanExecute)
		);
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_Header_Columns_ShowAllColumns", "Show All Columns"),
			LOCTEXT("ContextMenu_Header_Columns_ShowAllColumns_Desc", "Resets tree view to show all columns"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Profiler.EventGraph.ResetColumn"), Action_ShowAllColumns, NAME_None, EUserInterfaceActionType::Button
		);

		FUIAction Action_ShowMinMaxMedColumns
		(
			FExecuteAction::CreateSP(this, &SStatsView::ContextMenu_ShowMinMaxMedColumns_Execute),
			FCanExecuteAction::CreateSP(this, &SStatsView::ContextMenu_ShowMinMaxMedColumns_CanExecute)
		);
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_Header_Columns_ShowMinMaxMedColumns", "Reset Columns to Min/Max/Median Preset"),
			LOCTEXT("ContextMenu_Header_Columns_ShowMinMaxMedColumns_Desc", "Resets columns to Min/Max/Median preset"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Profiler.EventGraph.ResetColumn"), Action_ShowMinMaxMedColumns, NAME_None, EUserInterfaceActionType::Button
		);

		FUIAction Action_ResetColumns
		(
			FExecuteAction::CreateSP(this, &SStatsView::ContextMenu_ResetColumns_Execute),
			FCanExecuteAction::CreateSP(this, &SStatsView::ContextMenu_ResetColumns_CanExecute)
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

void SStatsView::TreeView_BuildSortByMenu(FMenuBuilder& MenuBuilder)
{
	// TODO: Refactor later @see TSharedPtr<SWidget> SCascadePreviewViewportToolBar::GenerateViewMenu() const

	MenuBuilder.BeginSection("ColumnName", LOCTEXT("ContextMenu_Header_Misc_ColumnName", "Column Name"));

	for (auto It = TreeViewHeaderColumns.CreateConstIterator(); It; ++It)
	{
		const FStatsViewColumn& Column = It.Value();

		if (Column.bIsVisible && Column.bCanBeSorted())
		{
			FUIAction Action_SortByColumn
			(
				FExecuteAction::CreateSP(this, &SStatsView::ContextMenu_SortByColumn_Execute, Column.Id),
				FCanExecuteAction::CreateSP(this, &SStatsView::ContextMenu_SortByColumn_CanExecute, Column.Id),
				FIsActionChecked::CreateSP(this, &SStatsView::ContextMenu_SortByColumn_IsChecked, Column.Id)
			);
			MenuBuilder.AddMenuEntry
			(
				Column.TitleName,
				Column.Description,
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
			FExecuteAction::CreateSP(this, &SStatsView::ContextMenu_SortMode_Execute, EColumnSortMode::Ascending),
			FCanExecuteAction::CreateSP(this, &SStatsView::ContextMenu_SortMode_CanExecute, EColumnSortMode::Ascending),
			FIsActionChecked::CreateSP(this, &SStatsView::ContextMenu_SortMode_IsChecked, EColumnSortMode::Ascending)
		);
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_Header_Misc_Sort_SortAscending", "Sort Ascending"),
			LOCTEXT("ContextMenu_Header_Misc_Sort_SortAscending_Desc", "Sorts ascending"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Profiler.Misc.SortAscending"), Action_SortAscending, NAME_None, EUserInterfaceActionType::RadioButton
		);

		FUIAction Action_SortDescending
		(
			FExecuteAction::CreateSP(this, &SStatsView::ContextMenu_SortMode_Execute, EColumnSortMode::Descending),
			FCanExecuteAction::CreateSP(this, &SStatsView::ContextMenu_SortMode_CanExecute, EColumnSortMode::Descending),
			FIsActionChecked::CreateSP(this, &SStatsView::ContextMenu_SortMode_IsChecked, EColumnSortMode::Descending)
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

void SStatsView::TreeView_BuildViewColumnMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection("ViewColumn", LOCTEXT("ContextMenu_Header_Columns_View", "View Column"));

	for (auto It = TreeViewHeaderColumns.CreateConstIterator(); It; ++It)
	{
		const FStatsViewColumn& Column = It.Value();

		FUIAction Action_ToggleColumn
		(
			FExecuteAction::CreateSP(this, &SStatsView::ContextMenu_ToggleColumn_Execute, Column.Id),
			FCanExecuteAction::CreateSP(this, &SStatsView::ContextMenu_ToggleColumn_CanExecute, Column.Id),
			FIsActionChecked::CreateSP(this, &SStatsView::ContextMenu_ToggleColumn_IsChecked, Column.Id)
		);
		MenuBuilder.AddMenuEntry
		(
			Column.TitleName,
			Column.Description,
			FSlateIcon(), Action_ToggleColumn, NAME_None, EUserInterfaceActionType::ToggleButton
		);
	}

	MenuBuilder.EndSection();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::InitializeAndShowHeaderColumns()
{
	const int32 NumColumns = FStatsViewColumnFactory::Get().Collection.Num();
	for (int32 ColumnIndex = 0; ColumnIndex < NumColumns; ColumnIndex++)
	{
		TreeViewHeaderRow_CreateColumnArgs(ColumnIndex);
	}

	for (auto It = TreeViewHeaderColumns.CreateConstIterator(); It; ++It)
	{
		const FStatsViewColumn& Column = It.Value();

		if (Column.bIsVisible)
		{
			TreeViewHeaderRow_ShowColumn(Column.Id);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::TreeViewHeaderRow_CreateColumnArgs(const int32 ColumnIndex)
{
	const FStatsViewColumn& Column = *FStatsViewColumnFactory::Get().Collection[ColumnIndex];
	SHeaderRow::FColumn::FArguments ColumnArgs;

	ColumnArgs
		.ColumnId(Column.Id)
		.DefaultLabel(Column.ShortName)
		.HAlignHeader(HAlign_Fill)
		.VAlignHeader(VAlign_Fill)
		.HeaderContentPadding(FMargin(2.0f))
		.HAlignCell(HAlign_Fill)
		.VAlignCell(VAlign_Fill)
		.SortMode(this, &SStatsView::GetSortModeForColumn, Column.Id)
		.OnSort(this, &SStatsView::OnSortModeChanged)
		.ManualWidth(Column.InitialColumnWidth)
		.FixedWidth(Column.bIsFixedColumnWidth() ? Column.InitialColumnWidth : TOptional<float>())
		.HeaderContent()
		[
			SNew(SBox)
			.ToolTip(SStatsViewTooltip::GetColumnTooltip(Column))
			.HAlign(Column.HorizontalAlignment)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Column.ShortName)
			]
		]
		.MenuContent()
		[
			TreeViewHeaderRow_GenerateColumnMenu(Column)
		];

	TreeViewHeaderColumnArgs.Add(Column.Id, ColumnArgs);
	TreeViewHeaderColumns.Add(Column.Id, Column);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::TreeViewHeaderRow_ShowColumn(const FName ColumnId)
{
	FStatsViewColumn& Column = TreeViewHeaderColumns.FindChecked(ColumnId);
	Column.bIsVisible = true;
	SHeaderRow::FColumn::FArguments& ColumnArgs = TreeViewHeaderColumnArgs.FindChecked(ColumnId);

	const int32 NumColumns = TreeViewHeaderRow->GetColumns().Num();
	int32 ColumnIndex = 0;
	for (; ColumnIndex < NumColumns; ColumnIndex++)
	{
		const SHeaderRow::FColumn& CurrentColumn = TreeViewHeaderRow->GetColumns()[ColumnIndex];
		const FStatsViewColumn& CurrentStatsViewColumn = TreeViewHeaderColumns.FindChecked(CurrentColumn.ColumnId);
		if (Column.Order < CurrentStatsViewColumn.Order)
			break;
	}

	TreeViewHeaderRow->InsertColumn(ColumnArgs, ColumnIndex);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SStatsView::TreeViewHeaderRow_GenerateColumnMenu(const FStatsViewColumn& Column)
{
	bool bIsMenuVisible = false;

	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, NULL);
	{
		if (Column.bCanBeHidden())
		{
			MenuBuilder.BeginSection("Column", LOCTEXT("TreeViewHeaderRow_Header_Column", "Column"));

			FUIAction Action_HideColumn
			(
				FExecuteAction::CreateSP(this, &SStatsView::HeaderMenu_HideColumn_Execute, Column.Id),
				FCanExecuteAction::CreateSP(this, &SStatsView::HeaderMenu_HideColumn_CanExecute, Column.Id)
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

		if (Column.bCanBeSorted())
		{
			MenuBuilder.BeginSection("SortMode", LOCTEXT("ContextMenu_Header_Misc_Sort_SortMode", "Sort Mode"));

			FUIAction Action_SortAscending
			(
				FExecuteAction::CreateSP(this, &SStatsView::HeaderMenu_SortMode_Execute, Column.Id, EColumnSortMode::Ascending),
				FCanExecuteAction::CreateSP(this, &SStatsView::HeaderMenu_SortMode_CanExecute, Column.Id, EColumnSortMode::Ascending),
				FIsActionChecked::CreateSP(this, &SStatsView::HeaderMenu_SortMode_IsChecked, Column.Id, EColumnSortMode::Ascending)
			);
			MenuBuilder.AddMenuEntry
			(
				LOCTEXT("ContextMenu_Header_Misc_Sort_SortAscending", "Sort Ascending"),
				LOCTEXT("ContextMenu_Header_Misc_Sort_SortAscending_Desc", "Sorts ascending"),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "Profiler.Misc.SortAscending"), Action_SortAscending, NAME_None, EUserInterfaceActionType::RadioButton
			);

			FUIAction Action_SortDescending
			(
				FExecuteAction::CreateSP(this, &SStatsView::HeaderMenu_SortMode_Execute, Column.Id, EColumnSortMode::Descending),
				FCanExecuteAction::CreateSP(this, &SStatsView::HeaderMenu_SortMode_CanExecute, Column.Id, EColumnSortMode::Descending),
				FIsActionChecked::CreateSP(this, &SStatsView::HeaderMenu_SortMode_IsChecked, Column.Id, EColumnSortMode::Descending)
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

		if (Column.bCanBeFiltered())
		{
			MenuBuilder.BeginSection("FilterMode", LOCTEXT("ContextMenu_Header_Misc_Filter_FilterMode", "Filter Mode"));
			bIsMenuVisible = true;
			MenuBuilder.EndSection();
		}
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

void SStatsView::InsightsManager_OnSessionChanged()
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

void SStatsView::UpdateTree()
{
	CreateGroups();
	SortStats();
	ApplyFiltering();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::ApplyFiltering()
{
	FilteredGroupNodes.Reset();

	// Apply filter to all groups and its children.
	const int32 NumGroups = GroupNodes.Num();
	for (int32 GroupIndex = 0; GroupIndex < NumGroups; ++GroupIndex)
	{
		FStatsNodePtr& GroupPtr = GroupNodes[GroupIndex];
		GroupPtr->ClearFilteredChildren();
		const bool bIsGroupVisible = Filters->PassesAllFilters(GroupPtr);

		const TArray<Insights::FBaseTreeNodePtr>& GroupChildren = GroupPtr->GetChildren();
		const int32 NumChildren = GroupChildren.Num();
		int32 NumVisibleChildren = 0;
		for (int32 Cx = 0; Cx < NumChildren; ++Cx)
		{
			// Add a child.
			const FStatsNodePtr& NodePtr = StaticCastSharedPtr<FStatsNode, Insights::FBaseTreeNode>(GroupChildren[Cx]);
			const bool bIsChildVisible = (!bFilterOutZeroCountStats || NodePtr->GetAggregatedStats().Count > 0)
									  && bStatsNodeIsVisible[static_cast<int>(NodePtr->GetType())]
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

	// Only expand stats nodes if we have a text filter.
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
			const FStatsNodePtr& GroupPtr = FilteredGroupNodes[Fx];
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

void SStatsView::HandleItemToStringArray(const FStatsNodePtr& FStatsNodePtr, TArray<FString>& OutSearchStrings) const
{
	OutSearchStrings.Add(FStatsNodePtr->GetName().GetPlainNameString());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SStatsView::GetToggleButtonForStatsType(const EStatsNodeType NodeType)
{
	return SNew(SCheckBox)
		.Style(FEditorStyle::Get(), "ToggleButtonCheckbox")
		.HAlign(HAlign_Center)
		.Padding(2.0f)
		.OnCheckStateChanged(this, &SStatsView::FilterByStatsType_OnCheckStateChanged, NodeType)
		.IsChecked(this, &SStatsView::FilterByStatsType_IsChecked, NodeType)
		.ToolTipText(StatsNodeTypeHelper::ToDescription(NodeType))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
						.Image(StatsNodeTypeHelper::GetIconForStatsNodeType(NodeType))
				]

			+ SHorizontalBox::Slot()
				.Padding(2.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
						.Text(StatsNodeTypeHelper::ToName(NodeType))
						.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Caption"))
				]
		];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::FilterOutZeroCountStats_OnCheckStateChanged(ECheckBoxState NewRadioState)
{
	bFilterOutZeroCountStats = (NewRadioState == ECheckBoxState::Checked);
	ApplyFiltering();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

ECheckBoxState SStatsView::FilterOutZeroCountStats_IsChecked() const
{
	return bFilterOutZeroCountStats ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::FilterByStatsType_OnCheckStateChanged(ECheckBoxState NewRadioState, const EStatsNodeType InStatType)
{
	bStatsNodeIsVisible[static_cast<int>(InStatType)] = (NewRadioState == ECheckBoxState::Checked);
	ApplyFiltering();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

ECheckBoxState SStatsView::FilterByStatsType_IsChecked(const EStatsNodeType InStatType) const
{
	return bStatsNodeIsVisible[static_cast<int>(InStatType)] ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// TreeView
////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::TreeView_Refresh()
{
	if (TreeView.IsValid())
	{
		TreeView->RequestTreeRefresh();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::TreeView_OnSelectionChanged(FStatsNodePtr SelectedItem, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo != ESelectInfo::Direct)
	{
		TArray<FStatsNodePtr> SelectedItems = TreeView->GetSelectedItems();
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

void SStatsView::TreeView_OnGetChildren(FStatsNodePtr InParent, TArray<FStatsNodePtr>& OutChildren)
{
	const TArray<Insights::FBaseTreeNodePtr>& Children = InParent->GetFilteredChildren();
	OutChildren.Reset(Children.Num());
	for (const Insights::FBaseTreeNodePtr& Child : Children)
	{
		OutChildren.Add(StaticCastSharedPtr<FStatsNode, Insights::FBaseTreeNode>(Child));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::UpdateStatsNode(FStatsNodePtr StatsNode)
{
	bool bAddedToGraphFlag = false;

	if (!StatsNode->IsGroup())
	{
		TSharedPtr<STimingProfilerWindow> Wnd = FTimingProfilerManager::Get()->GetProfilerWindow();
		if (Wnd.IsValid())
		{
			TSharedPtr<STimingView> TimingView = Wnd->GetTimingView();
			if (TimingView.IsValid())
			{
				TSharedPtr<FTimingGraphTrack> GraphTrack = TimingView->GetMainTimingGraphTrack();
				if (GraphTrack.IsValid())
				{
					uint32 StatsCounterId = static_cast<uint32>(StatsNode->GetId());
					TSharedPtr<FTimingGraphSeries> Series = GraphTrack->GetStatsCounterSeries(StatsCounterId);
					bAddedToGraphFlag = Series.IsValid();
				}
			}
		}
	}

	StatsNode->SetAddedToGraphFlag(bAddedToGraphFlag);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::TreeView_OnMouseButtonDoubleClick(FStatsNodePtr StatsNode)
{
	if (!StatsNode->IsGroup())
	{
		TSharedPtr<STimingProfilerWindow> Wnd = FTimingProfilerManager::Get()->GetProfilerWindow();
		if (Wnd.IsValid())
		{
			TSharedPtr<STimingView> TimingView = Wnd->GetTimingView();
			if (TimingView.IsValid())
			{
				TSharedPtr<FTimingGraphTrack> GraphTrack = TimingView->GetMainTimingGraphTrack();
				if (GraphTrack.IsValid())
				{
					uint32 StatsCounterId = static_cast<uint32>(StatsNode->GetId());
					TSharedPtr<FTimingGraphSeries> Series = GraphTrack->GetStatsCounterSeries(StatsCounterId);
					if (Series.IsValid())
					{
						GraphTrack->RemoveStatsCounterSeries(StatsCounterId);
						GraphTrack->SetDirtyFlag();
						StatsNode->SetAddedToGraphFlag(false);
					}
					else
					{
						GraphTrack->AddStatsCounterSeries(StatsCounterId, StatsNode->GetColor());
						GraphTrack->SetDirtyFlag();
						StatsNode->SetAddedToGraphFlag(true);
					}
				}
			}
		}

		//im:TODO: const bool bIsTracked = FTimingProfilerManager::Get()->IsStatsCounterTracked(StatsNode->GetId());
	//	if (!bIsTracked)
	//	{
	//		// Add a new graph series.
	//		FTimingProfilerManager::Get()->TrackStatsCounter(StatsNode->GetId());
	//	}
	//	else
	//	{
	//		// Remove the corresponding graph series.
	//		FTimingProfilerManager::Get()->UntrackStatsCounter(StatsNode->GetId());
	//	}
	}
	else
	{
		const bool bIsGroupExpanded = TreeView->IsItemExpanded(StatsNode);
		TreeView->SetItemExpansion(StatsNode, !bIsGroupExpanded);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Tree View's Table Row
////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<ITableRow> SStatsView::TreeView_OnGenerateRow(FStatsNodePtr StatsNodePtr, const TSharedRef<STableViewBase>& OwnerTable)
{
	TSharedRef<ITableRow> TableRow =
		SNew(SStatsTableRow, OwnerTable)
		.OnShouldBeEnabled(this, &SStatsView::TableRow_ShouldBeEnabled)
		.OnIsColumnVisible(this, &SStatsView::TableRow_IsColumnVisible)
		.OnSetHoveredTableCell(this, &SStatsView::TableRow_SetHoveredTableCell)
		.OnGetColumnOutlineHAlignmentDelegate(this, &SStatsView::TableRow_GetColumnOutlineHAlignment)
		.HighlightText(this, &SStatsView::TableRow_GetHighlightText)
		.HighlightedNodeName(this, &SStatsView::TableRow_GetHighlightedNodeName)
		.StatsNodePtr(StatsNodePtr);

	return TableRow;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SStatsView::TableRow_IsColumnVisible(const FName ColumnId) const
{
	bool bResult = false;
	const FStatsViewColumn& ColumnPtr = TreeViewHeaderColumns.FindChecked(ColumnId);
	return ColumnPtr.bIsVisible;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::TableRow_SetHoveredTableCell(const FName ColumnId, const FStatsNodePtr StatsNodePtr)
{
	HoveredColumnId = ColumnId;

	const bool bIsAnyMenusVisible = FSlateApplication::Get().AnyMenusVisible();
	if (!HasMouseCapture() && !bIsAnyMenusVisible)
	{
		HoveredStatsNodePtr = StatsNodePtr;
	}

	//UE_LOG(TimingProfiler, Log, TEXT("%s -> %s"), *HoveredColumnId.GetPlainNameString(), StatsNodePtr.IsValid() ? *StatsNodePtr->GetName().GetPlainNameString() : TEXT("nullptr"));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EHorizontalAlignment SStatsView::TableRow_GetColumnOutlineHAlignment(const FName ColumnId) const
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

FText SStatsView::TableRow_GetHighlightText() const
{
	return SearchBox->GetText();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FName SStatsView::TableRow_GetHighlightedNodeName() const
{
	return HighlightedNodeName;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SStatsView::TableRow_ShouldBeEnabled(const uint32 StatsId) const
{
	return true;//im:TODO: Session->GetAggregatedStat(StatsId) != nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// SearchBox
////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::SearchBox_OnTextChanged(const FText& InFilterText)
{
	TextFilter->SetRawFilterText(InFilterText);
	SearchBox->SetError(TextFilter->GetFilterErrorText());
	ApplyFiltering();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SStatsView::SearchBox_IsEnabled() const
{
	return StatsNodes.Num() > 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// GroupBy
////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::CreateGroups()
{
	TMap<FName, FStatsNodePtr> GroupNodeSet;

	if (GroupingMode == EStatsGroupingMode::Flat)
	{
		const FName GroupName(TEXT("All"));
		FStatsNodePtr* GroupPtr = GroupNodeSet.Find(GroupName);
		if (!GroupPtr)
		{
			GroupPtr = &GroupNodeSet.Add(GroupName, MakeShared<FStatsNode>(GroupName));
		}

		for (const FStatsNodePtr& StatsNodePtr : StatsNodes)
		{
			(*GroupPtr)->AddChildAndSetGroupPtr(StatsNodePtr);
		}

		TreeView->SetItemExpansion(*GroupPtr, true);
	}
	// Creates groups based on stat metadata groups.
	else if (GroupingMode == EStatsGroupingMode::ByMetaGroupName)
	{
		for (const FStatsNodePtr& StatsNodePtr : StatsNodes)
		{
			const FName GroupName = StatsNodePtr->GetMetaGroupName();

			FStatsNodePtr* GroupPtr = GroupNodeSet.Find(GroupName);
			if (!GroupPtr)
			{
				GroupPtr = &GroupNodeSet.Add(GroupName, MakeShared<FStatsNode>(GroupName));
			}

			(*GroupPtr)->AddChildAndSetGroupPtr(StatsNodePtr);
			TreeView->SetItemExpansion(*GroupPtr, true);
		}
	}
	// Creates one group for each stat type.
	else if (GroupingMode == EStatsGroupingMode::ByType)
	{
		for (const FStatsNodePtr& StatsNodePtr : StatsNodes)
		{
			const FName GroupName = *StatsNodeTypeHelper::ToName(StatsNodePtr->GetType()).ToString();

			FStatsNodePtr* GroupPtr = GroupNodeSet.Find(GroupName);
			if (!GroupPtr)
			{
				GroupPtr = &GroupNodeSet.Add(GroupName, MakeShared<FStatsNode>(GroupName));
			}

			(*GroupPtr)->AddChildAndSetGroupPtr(StatsNodePtr);
			TreeView->SetItemExpansion(*GroupPtr, true);
		}
	}
	// Creates one group for one letter.
	else if (GroupingMode == EStatsGroupingMode::ByName)
	{
		for (const FStatsNodePtr& StatsNodePtr : StatsNodes)
		{
			const FName GroupName = *StatsNodePtr->GetName().GetPlainNameString().Left(1).ToUpper();

			FStatsNodePtr* GroupPtr = GroupNodeSet.Find(GroupName);
			if (!GroupPtr)
			{
				GroupPtr = &GroupNodeSet.Add(GroupName, MakeShared<FStatsNode>(GroupName));
			}

			(*GroupPtr)->AddChildAndSetGroupPtr(StatsNodePtr);
		}
	}

	GroupNodeSet.GenerateValueArray(GroupNodes);

	// Sort by a fake group name.
	GroupNodes.Sort(StatsNodeSortingHelper::ByNameAscending());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::CreateGroupByOptionsSources()
{
	GroupByOptionsSource.Reset(3);

	// Must be added in order of elements in the EStatsGroupingMode.
	GroupByOptionsSource.Add(MakeShared<EStatsGroupingMode>(EStatsGroupingMode::Flat));
	GroupByOptionsSource.Add(MakeShared<EStatsGroupingMode>(EStatsGroupingMode::ByName));
	GroupByOptionsSource.Add(MakeShared<EStatsGroupingMode>(EStatsGroupingMode::ByMetaGroupName));
	GroupByOptionsSource.Add(MakeShared<EStatsGroupingMode>(EStatsGroupingMode::ByType));

	EStatsGroupingModePtr* GroupingModePtrPtr = GroupByOptionsSource.FindByPredicate([&](const EStatsGroupingModePtr InGroupingModePtr) { return *InGroupingModePtr == GroupingMode; });
	if (GroupingModePtrPtr != nullptr)
	{
		GroupByComboBox->SetSelectedItem(*GroupingModePtrPtr);
	}

	GroupByComboBox->RefreshOptions();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::GroupBy_OnSelectionChanged(TSharedPtr<EStatsGroupingMode> NewGroupingMode, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo != ESelectInfo::Direct)
	{
		GroupingMode = *NewGroupingMode;

		CreateGroups();
		SortStats();
		ApplyFiltering();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SStatsView::GroupBy_OnGenerateWidget(TSharedPtr<EStatsGroupingMode> InGroupingMode) const
{
	return SNew(STextBlock)
		.Text(StatsNodeGroupingHelper::ToName(*InGroupingMode))
		.ToolTipText(StatsNodeGroupingHelper::ToDescription(*InGroupingMode));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SStatsView::GroupBy_GetSelectedText() const
{
	return StatsNodeGroupingHelper::ToName(GroupingMode);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SStatsView::GroupBy_GetSelectedTooltipText() const
{
	return StatsNodeGroupingHelper::ToDescription(GroupingMode);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// SortBy
////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::SortStats()
{
	const int32 NumGroups = GroupNodes.Num();

	#define CHECK_AND_SORT_COLUMN(ColumnId, SortTypeName) \
		if (ColumnBeingSorted == ColumnId) \
		{ \
			if (ColumnSortMode == EColumnSortMode::Type::Descending) \
			{ \
				for (int32 ID = 0; ID < NumGroups; ++ID) \
				{ \
					GroupNodes[ID]->SortChildren(StatsNodeSortingHelper::SortTypeName##Descending()); \
				} \
			} \
			else /*if (ColumnSortMode == EColumnSortMode::Type::Ascending)*/ \
			{ \
				for (int32 ID = 0; ID < NumGroups; ++ID) \
				{ \
					GroupNodes[ID]->SortChildren(StatsNodeSortingHelper::SortTypeName##Ascending()); \
				} \
			} \
		}

		 CHECK_AND_SORT_COLUMN(FStatsViewColumns::NameColumnID,          ByName)
	else CHECK_AND_SORT_COLUMN(FStatsViewColumns::MetaGroupNameColumnID, ByMetaGroupName)
	else CHECK_AND_SORT_COLUMN(FStatsViewColumns::TypeColumnID,          ByType)
	else CHECK_AND_SORT_COLUMN(FStatsViewColumns::CountColumnID,         ByCount)
	else CHECK_AND_SORT_COLUMN(FStatsViewColumns::SumColumnID,           BySum)
	else CHECK_AND_SORT_COLUMN(FStatsViewColumns::MaxColumnID,           ByMax)
	else CHECK_AND_SORT_COLUMN(FStatsViewColumns::UpperQuartileColumnID, ByUpperQuartile)
	else CHECK_AND_SORT_COLUMN(FStatsViewColumns::AverageColumnID,       ByAverage)
	else CHECK_AND_SORT_COLUMN(FStatsViewColumns::MedianColumnID,        ByMedian)
	else CHECK_AND_SORT_COLUMN(FStatsViewColumns::LowerQuartileColumnID, ByLowerQuartile)
	else CHECK_AND_SORT_COLUMN(FStatsViewColumns::MinColumnID,           ByMin)

	#undef CHECK_AND_SORT_COLUMN
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EColumnSortMode::Type SStatsView::GetSortModeForColumn(const FName ColumnId) const
{
	if (ColumnBeingSorted != ColumnId)
	{
		return EColumnSortMode::None;
	}

	return ColumnSortMode;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::SetSortModeForColumn(const FName& ColumnId, const EColumnSortMode::Type SortMode)
{
	ColumnBeingSorted = ColumnId;
	ColumnSortMode = SortMode;

	// Sort stats and apply filtering.
	SortStats();
	ApplyFiltering();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::OnSortModeChanged(const EColumnSortPriority::Type SortPriority, const FName& ColumnId, const EColumnSortMode::Type SortMode)
{
	SetSortModeForColumn(ColumnId, SortMode);
	TreeView_Refresh();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// SortMode action (HeaderMenu)
////////////////////////////////////////////////////////////////////////////////////////////////////

bool SStatsView::HeaderMenu_SortMode_IsChecked(const FName ColumnId, const EColumnSortMode::Type InSortMode)
{
	return ColumnBeingSorted == ColumnId && ColumnSortMode == InSortMode;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SStatsView::HeaderMenu_SortMode_CanExecute(const FName ColumnId, const EColumnSortMode::Type InSortMode) const
{
	const FStatsViewColumn& Column = TreeViewHeaderColumns.FindChecked(ColumnId);
	return Column.bCanBeSorted();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::HeaderMenu_SortMode_Execute(const FName ColumnId, const EColumnSortMode::Type InSortMode)
{
	SetSortModeForColumn(ColumnId, InSortMode);
	TreeView_Refresh();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// SortMode action (ContextMenu)
////////////////////////////////////////////////////////////////////////////////////////////////////

bool SStatsView::ContextMenu_SortMode_IsChecked(const EColumnSortMode::Type InSortMode)
{
	return ColumnSortMode == InSortMode;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SStatsView::ContextMenu_SortMode_CanExecute(const EColumnSortMode::Type InSortMode) const
{
	return true; //ColumnSortMode != InSortMode;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::ContextMenu_SortMode_Execute(const EColumnSortMode::Type InSortMode)
{
	SetSortModeForColumn(ColumnBeingSorted, InSortMode);
	TreeView_Refresh();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// SortByColumn action (ContextMenu)
////////////////////////////////////////////////////////////////////////////////////////////////////

bool SStatsView::ContextMenu_SortByColumn_IsChecked(const FName ColumnId)
{
	return ColumnId == ColumnBeingSorted;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SStatsView::ContextMenu_SortByColumn_CanExecute(const FName ColumnId) const
{
	return true; //ColumnId != ColumnBeingSorted;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::ContextMenu_SortByColumn_Execute(const FName ColumnId)
{
	SetSortModeForColumn(ColumnId, EColumnSortMode::Descending);
	TreeView_Refresh();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// HideColumn action (HeaderMenu)
////////////////////////////////////////////////////////////////////////////////////////////////////

bool SStatsView::HeaderMenu_HideColumn_CanExecute(const FName ColumnId) const
{
	const FStatsViewColumn& Column = TreeViewHeaderColumns.FindChecked(ColumnId);
	return Column.bCanBeHidden();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::HeaderMenu_HideColumn_Execute(const FName ColumnId)
{
	FStatsViewColumn& Column = TreeViewHeaderColumns.FindChecked(ColumnId);
	Column.bIsVisible = false;
	TreeViewHeaderRow->RemoveColumn(ColumnId);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// ToggleColumn action (ContextMenu)
////////////////////////////////////////////////////////////////////////////////////////////////////

bool SStatsView::ContextMenu_ToggleColumn_IsChecked(const FName ColumnId)
{
	const FStatsViewColumn& Column = TreeViewHeaderColumns.FindChecked(ColumnId);
	return Column.bIsVisible;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SStatsView::ContextMenu_ToggleColumn_CanExecute(const FName ColumnId) const
{
	const FStatsViewColumn& Column = TreeViewHeaderColumns.FindChecked(ColumnId);
	return Column.bCanBeHidden();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::ContextMenu_ToggleColumn_Execute(const FName ColumnId)
{
	FStatsViewColumn& Column = TreeViewHeaderColumns.FindChecked(ColumnId);
	if (Column.bIsVisible)
	{
		HeaderMenu_HideColumn_Execute(ColumnId);
	}
	else
	{
		TreeViewHeaderRow_ShowColumn(ColumnId);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// "Show All Columns" action (ContextMenu)
////////////////////////////////////////////////////////////////////////////////////////////////////

bool SStatsView::ContextMenu_ShowAllColumns_CanExecute() const
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::ContextMenu_ShowAllColumns_Execute()
{
	ColumnSortMode = GetDefaultColumnSortMode();
	ColumnBeingSorted = GetDefaultColumnBeingSorted();

	const int32 NumColumns = FStatsViewColumnFactory::Get().Collection.Num();
	for (int32 ColumnIndex = 0; ColumnIndex < NumColumns; ColumnIndex++)
	{
		const FStatsViewColumn& DefaultColumn = *FStatsViewColumnFactory::Get().Collection[ColumnIndex];
		const FStatsViewColumn& CurrentColumn = TreeViewHeaderColumns.FindChecked(DefaultColumn.Id);

		if (!CurrentColumn.bIsVisible)
		{
			TreeViewHeaderRow_ShowColumn(DefaultColumn.Id);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// "Show Min/Max/Median Columns" action (ContextMenu)
////////////////////////////////////////////////////////////////////////////////////////////////////

bool SStatsView::ContextMenu_ShowMinMaxMedColumns_CanExecute() const
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::ContextMenu_ShowMinMaxMedColumns_Execute()
{
	TSet<FName> Preset =
	{
		FStatsViewColumns::NameColumnID,
		//FStatsViewColumns::MetaGroupNameColumnID,
		//FStatsViewColumns::TypeColumnID,
		FStatsViewColumns::CountColumnID,
		FStatsViewColumns::SumColumnID,
		FStatsViewColumns::MaxColumnID,
		FStatsViewColumns::UpperQuartileColumnID,
		//FStatsViewColumns::AverageColumnID,
		FStatsViewColumns::MedianColumnID,
		FStatsViewColumns::LowerQuartileColumnID,
		FStatsViewColumns::MinColumnID,
	};

	ColumnSortMode = EColumnSortMode::Descending;
	ColumnBeingSorted = FStatsViewColumns::CountColumnID;

	const int32 NumColumns = FStatsViewColumnFactory::Get().Collection.Num();
	for (int32 ColumnIndex = 0; ColumnIndex < NumColumns; ColumnIndex++)
	{
		const FStatsViewColumn& DefaultColumn = *FStatsViewColumnFactory::Get().Collection[ColumnIndex];
		const FStatsViewColumn& CurrentColumn = TreeViewHeaderColumns.FindChecked(DefaultColumn.Id);

		bool bIsVisible = Preset.Contains(DefaultColumn.Id);
		if (bIsVisible && !CurrentColumn.bIsVisible)
		{
			TreeViewHeaderRow_ShowColumn(DefaultColumn.Id);
		}
		else if (!bIsVisible && CurrentColumn.bIsVisible)
		{
			HeaderMenu_HideColumn_Execute(DefaultColumn.Id);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// ResetColumns action (ContextMenu)
////////////////////////////////////////////////////////////////////////////////////////////////////

bool SStatsView::ContextMenu_ResetColumns_CanExecute() const
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::ContextMenu_ResetColumns_Execute()
{
	ColumnSortMode = GetDefaultColumnSortMode();
	ColumnBeingSorted = GetDefaultColumnBeingSorted();

	const int32 NumColumns = FStatsViewColumnFactory::Get().Collection.Num();
	for (int32 ColumnIndex = 0; ColumnIndex < NumColumns; ColumnIndex++)
	{
		const FStatsViewColumn& DefaultColumn = *FStatsViewColumnFactory::Get().Collection[ColumnIndex];
		const FStatsViewColumn& CurrentColumn = TreeViewHeaderColumns.FindChecked(DefaultColumn.Id);

		if (DefaultColumn.bIsVisible && !CurrentColumn.bIsVisible)
		{
			TreeViewHeaderRow_ShowColumn(DefaultColumn.Id);
		}
		else if (!DefaultColumn.bIsVisible && CurrentColumn.bIsVisible)
		{
			HeaderMenu_HideColumn_Execute(DefaultColumn.Id);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::Reset()
{
	StatsStartTime = 0.0;
	StatsEndTime = 0.0;

	RebuildTree(true);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::RebuildTree(bool bResync)
{
	TArray<FStatsNodePtr> SelectedItems;
	bool bListHasChanged = false;

	if (bResync)
	{
		const int32 PreviousNodeCount = StatsNodes.Num();
		StatsNodes.Empty(PreviousNodeCount);
		//StatsNodesMap.Empty(PreviousNodeCount);
		StatsNodesIdMap.Empty(PreviousNodeCount);
		bListHasChanged = true;
	}

	if (Session.IsValid())
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

		const Trace::ICounterProvider& CountersProvider = Trace::ReadCounterProvider(*Session.Get());

		if (CountersProvider.GetCounterCount() != StatsNodes.Num())
		{
			bResync = true;
		}

		if (bResync)
		{
			// Save selection.
			TreeView->GetSelectedItems(SelectedItems);

			const int32 PreviousNodeCount = StatsNodes.Num();
			StatsNodes.Empty(PreviousNodeCount);
			//StatsNodesMap.Empty(PreviousNodeCount);
			StatsNodesIdMap.Empty(PreviousNodeCount);
			bListHasChanged = true;

			CountersProvider.EnumerateCounters([this](uint32 CounterId, const Trace::ICounter& Counter)
			{
				FName Name(Counter.GetName());
				FName Group(Counter.GetDisplayHint() == Trace::CounterDisplayHint_Memory ? TEXT("Memory") :
							Counter.IsFloatingPoint() ? TEXT("float") : TEXT("int64"));
				EStatsNodeType Type = Counter.IsFloatingPoint() ? EStatsNodeType::Float : EStatsNodeType::Int64;
				FStatsNodePtr StatsNodePtr = MakeShared<FStatsNode>(CounterId, Name, Group, Type);
				UpdateStatsNode(StatsNodePtr);
				StatsNodes.Add(StatsNodePtr);
				//StatsNodesMap.Add(Name, StatsNodePtr);
				StatsNodesIdMap.Add(CounterId, StatsNodePtr);
			});
		}
	}

	if (bListHasChanged)
	{
		UpdateTree();
		UpdateStats(StatsStartTime, StatsEndTime);

		TreeView->RebuildList();

		// Restore selection.
		if (SelectedItems.Num() > 0)
		{
			TreeView->ClearSelection();
			TArray<FStatsNodePtr> NewSelectedItems;
			for (const FStatsNodePtr& StatsNode : SelectedItems)
			{
				FStatsNodePtr* StatsNodePtrPtr = StatsNodesIdMap.Find(StatsNode->GetId());
				if (StatsNodePtrPtr != nullptr)
				{
					NewSelectedItems.Add(*StatsNodePtrPtr);
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

template<typename Type>
class TTimeCalculationHelper
{
public:
	TTimeCalculationHelper<Type>(double InIntervalStartTime, double InIntervalEndTime)
		: IntervalStartTime(InIntervalStartTime)
		, IntervalEndTime(InIntervalEndTime)
	{
	}

	void Update(uint32 CounterId, const Trace::ICounter& Counter)
	{
		EnumerateValues(CounterId, Counter, UpdateMinMax);
	}

	void PrecomputeHistograms();

	void UpdateHistograms(uint32 CounterId, const Trace::ICounter& Counter)
	{
		EnumerateValues(CounterId, Counter, UpdateHistogram);
	}

	void PostProcess(TMap<uint64, FStatsNodePtr>& StatsNodesIdMap, bool bComputeMedian);

private:
	template<typename CallbackType>
	void EnumerateValues(uint32 CounterId, const Trace::ICounter& Counter, CallbackType Callback);

	static void UpdateMinMax(TAggregatedStatsEx<Type>& Stats, Type Value);
	static void UpdateHistogram(TAggregatedStatsEx<Type>& StatsEx, Type Value);
	static void PostProcess(TAggregatedStatsEx<Type>& StatsEx, bool bComputeMedian);

	double IntervalStartTime;
	double IntervalEndTime;
	TMap<uint64, TAggregatedStatsEx<Type>> StatsMap;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// specialization for Type = double

template<>
template<typename CallbackType>
void TTimeCalculationHelper<double>::EnumerateValues(uint32 CounterId, const Trace::ICounter& Counter, CallbackType Callback)
{
	TAggregatedStatsEx<double>* StatsExPtr = StatsMap.Find(CounterId);
	if (!StatsExPtr)
	{
		StatsExPtr = &StatsMap.Add(CounterId);
		StatsExPtr->BaseStats.Min = +MAX_dbl;
		StatsExPtr->BaseStats.Max = -MAX_dbl;
	}
	TAggregatedStatsEx<double>& StatsEx = *StatsExPtr;

	Counter.EnumerateFloatValues(IntervalStartTime, IntervalEndTime, false, [this, &StatsEx, Callback](double Time, double Value)
	{
		Callback(StatsEx, Value);
	});
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// specialization for Type = int64

template<>
template<typename CallbackType>
void TTimeCalculationHelper<int64>::EnumerateValues(uint32 CounterId, const Trace::ICounter& Counter, CallbackType Callback)
{
	TAggregatedStatsEx<int64>* StatsExPtr = StatsMap.Find(CounterId);
	if (!StatsExPtr)
	{
		StatsExPtr = &StatsMap.Add(CounterId);
		StatsExPtr->BaseStats.Min = +MAX_int64;
		StatsExPtr->BaseStats.Max = -MAX_int64;
	}
	TAggregatedStatsEx<int64>& StatsEx = *StatsExPtr;

	Counter.EnumerateValues(IntervalStartTime, IntervalEndTime, false, [this, &StatsEx, Callback](double Time, int64 Value)
	{
		Callback(StatsEx, Value);
	});
}

////////////////////////////////////////////////////////////////////////////////////////////////////

template<typename Type>
void TTimeCalculationHelper<Type>::UpdateMinMax(TAggregatedStatsEx<Type>& StatsEx, Type Value)
{
	TAggregatedStats<Type>& Stats = StatsEx.BaseStats;

	Stats.Sum += Value;

	if (Value < Stats.Min)
	{
		Stats.Min = Value;
	}

	if (Value > Stats.Max)
	{
		Stats.Max = Value;
	}

	Stats.Count++;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// specialization for Type = double

template<>
void TTimeCalculationHelper<double>::PrecomputeHistograms()
{
	for (auto& KV : StatsMap)
	{
		TAggregatedStatsEx<double>& StatsEx = KV.Value;
		const TAggregatedStats<double>& Stats = StatsEx.BaseStats;

		// Each bucket (Histogram[i]) will be centered on a value.
		// I.e. First bucket (bucket 0) is centered on Min value: [Min-DT/2, Min+DT/2)
		// and last bucket (bucket N-1) is centered on Max value: [Max-DT/2, Max+DT/2).

		if (Stats.Max == Stats.Min)
		{
			StatsEx.DT = 1.0; // single large bucket
		}
		else
		{
			StatsEx.DT = (Stats.Max - Stats.Min) / (TAggregatedStatsEx<double>::HistogramLen - 1);
			if (StatsEx.DT == 0.0)
			{
				StatsEx.DT = 1.0;
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// specialization for Type = int64

template<>
void TTimeCalculationHelper<int64>::PrecomputeHistograms()
{
	for (auto& KV : StatsMap)
	{
		TAggregatedStatsEx<int64>& StatsEx = KV.Value;
		const TAggregatedStats<int64>& Stats = StatsEx.BaseStats;

		// Each bucket (Histogram[i]) will be centered on a value.
		// I.e. First bucket (bucket 0) is centered on Min value: [Min-DT/2, Min+DT/2)
		// and last bucket (bucket N-1) is centered on Max value: [Max-DT/2, Max+DT/2).

		if (Stats.Max == Stats.Min)
		{
			StatsEx.DT = 1; // single bucket
		}
		else
		{
			// DT = Ceil[(Max - Min) / (N - 1)]
			StatsEx.DT = (Stats.Max - Stats.Min + TAggregatedStatsEx<int64>::HistogramLen - 2) / (TAggregatedStatsEx<int64>::HistogramLen - 1);
			if (StatsEx.DT == 0)
			{
				StatsEx.DT = 1;
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

template<typename Type>
void TTimeCalculationHelper<Type>::UpdateHistogram(TAggregatedStatsEx<Type>& StatsEx, Type Value)
{
	const TAggregatedStats<Type>& Stats = StatsEx.BaseStats;

	// Index = (Value - Min + DT/2) / DT
	int32 Index = static_cast<int32>((Value - Stats.Min + StatsEx.DT/2) / StatsEx.DT);
	ensure(Index >= 0);
	if (Index < 0)
	{
		Index = 0;
	}
	ensure(Index < TAggregatedStatsEx<Type>::HistogramLen);
	if (Index >= TAggregatedStatsEx<Type>::HistogramLen)
	{
		Index = TAggregatedStatsEx<Type>::HistogramLen - 1;
	}
	StatsEx.Histogram[Index]++;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

template<typename Type>
void TTimeCalculationHelper<Type>::PostProcess(TAggregatedStatsEx<Type>& StatsEx, bool bComputeMedian)
{
	TAggregatedStats<Type>& Stats = StatsEx.BaseStats;

	// Compute average value.
	if (Stats.Count > 0)
	{
		Stats.Average = Stats.Sum / static_cast<Type>(Stats.Count);

		if (bComputeMedian)
		{
			const int32 HalfCount = Stats.Count / 2;

			// Compute median value.
			int32 Count = 0;
			for (int32 HistogramIndex = 0; HistogramIndex < TAggregatedStatsEx<Type>::HistogramLen; HistogramIndex++)
			{
				Count += StatsEx.Histogram[HistogramIndex];
				if (Count > HalfCount)
				{
					Stats.Median = Stats.Min + HistogramIndex * StatsEx.DT;

					if (HistogramIndex > 0 &&
						Stats.Count % 2 == 0 &&
						Count - StatsEx.Histogram[HistogramIndex] == HalfCount)
					{
						const Type PrevMedian = Stats.Min + (HistogramIndex - 1) * StatsEx.DT;
						Stats.Median = (Stats.Median + PrevMedian) / 2;
					}

					break;
				}
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// specialization for Type = double

template<>
void TTimeCalculationHelper<double>::PostProcess(TMap<uint64, FStatsNodePtr>& StatsNodesIdMap, bool bComputeMedian)
{
	for (auto& KV : StatsMap)
	{
		PostProcess(KV.Value, bComputeMedian);

		// Update the stats node.
		FStatsNodePtr* NodePtrPtr = StatsNodesIdMap.Find(KV.Key);
		if (NodePtrPtr != nullptr)
		{
			FStatsNodePtr NodePtr = *NodePtrPtr;
			NodePtr->SetAggregatedStats(KV.Value.BaseStats);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// specialization for Type = int64

template<>
void TTimeCalculationHelper<int64>::PostProcess(TMap<uint64, FStatsNodePtr>& StatsNodesIdMap, bool bComputeMedian)
{
	for (auto& KV : StatsMap)
	{
		PostProcess(KV.Value, bComputeMedian);

		// Update the stats node.
		FStatsNodePtr* NodePtrPtr = StatsNodesIdMap.Find(KV.Key);
		if (NodePtrPtr != nullptr)
		{
			FStatsNodePtr NodePtr = *NodePtrPtr;
			NodePtr->SetAggregatedIntegerStats(KV.Value.BaseStats);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::UpdateStats(double StartTime, double EndTime)
{
	FStopwatch AggregationStopwatch;
	FStopwatch Stopwatch;
	Stopwatch.Start();

	StatsStartTime = StartTime;
	StatsEndTime = EndTime;

	if (StartTime >= EndTime)
	{
		// keep previous aggregated stats
		return;
	}

	for (const FStatsNodePtr& StatsNodePtr : StatsNodes)
	{
		StatsNodePtr->ResetAggregatedStats();
	}

	if (Session.IsValid())
	{
		const bool bComputeMedian = true;

		TTimeCalculationHelper<double> CalculationHelperDbl(StartTime, EndTime);
		TTimeCalculationHelper<int64>  CalculationHelperInt(StartTime, EndTime);

		AggregationStopwatch.Start();
		{
			Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

			const Trace::ICounterProvider& CountersProvider = Trace::ReadCounterProvider(*Session.Get());

			// Compute instance count and total/min/max inclusive/exclusive times for each counter.
			// Iterate through all counters.
			CountersProvider.EnumerateCounters([&CalculationHelperDbl, &CalculationHelperInt](uint32 CounterId, const Trace::ICounter& Counter)
			{
				if (Counter.IsFloatingPoint())
				{
					CalculationHelperDbl.Update(CounterId, Counter);
				}
				else
				{
					CalculationHelperInt.Update(CounterId, Counter);
				}
			});

			// Now, as we know min/max inclusive/exclusive times for counter, we can compute histogram and median values.
			if (bComputeMedian)
			{
				// Update bucket size (DT) for computing histogram.
				CalculationHelperDbl.PrecomputeHistograms();
				CalculationHelperInt.PrecomputeHistograms();

				// Compute histogram.
				// Iterate again through all counters.
				CountersProvider.EnumerateCounters([&CalculationHelperDbl, &CalculationHelperInt](uint32 CounterId, const Trace::ICounter& Counter)
				{
					if (Counter.IsFloatingPoint())
					{
						CalculationHelperDbl.UpdateHistograms(CounterId, Counter);
					}
					else
					{
						CalculationHelperInt.UpdateHistograms(CounterId, Counter);
					}
				});
			}
		}
		AggregationStopwatch.Stop();

		// Compute average and median inclusive/exclusive times.
		CalculationHelperDbl.PostProcess(StatsNodesIdMap, bComputeMedian);
		CalculationHelperInt.PostProcess(StatsNodesIdMap, bComputeMedian);
	}

	// Invalidate all tree table rows.
	for (const FStatsNodePtr NodePtr : StatsNodes)
	{
		TSharedPtr<ITableRow> TableRowPtr = TreeView->WidgetFromItem(NodePtr);
		if (TableRowPtr.IsValid())
		{
			TSharedPtr<SStatsTableRow> StatsTableRowPtr = StaticCastSharedPtr<SStatsTableRow, ITableRow>(TableRowPtr);
			StatsTableRowPtr->InvalidateContent();
		}
	}

	UpdateTree();

	Stopwatch.Stop();
	UE_LOG(TimingProfiler, Log, TEXT("Counters updated in %.3fs (%.3fs)"), Stopwatch.GetAccumulatedTime(), AggregationStopwatch.GetAccumulatedTime());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::SelectStatsNode(uint64 Id)
{
	FStatsNodePtr* NodePtrPtr = StatsNodesIdMap.Find(Id);
	if (NodePtrPtr != nullptr)
	{
		FStatsNodePtr NodePtr = *NodePtrPtr;

		TreeView->SetSelection(NodePtr);
		TreeView->RequestScrollIntoView(NodePtr);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
