// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMemAllocTableTreeView.h"

#include "TraceServices/AnalysisService.h"
#include "TraceServices/Model/AllocationsProvider.h"

// Insights
#include "Insights/Common/Stopwatch.h"
#include "Insights/MemoryProfiler/MemoryProfilerManager.h"
#include "Insights/MemoryProfiler/ViewModels/MemAllocNode.h"
#include "Insights/MemoryProfiler/ViewModels/MemAllocTable.h"
#include "Insights/MemoryProfiler/ViewModels/MemorySharedState.h"
#include "Insights/TimingProfilerCommon.h"

#include <limits>

#define LOCTEXT_NAMESPACE "SMemAllocTableTreeView"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

SMemAllocTableTreeView::SMemAllocTableTreeView()
{
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
	// We need to check if the list of LLM tags has changed.
	// But, ensure we do not check too often.
	static uint64 NextTimestamp = 0;
	uint64 Time = FPlatformTime::Cycles64();
	if (Time > NextTimestamp)
	{
		RebuildTree(false);

		const double WaitTimeSec = 0.5;
		const uint64 WaitTime = static_cast<uint64>(WaitTimeSec / FPlatformTime::GetSecondsPerCycle64());
		NextTimestamp = Time + WaitTime;
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
		UpdateQuery();

		TArray<FMemoryAlloc>&  Allocs = MemAllocTable->GetAllocs();

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
#if defined(UE_USE_ALLOCATIONS_PROVIDER)
	check(Query == 0);

	if (!Rule)
	{
		UE_LOG(MemoryProfiler, Warning, TEXT("[MemAlloc] Invalid query rule!"));
		return;
	}

	const IAllocationsProvider* AllocationsProvider = TraceServices::ReadAllocationsProvider(*Session.Get());
	if (!AllocationsProvider)
	{
		UE_LOG(MemoryProfiler, Warning, TEXT("[MemAlloc] Invalid allocations provider!"));
		return;
	}
	const IAllocationsProvider& Provider = *AllocationsProvider;

	//TODO: Simpler API...
	//QueryHandle StartQuery(IAllocationsProvider::EQueryRule Rule, double TimeA, double TimeB, double TimeC, double TimeD);

	switch (Rule->GetValue())
	{
		case Insights::EMemoryRule::aAf:
		{
			Query = Provider.StartQuery(TimeMarkers[0], TimeMarkers[0], IAllocationsProvider::ECrosses::Or);
			break;
		}
		case Insights::EMemoryRule::afA:
		{
			Query = Provider.StartQuery(0.0, TimeMarkers[0], IAllocationsProvider::ECrosses::None);
			break;
		}
		case Insights::EMemoryRule::Aaf:
		{
			Query = Provider.StartQuery(TimeMarkers[0], std::numeric_limits<double>::infinity(), IAllocationsProvider::ECrosses::None);
			break;
		}
		//case Insights::EMemoryRule::aAfB:
		//{
		//	Query = Provider.StartQuery(TimeMarkers[0], TimeMarkers[1], IAllocationsProvider::ECrosses::A);
		//	break;
		//}
		//case Insights::EMemoryRule::AaBf:
		//{
		//	Query = Provider.StartQuery(TimeMarkers[0], TimeMarkers[1], IAllocationsProvider::ECrosses::B);
		//	break;
		//}
		case Insights::EMemoryRule::AafB:
		{
			Query = Provider.StartQuery(TimeMarkers[0], TimeMarkers[1], IAllocationsProvider::ECrosses::None);
			break;
		}
		case Insights::EMemoryRule::aABf:
		{
			Query = Provider.StartQuery(TimeMarkers[0], TimeMarkers[1], IAllocationsProvider::ECrosses::And);
			break;
		}
		case Insights::EMemoryRule::AaBCf:
		{
			Query = Provider.StartQuery(TimeMarkers[1], TimeMarkers[2], IAllocationsProvider::ECrosses::And);
			// needs additional filter Alloc.Start > A
			break;
		}
		//case Insights::EMemoryRule::AaBfC:
		//	TODO
		//	break;
		case Insights::EMemoryRule::aABfC:
		{
			Query = Provider.StartQuery(TimeMarkers[0], TimeMarkers[1], IAllocationsProvider::ECrosses::And);
			// needs additional filter Alloc.End < C
			break;
		}
		//case Insights::EMemoryRule::AaBCfD:
		//	TODO
		//	break;
		case Insights::EMemoryRule::A_vs_B:
		{
			Query = Provider.StartQuery(TimeMarkers[0], TimeMarkers[1], IAllocationsProvider::ECrosses::Or);
			break;
		}
		case Insights::EMemoryRule::A_or_B:
		{
			Query = Provider.StartQuery(TimeMarkers[0], TimeMarkers[1], IAllocationsProvider::ECrosses::Or);
			break;
		}
		case Insights::EMemoryRule::A_xor_B:
		{
			Query = Provider.StartQuery(TimeMarkers[0], TimeMarkers[1], IAllocationsProvider::ECrosses::Xor);
			break;
		}
	}

	if (Query == 0)
	{
		UE_LOG(MemoryProfiler, Error, TEXT("[MemAlloc] Unsupported query rule (%s)!"), *Rule->GetShortName().ToString());
	}
#endif // defined(UE_USE_ALLOCATIONS_PROVIDER)
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemAllocTableTreeView::UpdateQuery()
{
#if defined(UE_USE_ALLOCATIONS_PROVIDER)
	if (Query != 0)
	{
		const IAllocationsProvider* AllocationsProvider = TraceServices::ReadAllocationsProvider(*Session.Get());
		if (!AllocationsProvider)
		{
			UE_LOG(MemoryProfiler, Warning, TEXT("[MemAlloc] Invalid allocations provider!"));
			return;
		}
		const IAllocationsProvider& Provider = *AllocationsProvider;

		IAllocationsProvider::FQueryStatus Status = Provider.PollQuery(Query);
		if (Status.Status <= IAllocationsProvider::EQueryStatus::Done)
		{
			Query = 0;
			return;
		}

		TSharedPtr<Insights::FMemAllocTable> MemAllocTable = GetMemAllocTable();
		if (MemAllocTable)
		{
			TArray<FMemoryAlloc>& Allocs = MemAllocTable->GetAllocs();

			FStopwatch TotalStopwatch;
			FStopwatch PageStopwatch;
			TotalStopwatch.Start();
			uint32 PageCount = 0;
			uint32 TotatAllocCount = 0;

			// Multiple 'pages' of results will be returned. No guarantees are made
			// about the order of pages or the allocations they report.
			IAllocationsProvider::QueryResult Result = Status.NextResult();
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
					const IAllocationsProvider::FAllocation* Allocation = Result->Get(AllocIndex);
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

			TotalStopwatch.Stop();
			const double TotalTime = TotalStopwatch.GetAccumulatedTime();
			if (TotalTime > 0.01)
			{
				const double Speed = (TotalTime * 1000000.0) / TotatAllocCount;
				UE_LOG(MemoryProfiler, Log, TEXT("[MemAlloc] Query results (%u pages, %u allocs, slack=%u) retrieved in %.3fs (speed: %.3f seconds per 1M allocs)."), PageCount, TotatAllocCount, Allocs.GetSlack(), TotalTime, Speed);
			}
		}
	}
#endif // defined(UE_USE_ALLOCATIONS_PROVIDER)
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemAllocTableTreeView::CancelQuery()
{
#if defined(UE_USE_ALLOCATIONS_PROVIDER)
	if (Query != 0)
	{
		const IAllocationsProvider* AllocationsProvider = TraceServices::ReadAllocationsProvider(*Session.Get());
		if (AllocationsProvider)
		{
			AllocationsProvider->CancelQuery(Query);
			UE_LOG(MemoryProfiler, Log, TEXT("[MemAlloc] Query canceled."));
		}
		Query = 0;
	}
#endif // defined(UE_USE_ALLOCATIONS_PROVIDER)
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
