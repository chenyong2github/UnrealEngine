// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMemAllocTableTreeView.h"

#include "TraceServices/AnalysisService.h"
#include "TraceServices/Model/AllocationsProvider.h"

// Insights
#include "Insights/MemoryProfiler/MemoryProfilerManager.h"
#include "Insights/MemoryProfiler/ViewModels/MemAllocNode.h"
#include "Insights/MemoryProfiler/ViewModels/MemAllocTable.h"
#include "Insights/MemoryProfiler/ViewModels/MemorySharedState.h"
#include "Insights/MemoryProfiler/Widgets/SMemoryProfilerWindow.h"
#include "Insights/Table/ViewModels/TableCellValueFormatter.h"
#include "Insights/Table/ViewModels/TableColumn.h"
#include "Insights/TimingProfilerCommon.h"

#include <limits>

#define LOCTEXT_NAMESPACE "SMemAllocTableTreeView"

using namespace TraceServices;

namespace Insights
{

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

	//check(Table->Is<Insights::FMemAllocTable>());
	//TSharedPtr<Insights::FMemAllocTable> MemAllocTable = StaticCastSharedPtr<Insights::FMemAllocTable>(Table);

	TSharedPtr<Insights::FMemAllocTable> MemAllocTable = GetMemAllocTable();

