// Copyright Epic Games, Inc. All Rights Reserved.

#include "SStatsView.h"

#include "EditorStyleSet.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformApplicationMisc.h"
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
#include "Insights/Common/TimeUtils.h"
#include "Insights/Table/ViewModels/Table.h"
#include "Insights/Table/ViewModels/TableColumn.h"
#include "Insights/TimingProfilerCommon.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/ViewModels/CounterAggregation.h"
#include "Insights/ViewModels/StatsNodeHelper.h"
#include "Insights/ViewModels/StatsViewColumnFactory.h"
#include "Insights/ViewModels/TimingGraphTrack.h"
#include "Insights/Widgets/SAggregatorStatus.h"
#include "Insights/Widgets/SStatsViewTooltip.h"
#include "Insights/Widgets/SStatsTableRow.h"
#include "Insights/Widgets/STimingProfilerWindow.h"
#include "Insights/Widgets/STimingView.h"

#include <limits>

#define LOCTEXT_NAMESPACE "SStatsView"

////////////////////////////////////////////////////////////////////////////////////////////////////
// FStatsViewCommands
////////////////////////////////////////////////////////////////////////////////////////////////////

class FStatsViewCommands : public TCommands<FStatsViewCommands>
{
public:
	FStatsViewCommands()
		: TCommands<FStatsViewCommands>(TEXT("FStatsViewCommands"), NSLOCTEXT("FStatsViewCommands", "Stats View Commands", "Stats View Commands"), NAME_None, FEditorStyle::Get().GetStyleSetName())
	{
	}

	virtual ~FStatsViewCommands()
	{
	}

	// UI_COMMAND takes long for the compiler to optimize
	PRAGMA_DISABLE_OPTIMIZATION
	virtual void RegisterCommands() override
	{
		UI_COMMAND(Command_CopyToClipboard, "Copy To Clipboard", "Copies selection to clipboard", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control, EKeys::C));
	}
	PRAGMA_ENABLE_OPTIMIZATION

	TSharedPtr<FUICommandInfo> Command_CopyToClipboard;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// SStatsView
////////////////////////////////////////////////////////////////////////////////////////////////////

