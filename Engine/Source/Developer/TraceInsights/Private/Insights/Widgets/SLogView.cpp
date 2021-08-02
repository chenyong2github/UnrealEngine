// Copyright Epic Games, Inc. All Rights Reserved.

#include "SLogView.h"

#include "Algo/BinarySearch.h"
#include "Async/AsyncWork.h"
#include "DesktopPlatformModule.h"
#include "EditorFontGlyphs.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformFileManager.h"
#include "Logging/MessageLog.h"
#include "SlateOptMacros.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"

// Insights
#include "Insights/Common/TimeUtils.h"
#include "Insights/InsightsManager.h"
#include "Insights/InsightsStyle.h"
#include "Insights/Log.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/ViewModels/MarkersTimingTrack.h" // for FTimeMarkerTrackBuilder::GetColorBy*
#include "Insights/ViewModels/TimingViewDrawHelper.h"
#include "Insights/Widgets/STimingProfilerWindow.h"
#include "Insights/Widgets/STimingView.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "SLogView"

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTableTreeViewCommands
////////////////////////////////////////////////////////////////////////////////////////////////////

class FLogViewCommands : public TCommands<FLogViewCommands>
{
public:
	FLogViewCommands()
	: TCommands<FLogViewCommands>(TEXT("FLogViewCommands"), NSLOCTEXT("FLogViewCommands", "Log View Commands", "Log View Commands"), NAME_None, FEditorStyle::Get().GetStyleSetName())
	{
	}

	virtual ~FLogViewCommands()
	{
	}

	// UI_COMMAND takes long for the compiler to optimize
	PRAGMA_DISABLE_OPTIMIZATION
	virtual void RegisterCommands() override
	{
		UI_COMMAND(Command_HideSelectedCategory, "Hide Category", "Hides the selected log category.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(Command_ShowOnlySelectedCategory, "Show Only Selected Category", "Shows only the selected log category (hides all other log categories).", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(Command_ShowAllCategories, "Show All Categories", "Resets the category filter (shows all log categories).", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(Command_CopySelected, "Copy", "Copies the selected log (with all its properties) to clipboard.", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control, EKeys::C));
		UI_COMMAND(Command_CopyMessage, "Copy Message", "Copies the message text of selected log to clipboard.", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Shift, EKeys::C));
		UI_COMMAND(Command_CopyRange, "Copy Range", "Copies all the logs in the selected time range (highlighted in blue) to clipboard.", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control | EModifierKey::Shift, EKeys::C));
		UI_COMMAND(Command_CopyAll, "Copy All", "Copies all the (filtered) logs to clipboard.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(Command_SaveRange, "Save Range As...", "Saves all the logs in the selected time range (highlighted in blue) to a text file (tab-separated values or comma-separated values).", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control, EKeys::S));
		UI_COMMAND(Command_SaveAll, "Save All As...", "Saves all the (filtered) logs to a text file (tab-separated values or comma-separated values).", EUserInterfaceActionType::Button, FInputChord());
	}
	PRAGMA_ENABLE_OPTIMIZATION

	TSharedPtr<FUICommandInfo> Command_HideSelectedCategory;
	TSharedPtr<FUICommandInfo> Command_ShowOnlySelectedCategory;
	TSharedPtr<FUICommandInfo> Command_ShowAllCategories;
	TSharedPtr<FUICommandInfo> Command_CopySelected;
	TSharedPtr<FUICommandInfo> Command_CopyMessage;
	TSharedPtr<FUICommandInfo> Command_CopyRange;
	TSharedPtr<FUICommandInfo> Command_CopyAll;
	TSharedPtr<FUICommandInfo> Command_SaveRange;
	TSharedPtr<FUICommandInfo> Command_SaveAll;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// SLogMessageRow
////////////////////////////////////////////////////////////////////////////////////////////////////

class SLogMessageRow : public SMultiColumnTableRow<TSharedPtr<FLogMessage>>
{
	SLATE_BEGIN_ARGS(SLogMessageRow) {}
	SLATE_END_ARGS()

public:
	/**
	 * Constructs the widget.
	 *
	 * @param InArgs The construction arguments.
	 * @param InLogMessage The log message displayed by this row.
	 * @param InOwnerTableView The table to which the row must be added.
	 */
	void Construct(const FArguments& InArgs, TSharedPtr<FLogMessage> InLogMessage, TSharedRef<SLogView> InParentWidget, const TSharedRef<STableViewBase>& InOwnerTableView)
	{
		WeakLogMessage = MoveTemp(InLogMessage);
		WeakParentWidget = InParentWidget;

		SMultiColumnTableRow<TSharedPtr<FLogMessage>>::Construct(FSuperRowType::FArguments(), InOwnerTableView);

		TSharedRef<SWidget> Row = ChildSlot.GetChildAt(0);

		ChildSlot
		[
			SNew(SBorder)
				.BorderImage(FInsightsStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(this, &SLogMessageRow::GetBackgroundColor)
				[
					Row
				]
		];
	}

public:
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (ColumnName == LogViewColumns::IdColumnName)
		{
			return SNew(SBox)
				.Padding(FMargin(4.0, 0.0))
				[
					SNew(STextBlock)
					.Text(this, &SLogMessageRow::GetIndex)
				];
		}
		else if (ColumnName == LogViewColumns::TimeColumnName)
		{
			return SNew(SBox)
				.Padding(FMargin(4.0, 0.0))
				[
					SNew(STextBlock)
					.Text(this, &SLogMessageRow::GetTime)
					.ColorAndOpacity(this, &SLogMessageRow::GetColorAndOpacity)
				];
		}
		else if (ColumnName == LogViewColumns::VerbosityColumnName)
		{
			return SNew(SBox)
				.Padding(FMargin(4.0, 0.0))
				[
					SNew(STextBlock)
					.Text(this, &SLogMessageRow::GetVerbosity)
					.ColorAndOpacity(this, &SLogMessageRow::GetColorByVerbosity)
				];
		}
		else if (ColumnName == LogViewColumns::CategoryColumnName)
		{
			return SNew(SBox)
				.Padding(FMargin(4.0, 0.0))
				[
					SNew(STextBlock)
					.Text(this, &SLogMessageRow::GetCategory)
					.ColorAndOpacity(this, &SLogMessageRow::GetColorByCategory)
				];
		}
		else if (ColumnName == LogViewColumns::MessageColumnName)
		{
			return SNew(SBox)
				.Padding(FMargin(4.0, 0.0))
				[
					SNew(STextBlock)
					.Text(this, &SLogMessageRow::GetMessage)
					.HighlightText(this, &SLogMessageRow::GetMessageHighlightText)
				];
		}
		else if (ColumnName == LogViewColumns::FileColumnName)
		{
			return SNew(SBox)
				.Padding(FMargin(4.0, 0.0))
				[
					SNew(STextBlock)
					.Text(this, &SLogMessageRow::GetFile)
				];
		}
		else if (ColumnName == LogViewColumns::LineColumnName)
		{
			return SNew(SBox)
				.Padding(FMargin(4.0, 0.0))
				[
					SNew(STextBlock)
					.Text(this, &SLogMessageRow::GetLine)
				];
		}
		else
		{
			return SNew(STextBlock).Text(LOCTEXT("UnknownColumn", "Unknown Column"));
		}
	}

	FSlateColor GetBackgroundColor() const
	{
		TSharedPtr<SLogView> ParentWidgetPin = WeakParentWidget.Pin();
		TSharedPtr<FLogMessage> LogMessagePin = WeakLogMessage.Pin();
		if (ParentWidgetPin.IsValid() && LogMessagePin.IsValid())
		{
			TSharedPtr<FLogMessage> SelectedLogMessage = ParentWidgetPin->GetSelectedLogMessage();
			if (!SelectedLogMessage || SelectedLogMessage->GetIndex() != LogMessagePin->GetIndex()) // if row is not selected
			{
				FLogMessageRecord& CacheEntry = ParentWidgetPin->GetCache().Get(LogMessagePin->GetIndex());
				const double Time = CacheEntry.GetTime();

				TSharedPtr<STimingProfilerWindow> Window = FTimingProfilerManager::Get()->GetProfilerWindow();
				if (Window)
				{
					TSharedPtr<STimingView> TimingView = Window->GetTimingView();
					if (TimingView)
					{
						if (TimingView->IsTimeSelectedInclusive(Time))
						{
							return FSlateColor(FLinearColor(0.25f, 0.5f, 1.0f, 0.25f));
						}
					}
				}
			}
		}

		return FSlateColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
	}

