// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if PLATFORM_USE_BACKBUFFER_WRITE_TRANSITION_TRACKING
struct ID3D12GraphicsCommandList;
class FD3D12CommandContext;
class FD3D12LinearBatchedQueryPool;


struct FTimedIntervalQuery
{
	uint32	StartQuery;
	uint32	EndQuery;
};


class FD3D12TimedIntervalQueryTracker : public FNoncopyable
{
public:
	UE_NONCOPYABLE(FD3D12TimedIntervalQueryTracker)
	
	FD3D12TimedIntervalQueryTracker(FD3D12Device* Device, uint32 MaxIntervalQueriesIn, uint64 InvalidBatchIdIn);
	~FD3D12TimedIntervalQueryTracker();

	void BeginBatch(uint64 BatchId, bool bDeferred);
	void EndBatch(FD3D12CommandContext& Context);
	FTimedIntervalQuery BeginInterval(ID3D12GraphicsCommandList* CommandList);
	void EndInterval(ID3D12GraphicsCommandList* CommandList, FTimedIntervalQuery Interval);
	void ResolveBatches(uint64 TimeStampFrequency, bool bWait);
	void PurgeOutstandingBatches();
	
	DECLARE_DELEGATE_ThreeParams(FOnBatchResolvedDelegate, uint64, bool, uint64);
	FOnBatchResolvedDelegate OnBatchResolvedDelegate;

private:
	uint32							MaxIntervalQueries;
	FD3D12LinearBatchedQueryPool*	QueryPool;
};


class FD3D12ScopedTimedIntervalQuery : public FNoncopyable
{
public:
	UE_NONCOPYABLE(FD3D12ScopedTimedIntervalQuery)

	FD3D12ScopedTimedIntervalQuery(FD3D12TimedIntervalQueryTracker* TrackerIn, ID3D12GraphicsCommandList* CommandListIn)
		:	Tracker(TrackerIn),
			CommandList(CommandListIn)
	{
		check(TrackerIn);
		check(CommandListIn);
		Interval = TrackerIn->BeginInterval(CommandList);
	}

	~FD3D12ScopedTimedIntervalQuery()
	{
		Tracker->EndInterval(CommandList, MoveTemp(Interval));
	}

private:
	FD3D12TimedIntervalQueryTracker*	Tracker;
	ID3D12GraphicsCommandList*			CommandList;
	FTimedIntervalQuery					Interval;
};
#endif // #if PLATFORM_USE_BACKBUFFER_WRITE_TRANSITION_TRACKING
