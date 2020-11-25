// Copyright Epic Games, Inc. All Rights Reserved.

#include "AllocationsQuery.h"

#include "SbTree.h"
#include "Common/Utils.h"
#include "TraceServices/Containers/Allocators.h"

#include <limits>

namespace TraceServices
{

////////////////////////////////////////////////////////////////////////////////////////////////////
// FAllocationsImpl
////////////////////////////////////////////////////////////////////////////////////////////////////

FAllocationsImpl::FAllocationsImpl(uint32 NumItems)
	: Next(nullptr)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FAllocationsImpl::~FAllocationsImpl()
{
}

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
	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::FireAndForget; }

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

FAllocationsQuery::FAllocationsQuery(const FAllocationsProvider& InProvider, const IAllocationsProvider::FQueryParams& InParams)
	: Provider(InProvider)
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
	Status.Handle = 0;

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
		return Status;
	}
	else
	{
		Status.Status = IAllocationsProvider::EQueryStatus::Done;
		return Status;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAllocationsQuery::Run()
{
	// Note: This function is called from the async task (FAllocationsQueryAsyncTask),
	//       so no assumption can be made on which thread is running.

	uint64 StartTime = FPlatformTime::Cycles64();

	uint32 CellCount = 0;
	uint32 TotalAllocationCount = 0;

	Provider.BeginRead();

	const FSbTree* SbTree = Provider.GetSbTree();
	if (SbTree)
	{
		TArray<const FSbTreeCell*> Cells;

		if (!IsCanceling)
		{
			SbTree->Query(Cells, Params);
			CellCount += Cells.Num();
		}

		for (const FSbTreeCell* Cell : Cells)
		{
			if (IsCanceling)
			{
				break;
			}
			FAllocationsImpl* Result = new FAllocationsImpl(0);
			Cell->Query(Result->Items, Params);
			TotalAllocationCount += Result->Items.Num();
			Results.Enqueue(Result);
		}
	}

	Provider.EndRead();

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

} // namespace TraceServices
