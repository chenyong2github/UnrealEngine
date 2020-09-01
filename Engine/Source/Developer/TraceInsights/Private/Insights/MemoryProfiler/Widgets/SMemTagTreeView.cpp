// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMemTagTreeView.h"

#include "DesktopPlatformModule.h"
#include "EditorStyleSet.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "SlateOptMacros.h"
#include "Templates/UniquePtr.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Views/STableViewBase.h"

// Insights
#include "Insights/InsightsStyle.h"
#include "Insights/MemoryProfiler/MemoryProfilerManager.h"
#include "Insights/MemoryProfiler/ViewModels/MemoryTag.h"
#include "Insights/MemoryProfiler/ViewModels/MemoryTracker.h"
#include "Insights/MemoryProfiler/ViewModels/MemorySharedState.h"
#include "Insights/MemoryProfiler/ViewModels/MemTagTreeViewColumnFactory.h"
#include "Insights/MemoryProfiler/Widgets/SMemTagTreeViewTooltip.h"
#include "Insights/MemoryProfiler/Widgets/SMemTagTreeViewTableRow.h"
#include "Insights/MemoryProfiler/Widgets/SMemoryProfilerWindow.h"
#include "Insights/Table/ViewModels/Table.h"
#include "Insights/Table/ViewModels/TableColumn.h"
#include "Insights/ViewModels/TimingGraphTrack.h"
#include "Insights/Widgets/STimingView.h"

#define LOCTEXT_NAMESPACE "SMemTagTreeView"

////////////////////////////////////////////////////////////////////////////////////////////////////

SMemTagTreeView::SMemTagTreeView()
	: ProfilerWindow()
	, Table(MakeShared<Insights::FTable>())
	, bExpansionSaved(false)
	, bFilterOutZeroCountMemTags(false)
	, bFilterByTracker(true)
	, GroupingMode(EMemTagNodeGroupingMode::Flat)
	, AvailableSorters()
	, CurrentSorter(nullptr)
	, ColumnBeingSorted(GetDefaultColumnBeingSorted())
	, ColumnSortMode(GetDefaultColumnSortMode())
	, StatsStartTime(0.0)
	, StatsEndTime(0.0)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

