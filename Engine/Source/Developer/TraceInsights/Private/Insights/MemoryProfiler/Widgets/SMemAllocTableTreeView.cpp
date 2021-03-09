// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMemAllocTableTreeView.h"

#include "EditorStyleSet.h"
#include "SlateOptMacros.h"
#include "Styling/CoreStyle.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/Model/AllocationsProvider.h"
#include "TraceServices/Model/Callstack.h"
#include "TraceServices/Model/Modules.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SToolTip.h"

// Insights
#include "Insights/MemoryProfiler/MemoryProfilerManager.h"
#include "Insights/MemoryProfiler/ViewModels/MemAllocGroupingByCallstack.h"
#include "Insights/MemoryProfiler/ViewModels/MemAllocGroupingBySize.h"
#include "Insights/MemoryProfiler/ViewModels/MemAllocNode.h"
#include "Insights/MemoryProfiler/ViewModels/MemAllocTable.h"
#include "Insights/MemoryProfiler/ViewModels/MemorySharedState.h"
#include "Insights/MemoryProfiler/Widgets/SMemoryProfilerWindow.h"
#include "Insights/Table/ViewModels/TableCellValueFormatter.h"
#include "Insights/Table/ViewModels/TableColumn.h"
#include "Insights/TimingProfilerCommon.h"
#include "Insights/ViewModels/FilterConfigurator.h"

#include <limits>

#define LOCTEXT_NAMESPACE "SMemAllocTableTreeView"

using namespace TraceServices;