	FSlateColor GetColorAndOpacity() const
	{
		bool IsSelected = false;

		TSharedPtr<SLogView> ParentWidgetPin = WeakParentWidget.Pin();
		TSharedPtr<FLogMessage> LogMessagePin = WeakLogMessage.Pin();
		if (ParentWidgetPin.IsValid() && LogMessagePin.IsValid())
		{
			TSharedPtr<FLogMessage> SelectedLogMessage = ParentWidgetPin->GetSelectedLogMessage();
			if (SelectedLogMessage && SelectedLogMessage->GetIndex() == LogMessagePin->GetIndex())
			{
				IsSelected = true;
			}

			FLogMessageRecord& CacheEntry = ParentWidgetPin->GetCache().Get(LogMessagePin->GetIndex());
			const double Time = CacheEntry.GetTime();

			TSharedPtr<STimingProfilerWindow> Window = FTimingProfilerManager::Get()->GetProfilerWindow();
			if (Window)
			{
				TSharedPtr<STimingView> TimingView = Window->GetTimingView();
				if (TimingView)
				{
					if (TimingView->IsTimeSelectedInclusive(Time))
					{
						if (IsSelected)
						{
							//return FSlateColor(FLinearColor(0.0f, 0.1f, 0.5f, 1.0f));
							return FSlateColor(FLinearColor(0.0f, 0.05f, 0.2f, 1.0f));
						}
						else
						{
							//return FSlateColor(FLinearColor(0.2f, 0.4f, 0.8f, 1.0f));
							return FSlateColor(FLinearColor(0.4f, 0.8f, 1.6f, 1.0f));
						}
					}
				}
			}
		}

		if (IsSelected)
		{
			return FSlateColor(FLinearColor::Black);
		}
		else
		{
			return FSlateColor(FLinearColor::White);
		}
	}

	FText GetIndex() const
	{
		TSharedPtr<SLogView> ParentWidgetPin = WeakParentWidget.Pin();
		TSharedPtr<FLogMessage> LogMessagePin = WeakLogMessage.Pin();
		if (ParentWidgetPin.IsValid() && LogMessagePin.IsValid())
		{
			FLogMessageRecord& CacheEntry = ParentWidgetPin->GetCache().Get(LogMessagePin->GetIndex());
			return CacheEntry.GetIndexAsText();
		}
		else
		{
			return FText();
		}
	}

	FText GetTime() const
	{
		TSharedPtr<SLogView> ParentWidgetPin = WeakParentWidget.Pin();
		TSharedPtr<FLogMessage> LogMessagePin = WeakLogMessage.Pin();
		if (ParentWidgetPin.IsValid() && LogMessagePin.IsValid())
		{
			FLogMessageRecord& CacheEntry = ParentWidgetPin->GetCache().Get(LogMessagePin->GetIndex());
			return CacheEntry.GetTimeAsText();
		}
		else
		{
			return FText();
		}
	}

	FText GetVerbosity() const
	{
		TSharedPtr<SLogView> ParentWidgetPin = WeakParentWidget.Pin();
		TSharedPtr<FLogMessage> LogMessagePin = WeakLogMessage.Pin();
		if (ParentWidgetPin.IsValid() && LogMessagePin.IsValid())
		{
			FLogMessageRecord& CacheEntry = ParentWidgetPin->GetCache().Get(LogMessagePin->GetIndex());
			return CacheEntry.GetVerbosityAsText();
		}
		else
		{
			return FText();
		}
	}

	FSlateColor GetColorByVerbosity() const
	{
		if (IsSelected())
			return FSlateColor(FLinearColor::Black);

		TSharedPtr<SLogView> ParentWidgetPin = WeakParentWidget.Pin();
		TSharedPtr<FLogMessage> LogMessagePin = WeakLogMessage.Pin();
		if (ParentWidgetPin.IsValid() && LogMessagePin.IsValid())
		{
			FLogMessageRecord& CacheEntry = ParentWidgetPin->GetCache().Get(LogMessagePin->GetIndex());
			return FSlateColor(FTimeMarkerTrackBuilder::GetColorByVerbosity(CacheEntry.GetVerbosity()));
		}
		else
		{
			return FSlateColor(FLinearColor::White);
		}
	}

	FText GetCategory() const
	{
		TSharedPtr<SLogView> ParentWidgetPin = WeakParentWidget.Pin();
		TSharedPtr<FLogMessage> LogMessagePin = WeakLogMessage.Pin();
		if (ParentWidgetPin.IsValid() && LogMessagePin.IsValid())
		{
			FLogMessageRecord& CacheEntry = ParentWidgetPin->GetCache().Get(LogMessagePin->GetIndex());
			return CacheEntry.GetCategoryAsText();
		}
		else
		{
			return FText();
		}
	}

	FSlateColor GetColorByCategory() const
	{
		if (IsSelected())
			return FSlateColor(FLinearColor::Black);

		TSharedPtr<SLogView> ParentWidgetPin = WeakParentWidget.Pin();
		TSharedPtr<FLogMessage> LogMessagePin = WeakLogMessage.Pin();
		if (ParentWidgetPin.IsValid() && LogMessagePin.IsValid())
		{
			FLogMessageRecord& CacheEntry = ParentWidgetPin->GetCache().Get(LogMessagePin->GetIndex());
			return FSlateColor(FTimeMarkerTrackBuilder::GetColorByCategory(CacheEntry.GetCategory()));
		}
		else
		{
			return FSlateColor(FLinearColor::White);
		}
	}

	FText GetMessage() const
	{
		TSharedPtr<SLogView> ParentWidgetPin = WeakParentWidget.Pin();
		TSharedPtr<FLogMessage> LogMessagePin = WeakLogMessage.Pin();
		if (ParentWidgetPin.IsValid() && LogMessagePin.IsValid())
		{
			FLogMessageRecord& CacheEntry = ParentWidgetPin->GetCache().Get(LogMessagePin->GetIndex());
			return CacheEntry.GetMessageAsText();
		}
		else
		{
			return FText();
		}
	}

	FText GetMessageHighlightText() const
	{
		TSharedPtr<SLogView> ParentWidgetPin = WeakParentWidget.Pin();
		if (ParentWidgetPin.IsValid())
		{
			return ParentWidgetPin->GetFilterText();
		}
		else
		{
			return FText();
		}
	}

	FText GetFile() const
	{
		TSharedPtr<SLogView> ParentWidgetPin = WeakParentWidget.Pin();
		TSharedPtr<FLogMessage> LogMessagePin = WeakLogMessage.Pin();
		if (ParentWidgetPin.IsValid() && LogMessagePin.IsValid())
		{
			FLogMessageRecord& CacheEntry = ParentWidgetPin->GetCache().Get(LogMessagePin->GetIndex());
			return CacheEntry.GetFileAsText();
		}
		else
		{
			return FText();
		}
	}

	FText GetLine() const
	{
		TSharedPtr<SLogView> ParentWidgetPin = WeakParentWidget.Pin();
		TSharedPtr<FLogMessage> LogMessagePin = WeakLogMessage.Pin();
		if (ParentWidgetPin.IsValid() && LogMessagePin.IsValid())
		{
			FLogMessageRecord& CacheEntry = ParentWidgetPin->GetCache().Get(LogMessagePin->GetIndex());
			return CacheEntry.GetLineAsText();
		}
		else
		{
			return FText();
		}
	}

