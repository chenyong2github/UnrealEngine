// Copyright Epic Games, Inc. All Rights Reserved.

#include "D3D12RHIPrivate.h"

#if PLATFORM_USE_BACKBUFFER_WRITE_TRANSITION_TRACKING
static_assert(!ENABLE_RESIDENCY_MANAGEMENT, "FD3D12LinearBatchedQueryPool does not properly support residency management");

class FD3D12LinearBatchedQueryPool :  public FNoncopyable, public FD3D12DeviceChild, public FD3D12SingleNodeGPUObject
{
public:
	FD3D12LinearBatchedQueryPool(FD3D12Device* Parent, uint32 PoolSizeIn)
		:	FD3D12DeviceChild(Parent),
			FD3D12SingleNodeGPUObject(Parent->GetGPUMask()),
			PoolSize(PoolSizeIn)
	{
		check(IsInRenderingThread());
		check(PoolSize >= 2);

		// Create query heap
		D3D12_QUERY_HEAP_DESC HeapDesc = {};
		HeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
		HeapDesc.Count = PoolSizeIn;
		VERIFYD3D12RESULT(GetParentDevice()->GetDevice()->CreateQueryHeap(&HeapDesc, IID_PPV_ARGS(&QueryHeap)));
		check(QueryHeap);

		// Create resolve buffer
		const D3D12_HEAP_PROPERTIES BufferHeapProperities = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK, GetGPUMask().GetNative(), GetVisibilityMask().GetNative());
		const D3D12_RESOURCE_DESC BufferDesc = CD3DX12_RESOURCE_DESC::Buffer(PoolSize * sizeof(uint64));
		VERIFYD3D12RESULT(GetParentDevice()->GetDevice()->CreateCommittedResource(&BufferHeapProperities, D3D12_HEAP_FLAG_NONE, &BufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&ResolveBuffer)));
	}

	~FD3D12LinearBatchedQueryPool()
	{
		QueryHeap->Release();
		ResolveBuffer->Release();
	}

	uint32 GetInvalidQueryId() const
	{
		return PoolSize + 1;
	}

	bool HasOpenBatch() const
	{
		return Batches.Num() && Batches.Last().bOpen;
	}

	void StartBatch(uint64 BatchId, bool bDeferred)
	{
		check(!HasOpenBatch());

		QueryBatch NewBatch;
		NewBatch.StartIndex = Batches.Num() == 0 ? 0 : (Batches.Last().StartIndex + Batches.Last().Size) % PoolSize;
		NewBatch.Size = 0;
		NewBatch.Id = BatchId;
		NewBatch.bDeferred = bDeferred;
		NewBatch.bOpen = true;
		Batches.Add(NewBatch);
	}

	void EndBatch(FD3D12CommandContext& Context)
	{
		check(HasOpenBatch());

		QueryBatch& Batch = Batches.Last();

		if (Batch.Size)
		{
			if (Batch.StartIndex + Batch.Size > PoolSize)
			{
				Context.CommandListHandle->ResolveQueryData(QueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, Batch.StartIndex, PoolSize - Batch.StartIndex, ResolveBuffer, Batch.StartIndex * sizeof(uint64));
				Context.CommandListHandle->ResolveQueryData(QueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, 0, Batch.Size - (PoolSize - Batch.StartIndex), ResolveBuffer, 0);
			}
			else
			{
				Context.CommandListHandle->ResolveQueryData(QueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, Batch.StartIndex, Batch.Size, ResolveBuffer, Batch.StartIndex * sizeof(uint64));
			}
			++Context.otherWorkCounter;

			Batch.SyncPoint = Context.CommandListHandle;
		}
		Batch.bOpen = false;
	}

	bool GetResolvedBatchResults(bool bWait, uint64& BatchIdOut, bool& bDeferredOut, TArray<uint64>& QueryResultsOut)
	{
		if (Batches.Num())
		{
			QueryBatch& Batch = Batches[0];
			if (!Batch.bOpen)
			{
				if (Batch.Size)
				{
					check(Batch.SyncPoint.IsValid());

					if (bWait)
					{
						Batch.SyncPoint.WaitForCompletion();
					}
				
					if (Batch.SyncPoint.IsComplete())
					{
						BatchIdOut = Batch.Id;
						bDeferredOut = Batch.bDeferred;
						QueryResultsOut.SetNum(Batch.Size);

						uint64* MappedResults;
						if (Batch.StartIndex + Batch.Size > PoolSize)
						{
							D3D12_RANGE ReadRange;
							ReadRange.Begin = Batch.StartIndex * sizeof(uint64);
							ReadRange.End = ReadRange.Begin + Batch.Size * sizeof(uint64);
							VERIFYD3D12RESULT(ResolveBuffer->Map(0, &ReadRange, reinterpret_cast<void**>(&MappedResults)));
							FMemory::Memcpy(QueryResultsOut.GetData(), MappedResults + Batch.StartIndex, (PoolSize - Batch.StartIndex) * sizeof(uint64));
							ResolveBuffer->Unmap(0, nullptr);

							ReadRange.Begin = 0;
							ReadRange.End = ReadRange.Begin + (Batch.Size - (PoolSize - Batch.StartIndex)) * sizeof(uint64);
							VERIFYD3D12RESULT(ResolveBuffer->Map(0, &ReadRange, reinterpret_cast<void**>(&MappedResults)));
							FMemory::Memcpy(QueryResultsOut.GetData() + (PoolSize - Batch.StartIndex), MappedResults, (Batch.Size - (PoolSize - Batch.StartIndex)) * sizeof(uint64));
							ResolveBuffer->Unmap(0, nullptr);
						}
						else
						{
							D3D12_RANGE ReadRange;
							ReadRange.Begin = Batch.StartIndex * sizeof(uint64);
							ReadRange.End = ReadRange.Begin + Batch.Size * sizeof(uint64);
							VERIFYD3D12RESULT(ResolveBuffer->Map(0, &ReadRange, reinterpret_cast<void**>(&MappedResults)));
							FMemory::Memcpy(QueryResultsOut.GetData(), MappedResults + Batch.StartIndex, Batch.Size * sizeof(uint64));
							ResolveBuffer->Unmap(0, nullptr);
						}

						Batches.RemoveAt(0);
						return true;
					}
				}
				else
				{
					BatchIdOut = Batch.Id;
					bDeferredOut = Batch.bDeferred;
					QueryResultsOut.SetNum(0);
					Batches.RemoveAt(0);
					return true;
				}
			}
		}

		return false;
	}

	void PurgeBatches()
	{
		Batches.SetNum(0);
	}

	bool AllocateQueryPair(uint32& QueryId0, uint32& QueryId1)
	{
		FScopeLock ScopeLock(&QueryCriticalSection);

		check(Batches.Num());
		QueryBatch& OpenBatch = Batches.Last();
		check(OpenBatch.bOpen);

		QueryId0 = (OpenBatch.StartIndex + OpenBatch.Size) % PoolSize;
		QueryId1 = (OpenBatch.StartIndex + OpenBatch.Size + 1) % PoolSize;

		const uint32 OldestIndex = Batches[0].StartIndex;
		if ((QueryId0 != OldestIndex && QueryId1 != OldestIndex) || (Batches.Num() == 1 && Batches[0].Size == 0))
		{
			OpenBatch.Size += 2;
			return true;
		}
		else
		{
			QueryId0 = QueryId1 = GetInvalidQueryId();
			return false;
		}
	}

	void EndQuery(ID3D12GraphicsCommandList* CommandList, uint32 QueryId)
	{
		CommandList->EndQuery(QueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, QueryId);
	}