namespace Insights
{

const int SMemAllocTableTreeView::FullCallStackIndex = 0x0000FFFFF;

////////////////////////////////////////////////////////////////////////////////////////////////////

SMemAllocTableTreeView::SMemAllocTableTreeView()
{
	bRunInAsyncMode = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

SMemAllocTableTreeView::~SMemAllocTableTreeView()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemAllocTableTreeView::Construct(const FArguments& InArgs, TSharedPtr<Insights::FMemAllocTable> InTablePtr)
{
	ConstructWidget(InTablePtr);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemAllocTableTreeView::Reset()
{
	//...
	STableTreeView::Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/*
void SMemAllocTableTreeView::UpdateSourceTable(TSharedPtr<TraceServices::IMemAllocTable> SourceTable)
{
	//check(Table->Is<Insights::FMemAllocTable>());
	TSharedPtr<Insights::FMemAllocTable> MemAllocTable = StaticCastSharedPtr<Insights::FMemAllocTable>(Table);

	if (MemAllocTable->UpdateSourceTable(SourceTable))
	{
		RebuildColumns();
	}

	RebuildTree(true);
}
*/
////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemAllocTableTreeView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	STableTreeView::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (bHasPendingQueryReset && !bIsUpdateRunning)
	{
		ResetAndStartQuery();
		bHasPendingQueryReset = false;
	}

	if (!bIsUpdateRunning)
	{
		RebuildTree(false);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemAllocTableTreeView::RebuildTree(bool bResync)
{
	FStopwatch SyncStopwatch;
	FStopwatch Stopwatch;
	Stopwatch.Start();

	if (bResync)
	{
		TableTreeNodes.Empty();
	}

	const int32 PreviousNodeCount = TableTreeNodes.Num();

	TSharedPtr<Insights::FMemAllocTable> MemAllocTable = GetMemAllocTable();

	SyncStopwatch.Start();
	if (Session.IsValid() && MemAllocTable.IsValid())
	{
		TraceServices::IAllocationsProvider::EQueryStatus QueryStatus;
		UpdateQuery(QueryStatus);

		if (QueryStatus == TraceServices::IAllocationsProvider::EQueryStatus::Done)
		{
			UE_LOG(MemoryProfiler, Log, TEXT("[MemAlloc] Rebuilding tree..."));
			TArray<FMemoryAlloc>& Allocs = MemAllocTable->GetAllocs();

			const int32 TotalAllocCount = Allocs.Num();
			if (TotalAllocCount != TableTreeNodes.Num())
			{
				if (TableTreeNodes.Num() > TotalAllocCount)
				{
					TableTreeNodes.Empty();
				}
				TableTreeNodes.Reserve(TotalAllocCount);

				FName BaseNodeName(TEXT("alloc"));
				for (int32 AllocIndex = TableTreeNodes.Num(); AllocIndex < TotalAllocCount; ++AllocIndex)
				{
					FName NodeName(BaseNodeName, AllocIndex + 1);
					FMemAllocNodePtr NodePtr = MakeShared<FMemAllocNode>(NodeName, MemAllocTable, AllocIndex);
					TableTreeNodes.Add(NodePtr);
				}
				ensure(TableTreeNodes.Num() == TotalAllocCount);
			}
		}
	}
	SyncStopwatch.Stop();

	if (bResync || TableTreeNodes.Num() != PreviousNodeCount)
	{
		// Save selection.
		TArray<FTableTreeNodePtr> SelectedItems;
		TreeView->GetSelectedItems(SelectedItems);

		UpdateTree();

		TreeView->RebuildList();

		// Restore selection.
		if (SelectedItems.Num() > 0)
		{
			TreeView->ClearSelection();
			for (FTableTreeNodePtr& NodePtr : SelectedItems)
			{
				NodePtr = GetNodeByTableRowIndex(NodePtr->GetRowIndex());
			}
			SelectedItems.RemoveAll([](const FTableTreeNodePtr& NodePtr) { return !NodePtr.IsValid(); });
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
		UE_LOG(MemoryProfiler, Log, TEXT("[MemAlloc] Tree view rebuilt in %.3fs (%.3fs + %.3fs) --> %d allocs (%d added)"),
			TotalTime, SyncTime, TotalTime - SyncTime, TableTreeNodes.Num(), TableTreeNodes.Num() - PreviousNodeCount);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemAllocTableTreeView::OnQueryInvalidated()
{
	CancelQuery();

	if (bIsUpdateRunning)
	{
		bHasPendingQueryReset = true;
	}
	else
	{
		ResetAndStartQuery();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemAllocTableTreeView::ResetAndStartQuery()
{
	TableTreeNodes.Reset();

	TSharedPtr<Insights::FMemAllocTable> MemAllocTable = GetMemAllocTable();
	if (MemAllocTable)
	{
		TArray<FMemoryAlloc>& Allocs = MemAllocTable->GetAllocs();
		Allocs.Reset(10 * 1024 * 1024);
	}

	UpdateQueryInfo();

	StartQuery();

	RebuildTree(true);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemAllocTableTreeView::StartQuery()
{
	check(Query == 0);

	if (!Rule)
	{
		UE_LOG(MemoryProfiler, Warning, TEXT("[MemAlloc] Invalid query rule!"));
		return;
	}

	const TraceServices::IAllocationsProvider* AllocationsProvider = TraceServices::ReadAllocationsProvider(*Session.Get());
	if (!AllocationsProvider)
	{
		UE_LOG(MemoryProfiler, Warning, TEXT("[MemAlloc] Invalid allocations provider!"));
		return;
	}

	{
		const TraceServices::IAllocationsProvider& Provider = *AllocationsProvider;
		TraceServices::IAllocationsProvider::FReadScopeLock _(Provider);
		TraceServices::IAllocationsProvider::FQueryParams Params = { Rule->GetValue(), TimeMarkers[0], TimeMarkers[1], TimeMarkers[2], TimeMarkers[3] };
		Query = Provider.StartQuery(Params);
	}

	if (Query == 0)
	{
		UE_LOG(MemoryProfiler, Error, TEXT("[MemAlloc] Unsupported query rule (%s)!"), *Rule->GetShortName().ToString());
	}
	else
	{
		QueryStopwatch.Reset();
		QueryStopwatch.Start();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemAllocTableTreeView::UpdateQuery(TraceServices::IAllocationsProvider::EQueryStatus& OutStatus)
{
	if (Query == 0)
	{
		OutStatus = TraceServices::IAllocationsProvider::EQueryStatus::Unknown;
		return;
	}

	const TraceServices::IAllocationsProvider* AllocationsProvider = TraceServices::ReadAllocationsProvider(*Session.Get());
	if (!AllocationsProvider)
	{
		UE_LOG(MemoryProfiler, Warning, TEXT("[MemAlloc] Invalid allocations provider!"));
		return;
	}
	const TraceServices::IAllocationsProvider& Provider = *AllocationsProvider;

	constexpr double MaxPollTime = 0.03; // Stop getting results after 30 ms so we don't tank the frame rate too much.
	FStopwatch TotalStopwatch;
	TotalStopwatch.Start();

	do
	{
		TraceServices::IAllocationsProvider::FQueryStatus Status = Provider.PollQuery(Query);
		OutStatus = Status.Status;

		if (Status.Status <= TraceServices::IAllocationsProvider::EQueryStatus::Done)
		{
			UE_LOG(MemoryProfiler, Log, TEXT("[MemAlloc] Query completed."));
			Query = 0;
			QueryStopwatch.Stop();
			return;
		}

		if (Status.Status == IAllocationsProvider::EQueryStatus::Working)
		{
			break;
		}

		check(Status.Status == IAllocationsProvider::EQueryStatus::Available);

		TSharedPtr<Insights::FMemAllocTable> MemAllocTable = GetMemAllocTable();
		if (MemAllocTable)
		{
			TraceServices::IAllocationsProvider::FReadScopeLock _(Provider);

			TArray<FMemoryAlloc>& Allocs = MemAllocTable->GetAllocs();

			FStopwatch ResultStopwatch;
			FStopwatch PageStopwatch;
			ResultStopwatch.Start();
			uint32 PageCount = 0;
			uint32 TotalAllocCount = 0;

			// Multiple 'pages' of results will be returned. No guarantees are made
			// about the order of pages or the allocations they report.
			TraceServices::IAllocationsProvider::FQueryResult Result = Status.NextResult();
			while (Result.IsValid())
			{
				UE_LOG(MemoryProfiler, Log, TEXT("[MemAlloc] Page with %u allocs..."), Result->Num());

				++PageCount;
				PageStopwatch.Restart();

				const uint32 AllocCount = Result->Num();
				TotalAllocCount += AllocCount;

				uint64 AllocsDestIndex = Allocs.Num();
				Allocs.AddUninitialized(AllocCount);
				for (uint32 AllocIndex = 0; AllocIndex < AllocCount; ++AllocIndex, ++AllocsDestIndex)
				{
					const TraceServices::IAllocationsProvider::FAllocation* Allocation = Result->Get(AllocIndex);
					FMemoryAlloc& Alloc = Allocs[AllocsDestIndex];
					Alloc.StartTime = Allocation->GetStartTime();
					Alloc.EndTime = Allocation->GetEndTime();
					Alloc.Address = Allocation->GetAddress();
					Alloc.Size = Allocation->GetSize();
					Alloc.Tag = Provider.GetTagName(Allocation->GetTag());
					Alloc.Callstack = Allocation->GetCallstack();
				}

				PageStopwatch.Stop();
				const double PageTime = PageStopwatch.GetAccumulatedTime();
				if (PageTime > 0.01)
				{
					const double Speed = (PageTime * 1000000.0) / AllocCount;
					UE_LOG(MemoryProfiler, Log, TEXT("[MemAlloc] Query result for page %u (%u allocs, slack=%u) retrieved in %.3fs (speed: %.3f seconds per 1M allocs)."), PageCount, AllocCount, Allocs.GetSlack(), PageTime, Speed);
				}

				Result = Status.NextResult();
			}

			ResultStopwatch.Stop();
			const double TotalTime = ResultStopwatch.GetAccumulatedTime();
			if (TotalTime > 0.01)
			{
				const double Speed = (TotalTime * 1000000.0) / TotalAllocCount;
				UE_LOG(MemoryProfiler, Log, TEXT("[MemAlloc] Query results (%u pages, %u allocs, slack=%u) retrieved in %.3fs (speed: %.3f seconds per 1M allocs)."), PageCount, TotalAllocCount, Allocs.GetSlack(), TotalTime, Speed);
			}
		}

		TotalStopwatch.Update();
	}
	while (OutStatus == TraceServices::IAllocationsProvider::EQueryStatus::Available && TotalStopwatch.GetAccumulatedTime() < MaxPollTime);

	TotalStopwatch.Stop();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemAllocTableTreeView::CancelQuery()
{
	if (Query != 0)
	{
		const TraceServices::IAllocationsProvider* AllocationsProvider = TraceServices::ReadAllocationsProvider(*Session.Get());
		if (AllocationsProvider)
		{
			AllocationsProvider->CancelQuery(Query);
			UE_LOG(MemoryProfiler, Log, TEXT("[MemAlloc] Query canceled."));
		}

		Query = 0;
		QueryStopwatch.Stop();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SMemAllocTableTreeView::IsRunning() const
{
	return Query != 0 || STableTreeView::IsRunning();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

double SMemAllocTableTreeView::GetAllOperationsDuration()
{
	if (Query != 0)
	{
		QueryStopwatch.Update();
		return QueryStopwatch.GetAccumulatedTime();
	}

	return STableTreeView::GetAllOperationsDuration();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SMemAllocTableTreeView::GetCurrentOperationName() const
{
	if (Query != 0)
	{
		return LOCTEXT("CurrentOperationName", "Running Query");
	}

	return STableTreeView::GetCurrentOperationName();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedPtr<SWidget> SMemAllocTableTreeView::ConstructToolbar()
{
	return
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("DetailedBtn_Text", "Detailed"))
			.ToolTipText(LOCTEXT("DetailedBtn_Tooltip", "Detailed View\nConfigure the tree view to show detailed allocation info."))
			.OnClicked(this, &SMemAllocTableTreeView::OnDetailedViewClicked)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("BySizeBtn_Text", "By Size"))
			.ToolTipText(LOCTEXT("BySizeBtn_Tooltip", "Size Breakdown View\nConfigure the tree view to show a breakdown of allocations by their size."))
			.OnClicked(this, &SMemAllocTableTreeView::OnSizeViewClicked)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("ByTagBtn_Text", "By Tag"))
			.ToolTipText(LOCTEXT("ByTagBtn_Tooltip", "Tag Breakdown View\nConfigure the tree view to show a breakdown of allocations by their LLM tags."))
			.OnClicked(this, &SMemAllocTableTreeView::OnTagViewClicked)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("ByCallstackBtn_Text", "By Callstack"))
			.ToolTipText(LOCTEXT("ByCallstackBtn_Tooltip", "Callstack Breakdown View\nConfigure the tree view to show a breakdown of allocations by callstack."))
			.OnClicked(this, &SMemAllocTableTreeView::OnCallstackViewClicked, false)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("ByInvertedCallstackBtn_Text", "By Inverted Callstack"))
			.ToolTipText(LOCTEXT("ByInvertedCallstackBtn_Tooltip", "Inverted Callstack Breakdown View\nConfigure the tree view to show a breakdown of allocations by inverted callstack."))
			.OnClicked(this, &SMemAllocTableTreeView::OnCallstackViewClicked, true)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SCheckBox)
			.Style(FCoreStyle::Get(), "ToggleButtonCheckbox")
			.HAlign(HAlign_Center)
			.Padding(FMargin(2.0f, 4.0f, 2.0f, 0.0f))
			.OnCheckStateChanged(this, &SMemAllocTableTreeView::CallstackGroupingByFunction_OnCheckStateChanged)
			.IsChecked(this, &SMemAllocTableTreeView::CallstackGroupingByFunction_IsChecked)
			.ToolTip(
				SNew(SToolTip)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(2.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("CallstackGroupingByFunction_Tooltip_Title", "Callstack Grouping by Function Name"))
						.TextStyle(FEditorStyle::Get(), TEXT("Profiler.TooltipBold"))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(2.0f, 8.0f, 2.0f, 2.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("CallstackGroupingByFunction_Tooltip_Content", "If enabled, the callstack grouping will create a single group node per function name.\nExample 1: When two callstack frames are located in same function, but at different line numbers; \nExample 2: When a function is called recursively.\nOtherwise it will create separate group nodes for each unique callstack frame."))
						.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(2.0f, 8.0f, 2.0f, 2.0f)
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Top)
						.Padding(0.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("CallstackGroupingByFunction_Warning", "Warning:"))
							.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
							.ColorAndOpacity(FLinearColor(1.0f, 0.6f, 0.3f, 1.0f))
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(4.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("CallstackGroupingByFunction_Warning_Content", "When this option is enabled, the tree nodes that have merged multiple callstack frames\nwill show in their tooltips the source file name and the line number of an arbitrary\ncallstack frame from ones merged by respective tree node."))
							.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
						]
					]
				])
			[
				SNew(STextBlock)
				.Text(LOCTEXT("CallstackGroupingByFunction_Text", " Fn "))
			]
		]
	;
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SMemAllocTableTreeView::OnDetailedViewClicked()
{
	ColumnBeingSorted = FTable::GetHierarchyColumnId();
	ColumnSortMode = EColumnSortMode::Type::Ascending;
	UpdateCurrentSortingByColumn();

	PreChangeGroupings();

	CurrentGroupings.Reset();

	check(AvailableGroupings[0]->Is<FTreeNodeGroupingFlat>());
	CurrentGroupings.Add(AvailableGroupings[0]);

	PostChangeGroupings();

	FColumnConfig Preset[] =
	{
		{ FTable::GetHierarchyColumnId(),           true,  200.0f },
		{ FMemAllocTableColumns::StartTimeColumnId, true,  100.0f },
		{ FMemAllocTableColumns::EndTimeColumnId,   true,  100.0f },
		{ FMemAllocTableColumns::DurationColumnId,  true,  100.0f },
		{ FMemAllocTableColumns::AddressColumnId,   true,  120.0f },
		{ FMemAllocTableColumns::CountColumnId,     true,  100.0f },
		{ FMemAllocTableColumns::SizeColumnId,      true,  100.0f },
		{ FMemAllocTableColumns::TagColumnId,       true,  120.0f },
		{ FMemAllocTableColumns::FunctionColumnId,  true,  550.0f },
	};
	ApplyColumnConfig(TArrayView<FColumnConfig>(Preset, UE_ARRAY_COUNT(Preset)));

	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SMemAllocTableTreeView::OnSizeViewClicked()
{
	ColumnBeingSorted = FMemAllocTableColumns::SizeColumnId;
	ColumnSortMode = EColumnSortMode::Type::Descending;
	UpdateCurrentSortingByColumn();

	PreChangeGroupings();

	CurrentGroupings.Reset();

	check(AvailableGroupings[0]->Is<FTreeNodeGroupingFlat>());
	CurrentGroupings.Add(AvailableGroupings[0]);

	TSharedPtr<FTreeNodeGrouping>* SizeGrouping = AvailableGroupings.FindByPredicate(
		[](TSharedPtr<FTreeNodeGrouping>& Grouping)
		{
			return Grouping->Is<FMemAllocGroupingBySize>();
		});
	if (SizeGrouping)
	{
		CurrentGroupings.Add(*SizeGrouping);
	}

	PostChangeGroupings();

	FColumnConfig Preset[] =
	{
		{ FTable::GetHierarchyColumnId(),           true,  200.0f },
		{ FMemAllocTableColumns::StartTimeColumnId, false, 0.0f },
		{ FMemAllocTableColumns::EndTimeColumnId,   false, 0.0f },
		{ FMemAllocTableColumns::DurationColumnId,  false, 0.0f },
		{ FMemAllocTableColumns::AddressColumnId,   false, 0.0f },
		{ FMemAllocTableColumns::CountColumnId,     true,  100.0f },
		{ FMemAllocTableColumns::SizeColumnId,      true,  100.0f },
		{ FMemAllocTableColumns::TagColumnId,       true,  120.0f },
		{ FMemAllocTableColumns::FunctionColumnId,  true,  400.0f },
	};
	ApplyColumnConfig(TArrayView<FColumnConfig>(Preset, UE_ARRAY_COUNT(Preset)));

	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SMemAllocTableTreeView::OnTagViewClicked()
{
	ColumnBeingSorted = FTable::GetHierarchyColumnId();
	ColumnSortMode = EColumnSortMode::Type::Ascending;
	UpdateCurrentSortingByColumn();

	PreChangeGroupings();

	CurrentGroupings.Reset();

	check(AvailableGroupings[0]->Is<FTreeNodeGroupingFlat>());
	CurrentGroupings.Add(AvailableGroupings[0]);

	TSharedPtr<FTreeNodeGrouping>* TagGrouping = AvailableGroupings.FindByPredicate(
		[](TSharedPtr<FTreeNodeGrouping>& Grouping)
		{
			return Grouping->Is<FTreeNodeGroupingByUniqueValue>() &&
				   Grouping->As<FTreeNodeGroupingByUniqueValue>().GetColumnId() == FMemAllocTableColumns::TagColumnId;
		});
	if (TagGrouping)
	{
		CurrentGroupings.Add(*TagGrouping);
	}

	PostChangeGroupings();

	FColumnConfig Preset[] =
	{
		{ FTable::GetHierarchyColumnId(),           true,  200.0f },
		{ FMemAllocTableColumns::StartTimeColumnId, false, 0.0f },
		{ FMemAllocTableColumns::EndTimeColumnId,   false, 0.0f },
		{ FMemAllocTableColumns::DurationColumnId,  false, 0.0f },
		{ FMemAllocTableColumns::AddressColumnId,   false, 0.0f },
		{ FMemAllocTableColumns::CountColumnId,     true,  100.0f },
		{ FMemAllocTableColumns::SizeColumnId,      true,  100.0f },
		{ FMemAllocTableColumns::TagColumnId,       false, 0.0f },
		{ FMemAllocTableColumns::FunctionColumnId,  true,  400.0f },
	};
	ApplyColumnConfig(TArrayView<FColumnConfig>(Preset, UE_ARRAY_COUNT(Preset)));

	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SMemAllocTableTreeView::OnCallstackViewClicked(bool bIsInverted)
{
	ColumnBeingSorted = FMemAllocTableColumns::SizeColumnId;
	ColumnSortMode = EColumnSortMode::Type::Descending;
	UpdateCurrentSortingByColumn();

	PreChangeGroupings();

	CurrentGroupings.Reset();

	check(AvailableGroupings[0]->Is<FTreeNodeGroupingFlat>());
	CurrentGroupings.Add(AvailableGroupings[0]);

	TSharedPtr<FTreeNodeGrouping>* CallstackGrouping = AvailableGroupings.FindByPredicate(
		[bIsInverted](TSharedPtr<FTreeNodeGrouping>& Grouping)
		{
			return Grouping->Is<FMemAllocGroupingByCallstack>() &&
				   Grouping->As<FMemAllocGroupingByCallstack>().IsInverted() == bIsInverted;
		});
	if (CallstackGrouping)
	{
		CurrentGroupings.Add(*CallstackGrouping);
	}

	PostChangeGroupings();

	FColumnConfig Preset[] =
	{
		{ FTable::GetHierarchyColumnId(),           true,  400.0f },
		{ FMemAllocTableColumns::StartTimeColumnId, false, 0.0f },
		{ FMemAllocTableColumns::EndTimeColumnId,   false, 0.0f },
		{ FMemAllocTableColumns::DurationColumnId,  false, 0.0f },
		{ FMemAllocTableColumns::AddressColumnId,   false, 0.0f },
		{ FMemAllocTableColumns::CountColumnId,     true,  100.0f },
		{ FMemAllocTableColumns::SizeColumnId,      true,  100.0f },
		{ FMemAllocTableColumns::TagColumnId,       true,  200.0f },
		{ FMemAllocTableColumns::FunctionColumnId,  true,  200.0f },
	};
	ApplyColumnConfig(TArrayView<FColumnConfig>(Preset, UE_ARRAY_COUNT(Preset)));

	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemAllocTableTreeView::ApplyColumnConfig(const TArrayView<FColumnConfig>& Preset)
{
	for (const TSharedRef<FTableColumn>& ColumnRef : Table->GetColumns())
	{
		FTableColumn& Column = ColumnRef.Get();
		for (const FColumnConfig& Config : Preset)
		{
			if (Column.GetId() == Config.ColumnId)
			{
				if (Config.bIsVisible)
				{
					ShowColumn(Column);
					if (Config.Width > 0.0f)
					{
						TreeViewHeaderRow->SetColumnWidth(Column.GetId(), Config.Width);
					}
				}
				else
				{
					HideColumn(Column);
				}
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedPtr<SWidget> SMemAllocTableTreeView::ConstructFooter()
{
	return
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Left)
		.Padding(2.0f)
		[
			SNew(STextBlock)
			.Text(this, &SMemAllocTableTreeView::GetQueryInfo)
			.ToolTipText(this, &SMemAllocTableTreeView::GetQueryInfoTooltip)
		]

		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Right)
		.Padding(2.0f)
		[
			SNew(STextBlock)
			.Text(this, &SMemAllocTableTreeView::GetSymbolResolutionStatus)
		];
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SMemAllocTableTreeView::GetSymbolResolutionStatus() const
{
	auto ModuleProvider = Session->ReadProvider<IModuleProvider>(FName("ModuleProvider"));

	IModuleProvider::FStats Stats;
	if (ModuleProvider)
	{
		ModuleProvider->GetStats(&Stats);
		return FText::Format(LOCTEXT("SymbolsResolved", "{0} / {1} symbols resolved. {2} failed."), Stats.SymbolsResolved, Stats.SymbolsDiscovered, Stats.SymbolsFailed);
	}
	else
	{
		return LOCTEXT("SymbolsResolutionNotPossible", "Symbol resolution was not possible.");
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SMemAllocTableTreeView::GetQueryInfo() const
{
	return QueryInfo;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SMemAllocTableTreeView::GetQueryInfoTooltip() const
{
	return QueryInfoTooltip;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemAllocTableTreeView::InternalCreateGroupings()
{
	STableTreeView::InternalCreateGroupings();

	int32 Index = 1; // after the Flat ("All") grouping
	
	AvailableGroupings.Insert(MakeShared<FMemAllocGroupingBySize>(), Index++);

	TSharedPtr<FTreeNodeGrouping>* TagGroupingPtr = AvailableGroupings.FindByPredicate(
		[](TSharedPtr<FTreeNodeGrouping>& Grouping)
		{
			return Grouping->Is<FTreeNodeGroupingByUniqueValue>() &&
				   Grouping->As<FTreeNodeGroupingByUniqueValue>().GetColumnId() == FMemAllocTableColumns::TagColumnId;
		});
	if (TagGroupingPtr)
	{
		TSharedPtr<FTreeNodeGroupingByUniqueValue> TagGrouping = StaticCastSharedPtr<FTreeNodeGroupingByUniqueValue>(*TagGroupingPtr);
		AvailableGroupings.Remove(TagGrouping);
		//TODO: TagGrouping->SetShortName(LOCTEXT("Grouping_ByTag_ShortName", "LLM Tag"));
		//TODO: TagGrouping->SetTitleName(LOCTEXT("Grouping_ByTag_TitleName", "By LLM Tag"));
		AvailableGroupings.Insert(TagGrouping, Index++);
	}

	AvailableGroupings.Insert(MakeShared<FMemAllocGroupingByCallstack>(false, bIsCallstackGroupingByFunction), Index++);
	AvailableGroupings.Insert(MakeShared<FMemAllocGroupingByCallstack>(true, bIsCallstackGroupingByFunction), Index++);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemAllocTableTreeView::UpdateQueryInfo()
{
	if (Rule.IsValid())
	{
		FText TimeMarkersText;

		const int NumMarkers = Rule->GetNumTimeMarkers();
		switch (NumMarkers)
		{
		case 1:
			TimeMarkersText = FText::Format(LOCTEXT("TimeMarkersFmt", "A={0}"), FText::AsNumber(TimeMarkers[0]));
			break;
		case 2:
			TimeMarkersText = FText::Format(LOCTEXT("TimeMarkersFmt", "A={0}  B={1}"), FText::AsNumber(TimeMarkers[0]), FText::AsNumber(TimeMarkers[1]));
			break;
		case 3:
			TimeMarkersText = FText::Format(LOCTEXT("TimeMarkersFmt", "A={0}  B={1}  C={2}"), FText::AsNumber(TimeMarkers[0]), FText::AsNumber(TimeMarkers[1]), FText::AsNumber(TimeMarkers[2]));
			break;
		case 4:
			TimeMarkersText = FText::Format(LOCTEXT("TimeMarkersFmt", "A={0}  B={1}  C={2}  D={3}"), FText::AsNumber(TimeMarkers[0]), FText::AsNumber(TimeMarkers[1]), FText::AsNumber(TimeMarkers[2]), FText::AsNumber(TimeMarkers[3]));
			break;
		default:
			// Unhandled value
			check(false);
		}

		QueryInfo = FText::Format(LOCTEXT("QueryInfoFmt", "{0} ({1})"), Rule->GetVerboseName(), TimeMarkersText);
		QueryInfoTooltip = Rule->GetDescription();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SMemAllocTableTreeView::ApplyCustomAdvancedFilters(const FTableTreeNodePtr& NodePtr)
{
	FMemAllocNodePtr MemNodePtr = StaticCastSharedPtr<FMemAllocNode>(NodePtr);
	Context.SetFilterData<FString>(FullCallStackIndex, MemNodePtr->GetFullCallstack().ToString());

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemAllocTableTreeView::AddCustomAdvancedFilters()
{
	TSharedPtr<TArray<TSharedPtr<struct FFilter>>>& AvailableFilters = FilterConfigurator->GetAvailableFilters();

	AvailableFilters->Add(MakeShared<FFilter>(FullCallStackIndex, LOCTEXT("FullCallstack", "Full Callstack"), LOCTEXT("FullCallstack", "Search in all the callstack frames"), EFilterDataType::String, FFilterService::Get()->GetStringOperators()));
	Context.AddFilterData<FString>(FullCallStackIndex, FString());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemAllocTableTreeView::CallstackGroupingByFunction_OnCheckStateChanged(ECheckBoxState NewRadioState)
{
	PreChangeGroupings();
	bIsCallstackGroupingByFunction = (NewRadioState == ECheckBoxState::Checked);
	for (TSharedPtr<FTreeNodeGrouping>& Grouping : AvailableGroupings)
	{
		if (Grouping->Is<FMemAllocGroupingByCallstack>())
		{
			Grouping->As<FMemAllocGroupingByCallstack>().SetGroupingByFunction(bIsCallstackGroupingByFunction);
		}
	}
	PostChangeGroupings();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

ECheckBoxState SMemAllocTableTreeView::CallstackGroupingByFunction_IsChecked() const
{
	return bIsCallstackGroupingByFunction ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