	FText GetRowToolTip() const
	{
		TSharedPtr<SLogView> ParentWidgetPin = WeakParentWidget.Pin();
		TSharedPtr<FLogMessage> LogMessagePin = WeakLogMessage.Pin();
		if (ParentWidgetPin.IsValid() && LogMessagePin.IsValid())
		{
			FLogMessageRecord& CacheEntry = ParentWidgetPin->GetCache().Get(LogMessagePin->GetIndex());
			return CacheEntry.ToDisplayString();
		}
		else
		{
			return FText();
		}
	}

private:
	TWeakPtr<FLogMessage> WeakLogMessage;
	TWeakPtr<SLogView> WeakParentWidget;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// SLogView
////////////////////////////////////////////////////////////////////////////////////////////////////

SLogView::SLogView()
	: FilteringStartIndex(0)
	, FilteringEndIndex(0)
	, FilteringChangeNumber(0)
	, bIsFilteringAsyncTaskCancelRequested(false)
	, TotalNumCategories(0)
	, TotalNumMessages(0)
	, bIsDirty(false)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

SLogView::~SLogView()
{
	// Remove ourselves from the profiler manager.
	//if (FTimingProfilerManager::Get().IsValid())
	//{
	//	//TODO: FTimimgProfilerManager::Get()->OnRequestLogViewUpdate().RemoveAll(this);
	//}

	Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLogView::Reset()
{
	//ListView
	//ExternalScrollbar
	FilterTextBox->SetText(FText::GetEmpty());

	Filter.Reset();
	FilterChangeNumber = 0;

	FilteringStartIndex = 0;
	FilteringEndIndex = 0;
	FilteringChangeNumber = 0;

	// Clean up our async task if we're deleted before it is completed.
	if (FilteringAsyncTask.IsValid())
	{
		if (!FilteringAsyncTask->Cancel())
		{
			bIsFilteringAsyncTaskCancelRequested = true;
			FilteringAsyncTask->EnsureCompletion();
		}
		FilteringAsyncTask.Reset();
	}

	//bIsFilteringAsyncTaskCancelRequested = false;
	//FilteringStopwatch.Stop();

	TotalNumCategories = 0;
	TotalNumMessages = 0;

	bIsDirty = false;
	DirtyStopwatch.Stop();

	StatsText = FText::GetEmpty();

	Cache.Reset();

	Messages.Reset();

	ListView->RebuildList();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SLogView::Construct(const FArguments& InArgs)
{
	SAssignNew(ExternalScrollbar, SScrollBar)
	.AlwaysShowScrollbar(true);

	ChildSlot
	[
		SNew(SVerticalBox)

		// Toolbar
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)

			// Icon
			//+ SHorizontalBox::Slot()
			//.VAlign(VAlign_Center)
			//.AutoWidth()
			//[
			//	SNew(SImage)
			//	.Image(FEditorStyle::GetBrush(TEXT("Log.TabIcon")))
			//]
			//
			//// Label
			//+ SHorizontalBox::Slot()
			//.VAlign(VAlign_Center)
			//.Padding(2.0f, 0.0f)
			//.AutoWidth()
			//[
			//	SNew(STextBlock)
			//	.Text(LOCTEXT("LogViewLabel", "Log View"))
			//]

			// Verbosity Threshold Filter
			+SHorizontalBox::Slot()
			//.Padding(2.0f, 0.0f, 0.0f, 0.0f)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SComboButton)
				.ComboButtonStyle(FEditorStyle::Get(), "GenericFilters.ComboButtonStyle")
				.ForegroundColor(FLinearColor::White)
				.ContentPadding(0)
				.ToolTipText(LOCTEXT("VerbosityThresholdFilterToolTip", "Filter log messages by verbosity threshold."))
				.OnGetMenuContent(this, &SLogView::MakeVerbosityThresholdMenu)
				.HasDownArrow(true)
				.ContentPadding(FMargin(1.0f, 1.0f, 1.0f, 0.0f))
				.ButtonContent()
				[
					SNew(SHorizontalBox)

					+SHorizontalBox::Slot()
					.Padding(0.0f)
					.AutoWidth()
					[
						SNew(STextBlock)
						.TextStyle(FEditorStyle::Get(), "GenericFilters.TextStyle")
						.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.9"))
						.Text(FText::FromString(FString(TEXT("\xf0b0"))) /*fa-filter*/)
					]

					+SHorizontalBox::Slot()
					.Padding(2.0f, 0.0f, 0.0f, 0.0f)
					.AutoWidth()
					[
						SNew(STextBlock)
						.TextStyle(FEditorStyle::Get(), "GenericFilters.TextStyle")
						.Text(LOCTEXT("VerbosityThresholdFilter", "Verbosity Threshold"))
					]
				]
			]

			// Category Filter
			+SHorizontalBox::Slot()
			.Padding(2.0f, 0.0f, 0.0f, 0.0f)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SComboButton)
				.ComboButtonStyle(FEditorStyle::Get(), "GenericFilters.ComboButtonStyle")
				.ForegroundColor(FLinearColor::White)
				.ContentPadding(0)
				.ToolTipText(LOCTEXT("CategoryFilterToolTip", "Filter log messages by category."))
				.OnGetMenuContent(this, &SLogView::MakeCategoryFilterMenu)
				.HasDownArrow(true)
				.ContentPadding(FMargin(1.0f, 1.0f, 1.0f, 0.0f))
				.ButtonContent()
				[
					SNew(SHorizontalBox)

					+SHorizontalBox::Slot()
					.Padding(0.0f)
					.AutoWidth()
					[
						SNew(STextBlock)
						.TextStyle(FEditorStyle::Get(), "GenericFilters.TextStyle")
						.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.9"))
						.Text(FText::FromString(FString(TEXT("\xf0b0"))) /*fa-filter*/)
					]

					+SHorizontalBox::Slot()
					.Padding(2.0f, 0.0f, 0.0f, 0.0f)
					.AutoWidth()
					[
						SNew(STextBlock)
						.TextStyle(FEditorStyle::Get(), "GenericFilters.TextStyle")
						.Text(LOCTEXT("CategoryFilter", "Category Filter"))
					]
				]
			]

			// Text Filter (Search Box)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 0.0f)
			.AutoWidth()
			[
				SAssignNew(FilterTextBox, SSearchBox)
				.HintText(LOCTEXT("FilterTextBoxHint", "Search log messages"))
				.ToolTipText(LOCTEXT("FilterTextBoxToolTip", "Type here to filter the list of log messages."))
				.OnTextChanged(this, &SLogView::FilterTextBox_OnTextChanged)
			]