SMemTagTreeView::~SMemTagTreeView()
{
	// Remove ourselves from the Insights manager.
	if (FInsightsManager::Get().IsValid())
	{
		FInsightsManager::Get()->GetSessionChangedEvent().RemoveAll(this);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SMemTagTreeView::Construct(const FArguments& InArgs, TSharedPtr<SMemoryProfilerWindow> InProfilerWindow)
{
	check(InProfilerWindow.IsValid());
	ProfilerWindow = InProfilerWindow;

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
						.HintText(LOCTEXT("SearchBoxHint", "Search LLM tags or groups"))
						.OnTextChanged(this, &SMemTagTreeView::SearchBox_OnTextChanged)
						.IsEnabled(this, &SMemTagTreeView::SearchBox_IsEnabled)
						.ToolTipText(LOCTEXT("FilterSearchHint", "Type here to search LLM tags or groups"))
					]

					// Filter out LLM tags with zero instance count.
					//+ SHorizontalBox::Slot()
					//.VAlign(VAlign_Center)
					//.Padding(2.0f)
					//.AutoWidth()
					//[
					//	SNew(SCheckBox)
					//	.Style(FEditorStyle::Get(), "ToggleButtonCheckbox")
					//	.HAlign(HAlign_Center)
					//	.Padding(2.0f)
					//	.OnCheckStateChanged(this, &SMemTagTreeView::FilterOutZeroCountMemTags_OnCheckStateChanged)
					//	.IsChecked(this, &SMemTagTreeView::FilterOutZeroCountMemTags_IsChecked)
					//	.ToolTipText(LOCTEXT("FilterOutZeroCountMemTags_Tooltip", "Filter out the LLM tags having zero total instance count (aggregated stats)."))
					//	[
					//		SNew(STextBlock)
					//		.Text(LOCTEXT("FilterOutZeroCountMemTags_Button", " !0 "))
					//		.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Caption"))
					//	]
					//]

					// Filter out LLM tags by current tracker.
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.Padding(2.0f)
					.AutoWidth()
					[
						SNew(SCheckBox)
						.Style(FEditorStyle::Get(), "ToggleButtonCheckbox")
						.HAlign(HAlign_Center)
						.Padding(2.0f)
						.OnCheckStateChanged(this, &SMemTagTreeView::FilterByTracker_OnCheckStateChanged)
						.IsChecked(this, &SMemTagTreeView::FilterByTracker_IsChecked)
						.ToolTipText(LOCTEXT("FilterByTracker_Tooltip", "Filter the LLM tags to show only the ones used by the current tracker."))
						[
							SNew(STextBlock)
							.Text(LOCTEXT("FilterByTracker_Button", " T "))
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
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("TrackerText", "Tracker:"))
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SAssignNew(TrackerComboBox, SComboBox<TSharedPtr<Insights::FMemoryTracker>>)
						.ToolTipText(this, &SMemTagTreeView::Tracker_GetTooltipText)
						.OptionsSource(GetAvailableTrackers())
						.OnSelectionChanged(this, &SMemTagTreeView::Tracker_OnSelectionChanged)
						.OnGenerateWidget(this, &SMemTagTreeView::Tracker_OnGenerateWidget)
						[
							SNew(STextBlock)
							.Text(this, &SMemTagTreeView::Tracker_GetSelectedText)
						]
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(STextBlock)
						.MinDesiredWidth(60.0f)
						.Justification(ETextJustify::Right)
						.Text(LOCTEXT("GroupByText", "Group by:"))
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SAssignNew(GroupByComboBox, SComboBox<TSharedPtr<EMemTagNodeGroupingMode>>)
						.ToolTipText(this, &SMemTagTreeView::GroupBy_GetSelectedTooltipText)
						.OptionsSource(&GroupByOptionsSource)
						.OnSelectionChanged(this, &SMemTagTreeView::GroupBy_OnSelectionChanged)
						.OnGenerateWidget(this, &SMemTagTreeView::GroupBy_OnGenerateWidget)
						[
							SNew(STextBlock)
							.Text(this, &SMemTagTreeView::GroupBy_GetSelectedText)
						]
					]
				]

				// Tracks
				+ SVerticalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(2.0f)
				.AutoHeight()
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.Padding(FMargin(2.0f, 0.0f, 2.0f, 0.0f))
					.AutoWidth()
					[
						SNew(SButton)
						.ToolTipText(LOCTEXT("HideAll_ToolTip", "Remove all memory graph tracks."))
						.ContentPadding(FMargin(2.0f, 0.0f, 2.0f, 0.0f))
						.OnClicked(this, &SMemTagTreeView::HideAllTracks_OnClicked)
						.Content()
						[
							SNew(SImage)
							.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.0f, 0.0f, 1.0f)))
							.Image(FInsightsStyle::Get().GetBrush("Mem.Remove.Small"))
						]
					]

					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.Padding(FMargin(2.0f, 0.0f, 2.0f, 0.0f))
					.AutoWidth()
					[
						SNew(SButton)
						.ToolTipText(LOCTEXT("ShowAll_ToolTip", "Create memory graph tracks for all visible (filtered) LLM tags."))
						.ContentPadding(FMargin(2.0f, 0.0f, 2.0f, 0.0f))
						.OnClicked(this, &SMemTagTreeView::ShowAllTracks_OnClicked)
						.Content()
						[
							SNew(SImage)
							.ColorAndOpacity(FSlateColor(FLinearColor(0.0f, 0.5f, 0.0f, 1.0f)))
							.Image(FInsightsStyle::Get().GetBrush("Mem.Add.Small"))
						]
					]

					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.Padding(2.0f)
					.AutoWidth()
					[
						SNew(SButton)
						.ToolTipText(LOCTEXT("LoadReportXML_ToolTip", "Load LLMReportTypes.xml"))
						.ContentPadding(FMargin(2.0f, 2.0f, 2.0f, 2.0f))
						.OnClicked(this, &SMemTagTreeView::LoadReportXML_OnClicked)
						.Content()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("LoadReportXML_Text", "Load XML..."))
						]
					]

					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.Padding(2.0f)
					[
						SNew(SButton)
						.ToolTipText(LOCTEXT("SmallHeight_ToolTip", "Change height of LLM Tag Graph tracks to Small."))
						.ContentPadding(FMargin(2.0f, 2.0f, 2.0f, 2.0f))
						.OnClicked(this, &SMemTagTreeView::AllTracksSmallHeight_OnClicked)
						.Content()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("SmallHeight_Text", "\u2195S"))
						]
					]

					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.Padding(2.0f)
					[
						SNew(SButton)
						.ToolTipText(LOCTEXT("MediumHeight_ToolTip", "Change height of LLM Tag Graph tracks to Medium."))
						.ContentPadding(FMargin(2.0f, 2.0f, 2.0f, 2.0f))
						.OnClicked(this, &SMemTagTreeView::AllTracksMediumHeight_OnClicked)
						.Content()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("MediumHeight_Text", "\u2195M"))
						]
					]

					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.Padding(2.0f)
					[
						SNew(SButton)
						.ToolTipText(LOCTEXT("LargeHeight_ToolTip", "Change height of LLM Tag Graph tracks to Large."))
						.ContentPadding(FMargin(2.0f, 2.0f, 2.0f, 2.0f))
						.OnClicked(this, &SMemTagTreeView::AllTracksLargeHeight_OnClicked)
						.Content()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("LargeHeight_Text", "\u2195L"))
						]
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
						SAssignNew(TreeView, STreeView<FMemTagNodePtr>)
						.ExternalScrollbar(ExternalScrollbar)
						.SelectionMode(ESelectionMode::Multi)
						.TreeItemsSource(&FilteredGroupNodes)
						.OnGetChildren(this, &SMemTagTreeView::TreeView_OnGetChildren)
						.OnGenerateRow(this, &SMemTagTreeView::TreeView_OnGenerateRow)
						.OnSelectionChanged(this, &SMemTagTreeView::TreeView_OnSelectionChanged)
						.OnMouseButtonDoubleClick(this, &SMemTagTreeView::TreeView_OnMouseButtonDoubleClick)
						.OnContextMenuOpening(FOnContextMenuOpening::CreateSP(this, &SMemTagTreeView::TreeView_GetMenuContent))
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
	TextFilter = MakeShared<FMemTagNodeTextFilter>(FMemTagNodeTextFilter::FItemToStringArray::CreateSP(this, &SMemTagTreeView::HandleItemToStringArray));
	Filters = MakeShared<FMemTagNodeFilterCollection>();
	Filters->Add(TextFilter);

	CreateGroupByOptionsSources();
	CreateSortings();

	// Register ourselves with the Insights manager.
	FInsightsManager::Get()->GetSessionChangedEvent().AddSP(this, &SMemTagTreeView::InsightsManager_OnSessionChanged);

	// Update the Session (i.e. when analysis session was already started).
	InsightsManager_OnSessionChanged();
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<SWidget> SMemTagTreeView::TreeView_GetMenuContent()
{
	const TArray<FMemTagNodePtr> SelectedNodes = TreeView->GetSelectedItems();
	const int32 NumSelectedNodes = SelectedNodes.Num();
	FMemTagNodePtr SelectedNode = NumSelectedNodes ? SelectedNodes[0] : nullptr;

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
			FExecuteAction::CreateSP(this, &SMemTagTreeView::ContextMenu_CopySelectedToClipboard_Execute),
			FCanExecuteAction::CreateSP(this, &SMemTagTreeView::ContextMenu_CopySelectedToClipboard_CanExecute)
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
			LOCTEXT("ContextMenu_Misc_CreateGraphTracks", "Create Graph Tracks"),
			LOCTEXT("ContextMenu_Misc_CreateGraphTracks_Desc", "Create memory graph tracks."),
			FNewMenuDelegate::CreateSP(this, &SMemTagTreeView::TreeView_BuildCreateGraphTracksMenu),
			false,
			FSlateIcon()
		);

		MenuBuilder.AddSubMenu
		(
			LOCTEXT("ContextMenu_Misc_RemoveGraphTracks", "Remove Graph Tracks"),
			LOCTEXT("ContextMenu_Misc_RemoveGraphTracks_Desc", "Remove memory graph tracks."),
			FNewMenuDelegate::CreateSP(this, &SMemTagTreeView::TreeView_BuildRemoveGraphTracksMenu),
			false,
			FSlateIcon()
		);

		FUIAction Action_GenerateColorForSelectedMemTags
		(
			FExecuteAction::CreateSP(this, &SMemTagTreeView::GenerateColorForSelectedMemTags),
			FCanExecuteAction::CreateSP(this, &SMemTagTreeView::CanGenerateColorForSelectedMemTags)
		);
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_Misc_GenerateColorForSelectedMemTags", "Generate New Color"),
			LOCTEXT("ContextMenu_Misc_GenerateColorForSelectedMemTags_Desc", "Generate new color for selected LLM tag(s)."),
			FSlateIcon(), Action_GenerateColorForSelectedMemTags, NAME_None, EUserInterfaceActionType::Button
		);

		FUIAction Action_EditColorForSelectedMemTags
		(
			FExecuteAction::CreateSP(this, &SMemTagTreeView::EditColorForSelectedMemTags),
			FCanExecuteAction::CreateSP(this, &SMemTagTreeView::CanEditColorForSelectedMemTags)
		);
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_Misc_EditColorForSelectedMemTags", "Edit Color..."),
			LOCTEXT("ContextMenu_Misc_EditColorForSelectedMemTags_Desc", "Edit color for selected LLM tag(s)."),
			FSlateIcon(), Action_EditColorForSelectedMemTags, NAME_None, EUserInterfaceActionType::Button
		);

		MenuBuilder.AddSubMenu
		(
			LOCTEXT("ContextMenu_Header_Misc_Sort", "Sort By"),
			LOCTEXT("ContextMenu_Header_Misc_Sort_Desc", "Sort by column"),
			FNewMenuDelegate::CreateSP(this, &SMemTagTreeView::TreeView_BuildSortByMenu),
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
			FNewMenuDelegate::CreateSP(this, &SMemTagTreeView::TreeView_BuildViewColumnMenu),
			false,
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Profiler.EventGraph.ViewColumn")
		);

		FUIAction Action_ShowAllColumns
		(
			FExecuteAction::CreateSP(this, &SMemTagTreeView::ContextMenu_ShowAllColumns_Execute),
			FCanExecuteAction::CreateSP(this, &SMemTagTreeView::ContextMenu_ShowAllColumns_CanExecute)
		);
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_Header_Columns_ShowAllColumns", "Show All Columns"),
			LOCTEXT("ContextMenu_Header_Columns_ShowAllColumns_Desc", "Resets tree view to show all columns"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Profiler.EventGraph.ResetColumn"), Action_ShowAllColumns, NAME_None, EUserInterfaceActionType::Button
		);

		//FUIAction Action_ShowMinMaxMedColumns
		//(
		//	FExecuteAction::CreateSP(this, &SMemTagTreeView::ContextMenu_ShowMinMaxMedColumns_Execute),
		//	FCanExecuteAction::CreateSP(this, &SMemTagTreeView::ContextMenu_ShowMinMaxMedColumns_CanExecute)
		//);
		//MenuBuilder.AddMenuEntry
		//(
		//	LOCTEXT("ContextMenu_Header_Columns_ShowMinMaxMedColumns", "Reset Columns to Min/Max/Median Preset"),
		//	LOCTEXT("ContextMenu_Header_Columns_ShowMinMaxMedColumns_Desc", "Resets columns to Min/Max/Median preset"),
		//	FSlateIcon(FEditorStyle::GetStyleSetName(), "Profiler.EventGraph.ResetColumn"), Action_ShowMinMaxMedColumns, NAME_None, EUserInterfaceActionType::Button
		//);

		FUIAction Action_ResetColumns
		(
			FExecuteAction::CreateSP(this, &SMemTagTreeView::ContextMenu_ResetColumns_Execute),
			FCanExecuteAction::CreateSP(this, &SMemTagTreeView::ContextMenu_ResetColumns_CanExecute)
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

void SMemTagTreeView::TreeView_BuildCreateGraphTracksMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection("CreateGraphTracks");
	{
		// Create memory graph tracks for selected LLM tag(s)
		FUIAction Action_CreateGraphTracksForSelectedMemTags
		(
			FExecuteAction::CreateSP(this, &SMemTagTreeView::CreateGraphTracksForSelectedMemTags),
			FCanExecuteAction::CreateSP(this, &SMemTagTreeView::CanCreateGraphTracksForSelectedMemTags)
		);
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_Misc_CreateGraphTracksForSelectedMemTags", "Selected"),
			LOCTEXT("ContextMenu_Misc_CreateGraphTracksForSelectedMemTags_Desc", "Create memory graph tracks for selected LLM tag(s)."),
			FSlateIcon(), Action_CreateGraphTracksForSelectedMemTags, NAME_None, EUserInterfaceActionType::Button
		);

		// Create memory graph tracks for filtered LLM tags
		FUIAction Action_CreateGraphTracksForFilteredMemTags
		(
			FExecuteAction::CreateSP(this, &SMemTagTreeView::CreateGraphTracksForFilteredMemTags),
			FCanExecuteAction::CreateSP(this, &SMemTagTreeView::CanCreateGraphTracksForFilteredMemTags)
		);
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_Misc_CreateGraphTracksForFilteredMemTags", "Filtered"),
			LOCTEXT("ContextMenu_Misc_CreateGraphTracksForFilteredMemTags_Desc", "Create memory graph tracks for all visible (filtered) LLM tags."),
			FSlateIcon(), Action_CreateGraphTracksForFilteredMemTags, NAME_None, EUserInterfaceActionType::Button
		);

		// Create all graph series
		FUIAction Action_CreateAllGraphTracks
		(
			FExecuteAction::CreateSP(this, &SMemTagTreeView::CreateAllGraphTracks),
			FCanExecuteAction::CreateSP(this, &SMemTagTreeView::CanCreateAllGraphTracks)
		);
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_Misc_CreateAllGraphTracks", "All"),
			LOCTEXT("ContextMenu_Misc_CreateAllGraphTracks_Desc", "Create memory graph tracks for all LLM tags"),
			FSlateIcon(), Action_CreateAllGraphTracks, NAME_None, EUserInterfaceActionType::Button
		);
	}
	MenuBuilder.EndSection();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::TreeView_BuildRemoveGraphTracksMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection("RemoveGraphTracks");
	{
		// Remove memory graph tracks for selected LLM tag(s)
		FUIAction Action_RemoveGraphTracksForSelectedMemTags
		(
			FExecuteAction::CreateSP(this, &SMemTagTreeView::RemoveGraphTracksForSelectedMemTags),
			FCanExecuteAction::CreateSP(this, &SMemTagTreeView::CanRemoveGraphTracksForSelectedMemTags)
		);
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_Misc_RemoveGraphTracksForSelectedMemTags", "Selected"),
			LOCTEXT("ContextMenu_Misc_RemoveGraphTracksForSelectedMemTags_Desc", "Remove memory graph tracks for selected LLM tag(s)"),
			FSlateIcon(), Action_RemoveGraphTracksForSelectedMemTags, NAME_None, EUserInterfaceActionType::Button
		);

		// Remove memory graph tracks for LLM tags not used by the current tracker
		FUIAction Action_RemoveGraphTracksForUnusedMemTags
		(
			FExecuteAction::CreateSP(this, &SMemTagTreeView::RemoveGraphTracksForUnusedMemTags),
			FCanExecuteAction::CreateSP(this, &SMemTagTreeView::CanRemoveGraphTracksForUnusedMemTags)
		);
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_Misc_RemoveGraphTracksForUnusedMemTags", "Unused"),
			LOCTEXT("ContextMenu_Misc_RemoveGraphTracksForUnusedMemTags_Desc", "Remove memory graph tracks for LLM tags not used by the current tracker"),
			FSlateIcon(), Action_RemoveGraphTracksForUnusedMemTags, NAME_None, EUserInterfaceActionType::Button
		);

		// Remove all graph series
		FUIAction Action_RemoveAllGraphTracks
		(
			FExecuteAction::CreateSP(this, &SMemTagTreeView::RemoveAllGraphTracks),
			FCanExecuteAction::CreateSP(this, &SMemTagTreeView::CanRemoveAllGraphTracks)
		);
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_Misc_RemoveAllGraphTracks", "All"),
			LOCTEXT("ContextMenu_Misc_RemoveAllGraphTracks_Desc", "Remove all memory graph tracks."),
			FSlateIcon(), Action_RemoveAllGraphTracks, NAME_None, EUserInterfaceActionType::Button
		);
	}
	MenuBuilder.EndSection();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::TreeView_BuildSortByMenu(FMenuBuilder& MenuBuilder)
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
				FExecuteAction::CreateSP(this, &SMemTagTreeView::ContextMenu_SortByColumn_Execute, Column.GetId()),
				FCanExecuteAction::CreateSP(this, &SMemTagTreeView::ContextMenu_SortByColumn_CanExecute, Column.GetId()),
				FIsActionChecked::CreateSP(this, &SMemTagTreeView::ContextMenu_SortByColumn_IsChecked, Column.GetId())
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
			FExecuteAction::CreateSP(this, &SMemTagTreeView::ContextMenu_SortMode_Execute, EColumnSortMode::Ascending),
			FCanExecuteAction::CreateSP(this, &SMemTagTreeView::ContextMenu_SortMode_CanExecute, EColumnSortMode::Ascending),
			FIsActionChecked::CreateSP(this, &SMemTagTreeView::ContextMenu_SortMode_IsChecked, EColumnSortMode::Ascending)
		);
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_Header_Misc_Sort_SortAscending", "Sort Ascending"),
			LOCTEXT("ContextMenu_Header_Misc_Sort_SortAscending_Desc", "Sorts ascending"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Profiler.Misc.SortAscending"), Action_SortAscending, NAME_None, EUserInterfaceActionType::RadioButton
		);

		FUIAction Action_SortDescending
		(
			FExecuteAction::CreateSP(this, &SMemTagTreeView::ContextMenu_SortMode_Execute, EColumnSortMode::Descending),
			FCanExecuteAction::CreateSP(this, &SMemTagTreeView::ContextMenu_SortMode_CanExecute, EColumnSortMode::Descending),
			FIsActionChecked::CreateSP(this, &SMemTagTreeView::ContextMenu_SortMode_IsChecked, EColumnSortMode::Descending)
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

void SMemTagTreeView::TreeView_BuildViewColumnMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection("ViewColumn", LOCTEXT("ContextMenu_Header_Columns_View", "View Column"));

	for (const TSharedRef<Insights::FTableColumn>& ColumnRef : Table->GetColumns())
	{
		const Insights::FTableColumn& Column = *ColumnRef;

		FUIAction Action_ToggleColumn
		(
			FExecuteAction::CreateSP(this, &SMemTagTreeView::ToggleColumnVisibility, Column.GetId()),
			FCanExecuteAction::CreateSP(this, &SMemTagTreeView::CanToggleColumnVisibility, Column.GetId()),
			FIsActionChecked::CreateSP(this, &SMemTagTreeView::IsColumnVisible, Column.GetId())
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

void SMemTagTreeView::InitializeAndShowHeaderColumns()
{
	// Create columns.
	TArray<TSharedRef<Insights::FTableColumn>> Columns;
	FMemTagTreeViewColumnFactory::CreateMemTagTreeViewColumns(Columns);
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

FText SMemTagTreeView::GetColumnHeaderText(const FName ColumnId) const
{
	const Insights::FTableColumn& Column = *Table->FindColumnChecked(ColumnId);
	return Column.GetShortName();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SMemTagTreeView::TreeViewHeaderRow_GenerateColumnMenu(const Insights::FTableColumn& Column)
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
				FExecuteAction::CreateSP(this, &SMemTagTreeView::HideColumn, Column.GetId()),
				FCanExecuteAction::CreateSP(this, &SMemTagTreeView::CanHideColumn, Column.GetId())
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
				FExecuteAction::CreateSP(this, &SMemTagTreeView::HeaderMenu_SortMode_Execute, Column.GetId(), EColumnSortMode::Ascending),
				FCanExecuteAction::CreateSP(this, &SMemTagTreeView::HeaderMenu_SortMode_CanExecute, Column.GetId(), EColumnSortMode::Ascending),
				FIsActionChecked::CreateSP(this, &SMemTagTreeView::HeaderMenu_SortMode_IsChecked, Column.GetId(), EColumnSortMode::Ascending)
			);
			MenuBuilder.AddMenuEntry
			(
				LOCTEXT("ContextMenu_Header_Misc_Sort_SortAscending", "Sort Ascending"),
				LOCTEXT("ContextMenu_Header_Misc_Sort_SortAscending_Desc", "Sorts ascending"),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "Profiler.Misc.SortAscending"), Action_SortAscending, NAME_None, EUserInterfaceActionType::RadioButton
			);

			FUIAction Action_SortDescending
			(
				FExecuteAction::CreateSP(this, &SMemTagTreeView::HeaderMenu_SortMode_Execute, Column.GetId(), EColumnSortMode::Descending),
				FCanExecuteAction::CreateSP(this, &SMemTagTreeView::HeaderMenu_SortMode_CanExecute, Column.GetId(), EColumnSortMode::Descending),
				FIsActionChecked::CreateSP(this, &SMemTagTreeView::HeaderMenu_SortMode_IsChecked, Column.GetId(), EColumnSortMode::Descending)
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

void SMemTagTreeView::InsightsManager_OnSessionChanged()
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

void SMemTagTreeView::UpdateTree()
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
		UE_LOG(MemoryProfiler, Log, TEXT("[LLM Tags] Tree view updated in %.3fs (%d counters) --> G:%.3fs + S:%.3fs + F:%.3fs"),
			TotalTime, MemTagNodes.Num(), Time1, Time2 - Time1, TotalTime - Time2);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::ApplyFiltering()
{
	FilteredGroupNodes.Reset();

	uint64 TrackerFilterMask = uint64(-1);
	if (ProfilerWindow.IsValid())
	{
		FMemorySharedState& SharedState = ProfilerWindow->GetSharedState();
		if (SharedState.GetCurrentTracker())
		{
			TrackerFilterMask = 1ULL << static_cast<int32>(SharedState.GetCurrentTracker()->GetId());
		}
	}

	// Apply filter to all groups and its children.
	const int32 NumGroups = GroupNodes.Num();
	for (int32 GroupIndex = 0; GroupIndex < NumGroups; ++GroupIndex)
	{
		FMemTagNodePtr& GroupPtr = GroupNodes[GroupIndex];
		GroupPtr->ClearFilteredChildren();
		const bool bIsGroupVisible = Filters->PassesAllFilters(GroupPtr);

		const TArray<Insights::FBaseTreeNodePtr>& GroupChildren = GroupPtr->GetChildren();
		const int32 NumChildren = GroupChildren.Num();
		int32 NumVisibleChildren = 0;
		for (int32 Cx = 0; Cx < NumChildren; ++Cx)
		{
			// Add a child.
			const FMemTagNodePtr& NodePtr = StaticCastSharedPtr<FMemTagNode, Insights::FBaseTreeNode>(GroupChildren[Cx]);
			const bool bIsChildVisible = (!bFilterOutZeroCountMemTags || NodePtr->GetAggregatedStats().InstanceCount > 0)
									  && (!bFilterByTracker || (NodePtr->GetTrackers() & TrackerFilterMask) != 0)
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

	// Only expand LLM tag nodes if we have a text filter.
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
			const FMemTagNodePtr& GroupPtr = FilteredGroupNodes[Fx];
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

void SMemTagTreeView::HandleItemToStringArray(const FMemTagNodePtr& FMemTagNodePtr, TArray<FString>& OutSearchStrings) const
{
	OutSearchStrings.Add(FMemTagNodePtr->GetName().GetPlainNameString());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::FilterOutZeroCountMemTags_OnCheckStateChanged(ECheckBoxState NewRadioState)
{
	bFilterOutZeroCountMemTags = (NewRadioState == ECheckBoxState::Checked);
	ApplyFiltering();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

ECheckBoxState SMemTagTreeView::FilterOutZeroCountMemTags_IsChecked() const
{
	return bFilterOutZeroCountMemTags ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::FilterByTracker_OnCheckStateChanged(ECheckBoxState NewRadioState)
{
	bFilterByTracker = (NewRadioState == ECheckBoxState::Checked);
	ApplyFiltering();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

ECheckBoxState SMemTagTreeView::FilterByTracker_IsChecked() const
{
	return bFilterByTracker ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// TreeView
////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::TreeView_Refresh()
{
	if (TreeView.IsValid())
	{
		TreeView->RequestTreeRefresh();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::TreeView_OnSelectionChanged(FMemTagNodePtr SelectedItem, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo != ESelectInfo::Direct)
	{
		TArray<FMemTagNodePtr> SelectedItems = TreeView->GetSelectedItems();
		if (SelectedItems.Num() == 1 && !SelectedItems[0]->IsGroup())
		{
			//TODO: FMemoryProfilerManager::Get()->SetSelectedMemTag(SelectedItems[0]->GetId());
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::TreeView_OnGetChildren(FMemTagNodePtr InParent, TArray<FMemTagNodePtr>& OutChildren)
{
	const TArray<Insights::FBaseTreeNodePtr>& Children = InParent->GetFilteredChildren();
	OutChildren.Reset(Children.Num());
	for (const Insights::FBaseTreeNodePtr& Child : Children)
	{
		OutChildren.Add(StaticCastSharedPtr<FMemTagNode, Insights::FBaseTreeNode>(Child));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::TreeView_OnMouseButtonDoubleClick(FMemTagNodePtr MemTagNodePtr)
{
	if (MemTagNodePtr->IsGroup())
	{
		const bool bIsGroupExpanded = TreeView->IsItemExpanded(MemTagNodePtr);
		TreeView->SetItemExpansion(MemTagNodePtr, !bIsGroupExpanded);
	}
	else
	{
		if (ProfilerWindow.IsValid())
		{
			FMemorySharedState& SharedState = ProfilerWindow->GetSharedState();
			const Insights::FMemoryTagId MemTagId = MemTagNodePtr->GetMemTagId();
			TSharedPtr<FMemoryGraphTrack> GraphTrack = SharedState.GetMemTagGraphTrack(MemTagId);
			if (!GraphTrack.IsValid())
			{
				SharedState.CreateMemTagGraphTrack(MemTagId);
			}
			else
			{
				SharedState.RemoveMemTagGraphTrack(MemTagId);
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Tree View's Table Row
////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<ITableRow> SMemTagTreeView::TreeView_OnGenerateRow(FMemTagNodePtr MemTagNodePtr, const TSharedRef<STableViewBase>& OwnerTable)
{
	TSharedRef<ITableRow> TableRow =
		SNew(SMemTagTreeViewTableRow, OwnerTable)
		.OnShouldBeEnabled(this, &SMemTagTreeView::TableRow_ShouldBeEnabled)
		.OnIsColumnVisible(this, &SMemTagTreeView::IsColumnVisible)
		.OnSetHoveredCell(this, &SMemTagTreeView::TableRow_SetHoveredCell)
		.OnGetColumnOutlineHAlignmentDelegate(this, &SMemTagTreeView::TableRow_GetColumnOutlineHAlignment)
		.HighlightText(this, &SMemTagTreeView::TableRow_GetHighlightText)
		.HighlightedNodeName(this, &SMemTagTreeView::TableRow_GetHighlightedNodeName)
		.TablePtr(Table)
		.MemTagNodePtr(MemTagNodePtr);

	return TableRow;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SMemTagTreeView::TableRow_ShouldBeEnabled(FMemTagNodePtr NodePtr) const
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::TableRow_SetHoveredCell(TSharedPtr<Insights::FTable> InTablePtr, TSharedPtr<Insights::FTableColumn> InColumnPtr, FMemTagNodePtr InNodePtr)
{
	HoveredColumnId = InColumnPtr ? InColumnPtr->GetId() : FName();

	const bool bIsAnyMenusVisible = FSlateApplication::Get().AnyMenusVisible();
	if (!HasMouseCapture() && !bIsAnyMenusVisible)
	{
		HoveredNodePtr = InNodePtr;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EHorizontalAlignment SMemTagTreeView::TableRow_GetColumnOutlineHAlignment(const FName ColumnId) const
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

FText SMemTagTreeView::TableRow_GetHighlightText() const
{
	return SearchBox->GetText();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FName SMemTagTreeView::TableRow_GetHighlightedNodeName() const
{
	return HighlightedNodeName;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// SearchBox
////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::SearchBox_OnTextChanged(const FText& InFilterText)
{
	TextFilter->SetRawFilterText(InFilterText);
	SearchBox->SetError(TextFilter->GetFilterErrorText());
	ApplyFiltering();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SMemTagTreeView::SearchBox_IsEnabled() const
{
	return MemTagNodes.Num() > 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Grouping
////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::CreateGroups()
{
	if (GroupingMode == EMemTagNodeGroupingMode::Flat)
	{
		GroupNodes.Reset();

		const FName GroupName(TEXT("All"));
		FMemTagNodePtr GroupPtr = MakeShared<FMemTagNode>(GroupName);
		GroupNodes.Add(GroupPtr);

		for (const FMemTagNodePtr& NodePtr : MemTagNodes)
		{
			GroupPtr->AddChildAndSetGroupPtr(NodePtr);
		}
		TreeView->SetItemExpansion(GroupPtr, true);
	}
	// Creates one group for each stat type.
	else if (GroupingMode == EMemTagNodeGroupingMode::ByType)
	{
		TMap<EMemTagNodeType, FMemTagNodePtr> GroupNodeSet;
		for (const FMemTagNodePtr& NodePtr : MemTagNodes)
		{
			const EMemTagNodeType NodeType = NodePtr->GetType();
			FMemTagNodePtr GroupPtr = GroupNodeSet.FindRef(NodeType);
			if (!GroupPtr)
			{
				const FName GroupName = *MemTagNodeTypeHelper::ToText(NodeType).ToString();
				GroupPtr = GroupNodeSet.Add(NodeType, MakeShared<FMemTagNode>(GroupName));
			}
			GroupPtr->AddChildAndSetGroupPtr(NodePtr);
			TreeView->SetItemExpansion(GroupPtr, true);
		}
		GroupNodeSet.KeySort([](const EMemTagNodeType& A, const EMemTagNodeType& B) { return A < B; }); // sort groups by type
		GroupNodeSet.GenerateValueArray(GroupNodes);
	}
	// Creates one group for one letter.
	else if (GroupingMode == EMemTagNodeGroupingMode::ByName)
	{
		TMap<TCHAR, FMemTagNodePtr> GroupNodeSet;
		for (const FMemTagNodePtr& NodePtr : MemTagNodes)
		{
			FString FirstLetterStr(NodePtr->GetName().GetPlainNameString().Left(1).ToUpper());
			const TCHAR FirstLetter = FirstLetterStr[0];
			FMemTagNodePtr GroupPtr = GroupNodeSet.FindRef(FirstLetter);
			if (!GroupPtr)
			{
				const FName GroupName(FirstLetterStr);
				GroupPtr = GroupNodeSet.Add(FirstLetter, MakeShared<FMemTagNode>(GroupName));
			}
			GroupPtr->AddChildAndSetGroupPtr(NodePtr);
		}
		GroupNodeSet.KeySort([](const TCHAR& A, const TCHAR& B) { return A < B; }); // sort groups alphabetically
		GroupNodeSet.GenerateValueArray(GroupNodes);
	}
	// Groups LLM tags by tracker.
	else if (GroupingMode == EMemTagNodeGroupingMode::ByTracker)
	{
		TMap<uint64, FMemTagNodePtr> GroupNodeSet;
		for (const FMemTagNodePtr& NodePtr : MemTagNodes)
		{
			const uint64 Tracker = NodePtr->GetTrackers();
			FMemTagNodePtr GroupPtr = GroupNodeSet.FindRef(Tracker);
			if (!GroupPtr)
			{
				const FName GroupName = *NodePtr->GetTrackerText().ToString();
				GroupPtr = GroupNodeSet.Add(Tracker, MakeShared<FMemTagNode>(GroupName));
			}
			GroupPtr->AddChildAndSetGroupPtr(NodePtr);
			TreeView->SetItemExpansion(GroupPtr, true);
		}
		GroupNodeSet.GenerateValueArray(GroupNodes);
	}
	// Groups LLM tags by their hierarchy.
	else if (GroupingMode == EMemTagNodeGroupingMode::ByParent)
	{
		TMap<FName, FMemTagNodePtr> GroupNodeSet;
		for (const FMemTagNodePtr& NodePtr : MemTagNodes)
		{
			const FName GroupName = NodePtr->GetParentTagNode() ? NodePtr->GetParentTagNode()->GetName() : FName(TEXT("<LLM>"));
			FMemTagNodePtr GroupPtr = GroupNodeSet.FindRef(GroupName);
			if (!GroupPtr)
			{
				GroupPtr = GroupNodeSet.Add(GroupName, MakeShared<FMemTagNode>(GroupName));
			}
			GroupPtr->AddChildAndSetGroupPtr(NodePtr);
			TreeView->SetItemExpansion(GroupPtr, true);
		}
		GroupNodeSet.GenerateValueArray(GroupNodes);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::CreateGroupByOptionsSources()
{
	GroupByOptionsSource.Reset(4);

	// Must be added in order of elements in the EMemTagNodeGroupingMode.
	GroupByOptionsSource.Add(MakeShared<EMemTagNodeGroupingMode>(EMemTagNodeGroupingMode::Flat));
	GroupByOptionsSource.Add(MakeShared<EMemTagNodeGroupingMode>(EMemTagNodeGroupingMode::ByName));
	//GroupByOptionsSource.Add(MakeShared<EMemTagNodeGroupingMode>(EMemTagNodeGroupingMode::ByType));
	GroupByOptionsSource.Add(MakeShared<EMemTagNodeGroupingMode>(EMemTagNodeGroupingMode::ByTracker));
	GroupByOptionsSource.Add(MakeShared<EMemTagNodeGroupingMode>(EMemTagNodeGroupingMode::ByParent));

	EMemTagNodeGroupingModePtr* GroupingModePtrPtr = GroupByOptionsSource.FindByPredicate([&](const EMemTagNodeGroupingModePtr InGroupingModePtr) { return *InGroupingModePtr == GroupingMode; });
	if (GroupingModePtrPtr != nullptr)
	{
		GroupByComboBox->SetSelectedItem(*GroupingModePtrPtr);
	}

	GroupByComboBox->RefreshOptions();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::GroupBy_OnSelectionChanged(TSharedPtr<EMemTagNodeGroupingMode> NewGroupingMode, ESelectInfo::Type SelectInfo)
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

TSharedRef<SWidget> SMemTagTreeView::GroupBy_OnGenerateWidget(TSharedPtr<EMemTagNodeGroupingMode> InGroupingMode) const
{
	return SNew(STextBlock)
		.Text(MemTagNodeGroupingHelper::ToText(*InGroupingMode))
		.ToolTipText(MemTagNodeGroupingHelper::ToDescription(*InGroupingMode));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SMemTagTreeView::GroupBy_GetSelectedText() const
{
	return MemTagNodeGroupingHelper::ToText(GroupingMode);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SMemTagTreeView::GroupBy_GetSelectedTooltipText() const
{
	return MemTagNodeGroupingHelper::ToDescription(GroupingMode);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Sorting
////////////////////////////////////////////////////////////////////////////////////////////////////

const FName SMemTagTreeView::GetDefaultColumnBeingSorted()
{
	return FMemTagTreeViewColumns::TrackerColumnID;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const EColumnSortMode::Type SMemTagTreeView::GetDefaultColumnSortMode()
{
	return EColumnSortMode::Ascending;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::CreateSortings()
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

void SMemTagTreeView::UpdateCurrentSortingByColumn()
{
	TSharedPtr<Insights::FTableColumn> ColumnPtr = Table->FindColumn(ColumnBeingSorted);
	CurrentSorter = ColumnPtr.IsValid() ? ColumnPtr->GetValueSorter() : nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::SortTreeNodes()
{
	if (CurrentSorter.IsValid())
	{
		// Sort groups (always by name).
		TArray<Insights::FBaseTreeNodePtr> SortedGroupNodes;
		for (const FMemTagNodePtr& NodePtr : GroupNodes)
		{
			SortedGroupNodes.Add(NodePtr);
		}
		TSharedPtr<Insights::ITableCellValueSorter> Sorter = CurrentSorter;
		Insights::ESortMode SortMode = (ColumnSortMode == EColumnSortMode::Type::Descending) ? Insights::ESortMode::Descending : Insights::ESortMode::Ascending;
		if (CurrentSorter->GetName() != FName(TEXT("ByName")))
		{
			Sorter = MakeShared<Insights::FSorterByName>(Table->GetColumns()[0]);
			SortMode = Insights::ESortMode::Ascending;
		}
		Sorter->Sort(SortedGroupNodes, SortMode);
		GroupNodes.Reset();
		for (const Insights::FBaseTreeNodePtr& NodePtr : SortedGroupNodes)
		{
			GroupNodes.Add(StaticCastSharedPtr<FMemTagNode>(NodePtr));
		}

		// Sort nodes in each group.
		for (FMemTagNodePtr& Root : GroupNodes)
		{
			SortTreeNodesRec(*Root, *CurrentSorter);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::SortTreeNodesRec(FMemTagNode& Node, const Insights::ITableCellValueSorter& Sorter)
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
			SortTreeNodesRec(*StaticCastSharedPtr<FMemTagNode>(ChildPtr), Sorter);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EColumnSortMode::Type SMemTagTreeView::GetSortModeForColumn(const FName ColumnId) const
{
	if (ColumnBeingSorted != ColumnId)
	{
		return EColumnSortMode::None;
	}

	return ColumnSortMode;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::SetSortModeForColumn(const FName& ColumnId, const EColumnSortMode::Type SortMode)
{
	ColumnBeingSorted = ColumnId;
	ColumnSortMode = SortMode;
	UpdateCurrentSortingByColumn();

	SortTreeNodes();
	ApplyFiltering();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::OnSortModeChanged(const EColumnSortPriority::Type SortPriority, const FName& ColumnId, const EColumnSortMode::Type SortMode)
{
	SetSortModeForColumn(ColumnId, SortMode);
	TreeView_Refresh();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// SortMode action (HeaderMenu)
////////////////////////////////////////////////////////////////////////////////////////////////////

bool SMemTagTreeView::HeaderMenu_SortMode_IsChecked(const FName ColumnId, const EColumnSortMode::Type InSortMode)
{
	return ColumnBeingSorted == ColumnId && ColumnSortMode == InSortMode;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SMemTagTreeView::HeaderMenu_SortMode_CanExecute(const FName ColumnId, const EColumnSortMode::Type InSortMode) const
{
	const Insights::FTableColumn& Column = *Table->FindColumnChecked(ColumnId);
	return Column.CanBeSorted();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::HeaderMenu_SortMode_Execute(const FName ColumnId, const EColumnSortMode::Type InSortMode)
{
	SetSortModeForColumn(ColumnId, InSortMode);
	TreeView_Refresh();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// SortMode action (ContextMenu)
////////////////////////////////////////////////////////////////////////////////////////////////////

bool SMemTagTreeView::ContextMenu_SortMode_IsChecked(const EColumnSortMode::Type InSortMode)
{
	return ColumnSortMode == InSortMode;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SMemTagTreeView::ContextMenu_SortMode_CanExecute(const EColumnSortMode::Type InSortMode) const
{
	return true; //ColumnSortMode != InSortMode;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::ContextMenu_SortMode_Execute(const EColumnSortMode::Type InSortMode)
{
	SetSortModeForColumn(ColumnBeingSorted, InSortMode);
	TreeView_Refresh();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// SortByColumn action (ContextMenu)
////////////////////////////////////////////////////////////////////////////////////////////////////

bool SMemTagTreeView::ContextMenu_SortByColumn_IsChecked(const FName ColumnId)
{
	return ColumnId == ColumnBeingSorted;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SMemTagTreeView::ContextMenu_SortByColumn_CanExecute(const FName ColumnId) const
{
	return true; //ColumnId != ColumnBeingSorted;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::ContextMenu_SortByColumn_Execute(const FName ColumnId)
{
	SetSortModeForColumn(ColumnId, EColumnSortMode::Descending);
	TreeView_Refresh();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// ShowColumn action
////////////////////////////////////////////////////////////////////////////////////////////////////

bool SMemTagTreeView::CanShowColumn(const FName ColumnId) const
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::ShowColumn(const FName ColumnId)
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
		.SortMode(this, &SMemTagTreeView::GetSortModeForColumn, Column.GetId())
		.OnSort(this, &SMemTagTreeView::OnSortModeChanged)
		.ManualWidth(Column.GetInitialWidth())
		.FixedWidth(Column.IsFixedWidth() ? Column.GetInitialWidth() : TOptional<float>())
		.HeaderContent()
		[
			SNew(SBox)
			.ToolTip(SMemTagTreeViewTooltip::GetColumnTooltip(Column))
			.HAlign(Column.GetHorizontalAlignment())
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SMemTagTreeView::GetColumnHeaderText, Column.GetId())
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

bool SMemTagTreeView::CanHideColumn(const FName ColumnId) const
{
	const Insights::FTableColumn& Column = *Table->FindColumnChecked(ColumnId);
	return Column.CanBeHidden();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::HideColumn(const FName ColumnId)
{
	Insights::FTableColumn& Column = *Table->FindColumnChecked(ColumnId);
	Column.Hide();

	TreeViewHeaderRow->RemoveColumn(ColumnId);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// ToggleColumn action
////////////////////////////////////////////////////////////////////////////////////////////////////

bool SMemTagTreeView::IsColumnVisible(const FName ColumnId)
{
	const Insights::FTableColumn& Column = *Table->FindColumnChecked(ColumnId);
	return Column.IsVisible();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SMemTagTreeView::CanToggleColumnVisibility(const FName ColumnId) const
{
	const Insights::FTableColumn& Column = *Table->FindColumnChecked(ColumnId);
	return !Column.IsVisible() || Column.CanBeHidden();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::ToggleColumnVisibility(const FName ColumnId)
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

bool SMemTagTreeView::ContextMenu_ShowAllColumns_CanExecute() const
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::ContextMenu_ShowAllColumns_Execute()
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

bool SMemTagTreeView::ContextMenu_ShowMinMaxMedColumns_CanExecute() const
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::ContextMenu_ShowMinMaxMedColumns_Execute()
{
	TSet<FName> Preset =
	{
		FMemTagTreeViewColumns::NameColumnID,
		//FMemTagTreeViewColumns::MetaGroupNameColumnID,
		//FMemTagTreeViewColumns::TypeColumnID,
		FMemTagTreeViewColumns::TrackerColumnID,
		FMemTagTreeViewColumns::InstanceCountColumnID,

		FMemTagTreeViewColumns::MaxValueColumnID,
		FMemTagTreeViewColumns::AverageValueColumnID,
		//FMemTagTreeViewColumns::MedianValueColumnID,
		FMemTagTreeViewColumns::MinValueColumnID,
	};

	ColumnBeingSorted = FMemTagTreeViewColumns::MaxValueColumnID;
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

bool SMemTagTreeView::ContextMenu_ResetColumns_CanExecute() const
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::ContextMenu_ResetColumns_Execute()
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

void SMemTagTreeView::Reset()
{
	StatsStartTime = 0.0;
	StatsEndTime = 0.0;

	RebuildTree(true);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// We need to check if the list of LLM tags has changed.
	// But, ensure we do not check too often.
	static uint64 NextTimestamp = 0;
	uint64 Time = FPlatformTime::Cycles64();
	if (Time > NextTimestamp)
	{
		RebuildTree(false);

		// 1000 counters --> check each 150ms
		// 10000 counters --> check each 600ms
		// 100000 counters --> check each 5.1s
		const double WaitTimeSec = 0.1 + MemTagNodes.Num() / 20000.0;
		const uint64 WaitTime = static_cast<uint64>(WaitTimeSec / FPlatformTime::GetSecondsPerCycle64());
		NextTimestamp = Time + WaitTime;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::RebuildTree(bool bResync)
{
	FStopwatch SyncStopwatch;
	FStopwatch Stopwatch;
	Stopwatch.Start();

	bool bListHasChanged = false;

	if (bResync)
	{
		LastMemoryTagListSerialNumber = 0;
		MemTagNodes.Empty();
		MemTagNodesIdMap.Empty();
		bListHasChanged = true;
	}

	const uint32 PreviousNodeCount = MemTagNodes.Num();

	SyncStopwatch.Start();
	if (ProfilerWindow.IsValid())
	{
		FMemorySharedState& SharedState = ProfilerWindow->GetSharedState();
		const Insights::FMemoryTagList& TagList = SharedState.GetTagList();

		if (LastMemoryTagListSerialNumber != TagList.GetSerialNumber())
		{
			LastMemoryTagListSerialNumber = TagList.GetSerialNumber();

			const TArray<Insights::FMemoryTag*>& MemTags = TagList.GetTags();
			const int32 MemTagCount = MemTags.Num();

			MemTagNodes.Empty(MemTagCount);
			MemTagNodesIdMap.Empty(MemTagCount);
			bListHasChanged = true;

			for (Insights::FMemoryTag* MemTagPtr : MemTags)
			{
				FMemTagNodePtr MemTagNodePtr = MakeShared<FMemTagNode>(MemTagPtr);
				MemTagNodes.Add(MemTagNodePtr);
				MemTagNodesIdMap.Add(MemTagPtr->GetId(), MemTagNodePtr);
			}

			// Resolve pointers to parent tags.
			for (FMemTagNodePtr& NodePtr : MemTagNodes)
			{
				check(NodePtr->GetMemTag() != nullptr);
				Insights::FMemoryTag& MemTag = *NodePtr->GetMemTag();

				FMemTagNodePtr ParentNodePtr = MemTagNodesIdMap.FindRef(MemTag.GetParentId());
				if (ParentNodePtr)
				{
					check(ParentNodePtr != NodePtr);
					NodePtr->SetParentTagNode(ParentNodePtr);
				}
			}
		}
	}
	SyncStopwatch.Stop();

	if (bListHasChanged)
	{
		UpdateTree();
		UpdateStatsInternal();

		// Save selection.
		TArray<FMemTagNodePtr> SelectedItems;
		TreeView->GetSelectedItems(SelectedItems);

		TreeView->RebuildList();

		// Restore selection.
		if (SelectedItems.Num() > 0)
		{
			TreeView->ClearSelection();
			for (FMemTagNodePtr& NodePtr : SelectedItems)
			{
				NodePtr = GetMemTagNode(NodePtr->GetMemTagId());
			}
			SelectedItems.RemoveAll([](const FMemTagNodePtr& NodePtr) { return !NodePtr.IsValid(); });
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
		UE_LOG(MemoryProfiler, Log, TEXT("[LLM Tags] Tree view rebuilt in %.3fs (%.3fs + %.3fs) --> %d LLM tags (%d added)"),
			TotalTime, SyncTime, TotalTime - SyncTime, MemTagNodes.Num(), MemTagNodes.Num() - PreviousNodeCount);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::ResetStats()
{
	StatsStartTime = 0.0;
	StatsEndTime = 0.0;

	UpdateStatsInternal();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::UpdateStats(double StartTime, double EndTime)
{
	StatsStartTime = StartTime;
	StatsEndTime = EndTime;

	UpdateStatsInternal();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::UpdateStatsInternal()
{
	if (StatsStartTime >= StatsEndTime)
	{
		// keep previous aggregated stats
		return;
	}

	FStopwatch AggregationStopwatch;
	FStopwatch Stopwatch;
	Stopwatch.Start();

	for (const FMemTagNodePtr& NodePtr : MemTagNodes)
	{
		NodePtr->ResetAggregatedStats();
	}

	/*
	if (Session.IsValid())
	{
		TUniquePtr<Trace::ITable<Trace::FMemoryProfilerAggregatedStats>> AggregationResultTable;

		AggregationStopwatch.Start();
		{
			Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
			const Trace::IMemoryProfilerProvider& MemoryProfilerProvider = Trace::ReadMemoryProfilerProvider(*Session.Get());
			AggregationResultTable.Reset(MemoryProfilerProvider.CreateAggregation(StatsStartTime, StatsEndTime));
		}
		AggregationStopwatch.Stop();

		if (AggregationResultTable.IsValid())
		{
			TUniquePtr<Trace::ITableReader<Trace::FMemoryProfilerAggregatedStats>> TableReader(AggregationResultTable->CreateReader());
			while (TableReader->IsValid())
			{
				const Trace::FMemoryProfilerAggregatedStats* Row = TableReader->GetCurrentRow();
				FMemTagNodePtr* MemTagNodePtrPtr = MemTagNodesIdMap.Find(static_cast<uint64>(Row->EventTypeIndex));
				if (MemTagNodePtrPtr != nullptr)
				{
					FMemTagNodePtr MemTagNodePtr = *MemTagNodePtrPtr;
					->SetAggregatedStats(*Row);

					TSharedPtr<ITableRow> TableRowPtr = TreeView->WidgetFromItem(MemTagNodePtr);
					if (TableRowPtr.IsValid())
					{
						TSharedPtr<SMemTagsTableRow> RowPtr = StaticCastSharedPtr<SMemTagsTableRow, ITableRow>(TableRowPtr);
						RowPtr->InvalidateContent();
					}
				}
				TableReader->NextRow();
			}
		}
	}
	*/

	UpdateTree();

	const TArray<FMemTagNodePtr> SelectedNodes = TreeView->GetSelectedItems();
	if (SelectedNodes.Num() > 0)
	{
		TreeView->RequestScrollIntoView(SelectedNodes[0]);
	}

	Stopwatch.Stop();
	const double TotalTime = Stopwatch.GetAccumulatedTime();
	const double AggregationTime = AggregationStopwatch.GetAccumulatedTime();
	UE_LOG(MemoryProfiler, Log, TEXT("[LLM Tags] Aggregated stats updated in %.4fs (%.4fs + %.4fs)"),
		TotalTime, AggregationTime, TotalTime - AggregationTime);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::SelectMemTagNode(Insights::FMemoryTagId MemTagId)
{
	FMemTagNodePtr NodePtr = GetMemTagNode(MemTagId);
	if (NodePtr)
	{
		TreeView->SetSelection(NodePtr);
		TreeView->RequestScrollIntoView(NodePtr);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TArray<TSharedPtr<Insights::FMemoryTracker>>* SMemTagTreeView::GetAvailableTrackers()
{
	if (ProfilerWindow.IsValid())
	{
		FMemorySharedState& SharedState = ProfilerWindow->GetSharedState();
		return &SharedState.GetTrackers();
	}
	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::Tracker_OnSelectionChanged(TSharedPtr<Insights::FMemoryTracker> InTracker, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo != ESelectInfo::Direct)
	{
		if (ProfilerWindow.IsValid())
		{
			FMemorySharedState& SharedState = ProfilerWindow->GetSharedState();
			SharedState.SetCurrentTracker(InTracker);
			if (bFilterByTracker)
			{
				ApplyFiltering();
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SMemTagTreeView::Tracker_OnGenerateWidget(TSharedPtr<Insights::FMemoryTracker> InTracker)
{
	const FText TrackerText = FText::Format(LOCTEXT("TrackerComboBox_TextFmt", "{0} Tracker (id {1})"), FText::FromString(InTracker->GetName()), FText::AsNumber(InTracker->GetId()));
	return SNew(SCheckBox)
		.Style(FEditorStyle::Get(), "Toolbar.RadioButton")
		.OnCheckStateChanged(this, &SMemTagTreeView::Tracker_OnCheckStateChanged, InTracker)
		.IsChecked(this, &SMemTagTreeView::Tracker_IsChecked, InTracker)
		.Content()
		[
			SNew(STextBlock)
			.Text(TrackerText)
			.Margin(2.0f)
		];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::Tracker_OnCheckStateChanged(ECheckBoxState CheckType, TSharedPtr<Insights::FMemoryTracker> InTracker)
{
	if (CheckType == ECheckBoxState::Checked)
	{
		if (ProfilerWindow.IsValid())
		{
			FMemorySharedState& SharedState = ProfilerWindow->GetSharedState();
			SharedState.SetCurrentTracker(InTracker);
			if (bFilterByTracker)
			{
				ApplyFiltering();
			}
		}
	}
	TrackerComboBox->SetIsOpen(false);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

ECheckBoxState SMemTagTreeView::Tracker_IsChecked(TSharedPtr<Insights::FMemoryTracker> InTracker) const
{
	TSharedPtr<Insights::FMemoryTracker> CurrentTracker = nullptr;
	if (ProfilerWindow.IsValid())
	{
		FMemorySharedState& SharedState = ProfilerWindow->GetSharedState();
		CurrentTracker = SharedState.GetCurrentTracker();
	}
	return (InTracker == CurrentTracker) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SMemTagTreeView::Tracker_GetSelectedText() const
{
	if (ProfilerWindow.IsValid())
	{
		FMemorySharedState& SharedState = ProfilerWindow->GetSharedState();
		if (SharedState.GetCurrentTracker())
		{
			return FText::FromString(SharedState.GetCurrentTracker()->GetName());
		}
	}
	return FText::GetEmpty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SMemTagTreeView::Tracker_GetTooltipText() const
{
	return LOCTEXT("TrackerComboBox_Tooltip", "Choose the current memory tracker.");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SMemTagTreeView::LoadReportXML_OnClicked()
{
	if (ProfilerWindow.IsValid())
	{
		TArray<FString> OutFiles;
		bool bOpened = false;

		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
		if (DesktopPlatform != nullptr)
		{
			FSlateApplication::Get().CloseToolTip();

			const FString DefaultPath(FPaths::RootDir() / TEXT("Engine/Binaries/DotNET/CsvTools"));
			const FString DefaultFile(TEXT("LLMReportTypes.xml"));

			bOpened = DesktopPlatform->OpenFileDialog
			(
				FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
				LOCTEXT("LoadReportXML_FileDesc", "Open the LLMReportTypes.xml file...").ToString(),
				DefaultPath,
				DefaultFile, // Not actually used. See FDesktopPlatformWindows::FileDialogShared implementation. :(
				LOCTEXT("LoadReportXML_FileFilter", "XML files (*.xml)|*.xml|All files (*.*)|*.*").ToString(),
				EFileDialogFlags::None,
				OutFiles
			);
		}

		if (bOpened == true && OutFiles.Num() == 1)
		{
			FMemorySharedState& SharedState = ProfilerWindow->GetSharedState();
			SharedState.RemoveAllMemoryGraphTracks();
			SharedState.CreateTracksFromReport(OutFiles[0]);
		}
	}

	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SMemTagTreeView::ShowAllTracks_OnClicked()
{
	CreateGraphTracksForFilteredMemTags();
	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SMemTagTreeView::HideAllTracks_OnClicked()
{
	RemoveAllGraphTracks();
	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SMemTagTreeView::AllTracksSmallHeight_OnClicked()
{
	if (ProfilerWindow.IsValid())
	{
		FMemorySharedState& SharedState = ProfilerWindow->GetSharedState();
		SharedState.SetTrackHeightMode(EMemoryTrackHeightMode::Small);
	}

	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SMemTagTreeView::AllTracksMediumHeight_OnClicked()
{
	if (ProfilerWindow.IsValid())
	{
		FMemorySharedState& SharedState = ProfilerWindow->GetSharedState();
		SharedState.SetTrackHeightMode(EMemoryTrackHeightMode::Medium);
	}

	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SMemTagTreeView::AllTracksLargeHeight_OnClicked()
{
	if (ProfilerWindow.IsValid())
	{
		FMemorySharedState& SharedState = ProfilerWindow->GetSharedState();
		SharedState.SetTrackHeightMode(EMemoryTrackHeightMode::Large);
	}

	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Create memory graph tracks for selected LLM tag(s)
////////////////////////////////////////////////////////////////////////////////////////////////////

bool SMemTagTreeView::CanCreateGraphTracksForSelectedMemTags() const
{
	return ProfilerWindow.IsValid() && TreeView->GetNumItemsSelected() > 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::CreateGraphTracksForSelectedMemTags()
{
	if (ProfilerWindow.IsValid())
	{
		FMemorySharedState& SharedState = ProfilerWindow->GetSharedState();
		const TArray<FMemTagNodePtr> SelectedNodes = TreeView->GetSelectedItems();
		for (const FMemTagNodePtr& SelectedMemTagNode : SelectedNodes)
		{
			if (SelectedMemTagNode->IsGroup())
			{
				const TArray<Insights::FBaseTreeNodePtr>& Children = SelectedMemTagNode->GetFilteredChildren();
				for (const Insights::FBaseTreeNodePtr& Child : Children)
				{
					FMemTagNodePtr MemTagNode = StaticCastSharedPtr<FMemTagNode, Insights::FBaseTreeNode>(Child);
					const Insights::FMemoryTagId MemTagId = MemTagNode->GetMemTagId();
					SharedState.CreateMemTagGraphTrack(MemTagId);
				}
			}
			else
			{
				const Insights::FMemoryTagId MemTagId = SelectedMemTagNode->GetMemTagId();
				SharedState.CreateMemTagGraphTrack(MemTagId);
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Create memory graph tracks for filtered LLM tags
////////////////////////////////////////////////////////////////////////////////////////////////////

bool SMemTagTreeView::CanCreateGraphTracksForFilteredMemTags() const
{
	if (!ProfilerWindow.IsValid())
	{
		return false;
	}
	int32 FilteredNodeCount = 0;
	for (const FMemTagNodePtr& Group : FilteredGroupNodes)
	{
		FilteredNodeCount += Group->GetFilteredChildren().Num();
	}
	return FilteredNodeCount > 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::CreateGraphTracksForFilteredMemTags()
{
	if (ProfilerWindow.IsValid())
	{
		FMemorySharedState& SharedState = ProfilerWindow->GetSharedState();
		for (const FMemTagNodePtr& GroupNode : FilteredGroupNodes)
		{
			const TArray<Insights::FBaseTreeNodePtr>& Children = GroupNode->GetFilteredChildren();
			for (const Insights::FBaseTreeNodePtr& Child : Children)
			{
				FMemTagNodePtr MemTagNode = StaticCastSharedPtr<FMemTagNode, Insights::FBaseTreeNode>(Child);
				const Insights::FMemoryTagId MemTagId = MemTagNode->GetMemTagId();
				SharedState.CreateMemTagGraphTrack(MemTagId);
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Create all mem memory graph tracks for selected LLM tag(s)
////////////////////////////////////////////////////////////////////////////////////////////////////

bool SMemTagTreeView::CanCreateAllGraphTracks() const
{
	return ProfilerWindow.IsValid();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::CreateAllGraphTracks()
{
	if (ProfilerWindow.IsValid())
	{
		FMemorySharedState& SharedState = ProfilerWindow->GetSharedState();
		for (const FMemTagNodePtr& MemTagNode : MemTagNodes)
		{
			const Insights::FMemoryTagId MemTagId = MemTagNode->GetMemTagId();
			SharedState.CreateMemTagGraphTrack(MemTagId);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Remove memory graph tracks for selected LLM tag(s)
////////////////////////////////////////////////////////////////////////////////////////////////////

bool SMemTagTreeView::CanRemoveGraphTracksForSelectedMemTags() const
{
	return ProfilerWindow.IsValid() && TreeView->GetNumItemsSelected() > 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::RemoveGraphTracksForSelectedMemTags()
{
	if (ProfilerWindow.IsValid())
	{
		FMemorySharedState& SharedState = ProfilerWindow->GetSharedState();
		const TArray<FMemTagNodePtr> SelectedNodes = TreeView->GetSelectedItems();
		for (const FMemTagNodePtr& SelectedMemTagNode : SelectedNodes)
		{
			if (SelectedMemTagNode->IsGroup())
			{
				const TArray<Insights::FBaseTreeNodePtr>& Children = SelectedMemTagNode->GetFilteredChildren();
				for (const Insights::FBaseTreeNodePtr& Child : Children)
				{
					FMemTagNodePtr MemTagNode = StaticCastSharedPtr<FMemTagNode, Insights::FBaseTreeNode>(Child);
					const Insights::FMemoryTagId MemTagId = MemTagNode->GetMemTagId();
					SharedState.RemoveMemTagGraphTrack(MemTagId);
				}
			}
			else
			{
				const Insights::FMemoryTagId MemTagId = SelectedMemTagNode->GetMemTagId();
				SharedState.RemoveMemTagGraphTrack(MemTagId);
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Remove memory graph tracks for LLM tags not used by the current tracker
////////////////////////////////////////////////////////////////////////////////////////////////////

bool SMemTagTreeView::CanRemoveGraphTracksForUnusedMemTags() const
{
	return ProfilerWindow.IsValid();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::RemoveGraphTracksForUnusedMemTags()
{
	if (ProfilerWindow.IsValid())
	{
		FMemorySharedState& SharedState = ProfilerWindow->GetSharedState();
		SharedState.RemoveUnusedMemTagGraphTracks();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Remove all graph series
////////////////////////////////////////////////////////////////////////////////////////////////////

bool SMemTagTreeView::CanRemoveAllGraphTracks() const
{
	return ProfilerWindow.IsValid();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::RemoveAllGraphTracks()
{
	if (ProfilerWindow.IsValid())
	{
		FMemorySharedState& SharedState = ProfilerWindow->GetSharedState();
		SharedState.RemoveAllMemoryGraphTracks();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Generate new color for selected LLM tag(s)
////////////////////////////////////////////////////////////////////////////////////////////////////

bool SMemTagTreeView::CanGenerateColorForSelectedMemTags() const
{
	return ProfilerWindow.IsValid() && TreeView->GetNumItemsSelected() > 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::GenerateColorForSelectedMemTags()
{
	if (ProfilerWindow.IsValid())
	{
		const TArray<FMemTagNodePtr> SelectedNodes = TreeView->GetSelectedItems();
		for (const FMemTagNodePtr& SelectedMemTagNode : SelectedNodes)
		{
			constexpr bool bSetRandomColor = true;
			SetColorToNode(SelectedMemTagNode, FLinearColor(), bSetRandomColor);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::SetColorToNode(const FMemTagNodePtr& MemTagNode, FLinearColor Color, bool bSetRandomColor)
{
	if (MemTagNode->IsGroup())
	{
		const TArray<Insights::FBaseTreeNodePtr>& Children = MemTagNode->GetFilteredChildren();
		for (const Insights::FBaseTreeNodePtr& Child : Children)
		{
			const FMemTagNodePtr ChildMemTagNode = StaticCastSharedPtr<FMemTagNode, Insights::FBaseTreeNode>(Child);
			SetColorToNode(ChildMemTagNode, Color, bSetRandomColor);
		}
		return;
	}

	Insights::FMemoryTag* MemTag = MemTagNode->GetMemTag();
	if (!MemTag)
	{
		return;
	}

	if (bSetRandomColor)
	{
		MemTag->SetRandomColor();
		Color = MemTag->GetColor();
	}
	else
	{
		MemTag->SetColor(Color);
	}

	const FLinearColor BorderColor(FMath::Min(Color.R + 0.4f, 1.0f), FMath::Min(Color.G + 0.4f, 1.0f), FMath::Min(Color.B + 0.4f, 1.0f), 1.0f);

	const Insights::FMemoryTagId MemTagId = MemTagNode->GetMemTagId();

	FMemorySharedState& SharedState = ProfilerWindow->GetSharedState();

	TSharedPtr<FMemoryGraphTrack> MainGraphTrack = SharedState.GetMainGraphTrack();
	for (const TSharedPtr<FMemoryGraphTrack>& GraphTrack : MemTag->GetGraphTracks())
	{
		for (TSharedPtr<FGraphSeries>& Series : GraphTrack->GetSeries())
		{
			//TODO: if (Series->Is<FMemoryGraphSeries>())
			TSharedPtr<FMemoryGraphSeries> MemorySeries = StaticCastSharedPtr<FMemoryGraphSeries>(Series);
			if (MemorySeries->GetTagId() == MemTagId)
			{
				if (GraphTrack == MainGraphTrack)
				{
					MemorySeries->SetColor(Color, BorderColor, Color.CopyWithNewOpacity(0.1f));
				}
				else
				{
					MemorySeries->SetColor(Color, BorderColor);
				}
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Edit color for selected LLM tag(s)
////////////////////////////////////////////////////////////////////////////////////////////////////

bool SMemTagTreeView::CanEditColorForSelectedMemTags() const
{
	return ProfilerWindow.IsValid() && TreeView->GetNumItemsSelected() > 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::EditColorForSelectedMemTags()
{
	if (ProfilerWindow.IsValid())
	{
		EditableColorValue = FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);
		const TArray<FMemTagNodePtr> SelectedNodes = TreeView->GetSelectedItems();
		if (SelectedNodes.Num() > 0)
		{
			EditableColorValue = SelectedNodes[0]->GetColor();
		}

		FColorPickerArgs PickerArgs;
		{
			PickerArgs.bUseAlpha = true;
			PickerArgs.bOnlyRefreshOnMouseUp = false;
			PickerArgs.bOnlyRefreshOnOk = false;
			PickerArgs.bExpandAdvancedSection = false;
			//PickerArgs.DisplayGamma = TAttribute<float>::Create(TAttribute<float>::FGetter::CreateUObject(GEngine, &UEngine::GetDisplayGamma));
			PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateSP(this, &SMemTagTreeView::SetEditableColor);
			PickerArgs.OnColorPickerCancelled = FOnColorPickerCancelled::CreateSP(this, &SMemTagTreeView::ColorPickerCancelled);
			//PickerArgs.OnInteractivePickBegin = FSimpleDelegate::CreateSP(this, &SMemTagTreeView::InteractivePickBegin);
			//PickerArgs.OnInteractivePickEnd = FSimpleDelegate::CreateSP(this, &SMemTagTreeView::InteractivePickEnd);
			//PickerArgs.OnColorPickerWindowClosed = FOnWindowClosed::CreateSP(this, &SMemTagTreeView::ColorPickerClosed);
			PickerArgs.InitialColorOverride = EditableColorValue;
			PickerArgs.ParentWidget = SharedThis(this);
		}

		OpenColorPicker(PickerArgs);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FLinearColor SMemTagTreeView::GetEditableColor() const
{
	return EditableColorValue;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::SetEditableColor(FLinearColor NewColor)
{
	EditableColorValue = NewColor;

	const TArray<FMemTagNodePtr> SelectedNodes = TreeView->GetSelectedItems();
	for (const FMemTagNodePtr& SelectedMemTagNode : SelectedNodes)
	{
		constexpr bool bSetRandomColor = false;
		SetColorToNode(SelectedMemTagNode, EditableColorValue, bSetRandomColor);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemTagTreeView::ColorPickerCancelled(FLinearColor OriginalColor)
{
	SetEditableColor(OriginalColor);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