SStatsView::SStatsView()
	: Table(MakeShared<Insights::FTable>())
	, bExpansionSaved(false)
	, bFilterOutZeroCountStats(false)
	, GroupingMode(EStatsGroupingMode::Flat)
	, AvailableSorters()
	, CurrentSorter(nullptr)
	, ColumnBeingSorted(GetDefaultColumnBeingSorted())
	, ColumnSortMode(GetDefaultColumnSortMode())
	, Aggregator(MakeShared<Insights::FCounterAggregator>())
{
	FMemory::Memset(FilterByNodeType, 1);
	FMemory::Memset(FilterByDataType, 1);
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

void SStatsView::InitCommandList()
{
	FStatsViewCommands::Register();
	CommandList = MakeShared<FUICommandList>();
	CommandList->MapAction(FStatsViewCommands::Get().Command_CopyToClipboard, FExecuteAction::CreateSP(this, &SStatsView::ContextMenu_CopySelectedToClipboard_Execute), FCanExecuteAction::CreateSP(this, &SStatsView::ContextMenu_CopySelectedToClipboard_CanExecute));
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
						GetToggleButtonForDataType(EStatsNodeDataType::Int64)
					]

					+ SHorizontalBox::Slot()
					.Padding(FMargin(1.0f,0.0f,1.0f,0.0f))
					.FillWidth(1.0f)
					[
						GetToggleButtonForDataType(EStatsNodeDataType::Double)
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
					SNew(SOverlay)

					+ SOverlay::Slot()
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Fill)
					[
					//SNew(SBorder)
					//.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
					//.Padding(0.0f)
					//[
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
					//]
					]

					+ SOverlay::Slot()
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Bottom)
					.Padding(16.0f)
					[
						SAssignNew(AggregatorStatus, Insights::SAggregatorStatus, Aggregator)
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
	CreateSortings();

	InitCommandList();

	// Register ourselves with the Insights manager.
	FInsightsManager::Get()->GetSessionChangedEvent().AddSP(this, &SStatsView::InsightsManager_OnSessionChanged);

	// Update the Session (i.e. when analysis session was already started).
	InsightsManager_OnSessionChanged();
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<SWidget> SStatsView::TreeView_GetMenuContent()
{
	const TArray<FStatsNodePtr> SelectedNodes = TreeView->GetSelectedItems();
	const int32 NumSelectedNodes = SelectedNodes.Num();
	FStatsNodePtr SelectedNode = NumSelectedNodes ? SelectedNodes[0] : nullptr;

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
		FString ItemName = SelectedNode->GetName().ToString();
		const int32 MaxStringLen = 64;
		if (ItemName.Len() > MaxStringLen)
		{
			ItemName = ItemName.Left(MaxStringLen) + TEXT("...");
		}
		SelectionStr = FText::FromString(ItemName);
	}
	else
	{
		SelectionStr = LOCTEXT("MultipleSelection", "Multiple selection");
	}

	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, CommandList.ToSharedRef());

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
		MenuBuilder.AddMenuEntry
		(
			FStatsViewCommands::Get().Command_CopyToClipboard,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Profiler.Misc.CopyToClipboard")
		);

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

	for (const TSharedRef<Insights::FTableColumn>& ColumnRef : Table->GetColumns())
	{
		const Insights::FTableColumn& Column = *ColumnRef;

		if (Column.IsVisible() && Column.CanBeSorted())
		{
			FUIAction Action_SortByColumn
			(
				FExecuteAction::CreateSP(this, &SStatsView::ContextMenu_SortByColumn_Execute, Column.GetId()),
				FCanExecuteAction::CreateSP(this, &SStatsView::ContextMenu_SortByColumn_CanExecute, Column.GetId()),
				FIsActionChecked::CreateSP(this, &SStatsView::ContextMenu_SortByColumn_IsChecked, Column.GetId())
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

	for (const TSharedRef<Insights::FTableColumn>& ColumnRef : Table->GetColumns())
	{
		const Insights::FTableColumn& Column = *ColumnRef;

		FUIAction Action_ToggleColumn
		(
			FExecuteAction::CreateSP(this, &SStatsView::ToggleColumnVisibility, Column.GetId()),
			FCanExecuteAction::CreateSP(this, &SStatsView::CanToggleColumnVisibility, Column.GetId()),
			FIsActionChecked::CreateSP(this, &SStatsView::IsColumnVisible, Column.GetId())
		);
		MenuBuilder.AddMenuEntry
		(
			Column.GetTitleName() ,
			Column.GetDescription(),
			FSlateIcon(), Action_ToggleColumn, NAME_None, EUserInterfaceActionType::ToggleButton
		);
	}

	MenuBuilder.EndSection();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::InitializeAndShowHeaderColumns()
{
	// Create columns.
	TArray<TSharedRef<Insights::FTableColumn>> Columns;
	FStatsViewColumnFactory::CreateStatsViewColumns(Columns);
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

FText SStatsView::GetColumnHeaderText(const FName ColumnId) const
{
	const Insights::FTableColumn& Column = *Table->FindColumnChecked(ColumnId);
	return Column.GetShortName();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SStatsView::TreeViewHeaderRow_GenerateColumnMenu(const Insights::FTableColumn& Column)
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
				FExecuteAction::CreateSP(this, &SStatsView::HideColumn, Column.GetId()),
				FCanExecuteAction::CreateSP(this, &SStatsView::CanHideColumn, Column.GetId())
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
				FExecuteAction::CreateSP(this, &SStatsView::HeaderMenu_SortMode_Execute, Column.GetId(), EColumnSortMode::Ascending),
				FCanExecuteAction::CreateSP(this, &SStatsView::HeaderMenu_SortMode_CanExecute, Column.GetId(), EColumnSortMode::Ascending),
				FIsActionChecked::CreateSP(this, &SStatsView::HeaderMenu_SortMode_IsChecked, Column.GetId(), EColumnSortMode::Ascending)
			);
			MenuBuilder.AddMenuEntry
			(
				LOCTEXT("ContextMenu_Header_Misc_Sort_SortAscending", "Sort Ascending"),
				LOCTEXT("ContextMenu_Header_Misc_Sort_SortAscending_Desc", "Sorts ascending"),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "Profiler.Misc.SortAscending"), Action_SortAscending, NAME_None, EUserInterfaceActionType::RadioButton
			);

			FUIAction Action_SortDescending
			(
				FExecuteAction::CreateSP(this, &SStatsView::HeaderMenu_SortMode_Execute, Column.GetId(), EColumnSortMode::Descending),
				FCanExecuteAction::CreateSP(this, &SStatsView::HeaderMenu_SortMode_CanExecute, Column.GetId(), EColumnSortMode::Descending),
				FIsActionChecked::CreateSP(this, &SStatsView::HeaderMenu_SortMode_IsChecked, Column.GetId(), EColumnSortMode::Descending)
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
	FStopwatch Stopwatch;
	Stopwatch.Start();

	CreateGroups();

	Stopwatch.Update();
	const double Time1 = Stopwatch.GetAccumulatedTime();

	SortTreeNodes();

	Stopwatch.Update();
	const double Time2 = Stopwatch.GetAccumulatedTime();

	ApplyFiltering();

	Stopwatch.Stop();
	const double TotalTime = Stopwatch.GetAccumulatedTime();
	if (TotalTime > 0.1)
	{
		UE_LOG(TimingProfiler, Log, TEXT("[Counters] Tree view updated in %.3fs (%d counters) --> G:%.3fs + S:%.3fs + F:%.3fs"),
			TotalTime, StatsNodes.Num(), Time1, Time2 - Time1, TotalTime - Time2);
	}
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
									  && FilterByNodeType[static_cast<int>(NodePtr->GetType())]
									  && FilterByDataType[static_cast<int>(NodePtr->GetDataType())]
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

	// Update aggregations for groups.
	for (FStatsNodePtr& GroupPtr : FilteredGroupNodes)
	{
		FAggregatedStats& AggregatedStats = GroupPtr->GetAggregatedStats();

		GroupPtr->ResetAggregatedStats();

		AggregatedStats.DoubleStats.Min = std::numeric_limits<double>::max();
		AggregatedStats.DoubleStats.Max = std::numeric_limits<double>::lowest();
		constexpr double NotAvailableDoubleValue = std::numeric_limits<double>::quiet_NaN();
		AggregatedStats.DoubleStats.Average = NotAvailableDoubleValue;
		AggregatedStats.DoubleStats.Median = NotAvailableDoubleValue;
		AggregatedStats.DoubleStats.LowerQuartile = NotAvailableDoubleValue;
		AggregatedStats.DoubleStats.UpperQuartile = NotAvailableDoubleValue;

		AggregatedStats.Int64Stats.Min = std::numeric_limits<int64>::max();
		AggregatedStats.Int64Stats.Max = std::numeric_limits<int64>::lowest();
		constexpr int64 NotAvailableIntegerValue = 0;// std::numeric_limits<int64>::max();
		AggregatedStats.Int64Stats.Average = NotAvailableIntegerValue;
		AggregatedStats.Int64Stats.Median = NotAvailableIntegerValue;
		AggregatedStats.Int64Stats.LowerQuartile = NotAvailableIntegerValue;
		AggregatedStats.Int64Stats.UpperQuartile = NotAvailableIntegerValue;

		EStatsNodeDataType GroupDataType = EStatsNodeDataType::InvalidOrMax;

		const TArray<Insights::FBaseTreeNodePtr>& GroupChildren = GroupPtr->GetFilteredChildren();
		for (const Insights::FBaseTreeNodePtr& ChildPtr : GroupChildren)
		{
			const FStatsNodePtr& NodePtr = StaticCastSharedPtr<FStatsNode, Insights::FBaseTreeNode>(ChildPtr);
			const FAggregatedStats& NodeAggregatedStats = NodePtr->GetAggregatedStats();

			if (NodeAggregatedStats.Count > 0)
			{
				AggregatedStats.Count += NodeAggregatedStats.Count;

				AggregatedStats.DoubleStats.Sum += NodeAggregatedStats.DoubleStats.Sum;
				AggregatedStats.DoubleStats.Min = FMath::Min(AggregatedStats.DoubleStats.Min, NodeAggregatedStats.DoubleStats.Min);
				AggregatedStats.DoubleStats.Max = FMath::Max(AggregatedStats.DoubleStats.Max, NodeAggregatedStats.DoubleStats.Max);

				AggregatedStats.Int64Stats.Sum += NodeAggregatedStats.Int64Stats.Sum;
				AggregatedStats.Int64Stats.Min = FMath::Min(AggregatedStats.Int64Stats.Min, NodeAggregatedStats.Int64Stats.Min);
				AggregatedStats.Int64Stats.Max = FMath::Max(AggregatedStats.Int64Stats.Max, NodeAggregatedStats.Int64Stats.Max);
			}

			if (GroupDataType == EStatsNodeDataType::InvalidOrMax)
			{
				GroupDataType = NodePtr->GetDataType();
			}
			else
			{
				if (GroupDataType != NodePtr->GetDataType())
				{
					GroupDataType = EStatsNodeDataType::Undefined;
				}
			}
		}

		// If not all children have same type, reset aggregated stats for group.
		if (GroupDataType >= EStatsNodeDataType::Undefined)
		{
			AggregatedStats.DoubleStats.Sum = NotAvailableDoubleValue;
			AggregatedStats.DoubleStats.Min = NotAvailableDoubleValue;
			AggregatedStats.DoubleStats.Max = NotAvailableDoubleValue;

			AggregatedStats.Int64Stats.Sum = NotAvailableIntegerValue;
			AggregatedStats.Int64Stats.Min = NotAvailableIntegerValue;
			AggregatedStats.Int64Stats.Max = NotAvailableIntegerValue;
		}
		GroupPtr->SetDataType(GroupDataType);
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

TSharedRef<SWidget> SStatsView::GetToggleButtonForNodeType(const EStatsNodeType NodeType)
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
						.Image(StatsNodeTypeHelper::GetIcon(NodeType))
				]

			+ SHorizontalBox::Slot()
				.Padding(2.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
						.Text(StatsNodeTypeHelper::ToText(NodeType))
						.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Caption"))
				]
		];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SStatsView::GetToggleButtonForDataType(const EStatsNodeDataType DataType)
{
	return SNew(SCheckBox)
		.Style(FEditorStyle::Get(), "ToggleButtonCheckbox")
		.HAlign(HAlign_Center)
		.Padding(2.0f)
		.OnCheckStateChanged(this, &SStatsView::FilterByStatsDataType_OnCheckStateChanged, DataType)
		.IsChecked(this, &SStatsView::FilterByStatsDataType_IsChecked, DataType)
		.ToolTipText(StatsNodeDataTypeHelper::ToDescription(DataType))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
						.Image(StatsNodeDataTypeHelper::GetIcon(DataType))
				]

			+ SHorizontalBox::Slot()
				.Padding(2.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
						.Text(StatsNodeDataTypeHelper::ToText(DataType))
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

void SStatsView::FilterByStatsType_OnCheckStateChanged(ECheckBoxState NewRadioState, const EStatsNodeType InNodeType)
{
	FilterByNodeType[static_cast<int>(InNodeType)] = (NewRadioState == ECheckBoxState::Checked);
	ApplyFiltering();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

ECheckBoxState SStatsView::FilterByStatsType_IsChecked(const EStatsNodeType InNodeType) const
{
	return FilterByNodeType[static_cast<int>(InNodeType)] ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::FilterByStatsDataType_OnCheckStateChanged(ECheckBoxState NewRadioState, const EStatsNodeDataType InDataType)
{
	FilterByDataType[static_cast<int>(InDataType)] = (NewRadioState == ECheckBoxState::Checked);
	ApplyFiltering();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

ECheckBoxState SStatsView::FilterByStatsDataType_IsChecked(const EStatsNodeDataType InDataType) const
{
	return FilterByDataType[static_cast<int>(InDataType)] ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
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
		//TArray<FStatsNodePtr> SelectedItems = TreeView->GetSelectedItems();
		//if (SelectedItems.Num() == 1 && !SelectedItems[0]->IsGroup())
		//{
		//	FTimingProfilerManager::Get()->SetSelectedCounter(SelectedItems[0]->GetCounterId());
		//}
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

void SStatsView::TreeView_OnMouseButtonDoubleClick(FStatsNodePtr NodePtr)
{
	if (NodePtr->IsGroup())
	{
		const bool bIsGroupExpanded = TreeView->IsItemExpanded(NodePtr);
		TreeView->SetItemExpansion(NodePtr, !bIsGroupExpanded);
	}
	else
	{
		TSharedPtr<STimingProfilerWindow> Wnd = FTimingProfilerManager::Get()->GetProfilerWindow();
		TSharedPtr<STimingView> TimingView = Wnd.IsValid() ? Wnd->GetTimingView() : nullptr;
		if (TimingView.IsValid())
		{
			TSharedPtr<FTimingGraphTrack> GraphTrack = TimingView->GetMainTimingGraphTrack();
			if (GraphTrack.IsValid())
			{
				ToggleGraphSeries(GraphTrack.ToSharedRef(), NodePtr.ToSharedRef());
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::ToggleGraphSeries(TSharedRef<FTimingGraphTrack> GraphTrack, FStatsNodeRef NodePtr)
{
	const uint32 CounterId = NodePtr->GetCounterId();
	TSharedPtr<FTimingGraphSeries> Series = GraphTrack->GetStatsCounterSeries(CounterId);
	if (Series.IsValid())
	{
		GraphTrack->RemoveStatsCounterSeries(CounterId);
		GraphTrack->SetDirtyFlag();
		NodePtr->SetAddedToGraphFlag(false);
	}
	else
	{
		GraphTrack->Show();
		Series = GraphTrack->AddStatsCounterSeries(CounterId, NodePtr->GetColor());
		GraphTrack->SetDirtyFlag();
		NodePtr->SetAddedToGraphFlag(true);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Tree View's Table Row
////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<ITableRow> SStatsView::TreeView_OnGenerateRow(FStatsNodePtr NodePtr, const TSharedRef<STableViewBase>& OwnerTable)
{
	TSharedRef<ITableRow> TableRow =
		SNew(SStatsTableRow, OwnerTable)
		.OnShouldBeEnabled(this, &SStatsView::TableRow_ShouldBeEnabled)
		.OnIsColumnVisible(this, &SStatsView::IsColumnVisible)
		.OnSetHoveredCell(this, &SStatsView::TableRow_SetHoveredCell)
		.OnGetColumnOutlineHAlignmentDelegate(this, &SStatsView::TableRow_GetColumnOutlineHAlignment)
		.HighlightText(this, &SStatsView::TableRow_GetHighlightText)
		.HighlightedNodeName(this, &SStatsView::TableRow_GetHighlightedNodeName)
		.TablePtr(Table)
		.StatsNodePtr(NodePtr);

	return TableRow;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SStatsView::TableRow_ShouldBeEnabled(FStatsNodePtr NodePtr) const
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::TableRow_SetHoveredCell(TSharedPtr<Insights::FTable> InTablePtr, TSharedPtr<Insights::FTableColumn> InColumnPtr, FStatsNodePtr InNodePtr)
{
	HoveredColumnId = InColumnPtr ? InColumnPtr->GetId() : FName();

	const bool bIsAnyMenusVisible = FSlateApplication::Get().AnyMenusVisible();
	if (!HasMouseCapture() && !bIsAnyMenusVisible)
	{
		HoveredNodePtr = InNodePtr;
	}
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
// Grouping
////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::CreateGroups()
{
	if (GroupingMode == EStatsGroupingMode::Flat)
	{
		GroupNodes.Reset();

		const FName GroupName(TEXT("All"));
		FStatsNodePtr GroupPtr = MakeShared<FStatsNode>(GroupName);
		GroupNodes.Add(GroupPtr);

		for (const FStatsNodePtr& NodePtr : StatsNodes)
		{
			GroupPtr->AddChildAndSetGroupPtr(NodePtr);
		}
		TreeView->SetItemExpansion(GroupPtr, true);
	}
	// Creates groups based on stat metadata groups.
	else if (GroupingMode == EStatsGroupingMode::ByMetaGroupName)
	{
		TMap<FName, FStatsNodePtr> GroupNodeSet;
		for (const FStatsNodePtr& NodePtr : StatsNodes)
		{
			const FName GroupName = NodePtr->GetMetaGroupName();
			FStatsNodePtr GroupPtr = GroupNodeSet.FindRef(GroupName);
			if (!GroupPtr)
			{
				GroupPtr = GroupNodeSet.Add(GroupName, MakeShared<FStatsNode>(GroupName));
			}
			GroupPtr->AddChildAndSetGroupPtr(NodePtr);
			TreeView->SetItemExpansion(GroupPtr, true);
		}
		GroupNodeSet.KeySort([](const FName& A, const FName& B) { return A.Compare(B) < 0; }); // sort groups by name
		GroupNodeSet.GenerateValueArray(GroupNodes);
	}
	// Creates one group for each node type.
	else if (GroupingMode == EStatsGroupingMode::ByType)
	{
		TMap<EStatsNodeType, FStatsNodePtr> GroupNodeSet;
		for (const FStatsNodePtr& NodePtr : StatsNodes)
		{
			const EStatsNodeType NodeType = NodePtr->GetType();
			FStatsNodePtr GroupPtr = GroupNodeSet.FindRef(NodeType);
			if (!GroupPtr)
			{
				const FName GroupName = *StatsNodeTypeHelper::ToText(NodeType).ToString();
				GroupPtr = GroupNodeSet.Add(NodeType, MakeShared<FStatsNode>(GroupName));
			}
			GroupPtr->AddChildAndSetGroupPtr(NodePtr);
			TreeView->SetItemExpansion(GroupPtr, true);
		}
		GroupNodeSet.KeySort([](const EStatsNodeType& A, const EStatsNodeType& B) { return A < B; }); // sort groups by type
		GroupNodeSet.GenerateValueArray(GroupNodes);
	}
	// Creates one group for each data type.
	else if (GroupingMode == EStatsGroupingMode::ByDataType)
	{
		TMap<EStatsNodeDataType, FStatsNodePtr> GroupNodeSet;
		for (const FStatsNodePtr& NodePtr : StatsNodes)
		{
			const EStatsNodeDataType DataType = NodePtr->GetDataType();
			FStatsNodePtr GroupPtr = GroupNodeSet.FindRef(DataType);
			if (!GroupPtr)
			{
				const FName GroupName = *StatsNodeDataTypeHelper::ToText(DataType).ToString();
				GroupPtr = GroupNodeSet.Add(DataType, MakeShared<FStatsNode>(GroupName));
			}
			GroupPtr->AddChildAndSetGroupPtr(NodePtr);
			TreeView->SetItemExpansion(GroupPtr, true);
		}
		GroupNodeSet.KeySort([](const EStatsNodeDataType& A, const EStatsNodeDataType& B) { return A < B; }); // sort groups by data type
		GroupNodeSet.GenerateValueArray(GroupNodes);
	}
	// Creates one group for one letter.
	else if (GroupingMode == EStatsGroupingMode::ByName)
	{
		TMap<TCHAR, FStatsNodePtr> GroupNodeSet;
		for (const FStatsNodePtr& NodePtr : StatsNodes)
		{
			FString FirstLetterStr(NodePtr->GetName().GetPlainNameString().Left(1).ToUpper());
			const TCHAR FirstLetter = FirstLetterStr[0];
			FStatsNodePtr GroupPtr = GroupNodeSet.FindRef(FirstLetter);
			if (!GroupPtr)
			{
				const FName GroupName(FirstLetterStr);
				GroupPtr = GroupNodeSet.Add(FirstLetter, MakeShared<FStatsNode>(GroupName));
			}
			GroupPtr->AddChildAndSetGroupPtr(NodePtr);
		}
		GroupNodeSet.KeySort([](const TCHAR& A, const TCHAR& B) { return A < B; }); // sort groups alphabetically
		GroupNodeSet.GenerateValueArray(GroupNodes);
	}
	// Creates one group for each logarithmic range ie. 0, [1 .. 10), [10 .. 100), [100 .. 1K), etc.
	else if (GroupingMode == EStatsGroupingMode::ByCount)
	{
		const TCHAR* Orders[] =
		{
			TEXT("1"), TEXT("10"), TEXT("100"),
			TEXT("1K"), TEXT("10K"), TEXT("100K"),
			TEXT("1M"), TEXT("10M"), TEXT("100M"),
			TEXT("1G"), TEXT("10G"), TEXT("100G"),
			TEXT("1T")
		};
		const uint32 MaxOrder = UE_ARRAY_COUNT(Orders);
		TMap<uint32, FStatsNodePtr> GroupNodeSet;
		for (const FStatsNodePtr& NodePtr : StatsNodes)
		{
			uint64 InstanceCount = NodePtr->GetAggregatedStats().Count;
			uint32 Order = 0;
			while (InstanceCount)
			{
				InstanceCount /= 10;
				Order++;
			}
			FStatsNodePtr GroupPtr = GroupNodeSet.FindRef(Order);
			if (!GroupPtr)
			{
				const FName GroupName =
					(Order == 0) ? FName(TEXT("Count == 0")) :
					(Order < MaxOrder) ? FName(FString::Printf(TEXT("Count: [%s .. %s)"), Orders[Order - 1], Orders[Order])) :
					FName(FString::Printf(TEXT("Count >= %s"), Orders[MaxOrder - 1]));
				GroupPtr = GroupNodeSet.Add(Order, MakeShared<FStatsNode>(GroupName));
			}
			GroupPtr->AddChildAndSetGroupPtr(NodePtr);
		}
		GroupNodeSet.KeySort([](const uint32& A, const uint32& B) { return A > B; }); // sort groups by order
		GroupNodeSet.GenerateValueArray(GroupNodes);
	}
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
	GroupByOptionsSource.Add(MakeShared<EStatsGroupingMode>(EStatsGroupingMode::ByDataType));
	GroupByOptionsSource.Add(MakeShared<EStatsGroupingMode>(EStatsGroupingMode::ByCount));

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
		SortTreeNodes();
		ApplyFiltering();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SStatsView::GroupBy_OnGenerateWidget(TSharedPtr<EStatsGroupingMode> InGroupingMode) const
{
	return SNew(STextBlock)
		.Text(StatsNodeGroupingHelper::ToText(*InGroupingMode))
		.ToolTipText(StatsNodeGroupingHelper::ToDescription(*InGroupingMode));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SStatsView::GroupBy_GetSelectedText() const
{
	return StatsNodeGroupingHelper::ToText(GroupingMode);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SStatsView::GroupBy_GetSelectedTooltipText() const
{
	return StatsNodeGroupingHelper::ToDescription(GroupingMode);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Sorting
////////////////////////////////////////////////////////////////////////////////////////////////////

const FName SStatsView::GetDefaultColumnBeingSorted()
{
	return FStatsViewColumns::NameColumnID;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const EColumnSortMode::Type SStatsView::GetDefaultColumnSortMode()
{
	return EColumnSortMode::Ascending;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::CreateSortings()
{
	AvailableSorters.Reset();
	CurrentSorter = nullptr;

	for (const TSharedRef<Insights::FTableColumn>& ColumnRef : Table->GetColumns())
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

void SStatsView::UpdateCurrentSortingByColumn()
{
	TSharedPtr<Insights::FTableColumn> ColumnPtr = Table->FindColumn(ColumnBeingSorted);
	CurrentSorter = ColumnPtr.IsValid() ? ColumnPtr->GetValueSorter() : nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::SortTreeNodes()
{
	if (CurrentSorter.IsValid())
	{
		for (FStatsNodePtr& Root : GroupNodes)
		{
			SortTreeNodesRec(*Root, *CurrentSorter);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::SortTreeNodesRec(FStatsNode& Node, const Insights::ITableCellValueSorter& Sorter)
{
	if (ColumnSortMode == EColumnSortMode::Type::Descending)
	{
		Node.SortChildrenDescending(Sorter);
	}
	else // if (ColumnSortMode == EColumnSortMode::Type::Ascending)
	{
		Node.SortChildrenAscending(Sorter);
	}

	//for (Insights::FBaseTreeNodePtr ChildPtr : Node.GetChildren())
	//{
	//	//if (ChildPtr->IsGroup())
	//	if (ChildPtr->GetChildren().Num() > 0)
	//	{
	//		SortTreeNodesRec(*StaticCastSharedPtr<FStatsNode>(ChildPtr), Sorter);
	//	}
	//}
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
	UpdateCurrentSortingByColumn();

	SortTreeNodes();
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
	const Insights::FTableColumn& Column = *Table->FindColumnChecked(ColumnId);
	return Column.CanBeSorted();
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
// ShowColumn action
////////////////////////////////////////////////////////////////////////////////////////////////////

bool SStatsView::CanShowColumn(const FName ColumnId) const
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::ShowColumn(const FName ColumnId)
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
		.SortMode(this, &SStatsView::GetSortModeForColumn, Column.GetId())
		.OnSort(this, &SStatsView::OnSortModeChanged)
		.ManualWidth(Column.GetInitialWidth())
		.FixedWidth(Column.IsFixedWidth() ? Column.GetInitialWidth() : TOptional<float>())
		.HeaderContent()
		[
			SNew(SBox)
			.ToolTip(SStatsViewTooltip::GetColumnTooltip(Column))
			.HAlign(Column.GetHorizontalAlignment())
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SStatsView::GetColumnHeaderText, Column.GetId())
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

bool SStatsView::CanHideColumn(const FName ColumnId) const
{
	const Insights::FTableColumn& Column = *Table->FindColumnChecked(ColumnId);
	return Column.CanBeHidden();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::HideColumn(const FName ColumnId)
{
	Insights::FTableColumn& Column = *Table->FindColumnChecked(ColumnId);
	Column.Hide();

	TreeViewHeaderRow->RemoveColumn(ColumnId);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// ToggleColumn action
////////////////////////////////////////////////////////////////////////////////////////////////////

bool SStatsView::IsColumnVisible(const FName ColumnId) const
{
	const Insights::FTableColumn& Column = *Table->FindColumnChecked(ColumnId);
	return Column.IsVisible();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SStatsView::CanToggleColumnVisibility(const FName ColumnId) const
{
	const Insights::FTableColumn& Column = *Table->FindColumnChecked(ColumnId);
	return !Column.IsVisible() || Column.CanBeHidden();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::ToggleColumnVisibility(const FName ColumnId)
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

bool SStatsView::ContextMenu_ShowAllColumns_CanExecute() const
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::ContextMenu_ShowAllColumns_Execute()
{
	ColumnBeingSorted = GetDefaultColumnBeingSorted();
	ColumnSortMode = GetDefaultColumnSortMode();
	UpdateCurrentSortingByColumn();

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

	ColumnBeingSorted = FStatsViewColumns::CountColumnID;
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

bool SStatsView::ContextMenu_ResetColumns_CanExecute() const
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::ContextMenu_ResetColumns_Execute()
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

void SStatsView::Reset()
{
	Aggregator->Cancel();
	Aggregator->SetTimeInterval(0.0, 0.0);

	RebuildTree(true);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// Check if we need to update the list of counters, but not too often.
	static uint64 NextTimestamp = 0;
	const uint64 Time = FPlatformTime::Cycles64();
	if (Time > NextTimestamp)
	{
		RebuildTree(false);

		// 1000 counters --> check each 150ms
		// 10000 counters --> check each 600ms
		// 100000 counters --> check each 5.1s
		const double WaitTimeSec = 0.1 + StatsNodes.Num() / 20000.0;
		const uint64 WaitTime = static_cast<uint64>(WaitTimeSec / FPlatformTime::GetSecondsPerCycle64());
		NextTimestamp = Time + WaitTime;
	}

	Aggregator->Tick(Session, InCurrentTime, InDeltaTime, [this]() { FinishAggregation(); });
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::RebuildTree(bool bResync)
{
	FStopwatch SyncStopwatch;
	FStopwatch Stopwatch;
	Stopwatch.Start();

	if (bResync)
	{
		StatsNodes.Empty();
		StatsNodesIdMap.Empty();
	}

	const uint32 PreviousNodeCount = StatsNodes.Num();

	SyncStopwatch.Start();
	if (Session.IsValid())
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

		const Trace::ICounterProvider& CountersProvider = Trace::ReadCounterProvider(*Session.Get());

		const uint32 CounterCount = CountersProvider.GetCounterCount();
		if (CounterCount != PreviousNodeCount)
		{
			check(CounterCount > PreviousNodeCount);

			StatsNodes.Reserve(CounterCount);
			StatsNodesIdMap.Reserve(CounterCount);

			const FName MemoryGroup(TEXT("Memory"));
			const FName MiscFloatGroup(TEXT("Misc_double"));
			const FName MiscInt64Group(TEXT("Misc_int64"));

			// Add nodes only for new counters.
			CountersProvider.EnumerateCounters([this, MemoryGroup, MiscFloatGroup, MiscInt64Group](uint32 CounterId, const Trace::ICounter& Counter)
			{
				FStatsNodePtr NodePtr = StatsNodesIdMap.FindRef(CounterId);
				if (!NodePtr)
				{
					FName Name(Counter.GetName());
					const FName Group = ((Counter.GetDisplayHint() == Trace::CounterDisplayHint_Memory) ? MemoryGroup :
										  Counter.IsFloatingPoint() ? MiscFloatGroup : MiscInt64Group);
					const EStatsNodeType Type = EStatsNodeType::Counter;
					const EStatsNodeDataType DataType = Counter.IsFloatingPoint() ? EStatsNodeDataType::Double : EStatsNodeDataType::Int64;
					NodePtr = MakeShared<FStatsNode>(CounterId, Name, Group, Type, DataType);
					UpdateNode(NodePtr);
					StatsNodes.Add(NodePtr);
					StatsNodesIdMap.Add(CounterId, NodePtr);
				}
			});
			ensure(StatsNodes.Num() == CounterCount);
		}
	}
	SyncStopwatch.Stop();

	if (bResync || StatsNodes.Num() != PreviousNodeCount)
	{
		// Disable sorting if too many items.
		if (StatsNodes.Num() > 10000)
		{
			ColumnBeingSorted = NAME_None;
			ColumnSortMode = GetDefaultColumnSortMode();
			UpdateCurrentSortingByColumn();
		}

		UpdateTree();
		Aggregator->Cancel();
		Aggregator->Start();

		// Save selection.
		TArray<FStatsNodePtr> SelectedItems;
		TreeView->GetSelectedItems(SelectedItems);

		TreeView->RebuildList();

		// Restore selection.
		if (SelectedItems.Num() > 0)
		{
			TreeView->ClearSelection();
			for (FStatsNodePtr& NodePtr : SelectedItems)
			{
				NodePtr = GetCounterNode(NodePtr->GetCounterId());
			}
			SelectedItems.RemoveAll([](const FStatsNodePtr& NodePtr) { return !NodePtr.IsValid(); });
			if (SelectedItems.Num() > 0)
			{
				TreeView->SetItemSelection(SelectedItems, true);
				TreeView->RequestScrollIntoView(SelectedItems.Last());
			}
		}
	}

	Stopwatch.Stop();
	const double TotalTime = Stopwatch.GetAccumulatedTime();
	if (TotalTime > 0.01)
	{
		const double SyncTime = SyncStopwatch.GetAccumulatedTime();
		UE_LOG(TimingProfiler, Log, TEXT("[Counters] Tree view rebuilt in %.4fs (%.4fs + %.4fs) --> %d counters (%d added)"),
			TotalTime, SyncTime, TotalTime - SyncTime, StatsNodes.Num(), StatsNodes.Num() - PreviousNodeCount);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::UpdateNode(FStatsNodePtr NodePtr)
{
	bool bAddedToGraphFlag = false;

	if (!NodePtr->IsGroup())
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
					TSharedPtr<FTimingGraphSeries> Series = GraphTrack->GetStatsCounterSeries(NodePtr->GetCounterId());
					bAddedToGraphFlag = Series.IsValid();
				}
			}
		}
	}

	NodePtr->SetAddedToGraphFlag(bAddedToGraphFlag);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::ResetStats()
{
	Aggregator->Cancel();
	Aggregator->SetTimeInterval(0.0, 0.0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::UpdateStats(double StartTime, double EndTime)
{
	Aggregator->Cancel();
	Aggregator->SetTimeInterval(StartTime, EndTime);
	Aggregator->Start();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::FinishAggregation()
{
	for (const FStatsNodePtr& NodePtr : StatsNodes)
	{
		NodePtr->ResetAggregatedStats();
	}

	Aggregator->ApplyResultsTo(StatsNodesIdMap);
	Aggregator->ResetResults();

	// Invalidate all tree table rows.
	for (const FStatsNodePtr& NodePtr : StatsNodes)
	{
		TSharedPtr<ITableRow> TableRowPtr = TreeView->WidgetFromItem(NodePtr);
		if (TableRowPtr.IsValid())
		{
			TSharedPtr<SStatsTableRow> RowPtr = StaticCastSharedPtr<SStatsTableRow, ITableRow>(TableRowPtr);
			RowPtr->InvalidateContent();
		}
	}

	UpdateTree(); // grouping + sorting + filtering

	// Ensure the last selected item is visible.
	const TArray<FStatsNodePtr> SelectedNodes = TreeView->GetSelectedItems();
	if (SelectedNodes.Num() > 0)
	{
		TreeView->RequestScrollIntoView(SelectedNodes.Last());
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FStatsNodePtr SStatsView::GetCounterNode(uint32 CounterId) const
{
	return StatsNodesIdMap.FindRef(CounterId);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::SelectCounterNode(uint32 CounterId)
{
	FStatsNodePtr NodePtr = GetCounterNode(CounterId);
	if (NodePtr)
	{
		TreeView->SetSelection(NodePtr);
		TreeView->RequestScrollIntoView(NodePtr);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SStatsView::ContextMenu_CopySelectedToClipboard_CanExecute() const
{
	const TArray<FStatsNodePtr> SelectedNodes = TreeView->GetSelectedItems();

	return SelectedNodes.Num() > 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsView::ContextMenu_CopySelectedToClipboard_Execute()
{
	if (!Table->IsValid())
	{
		return;
	}

	TArray<Insights::FBaseTreeNodePtr> SelectedNodes;
	for (FStatsNodePtr TimerPtr : TreeView->GetSelectedItems())
	{
		SelectedNodes.Add(TimerPtr);
	}

	if (SelectedNodes.Num() == 0)
	{
		return;
	}

	FString ClipboardText;

	if (CurrentSorter.IsValid())
	{
		CurrentSorter->Sort(SelectedNodes, ColumnSortMode == EColumnSortMode::Ascending ? Insights::ESortMode::Ascending : Insights::ESortMode::Descending);
	}

	Table->GetVisibleColumnsData(SelectedNodes, ClipboardText);

	if (ClipboardText.Len() > 0)
	{
		FPlatformApplicationMisc::ClipboardCopy(*ClipboardText);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SStatsView::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	return CommandList->ProcessCommandBindings(InKeyEvent) == true ? FReply::Handled() : FReply::Unhandled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