			// Stats Text
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 0.0f)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(this, &SLogView::GetStatsText)
				.ColorAndOpacity(this, &SLogView::GetStatsTextColor)
			]
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SBox)
			.VAlign(VAlign_Fill)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(0.0f)
				.VAlign(VAlign_Fill)
				[
					SNew(SScrollBox)
					.Orientation(Orient_Horizontal)

					+ SScrollBox::Slot()
					.VAlign(VAlign_Fill)
					[
						SAssignNew(ListView, SListView<TSharedPtr<FLogMessage>>)
						.ExternalScrollbar(ExternalScrollbar)
						.ItemHeight(20.0f)
						.SelectionMode(ESelectionMode::Single)
						.OnMouseButtonClick(this, &SLogView::OnMouseButtonClick)
						.OnSelectionChanged(this, &SLogView::OnSelectionChanged)
						.ListItemsSource(&Messages)
						.OnGenerateRow(this, &SLogView::OnGenerateRow)
						.ConsumeMouseWheel(EConsumeMouseWheel::Always)
						.OnContextMenuOpening(FOnContextMenuOpening::CreateSP(this, &SLogView::ListView_GetContextMenu))
						.HeaderRow
						(
							SNew(SHeaderRow)

							+ SHeaderRow::Column(LogViewColumns::IdColumnName)
							.ManualWidth(60.0f)
							.DefaultLabel(LOCTEXT("IdColumn", "Index"))

							+ SHeaderRow::Column(LogViewColumns::TimeColumnName)
							.ManualWidth(94.0f)
							.DefaultLabel(LOCTEXT("TimeColumn", "Time"))

							+ SHeaderRow::Column(LogViewColumns::VerbosityColumnName)
							.ManualWidth(80.0f)
							.DefaultLabel(LOCTEXT("VerbosityColumn", "Verbosity"))

							+ SHeaderRow::Column(LogViewColumns::CategoryColumnName)
							.ManualWidth(120.0f)
							.DefaultLabel(LOCTEXT("CategoryColumn", "Category"))

							+ SHeaderRow::Column(LogViewColumns::MessageColumnName)
							.ManualWidth(880.0f)
							.DefaultLabel(LOCTEXT("MessageColumn", "Message"))

							+ SHeaderRow::Column(LogViewColumns::FileColumnName)
							.ManualWidth(600.0f)
							.DefaultLabel(LOCTEXT("FileColumn", "File"))

							+ SHeaderRow::Column(LogViewColumns::LineColumnName)
							.ManualWidth(60.0f)
							.DefaultLabel(LOCTEXT("LineColumn", "Line"))
						)
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
		]
	];

	InitCommandList();

	// Register ourselves with the profiler manager.
	//TODO: FTimingProfilerManager::Get()->OnRequestLogViewUpdate().AddSP(this, &SLogView::ProfilerManager_OnRequestLogViewUpdate);
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<ITableRow> SLogView::OnGenerateRow(TSharedPtr<FLogMessage> InLogMessage, const TSharedRef<STableViewBase>& OwnerTable)
{
	// Generate a row for the log message corresponding to InLogMessage.
	return SNew(SLogMessageRow, InLogMessage, SharedThis(this), OwnerTable);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLogView::InitCommandList()
{
	FLogViewCommands::Register();
	CommandList = MakeShared<FUICommandList>();
	CommandList->MapAction(FLogViewCommands::Get().Command_HideSelectedCategory, FExecuteAction::CreateSP(this, &SLogView::HideSelectedCategory), FCanExecuteAction::CreateSP(this, &SLogView::CanHideSelectedCategory));
	CommandList->MapAction(FLogViewCommands::Get().Command_ShowOnlySelectedCategory, FExecuteAction::CreateSP(this, &SLogView::ShowOnlySelectedCategory), FCanExecuteAction::CreateSP(this, &SLogView::CanShowOnlySelectedCategory));
	CommandList->MapAction(FLogViewCommands::Get().Command_ShowAllCategories, FExecuteAction::CreateSP(this, &SLogView::ShowAllCategories), FCanExecuteAction::CreateSP(this, &SLogView::CanShowAllCategories));
	CommandList->MapAction(FLogViewCommands::Get().Command_CopySelected, FExecuteAction::CreateSP(this, &SLogView::CopySelected), FCanExecuteAction::CreateSP(this, &SLogView::CanCopySelected));
	CommandList->MapAction(FLogViewCommands::Get().Command_CopyMessage, FExecuteAction::CreateSP(this, &SLogView::CopyMessage), FCanExecuteAction::CreateSP(this, &SLogView::CanCopyMessage));
	CommandList->MapAction(FLogViewCommands::Get().Command_CopyRange, FExecuteAction::CreateSP(this, &SLogView::CopyRange), FCanExecuteAction::CreateSP(this, &SLogView::CanCopyRange));
	CommandList->MapAction(FLogViewCommands::Get().Command_CopyAll, FExecuteAction::CreateSP(this, &SLogView::CopyAll), FCanExecuteAction::CreateSP(this, &SLogView::CanCopyAll));
	CommandList->MapAction(FLogViewCommands::Get().Command_SaveRange, FExecuteAction::CreateSP(this, &SLogView::SaveRange), FCanExecuteAction::CreateSP(this, &SLogView::CanSaveRange));
	CommandList->MapAction(FLogViewCommands::Get().Command_SaveAll, FExecuteAction::CreateSP(this, &SLogView::SaveAll), FCanExecuteAction::CreateSP(this, &SLogView::CanSaveAll));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLogView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	int32 NewMessageCount = 0;

	TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();

	Cache.SetSession(Session);

	if (Session.IsValid())
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
		const TraceServices::ILogProvider& LogProvider = TraceServices::ReadLogProvider(*Session.Get());

		NewMessageCount = static_cast<int32>(LogProvider.GetMessageCount());

		//TODO: show only categories that are used in current trace
		//TODO: cause of duplicates: a) runtime, b) case insensitive, c) stripped "Log" prefix

		const int32 NumCategories = static_cast<int32>(LogProvider.GetCategoryCount());
		if (NumCategories != TotalNumCategories)
		{
			TotalNumCategories = NumCategories;
			UE_LOG(TraceInsights, Log, TEXT("[LogView] Total Log Categories: %d"), TotalNumCategories);

			TSet<FName> Categories;
			LogProvider.EnumerateCategories([&Categories](const TraceServices::FLogCategoryInfo& Category)
			{
				FString CategoryStr(Category.Name);
				if (CategoryStr.StartsWith(TEXT("Log")))
				{
					CategoryStr.RightChopInline(3, false);
				}
				if (Categories.Contains(FName(*CategoryStr)))
				{
					UE_LOG(TraceInsights, Log, TEXT("[LogView] Duplicated Log Category: \"%s\""), Category.Name);
				}
				Categories.Add(FName(*CategoryStr));
			});
			Filter.SyncAvailableCategories(Categories);
			UE_LOG(TraceInsights, Log, TEXT("[LogView] Unique Log Categories: %d"), Filter.GetAvailableLogCategories().Num());

			//Cache.Reset();
			Messages.Reset();
			TotalNumMessages = 0;
			//ListView->RebuildList();
			bIsDirty = true;
			DirtyStopwatch.Start();
		}
	}

	if (NewMessageCount != TotalNumMessages)
	{
		bIsDirty = true;
		DirtyStopwatch.Start();

		if (NewMessageCount > TotalNumMessages)
		{
			if (Filter.IsFilterSet())
			{
				// Filter messages async.
				if (!FilteringAsyncTask.IsValid())
				{
					FilteringStopwatch.Restart();

					bIsFilteringAsyncTaskCancelRequested = false;
					FilteringStartIndex = TotalNumMessages;
					FilteringEndIndex = NewMessageCount;
					FilteringChangeNumber = Filter.GetChangeNumber();
					FilteringAsyncTask = MakeUnique<FAsyncTask<FLogFilteringAsyncTask>>(FilteringStartIndex, FilteringEndIndex, Filter, SharedThis(this));
					UE_LOG(TraceInsights, Log, TEXT("[LogView] Start async task for filtering by%s%s%s (\"%s\") (%d to %d)"),
						Filter.IsFilterSetByVerbosity() ? TEXT(" Verbosity,") : TEXT(""),
						Filter.IsFilterSetByCategory() ? TEXT(" Category,") : TEXT(""),
						Filter.IsFilterSetByText() ? TEXT(" Text") : TEXT(""),
						*Filter.GetFilterText().ToString(),
						FilteringStartIndex, FilteringEndIndex);
					FilteringAsyncTask->StartBackgroundTask();
				}
				else
				{
					// A task is already in progress.
					if (FilteringStartIndex == TotalNumMessages &&
						FilteringEndIndex <= NewMessageCount &&
						FilteringChangeNumber == Filter.GetChangeNumber())
					{
						// The filter is still valid. Just wait.
					}
					else
					{
						// The filter used by running task is obsolete. Cancel the task.
						bIsFilteringAsyncTaskCancelRequested = true;
					}
				}
			}
			else // no filtering
			{
				FilteringStopwatch.Restart();

				for (int32 Index = TotalNumMessages; Index < NewMessageCount; Index++)
				{
					Messages.Add(MakeShared<FLogMessage>(Index));
				}

				const int32 NumAddedMessages = NewMessageCount - TotalNumMessages;
				TotalNumMessages = NewMessageCount;
				TSharedPtr<FLogMessage> SelectedLogMessage = GetSelectedLogMessage();
				ListView->RebuildList();
				if (SelectedLogMessage.IsValid())
				{
					// Restore selection.
					SelectedLogMessageByLogIndex(SelectedLogMessage->GetIndex());
				}
				bIsDirty = false;
				DirtyStopwatch.Reset();
				UpdateStatsText();

				FilteringStopwatch.Stop();
				uint64 DurationMs = FilteringStopwatch.GetAccumulatedTimeMs();
				if (DurationMs > 10) // avoids spams
				{
					UE_LOG(TraceInsights, Log, TEXT("[LogView] Updated (no filter; %d added / %d total messages) in %llu ms."),
						NumAddedMessages, NewMessageCount, DurationMs);
				}
			}
		}
		else // if (NewMessageCount < TotalNumMessages)
		{
			// Just reset. On next Tick() the list will grow if needed.
			UE_LOG(TraceInsights, Log, TEXT("[LogView] RESET"));
			Cache.Reset();
			Messages.Reset();
			TotalNumMessages = 0;
			ListView->RebuildList();
			bIsDirty = (NewMessageCount != 0);
			if (bIsDirty)
			{
				DirtyStopwatch.Start();
			}
			else
			{
				DirtyStopwatch.Reset();
			}
			UpdateStatsText();
		}
	}
	else if (bIsDirty && !FilteringAsyncTask.IsValid())
	{
		bIsDirty = false;
		DirtyStopwatch.Reset();
		UpdateStatsText();
	}

	if (FilteringAsyncTask.IsValid() &&
		FilteringAsyncTask->IsDone())
	{
		// A filtering async task has completed. Check if filter used is still valid.
		if (!bIsFilteringAsyncTaskCancelRequested &&
			FilteringStartIndex == TotalNumMessages &&
			FilteringEndIndex <= NewMessageCount &&
			FilteringChangeNumber == Filter.GetChangeNumber())
		{
			FLogFilteringAsyncTask& Task = FilteringAsyncTask->GetTask();
			const TArray<uint32>& FilteredMessages = Task.GetFilteredMessages();

			// Add filtered messages to current Messages array.
			const int32 NumFilteredMessages = FilteredMessages.Num();
			for (int32 Index = 0; Index < NumFilteredMessages; Index++)
			{
				Messages.Add(MakeShared<FLogMessage>(FilteredMessages[Index]));
			}

			TotalNumMessages = Task.GetEndIndex();
			TSharedPtr<FLogMessage> SelectedLogMessage = GetSelectedLogMessage();
			ListView->RebuildList();
			if (SelectedLogMessage.IsValid())
			{
				// Restore selection.
				SelectedLogMessageByLogIndex(SelectedLogMessage->GetIndex());
			}
			bIsDirty = false;
			DirtyStopwatch.Reset();
			UpdateStatsText();

			FilteringStopwatch.Stop();
			uint64 DurationMs = FilteringStopwatch.GetAccumulatedTimeMs();
			if (DurationMs > 10) // avoids spams
			{
				int32 NumAsyncFilteredMessages = Task.GetEndIndex() - Task.GetStartIndex();
				double Speed = static_cast<double>(NumAsyncFilteredMessages) / FilteringStopwatch.GetAccumulatedTime();
				UE_LOG(TraceInsights, Log, TEXT("[LogView] Updated (%d added / %d async filtered / %d total messages) in %llu ms (%.2f messages/second)."),
					NumFilteredMessages, NumAsyncFilteredMessages, TotalNumMessages, DurationMs, Speed);
			}
		}

		FilteringAsyncTask.Reset();
	}

	//SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FLogMessage> SLogView::GetSelectedLogMessage() const
{
	TArray<TSharedPtr<FLogMessage>> SelectedItems = ListView->GetSelectedItems();
	return (SelectedItems.Num() == 1) ? SelectedItems[0] : nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLogView::SelectedLogMessageByLogIndex(int32 LogIndex)
{
	// We are assuming the Messages list is sorted by log index...
	int32 MessageIndex = Algo::BinarySearchBy(Messages, LogIndex, &FLogMessage::GetIndex);
	if (MessageIndex != INDEX_NONE)
	{
		ListView->SetItemSelection(Messages[MessageIndex], true);
		ListView->RequestScrollIntoView(Messages[MessageIndex]);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLogView::SelectLogMessage(TSharedPtr<FLogMessage> LogMessage)
{
	if (LogMessage.IsValid())
	{
		TSharedPtr<STimingProfilerWindow> Window = FTimingProfilerManager::Get()->GetProfilerWindow();
		if (Window)
		{
			TSharedPtr<STimingView> TimingView = Window->GetTimingView();
			if (TimingView)
			{
				const double Time = Cache.Get(LogMessage->GetIndex()).GetTime();

				if (FSlateApplication::Get().GetModifierKeys().IsShiftDown())
				{
					TimingView->SelectToTimeMarker(Time);
				}
				else
				{
					TimingView->SetAndCenterOnTimeMarker(Time);
				}
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLogView::OnMouseButtonClick(TSharedPtr<FLogMessage> LogMessage)
{
	SelectLogMessage(LogMessage);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLogView::OnSelectionChanged(TSharedPtr<FLogMessage> LogMessage, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo != ESelectInfo::Direct &&
		SelectInfo != ESelectInfo::OnMouseClick)
	{
		SelectLogMessage(LogMessage);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLogView::FilterTextBox_OnTextChanged(const FText& InFilterText)
{
	if (Filter.GetFilterText().ToString().Equals(InFilterText.ToString(), ESearchCase::CaseSensitive))
	{
		// nothing to do
		return;
	}

	// Set filter phrases.
	Filter.SetFilterText(InFilterText);

	// Report possible syntax errors back to the user.
	FilterTextBox->SetError(Filter.GetSyntaxErrors());

	OnFilterChanged();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLogView::OnFilterChanged()
{
	const FString FilterText = FilterTextBox->GetText().ToString();
	UE_LOG(TraceInsights, Log, TEXT("[LogView] OnFilterChanged: \"%s\""), *FilterText);
	Cache.Reset();
	Messages.Reset();
	TotalNumMessages = 0;
	bIsDirty = true;
	DirtyStopwatch.Start();
	// The next Tick() will update the filtered list of messages.
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLogView::UpdateStatsText()
{
	if (Messages.Num() == TotalNumMessages)
	{
		StatsText = FText::Format(LOCTEXT("StatsText1", "{0} logs"), FText::AsNumber(TotalNumMessages));
	}
	else
	{
		StatsText = FText::Format(LOCTEXT("StatsText2", "{0} / {1} logs"), FText::AsNumber(Messages.Num()), FText::AsNumber(TotalNumMessages));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SLogView::GetStatsText() const
{
	if (bIsDirty)
	{
		DirtyStopwatch.Update();
		double DT = DirtyStopwatch.GetAccumulatedTime();
		if (DT > 1.0)
		{
			return FText::Format(LOCTEXT("StatsTextEx", "{0} (filtering... please wait... {1}s)"), StatsText, FText::AsNumber(FMath::RoundToInt(DT)));
		}
	}

	return StatsText;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FSlateColor SLogView::GetStatsTextColor() const
{
	if (bIsDirty)
	{
		return FSlateColor(FLinearColor(1.0f, 0.5f, 0.5f, 1.0f));
	}
	else
	{
		return FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<SWidget> SLogView::ListView_GetContextMenu()
{
	TSharedPtr<FLogMessage> SelectedLogMessage = GetSelectedLogMessage();

	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, CommandList);

	MenuBuilder.BeginSection("LogViewContextMenu");
	{
		if (SelectedLogMessage.IsValid())
		{
			FLogMessageRecord& Record = Cache.Get(SelectedLogMessage->GetIndex());
			FName CategoryName(Record.GetCategory());

			MenuBuilder.AddMenuEntry(
				FLogViewCommands::Get().Command_HideSelectedCategory,
				NAME_None,
				FText::Format(LOCTEXT("HideCategory", "Hide \"{0}\" Category"), Record.GetCategoryAsText()),
				FText::Format(LOCTEXT("HideCategory_Tooltip", "Hide the \"{0}\" log category."), Record.GetCategoryAsText()),
				FSlateIcon(FCoreStyle::Get().GetStyleSetName(), "Icons.Hidden")
			);

			MenuBuilder.AddMenuEntry(
				FLogViewCommands::Get().Command_ShowOnlySelectedCategory,
				NAME_None,
				FText::Format(LOCTEXT("ShowOnlyCategory", "Show Only \"{0}\" Category"), Record.GetCategoryAsText()),
				FText::Format(LOCTEXT("ShowOnlyCategory_Tooltip", "Show only the \"{0}\" log category (hide all other log categories)."), Record.GetCategoryAsText()),
				FSlateIcon(FCoreStyle::Get().GetStyleSetName(), "Icons.Visible")
			);
		}

		MenuBuilder.AddMenuEntry(
			FLogViewCommands::Get().Command_ShowAllCategories,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FCoreStyle::Get().GetStyleSetName(), "Icons.Visible")
		);

		MenuBuilder.AddSeparator();

		MenuBuilder.AddMenuEntry(
			FLogViewCommands::Get().Command_CopySelected,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FCoreStyle::Get().GetStyleSetName(), "GenericCommands.Copy")
		);

		MenuBuilder.AddMenuEntry(
			FLogViewCommands::Get().Command_CopyMessage,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FCoreStyle::Get().GetStyleSetName(), "GenericCommands.Copy")
		);

		MenuBuilder.AddMenuEntry(
			FLogViewCommands::Get().Command_CopyRange,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FCoreStyle::Get().GetStyleSetName(), "GenericCommands.Copy")
		);

		MenuBuilder.AddMenuEntry(
			FLogViewCommands::Get().Command_CopyAll,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FCoreStyle::Get().GetStyleSetName(), "GenericCommands.Copy")
		);

		MenuBuilder.AddSeparator();

		MenuBuilder.AddMenuEntry(
			FLogViewCommands::Get().Command_SaveRange,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FCoreStyle::Get().GetStyleSetName(), "Icons.Save")
		);

		MenuBuilder.AddMenuEntry(
			FLogViewCommands::Get().Command_SaveAll,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FCoreStyle::Get().GetStyleSetName(), "Icons.Save")
		);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SLogView::MakeVerbosityThresholdMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	MenuBuilder.BeginSection("LogViewVerbosityThreshold"/*, LOCTEXT("VerbosityThresholdHeading", "Verbosity Threshold")*/);
	CreateVerbosityThresholdMenuSection(MenuBuilder);
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void  SLogView::CreateVerbosityThresholdMenuSection(FMenuBuilder& MenuBuilder)
{
	struct FVerbosityThresholdInfo
	{
		ELogVerbosity::Type Verbosity;
		FText Label;
		FText ToolTip;
	};

	FVerbosityThresholdInfo VerbosityThresholds[] =
	{
		{ ELogVerbosity::VeryVerbose, LOCTEXT("VerbosityThresholdVeryVerbose", "VeryVerbose"), LOCTEXT("VerbosityThresholdVeryVerbose_Tooltip", "Show all log messages (any verbosity level)."), },
		{ ELogVerbosity::Verbose,     LOCTEXT("VerbosityThresholdVerbose",     "Verbose"),     LOCTEXT("VerbosityThresholdVerbose_Tooltip",     "Show Verbose, Log, Display, Warning, Error and Fatal log messages (i.e. hide VeryVerbose log messages)."), },
		{ ELogVerbosity::Log,         LOCTEXT("VerbosityThresholdLog",         "Log"),         LOCTEXT("VerbosityThresholdLog_Tooltip",         "Show Log, Display, Warning, Error and Fatal log messages (i.e. hide Verbose and VeryVerbose log messages)."), },
		{ ELogVerbosity::Display,     LOCTEXT("VerbosityThresholdDisplay",     "Display"),     LOCTEXT("VerbosityThresholdDisplay_Tooltip",     "Show Display, Warning, Error and Fatal log messages (i.e. hide Log, Verbose and VeryVerbose log messages)."), },
		{ ELogVerbosity::Warning,     LOCTEXT("VerbosityThresholdWarning",     "Warning"),     LOCTEXT("VerbosityThresholdWarning_Tooltip",     "Show only Warning, Error and Fatal log messages."), },
		{ ELogVerbosity::Error,       LOCTEXT("VerbosityThresholdError",       "Error"),       LOCTEXT("VerbosityThresholdError_Tooltip",       "Show only Error and Fatal log messages."), },
		{ ELogVerbosity::Fatal,       LOCTEXT("VerbosityThresholdFatal",       "Fatal"),       LOCTEXT("VerbosityThresholdFatal_Tooltip",       "Show only Fatal log messages."), },
	};

	for (int32 Index = 0; Index < sizeof(VerbosityThresholds) / sizeof(FVerbosityThresholdInfo); ++Index)
	{
		const FVerbosityThresholdInfo& Threshold = VerbosityThresholds[Index];

		const TSharedRef<SWidget> TextBlock = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(Threshold.Label)
				.ShadowColorAndOpacity(FLinearColor(0.05f, 0.05f, 0.05f, 1.0f))
				.ShadowOffset(FVector2D(1.0f, 1.0f))
				.ColorAndOpacity(FSlateColor(FTimeMarkerTrackBuilder::GetColorByVerbosity(Threshold.Verbosity)))
			]
			+ SHorizontalBox::Slot()
			.Padding(2.0f, 0.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.9"))
				.Text(this, &SLogView::VerbosityThreshold_GetSuffixGlyph, Threshold.Verbosity)
				.ColorAndOpacity(this, &SLogView::VerbosityThreshold_GetSuffixColor, Threshold.Verbosity)
			];

		MenuBuilder.AddMenuEntry(
			FUIAction(FExecuteAction::CreateSP(this, &SLogView::VerbosityThreshold_Execute, Threshold.Verbosity),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &SLogView::VerbosityThreshold_IsChecked, Threshold.Verbosity)),
			TextBlock,
			NAME_None,
			Threshold.ToolTip,
			EUserInterfaceActionType::RadioButton
		);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SLogView::MakeCategoryFilterMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	MenuBuilder.BeginSection("LogViewShowAllCategories", LOCTEXT("QuickFilterHeading", "Quick Filter"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ShowAllCategories", "Show/Hide All"),
			LOCTEXT("ShowAllCategories_Tooltip", "Change filtering to show/hide all categories"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SLogView::ShowHideAllCategories_Execute),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &SLogView::ShowHideAllCategories_IsChecked)),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("LogViewCategoriesEntries", LOCTEXT("CategoriesHeading", "Categories"));
	CreateCategoriesFilterMenuSection(MenuBuilder);
	MenuBuilder.EndSection();

	const float MaxMenuHeight = FMath::Clamp(this->GetCachedGeometry().GetLocalSize().Y - 40.0f, 100.0f, 500.0f);
	return MenuBuilder.MakeWidget(nullptr, MaxMenuHeight);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLogView::CreateCategoriesFilterMenuSection(FMenuBuilder& MenuBuilder)
{
	for (const FName& CategoryName : Filter.GetAvailableLogCategories())
	{
		const FString CategoryString = CategoryName.ToString();
		const FText CategoryText(FText::AsCultureInvariant(CategoryString));

		const TSharedRef<SWidget> TextBlock = SNew(STextBlock)
			.Text(FText::AsCultureInvariant(CategoryString))
			.ShadowColorAndOpacity(FLinearColor(0.05f, 0.05f, 0.05f, 1.0f))
			.ShadowOffset(FVector2D(1.0f, 1.0f))
			.ColorAndOpacity(FSlateColor(FTimeMarkerTrackBuilder::GetColorByCategory(*CategoryString)));

		MenuBuilder.AddMenuEntry(
			FUIAction(FExecuteAction::CreateSP(this, &SLogView::ToggleCategory, CategoryName),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(this, &SLogView::IsLogCategoryEnabled, CategoryName)),
			TextBlock,
			NAME_None,
			FText::Format(LOCTEXT("Category_Tooltip", "Filter the Log View to show/hide category: {0}"), CategoryText),
			EUserInterfaceActionType::ToggleButton
		);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SLogView::VerbosityThreshold_IsChecked(ELogVerbosity::Type Verbosity) const
{
	return Verbosity == Filter.GetVerbosityThreshold();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLogView::VerbosityThreshold_Execute(ELogVerbosity::Type Verbosity)
{
	Filter.SetVerbosityThreshold(Verbosity);
	OnFilterChanged();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SLogView::VerbosityThreshold_GetSuffixGlyph(ELogVerbosity::Type Verbosity) const
{
	return Verbosity <= Filter.GetVerbosityThreshold() ? FEditorFontGlyphs::Check : FEditorFontGlyphs::Times;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FSlateColor SLogView::VerbosityThreshold_GetSuffixColor(ELogVerbosity::Type Verbosity) const
{
	return Verbosity <= Filter.GetVerbosityThreshold() ?
		FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f)) :
		FSlateColor(FLinearColor(1.0f, 0.0f, 0.0f, 1.0f));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SLogView::ShowHideAllCategories_IsChecked() const
{
	return Filter.IsShowAllCategoriesEnabled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLogView::ShowHideAllCategories_Execute()
{
	Filter.ToggleShowAllCategories();
	OnFilterChanged();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SLogView::IsLogCategoryEnabled(FName InName) const
{
	return Filter.IsLogCategoryEnabled(InName);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLogView::ToggleCategory(FName InName)
{
	Filter.ToggleLogCategory(InName);
	OnFilterChanged();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SLogView::CanHideSelectedCategory() const
{
	return ListView->GetSelectedItems().Num() == 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLogView::HideSelectedCategory()
{
	TSharedPtr<FLogMessage> SelectedLogMessage = GetSelectedLogMessage();
	if (SelectedLogMessage.IsValid())
	{
		FLogMessageRecord& Record = Cache.Get(SelectedLogMessage->GetIndex());
		FName CategoryName(Record.GetCategory());
		Filter.DisableLogCategory(CategoryName);
		OnFilterChanged();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SLogView::CanShowOnlySelectedCategory() const
{
	return ListView->GetSelectedItems().Num() == 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLogView::ShowOnlySelectedCategory()
{
	TSharedPtr<FLogMessage> SelectedLogMessage = GetSelectedLogMessage();
	if (SelectedLogMessage.IsValid())
	{
		FLogMessageRecord& Record = Cache.Get(SelectedLogMessage->GetIndex());
		FName CategoryName(Record.GetCategory());
		Filter.EnableOnlyCategory(CategoryName);
		OnFilterChanged();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SLogView::CanShowAllCategories() const
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLogView::ShowAllCategories()
{
	if (Filter.IsShowAllCategoriesEnabled())
	{
		Filter.ToggleShowAllCategories(); // hide
		Filter.ToggleShowAllCategories(); // show
	}
	else
	{
		Filter.ToggleShowAllCategories(); // show
	}
	OnFilterChanged();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SLogView::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	return CommandList->ProcessCommandBindings(InKeyEvent) == true ? FReply::Handled() : FReply::Unhandled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLogView::AppendFormatMessageDetailed(const FLogMessageRecord& Log, TStringBuilderBase<TCHAR>& InOutStringBuilder) const
{
	InOutStringBuilder.Appendf(TEXT("Index=%d"), Log.GetIndex());
	InOutStringBuilder.Append(TEXT("\nTime="));
	InOutStringBuilder.Append(TimeUtils::FormatTimeHMS(Log.GetTime(), TimeUtils::Microsecond));
	InOutStringBuilder.Append(TEXT("\nVerbosity="));
	InOutStringBuilder.Append(::ToString(Log.GetVerbosity()));
	InOutStringBuilder.Append(TEXT("\nCategory="));
	InOutStringBuilder.Append(Log.GetCategoryAsString());
	InOutStringBuilder.Append(TEXT("\nMessage="));
	InOutStringBuilder.Append(Log.GetMessageAsString());
	InOutStringBuilder.Append(TEXT("\nFile="));
	InOutStringBuilder.Append(Log.GetFile());
	InOutStringBuilder.Appendf(TEXT("\nLine=%d\n"), Log.GetLine());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLogView::AppendFormatMessageDelimitedHeader(TStringBuilderBase<TCHAR>& InOutStringBuilder, TCHAR Separator) const
{
	InOutStringBuilder.Append(TEXT("Index"));
	InOutStringBuilder.Append(Separator);
	InOutStringBuilder.Append(TEXT("Time"));
	InOutStringBuilder.Append(Separator);
	InOutStringBuilder.Append(TEXT("Verbosity"));
	InOutStringBuilder.Append(Separator);
	InOutStringBuilder.Append(TEXT("Category"));
	InOutStringBuilder.Append(Separator);
	InOutStringBuilder.Append(TEXT("Message"));
	InOutStringBuilder.Append(Separator);
	InOutStringBuilder.Append(TEXT("File"));
	InOutStringBuilder.Append(Separator);
	InOutStringBuilder.Append(TEXT("Line"));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLogView::AppendFormatMessageDelimited(const FLogMessageRecord& Log, TStringBuilderBase<TCHAR>& InOutStringBuilder, TCHAR Separator) const
{
	InOutStringBuilder.Appendf(TEXT("%d"), Log.GetIndex());
	InOutStringBuilder.Append(Separator);
	InOutStringBuilder.Append(TimeUtils::FormatTimeHMS(Log.GetTime(), TimeUtils::Microsecond));
	InOutStringBuilder.Append(Separator);
	InOutStringBuilder.Append(::ToString(Log.GetVerbosity()));
	InOutStringBuilder.Append(Separator);
	InOutStringBuilder.Append(Log.GetCategoryAsString());
	InOutStringBuilder.Append(Separator);
	FString Message = Log.GetMessageAsString();
	if (Separator == TEXT('\t'))
	{
		Message.ReplaceCharInline(TEXT('\t'), TEXT(' '), ESearchCase::CaseSensitive);
		InOutStringBuilder.Append(Message);
	}
	else if (Separator == TEXT(','))
	{
		InOutStringBuilder.Append(TEXT('\"'));
		FString EscapedMessage = Message.Replace(TEXT("\""), TEXT("\"\""), ESearchCase::CaseSensitive);
		InOutStringBuilder.Append(EscapedMessage);
		InOutStringBuilder.Append(TEXT('\"'));
	}
	else
	{
		InOutStringBuilder.Append(Message);
	}
	InOutStringBuilder.Append(Separator);
	InOutStringBuilder.Append(Log.GetFile());
	InOutStringBuilder.Append(Separator);
	InOutStringBuilder.Appendf(TEXT("%d"), Log.GetLine());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SLogView::CanCopySelected() const
{
	return ListView->GetSelectedItems().Num() == 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLogView::CopySelected() const
{
	TSharedPtr<FLogMessage> SelectedLogMessage = GetSelectedLogMessage();
	if (SelectedLogMessage)
	{
		const FLogMessageRecord& Log = Cache.Get(SelectedLogMessage->GetIndex());
		TStringBuilder<1024> StringBuilder;
		AppendFormatMessageDetailed(Log, StringBuilder);
		FPlatformApplicationMisc::ClipboardCopy(StringBuilder.ToString());
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SLogView::CanCopyMessage() const
{
	return ListView->GetSelectedItems().Num() == 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLogView::CopyMessage() const
{
	TSharedPtr<FLogMessage> SelectedLogMessage = GetSelectedLogMessage();
	if (SelectedLogMessage)
	{
		const FLogMessageRecord& Log = Cache.Get(SelectedLogMessage->GetIndex());
		FPlatformApplicationMisc::ClipboardCopy(*Log.GetMessageAsString());
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SLogView::CanCopyRange() const
{
	if (Messages.Num() == 0)
	{
		return false;
	}

	TSharedPtr<STimingProfilerWindow> Window = FTimingProfilerManager::Get()->GetProfilerWindow();
	TSharedPtr<STimingView> TimingView = Window ? Window->GetTimingView() : nullptr;
	if (!TimingView)
	{
		return false;
	}

	const double SelectionStartTime = TimingView->GetSelectionStartTime();
	const double SelectionEndTime = TimingView->GetSelectionEndTime();
	return SelectionStartTime < SelectionEndTime;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLogView::CopyRange() const
{
	if (Messages.Num() == 0)
	{
		return;
	}

	TSharedPtr<STimingProfilerWindow> Window = FTimingProfilerManager::Get()->GetProfilerWindow();
	TSharedPtr<STimingView> TimingView = Window ? Window->GetTimingView() : nullptr;
	if (!TimingView)
	{
		return;
	}

	const double SelectionStartTime = TimingView->GetSelectionStartTime();
	const double SelectionEndTime = TimingView->GetSelectionEndTime();
	if (SelectionStartTime >= SelectionEndTime)
	{
		return;
	}

	TStringBuilder<1024> StringBuilder;
	AppendFormatMessageDelimitedHeader(StringBuilder, TEXT('\t'));
	StringBuilder.Append(TEXT('\n'));

	int32 NumMessagesInSelectedRange = 0;

	TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid())
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
		const TraceServices::ILogProvider& LogProvider = TraceServices::ReadLogProvider(*Session.Get());

		for (const TSharedPtr<FLogMessage>& Message : Messages)
		{
			LogProvider.ReadMessage(Message->GetIndex(), [this, SelectionStartTime, SelectionEndTime, &StringBuilder, &NumMessagesInSelectedRange](const TraceServices::FLogMessageInfo& MessageInfo)
			{
				if (MessageInfo.Time >= SelectionStartTime && MessageInfo.Time <= SelectionEndTime)
				{
					FLogMessageRecord Log(MessageInfo);
					AppendFormatMessageDelimited(Log, StringBuilder, TEXT('\t'));
					StringBuilder.Append(TEXT('\n'));
					++NumMessagesInSelectedRange;
				}
			});
		}
	}

	if (NumMessagesInSelectedRange > 0)
	{
		FPlatformApplicationMisc::ClipboardCopy(StringBuilder.ToString());
		UE_LOG(TraceInsights, Log, TEXT("Copied %d logs to clipboard."), NumMessagesInSelectedRange);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SLogView::CanCopyAll() const
{
	return Messages.Num() > 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLogView::CopyAll() const
{
	if (Messages.Num() == 0)
	{
		return;
	}

	TStringBuilder<1024> StringBuilder;
	AppendFormatMessageDelimitedHeader(StringBuilder, TEXT('\t'));
	StringBuilder.Append(TEXT('\n'));

	TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid())
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
		const TraceServices::ILogProvider& LogProvider = TraceServices::ReadLogProvider(*Session.Get());

		for (const TSharedPtr<FLogMessage>& Message : Messages)
		{
			LogProvider.ReadMessage(Message->GetIndex(), [this, &StringBuilder](const TraceServices::FLogMessageInfo& MessageInfo)
			{
				FLogMessageRecord Log(MessageInfo);
				AppendFormatMessageDelimited(Log, StringBuilder, TEXT('\t'));
				StringBuilder.Append(TEXT('\n'));
			});
		}
	}

	FPlatformApplicationMisc::ClipboardCopy(StringBuilder.ToString());
	UE_LOG(TraceInsights, Log, TEXT("Copied %d logs to clipboard."), Messages.Num());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SLogView::CanSaveRange() const
{
	return CanCopyRange();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLogView::SaveRange() const
{
	constexpr bool bSaveLogsInSelectedRangeOnly = true;
	SaveLogsToFile(bSaveLogsInSelectedRangeOnly);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SLogView::CanSaveAll() const
{
	return Messages.Num() > 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLogView::SaveAll() const
{
	constexpr bool bSaveLogsInSelectedRangeOnly = false;
	SaveLogsToFile(bSaveLogsInSelectedRangeOnly);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SLogView::SaveLogsToFile(bool bSaveLogsInSelectedRangeOnly) const
{
	if (Messages.Num() == 0)
	{
		return;
	}

	double SelectionStartTime = 0.0;
	double SelectionEndTime = 0.0;
	if (bSaveLogsInSelectedRangeOnly)
	{
		TSharedPtr<STimingProfilerWindow> Window = FTimingProfilerManager::Get()->GetProfilerWindow();
		TSharedPtr<STimingView> TimingView = Window ? Window->GetTimingView() : nullptr;
		if (!TimingView)
		{
			return;
		}

		SelectionStartTime = TimingView->GetSelectionStartTime();
		SelectionEndTime = TimingView->GetSelectionEndTime();
		if (SelectionStartTime >= SelectionEndTime)
		{
			return;
		}
	}

	TArray<FString> SaveFilenames;
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	bool bDialogResult = false;
	if (DesktopPlatform)
	{
		const FString DefaultBrowsePath = FPaths::ProjectLogDir();
		bDialogResult = DesktopPlatform->SaveFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			LOCTEXT("SaveLogsToFileTitle", "Save Logs").ToString(),
			DefaultBrowsePath,
			TEXT(""),
			TEXT("Tab-Separated Values (*.tsv)|*.tsv|Text Files (*.txt)|*.txt|Comma-Separated Values (*.csv)|*.csv|All Files (*.*)|*.*"),
			EFileDialogFlags::None,
			SaveFilenames
		);
	}

	if (!bDialogResult || SaveFilenames.Num() == 0)
	{
		return;
	}

	FString& Path = SaveFilenames[0];
	TCHAR Separator = TEXT('\t');
	if (Path.EndsWith(TEXT(".csv")))
	{
		Separator = TEXT(',');
	}

	IFileHandle* ExportFileHandle = FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*Path);
	if (ExportFileHandle == nullptr)
	{
		FMessageLog ReportMessageLog(FInsightsManager::Get()->GetLogListingName());
		ReportMessageLog.Error(LOCTEXT("FailedToOpenFile", "Save logs failed. Failed to open file for write."));
		ReportMessageLog.Notify();
		return;
	}

	FStopwatch Stopwatch;
	Stopwatch.Start();

	UTF16CHAR BOM = UNICODE_BOM;
	ExportFileHandle->Write((uint8*)&BOM, sizeof(UTF16CHAR));

	// Write header.
	{
		TStringBuilder<1024> StringBuilder;
		AppendFormatMessageDelimitedHeader(StringBuilder, Separator);
		StringBuilder.Append(TEXT('\n'));
		FTCHARToUTF16 UTF16String(StringBuilder.ToString(), StringBuilder.Len());
		ExportFileHandle->Write((const uint8*)UTF16String.Get(), UTF16String.Length() * sizeof(UTF16CHAR));
	}

	int32 NumMessagesInSelectedRange = 0;

	TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid())
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
		const TraceServices::ILogProvider& LogProvider = TraceServices::ReadLogProvider(*Session.Get());

		for (const TSharedPtr<FLogMessage>& Message : Messages)
		{
			LogProvider.ReadMessage(Message->GetIndex(), [this, bSaveLogsInSelectedRangeOnly, SelectionStartTime, SelectionEndTime, Separator, ExportFileHandle, &NumMessagesInSelectedRange](const TraceServices::FLogMessageInfo& MessageInfo)
			{
				if (!bSaveLogsInSelectedRangeOnly || (MessageInfo.Time >= SelectionStartTime && MessageInfo.Time <= SelectionEndTime))
				{
					TStringBuilder<1024> StringBuilder;
					FLogMessageRecord Log(MessageInfo);
					AppendFormatMessageDelimited(Log, StringBuilder, Separator);
					StringBuilder.Append(TEXT('\n'));
					FTCHARToUTF16 UTF16String(StringBuilder.ToString(), StringBuilder.Len());
					ExportFileHandle->Write((const uint8*)UTF16String.Get(), UTF16String.Length() * sizeof(UTF16CHAR));
					++NumMessagesInSelectedRange;
				}
			});
		}
	}

	ExportFileHandle->Flush();
	delete ExportFileHandle;
	ExportFileHandle = nullptr;

	Stopwatch.Stop();
	const double TotalTime = Stopwatch.GetAccumulatedTime();
	UE_LOG(TraceInsights, Log, TEXT("Saved %d logs to file in %.3fs."), NumMessagesInSelectedRange, TotalTime);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
