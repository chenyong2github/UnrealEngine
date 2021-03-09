// Copyright Epic Games, Inc. All Rights Reserved.

#include "AllocationsQuery.h"

#include "SbTree.h"
#include "Common/Utils.h"
#include "TraceServices/Containers/Allocators.h"

#include <limits>

namespace TraceServices
{

////////////////////////////////////////////////////////////////////////////////////////////////////
// FAllocationsQueryAsyncTask
////////////////////////////////////////////////////////////////////////////////////////////////////

class FAllocationsQueryAsyncTask
{
public:
	FAllocationsQueryAsyncTask(FAllocationsQuery* InQuery)
	{
		QueryPtr = InQuery;
	}

	FORCEINLINE TStatId GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(FAllocationsQueryAsyncTask, STATGROUP_TaskGraphTasks); }
	ENamedThreads::Type GetDesiredThread() { return ENamedThreads::Type::AnyThread; }
	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		if (QueryPtr)
		{
			QueryPtr->Run();
		}
	}

private:
	FAllocationsQuery* QueryPtr = nullptr;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// FAllocationsQuery
////////////////////////////////////////////////////////////////////////////////////////////////////

FAllocationsQuery::FAllocationsQuery(const FAllocationsProvider& InAllocationProvider, const FCallstacksProvider& InCallstacksProvider, const IAllocationsProvider::FQueryParams& InParams)
	: AllocationsProvider(InAllocationProvider)
	, CallstacksProvider(InCallstacksProvider)
	, Params(InParams)
	, IsWorking(true)
	, IsCanceling(false)
{
	// Start the async task.
	CompletedEvent = TGraphTask<FAllocationsQueryAsyncTask>::CreateTask().ConstructAndDispatchWhenReady(this);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAllocationsQuery::Cancel()
{
	if (CompletedEvent.IsValid())
	{
		// Cancel the async task.
		IsCanceling = true;
		CompletedEvent->Wait();
	}
	delete this;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

IAllocationsProvider::FQueryStatus FAllocationsQuery::Poll()
{
	IAllocationsProvider::FQueryStatus Status = {};

	FAllocationsImpl* Allocations = nullptr;
	if (Results.Dequeue(Allocations))
	{
		Status.Status = IAllocationsProvider::EQueryStatus::Available;
		Status.Handle = UPTRINT(Allocations);
		return Status;
	}

	if (IsWorking)
	{
		Status.Status = IAllocationsProvider::EQueryStatus::Working;
	}
	else
	{
		Status.Status = IAllocationsProvider::EQueryStatus::Done;
	}
	Status.Handle = 0;
	return Status;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAllocationsQuery::Run()
{
	// Note: This function is called from the async task (FAllocationsQueryAsyncTask),
	//       so no assumption can be made on which thread is running.

	uint64 StartTime = FPlatformTime::Cycles64();

	uint32 CellCount = 0;
	uint32 TotalAllocationCount = 0;

	AllocationsProvider.BeginRead();

	const FSbTree* SbTree = AllocationsProvider.GetSbTree();
	if (SbTree)
	{
		UE_LOG(LogTraceServices, Log, TEXT("[MemAlloc] Processing %d live allocs..."), AllocationsProvider.GetNumLiveAllocs());

		FAllocationsImpl* LiveAllocsResult = new FAllocationsImpl();
		QueryLiveAllocs(LiveAllocsResult->Items);

		const uint32 NumLiveAllocs = static_cast<uint32>(LiveAllocsResult->Items.Num());
		if (NumLiveAllocs != 0)
		{
			UE_LOG(LogTraceServices, Log, TEXT("[MemAlloc] Enqueue %u live allocs..."), NumLiveAllocs);
			TotalAllocationCount += NumLiveAllocs;

			QueryCallstacks(LiveAllocsResult);

			Results.Enqueue(LiveAllocsResult);
		}
		else
		{
			delete LiveAllocsResult;
		}

		UE_LOG(LogTraceServices, Log, TEXT("[MemAlloc] Detecting cells..."));

		TArray<const FSbTreeCell*> Cells;

		if (!IsCanceling)
		{
			SbTree->Query(Cells, Params);
			CellCount += Cells.Num();
		}

		UE_LOG(LogTraceServices, Log, TEXT("[MemAlloc] %d cells to process"), Cells.Num());
		uint32 CellIndex = 0;

		for (const FSbTreeCell* Cell : Cells)
		{
			if (IsCanceling)
			{
				break;
			}

			++CellIndex;
			UE_LOG(LogTraceServices, Log, TEXT("[MemAlloc] Processing cell %u (%u allocs)..."), CellIndex, Cell->GetAllocCount());

			FAllocationsImpl* CellResult = new FAllocationsImpl();
			Cell->Query(CellResult->Items, Params);

			const uint32 NumAllocs = static_cast<uint32>(CellResult->Items.Num());
			if (NumAllocs != 0)
			{
				QueryCallstacks(CellResult);

				UE_LOG(LogTraceServices, Log, TEXT("[MemAlloc] Enqueue %u allocs..."), NumAllocs);
				TotalAllocationCount += NumAllocs;
				Results.Enqueue(CellResult);
			}
			else
			{
				delete CellResult;
			}
		}

		UE_LOG(LogTraceServices, Log, TEXT("[MemAlloc] Done"));
	}

	AllocationsProvider.EndRead();

	IsWorking = false;

	uint64 EndTime = FPlatformTime::Cycles64();
	const double TotalTime = static_cast<double>(EndTime - StartTime) * FPlatformTime::GetSecondsPerCycle64();
	if (TotalTime > 0.1)
	{
		UE_LOG(LogTraceServices, Log, TEXT("[MemAlloc] Allocations query completed in %.3fs (%u cells, %u allocations)"),
			TotalTime, CellCount, TotalAllocationCount);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAllocationsQuery::QueryLiveAllocs(TArray<const FAllocationItem*>& OutAllocs) const
{
	const TMap<uint64, FAllocationItem>& LiveAllocs = AllocationsProvider.GetLiveAllocs();

	// TODO: Live allocs have the end time set to std::numeric_limits<double>::infinity(),
	//       so the conditions below could be simplified to remove the end time checks.
	//       For now, the code remains "unoptimized" for debugging purposes.

	switch (Params.Rule)
	{
	case IAllocationsProvider::EQueryRule::aAf: // active allocs at A
	{
		const double Time = Params.TimeA;
		for (const auto& KV : LiveAllocs)
		{
			const FAllocationItem& Alloc = KV.Value;
			if (Alloc.StartTime <= Time && Time <= Alloc.EndTime)
			{
				OutAllocs.Add(&Alloc);
			}
		}
	}
	break;

	case IAllocationsProvider::EQueryRule::afA: // before
	{
		const double Time = Params.TimeA;
		for (const auto& KV : LiveAllocs)
		{
			const FAllocationItem& Alloc = KV.Value;
			if (Alloc.EndTime <= Time)
			{
				OutAllocs.Add(&Alloc);
			}
		}
	}
	break;

	case IAllocationsProvider::EQueryRule::Aaf: // after
	{
		const double Time = Params.TimeA;
		for (const auto& KV : LiveAllocs)
		{
			const FAllocationItem& Alloc = KV.Value;
			if (Alloc.StartTime >= Time)
			{
				OutAllocs.Add(&Alloc);
			}
		}
	}
	break;

	case IAllocationsProvider::EQueryRule::aAfB: // decline
	{
		const double TimeA = Params.TimeA;
		const double TimeB = Params.TimeB;
		for (const auto& KV : LiveAllocs)
		{
			const FAllocationItem& Alloc = KV.Value;
			if (Alloc.StartTime <= TimeA && Alloc.EndTime >= TimeA && Alloc.EndTime <= TimeB)
			{
				OutAllocs.Add(&Alloc);
			}
		}
	}
	break;

	case IAllocationsProvider::EQueryRule::AaBf: // growth
	{
		const double TimeA = Params.TimeA;
		const double TimeB = Params.TimeB;
		for (const auto& KV : LiveAllocs)
		{
			const FAllocationItem& Alloc = KV.Value;
			if (Alloc.StartTime >= TimeA && Alloc.StartTime <= TimeB && Alloc.EndTime >= TimeB)
			{
				OutAllocs.Add(&Alloc);
			}
		}
	}
	break;

	case IAllocationsProvider::EQueryRule::AfB: // free events
	{
		const double TimeA = Params.TimeA;
		const double TimeB = Params.TimeB;
		for (const auto& KV : LiveAllocs)
		{
			const FAllocationItem& Alloc = KV.Value;
			if (Alloc.EndTime >= TimeA && Alloc.EndTime <= TimeB)
			{
				OutAllocs.Add(&Alloc);
			}
		}
	}
	break;

	case IAllocationsProvider::EQueryRule::AaB: // alloc events
	{
		const double TimeA = Params.TimeA;
		const double TimeB = Params.TimeB;
		for (const auto& KV : LiveAllocs)
		{
			const FAllocationItem& Alloc = KV.Value;
			if (Alloc.StartTime >= TimeA && Alloc.StartTime <= TimeB)
			{
				OutAllocs.Add(&Alloc);
			}
		}
	}
	break;

	case IAllocationsProvider::EQueryRule::AafB: // short living allocs
	{
		const double TimeA = Params.TimeA;
		const double TimeB = Params.TimeB;
		for (const auto& KV : LiveAllocs)
		{
			const FAllocationItem& Alloc = KV.Value;
			if (Alloc.StartTime >= TimeA && Alloc.EndTime <= TimeB)
			{
				OutAllocs.Add(&Alloc);
			}
		}
	}
	break;

	case IAllocationsProvider::EQueryRule::aABf: // long living allocs
	{
		const double TimeA = Params.TimeA;
		const double TimeB = Params.TimeB;
		for (const auto& KV : LiveAllocs)
		{
			const FAllocationItem& Alloc = KV.Value;
			if (Alloc.StartTime <= TimeA && Alloc.EndTime >= TimeB)
			{
				OutAllocs.Add(&Alloc);
			}
		}
	}
	break;

	case IAllocationsProvider::EQueryRule::AaBCf: // memory leaks
	{
		const double TimeA = Params.TimeA;
		const double TimeB = Params.TimeB;
		const double TimeC = Params.TimeC;
		for (const auto& KV : LiveAllocs)
		{
			const FAllocationItem& Alloc = KV.Value;
			if (Alloc.StartTime >= TimeA && Alloc.StartTime <= TimeB && Alloc.EndTime >= TimeC)
			{
				OutAllocs.Add(&Alloc);
			}
		}
	}
	break;

	case IAllocationsProvider::EQueryRule::AaBfC: // limited lifetime
	{
		const double TimeA = Params.TimeA;
		const double TimeB = Params.TimeB;
		const double TimeC = Params.TimeC;
		for (const auto& KV : LiveAllocs)
		{
			const FAllocationItem& Alloc = KV.Value;
			if (Alloc.StartTime >= TimeA && Alloc.StartTime <= TimeB && Alloc.EndTime >= TimeB && Alloc.EndTime <= TimeC)
			{
				OutAllocs.Add(&Alloc);
			}
		}
	}
	break;

	case IAllocationsProvider::EQueryRule::aABfC: // decline of long living allocs
	{
		const double TimeA = Params.TimeA;
		const double TimeB = Params.TimeB;
		const double TimeC = Params.TimeC;
		for (const auto& KV : LiveAllocs)
		{
			const FAllocationItem& Alloc = KV.Value;
			if (Alloc.StartTime <= TimeA && Alloc.EndTime >= TimeB && Alloc.EndTime <= TimeC)
			{
				OutAllocs.Add(&Alloc);
			}
		}
	}
	break;

	case IAllocationsProvider::EQueryRule::AaBCfD: // specific lifetime
	{
		const double TimeA = Params.TimeA;
		const double TimeB = Params.TimeB;
		const double TimeC = Params.TimeC;
		const double TimeD = Params.TimeD;
		for (const auto& KV : LiveAllocs)
		{
			const FAllocationItem& Alloc = KV.Value;
			if (Alloc.StartTime >= TimeA && Alloc.StartTime <= TimeB && Alloc.EndTime >= TimeC && Alloc.EndTime <= TimeD)
			{
				OutAllocs.Add(&Alloc);
			}
		}
	}
	break;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAllocationsQuery::QueryCallstacks(FAllocationsImpl* Result) const
{
	for (auto Item : Result->Items)
	{
		// Callstacks could have been resolved in previous queries, 
		// check before querying again
		if (!Item->Callstack)
		{
			Item->Callstack = CallstacksProvider.GetCallstack(Item->Owner);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace TraceServices