	SyncStopwatch.Start();
	if (Session.IsValid() && MemAllocTable.IsValid())
	{
		TraceServices::IAllocationsProvider::EQueryStatus QueryStatus;
		UpdateQuery(QueryStatus);

		if (QueryStatus == TraceServices::IAllocationsProvider::EQueryStatus::Done)
		{
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

	TableTreeNodes.Reset();

	TSharedPtr<Insights::FMemAllocTable> MemAllocTable = GetMemAllocTable();
	if (MemAllocTable)
	{
		TArray<FMemoryAlloc>& Allocs = MemAllocTable->GetAllocs();
		Allocs.Reset(10 * 1024 * 1024);
	}

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
	TraceServices::IAllocationsProvider::FReadScopeLock _(Provider);

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

		TSharedPtr<Insights::FMemAllocTable> MemAllocTable = GetMemAllocTable();
		if (MemAllocTable)
		{
			TArray<FMemoryAlloc>& Allocs = MemAllocTable->GetAllocs();

			FStopwatch ResultStopwatch;
			FStopwatch PageStopwatch;
			ResultStopwatch.Start();
			uint32 PageCount = 0;
			uint32 TotatAllocCount = 0;

			// Multiple 'pages' of results will be returned. No guarantees are made
			// about the order of pages or the allocations they report.
			TraceServices::IAllocationsProvider::FQueryResult Result = Status.NextResult();
			while (Result.IsValid())
			{
				++PageCount;
				PageStopwatch.Restart();

				const uint32 AllocCount = Result->Num();
				TotatAllocCount += AllocCount;
#if 0
				Allocs.Reserve(Allocs.Num() + AllocCount);

				for (uint32 AllocIndex = 0; AllocIndex < AllocCount; ++AllocIndex)
				{
					const IAllocationsProvider::FAllocation* Allocation = Result->Get(AllocIndex);

					const double AllocStartTime = Allocation->GetStartTime();
					const double AllocEndTime = Allocation->GetEndTime();
					const uint64 AllocAddress = Allocation->GetAddress();
					const uint64 AllocationSize = Allocation->GetSize();
					const FMemoryTagId AllocTag = static_cast<FMemoryTagId>(Allocation->GetTag());
					const uint64 BacktraceId = Allocation->GetBacktraceId();

					//Allocs.Add(FMemoryAlloc(AllocStartTime, AllocEndTime, AllocAddress, AllocationSize, AllocTag, BacktraceId));
					Allocs.Emplace(AllocStartTime, AllocEndTime, AllocAddress, AllocationSize, AllocTag, BacktraceId);
				}
#else // 14% faster, but uses more mem
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
					Alloc.MemTag = static_cast<FMemoryTagId>(Allocation->GetTag());
					Alloc.BacktraceId = Allocation->GetBacktraceId();
				}
#endif

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
				const double Speed = (TotalTime * 1000000.0) / TotatAllocCount;
				UE_LOG(MemoryProfiler, Log, TEXT("[MemAlloc] Query results (%u pages, %u allocs, slack=%u) retrieved in %.3fs (speed: %.3f seconds per 1M allocs)."), PageCount, TotatAllocCount, Allocs.GetSlack(), TotalTime, Speed);
			}
		}

		TotalStopwatch.Update();
	} while (OutStatus == TraceServices::IAllocationsProvider::EQueryStatus::Available && TotalStopwatch.GetAccumulatedTime() < MaxPollTime);

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

void SMemAllocTableTreeView::OnPreAsyncUpdate()
{
	auto LLMColumn = Table->FindColumn(TEXT("AllocLlmTag"));
	OriginalLlmTagValueFormatter = LLMColumn->GetValueFormatter();
	LLMColumn->SetValueFormatter(CreateCachedLlmTagValueFormatter());

	STableTreeView::OnPreAsyncUpdate();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemAllocTableTreeView::OnPostAsyncUpdate()
{
	if (OriginalLlmTagValueFormatter.IsValid())
	{
		auto LLMColumn = Table->FindColumn(TEXT("AllocLlmTag"));
		LLMColumn->SetValueFormatter(OriginalLlmTagValueFormatter.ToSharedRef());
		OriginalLlmTagValueFormatter = nullptr;
	}

	STableTreeView::OnPostAsyncUpdate();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<ITableCellValueFormatter> SMemAllocTableTreeView::CreateCachedLlmTagValueFormatter()
{
	class FCachedMemAllocLlmTagValueFormatter : public FTableCellValueFormatter
	{
	public:
		virtual FText FormatValue(const TOptional<FTableCellValue>& InValue) const override
		{
			if (InValue.IsSet())
			{
				const FMemoryTagId MemTagId = static_cast<FMemoryTagId>(InValue.GetValue().Int64);

				if (TagIdToStatNameMap.Contains(MemTagId))
				{
					FText::FromString(TagIdToStatNameMap[MemTagId]);
				}
				return FText::FromString(FString());
			}
			return FText::GetEmpty();
		}
		virtual FText FormatValueForTooltip(const TOptional<FTableCellValue>& InValue) const override
		{
			if (InValue.IsSet())
			{
				const FMemoryTagId MemTagId = static_cast<FMemoryTagId>(InValue.GetValue().Int64);
				if (TagIdToStatNameMap.Contains(MemTagId))
				{
					return FText::FromString(FString::Printf(TEXT("%lli (%s)"), MemTagId, *TagIdToStatNameMap[MemTagId]));
				}
				
				return FText::FromString(FString::Printf(TEXT("%lli ()"), MemTagId));
			}
			return FText::GetEmpty();
		}
		virtual FText FormatValue(const FTableColumn& Column, const FBaseTreeNode& Node) const override { return FormatValue(Column.GetValue(Node)); }
		virtual FText FormatValueForTooltip(const FTableColumn& Column, const FBaseTreeNode& Node) const override { return FormatValueForTooltip(Column.GetValue(Node)); }

		void AddTagToCache(FMemoryTagId TagId, const FString& StatName)
		{
			TagIdToStatNameMap.Add(TagId, StatName);
		}

	private:
		TMap<FMemoryTagId, FString> TagIdToStatNameMap;
	};

	TSharedRef<FCachedMemAllocLlmTagValueFormatter> Formatter = MakeShared<FCachedMemAllocLlmTagValueFormatter>();

	TSharedPtr<SMemoryProfilerWindow> ProfilerWindow = FMemoryProfilerManager::Get()->GetProfilerWindow();
	if (ProfilerWindow)
	{
		auto& SharedState = ProfilerWindow->GetSharedState();
		const Insights::FMemoryTagList& TagList = SharedState.GetTagList();
		for (FMemoryTag* CurrentTag : TagList.GetTags())
		{
			Formatter->AddTagToCache(CurrentTag->GetId(), CurrentTag->GetStatName());
		}

	}

	return Formatter;
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

} // namespace Insights

#undef LOCTEXT_NAMESPACE