private:
	struct QueryBatch
	{
		uint32				StartIndex;
		uint32				Size;
		uint64				Id;
		uint64				TimeStampFrequency;
		FD3D12CLSyncPoint	SyncPoint;
		bool				bDeferred;
		bool				bOpen;
		
	};

	uint32							PoolSize;
	TArray<QueryBatch>				Batches;
	ID3D12QueryHeap*				QueryHeap;
	ID3D12Resource*					ResolveBuffer;
	FCriticalSection				QueryCriticalSection;
};


FD3D12TimedIntervalQueryTracker::FD3D12TimedIntervalQueryTracker(FD3D12Device* Device, uint32 MaxIntervalQueriesIn, uint64 InvalidBatchIdIn)
	:	MaxIntervalQueries(MaxIntervalQueriesIn),
		QueryPool(nullptr)
{
	check(Device);
	QueryPool = new FD3D12LinearBatchedQueryPool(Device, MaxIntervalQueries * 2);
}


FD3D12TimedIntervalQueryTracker::~FD3D12TimedIntervalQueryTracker()
{
	PurgeOutstandingBatches();

	if (QueryPool)
	{
		delete QueryPool;
	}
}


void FD3D12TimedIntervalQueryTracker::BeginBatch(uint64 BatchId, bool bDeferred)
{
	// Start new batch
	QueryPool->StartBatch(BatchId, bDeferred);
}


void FD3D12TimedIntervalQueryTracker::EndBatch(FD3D12CommandContext& Context)
{
	if (QueryPool->HasOpenBatch())
	{
		QueryPool->EndBatch(Context);
	}
}


FTimedIntervalQuery FD3D12TimedIntervalQueryTracker::BeginInterval(ID3D12GraphicsCommandList* CommandList)
{
	FTimedIntervalQuery Interval = {0, 0};

	if (QueryPool->HasOpenBatch())
	{
		if (!QueryPool->AllocateQueryPair(Interval.StartQuery, Interval.EndQuery))
		{
			UE_LOG(LogD3D12RHI, Error, TEXT("Timed interval query pool size has been exceeded.  Timer queries were discarded."));
		}
		QueryPool->EndQuery(CommandList, Interval.StartQuery);
	}

	return Interval;
}


void FD3D12TimedIntervalQueryTracker::EndInterval(ID3D12GraphicsCommandList* CommandList, FTimedIntervalQuery Interval)
{
	if (QueryPool->HasOpenBatch() && Interval.EndQuery != QueryPool->GetInvalidQueryId())
	{
		QueryPool->EndQuery(CommandList, Interval.EndQuery);
	}
}


void FD3D12TimedIntervalQueryTracker::ResolveBatches(uint64 TimeStampFrequency, bool bWait)
{
	uint64 BatchId = 0;
	bool bDeferred = false;
	TArray<uint64> QueryResults;
	while (QueryPool->GetResolvedBatchResults(bWait, BatchId, bDeferred, QueryResults))
	{
		check(QueryResults.Num() % 2 == 0);

		uint64 TotalTime = 0;
		for (int32 QueryIndex = 0; QueryIndex < QueryResults.Num(); QueryIndex += 2)
		{
			if (QueryResults[QueryIndex + 1] >= QueryResults[QueryIndex])
			{
				TotalTime += QueryResults[QueryIndex + 1] - QueryResults[QueryIndex];
			}
		}

		const uint64 TotalTimeUS = TotalTime * 1e6 / TimeStampFrequency;
		OnBatchResolvedDelegate.ExecuteIfBound(BatchId, bDeferred, TotalTimeUS);
	}
}

void FD3D12TimedIntervalQueryTracker::PurgeOutstandingBatches()
{
	QueryPool->PurgeBatches();
}
#endif // #if PLATFORM_USE_BACKBUFFER_WRITE_TRANSITION_TRACKING
