// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
D3D12Query.h: Implementation of D3D12 Query
=============================================================================*/
#pragma once

/** D3D12 Render query */
class FD3D12RenderQuery : public FRHIRenderQuery, public FD3D12DeviceChild, public FD3D12LinkedAdapterObject<FD3D12RenderQuery>
{
public:
	/** Refcounted storage to result buffer because it could be holding the last valid reference if we have more than n active batches per frame */
	TRefCountPtr<FD3D12Resource> ResultBuffer;

	/** The cached query result. */
	uint64 Result;

	// A timestamp so that LDA query results only handle object from the most recent frames.
	uint32 Timestamp;

	/** The query's index in its heap. */
	uint32 HeapIndex;

	/** The Frame the query was submitted on. */
	uint32 FrameSubmitted;

	/** The query's type. */
	const ERenderQueryType Type;

	/** True if the query's result is cached. */
	bool bResultIsCached : 1;

	/** True if the query has been resolved. */
	bool bResolved : 1;

	/** Initialization constructor. */
	FD3D12RenderQuery(FD3D12Device* Parent, ERenderQueryType InQueryType) :
		FD3D12DeviceChild(Parent),
		ResultBuffer(nullptr),
		Result(0),
		Timestamp(0),
		Type(InQueryType)
	{
		Reset();
	}

	inline void Reset()
	{
		ResultBuffer = nullptr;
		HeapIndex = INDEX_NONE;
		bResultIsCached = false;
		bResolved = false;		
		FrameSubmitted = -1;
	}

	// Indicate the command list that was used to resolve the query.
	inline void MarkResolved(FD3D12CommandListHandle& CommandList, FD3D12Resource* InResultBuffer)
	{
		CLSyncPoint = CommandList;
		ResultBuffer = InResultBuffer;
		bResolved = true;
	}

	inline FD3D12CLSyncPoint& GetSyncPoint()
	{
		// Sync point is only valid if we've resolved the query.
		check(bResolved);
		return CLSyncPoint;
	}

private:
	// When the query result is ready on the GPU.
	FD3D12CLSyncPoint CLSyncPoint;
};

template<>
struct TD3D12ResourceTraits<FRHIRenderQuery>
{
	typedef FD3D12RenderQuery TConcreteType;
};

// This class handles query heaps
class FD3D12QueryHeap : public FD3D12DeviceChild, public FD3D12SingleNodeGPUObject
{
private:
	struct QueryBatch
	{
	public:
		uint32 StartElement;    							// The first element in the batch (inclusive)
		uint32 ElementCount;    							// The number of elements in the batch
		bool bOpen;             							// Is the batch still open for more begin/end queries?

		TRefCountPtr<ID3D12QueryHeap> UsedQueryHeap;		// The query heap where all elements reside
		TRefCountPtr<FD3D12Resource> UsedResultBuffer;		// The buffer where all query results are stored
		
		// A list of all FD3D12RenderQuery objects used in the batch. 
		// This is used to set when each queries' result is ready to be read.
		TArray<FD3D12RenderQuery*> RenderQueries;

		QueryBatch()
		{
			RenderQueries.Reserve(256);
			Clear();
		}

		inline void Clear()
		{
			StartElement = 0;
			ElementCount = 0;
			bOpen = false;
			RenderQueries.Reset();

			UsedQueryHeap = nullptr;
			UsedResultBuffer = nullptr;
		}
	};

public:
	FD3D12QueryHeap(class FD3D12Device* InParent, const D3D12_QUERY_TYPE InQueryType, uint32 InQueryHeapCount, uint32 InMaxActiveBatches);

	void Init();
	void Destroy();

	// Start tracking a new batch of begin/end query calls that will be resolved together
	void StartQueryBatch(FD3D12CommandContext& CmdContext, uint32 NumQueriesInBatch);
	// Stop tracking the current batch of begin/end query calls that will be resolved together.
	void EndQueryBatchAndResolveQueryData(FD3D12CommandContext& CmdContext);  

	uint32 AllocQuery(FD3D12CommandContext& CmdContext); // Some query types don't need a BeginQuery call. Instead just alloc a slot to EndQuery with.
	void BeginQuery(FD3D12CommandContext& CmdContext, FD3D12RenderQuery* RenderQuery); // Obtain a query from the store of available queries
	void EndQuery(FD3D12CommandContext& CmdContext, FD3D12RenderQuery* RenderQuery);

private:
	uint32 GetNextElement(uint32 InElement); // Get the next element, after the specified element. Handles overflow.

	uint32 GetNextBatchElement(uint32 InBatchElement);

	void CreateQueryHeap();
	void DestroyQueryHeap();

	uint64 GetResultBufferOffsetForElement(uint32 InElement) const { return ResultSize * InElement; };

private:
	QueryBatch CurrentQueryBatch;                       // The current recording batch.

	TArray<QueryBatch> ActiveQueryBatches;              // List of active query batches. The data for these is in use.
	uint32 LastBatch;                                   // The index of the newest batch.

	uint32 ActiveAllocatedElementCount;					// The number of elements that are in use (Active). Between the head and the tail.

	uint32 LastAllocatedElement;						// The last element that was allocated for BeginQuery
	const D3D12_QUERY_TYPE QueryType;
	uint32 QueryHeapCount;
	TRefCountPtr<ID3D12QueryHeap> ActiveQueryHeap;		// The query heap where all elements reside
	FD3D12ResidencyHandle ActiveQueryHeapResidencyHandle;
	TRefCountPtr<FD3D12Resource> ActiveResultBuffer;	// The buffer where all query results are stored

	static const uint32 ResultSize = 8;					// The byte size of a result for a single query
};

/**
 * A simple linear query allocator.
 * Never resolve or cleanup until results are explicitly requested.
 * Begin/EndQuery are thread-safe but other methods are not. Make sure no thread may
 * call Begin/EndQuery before calling ResolveAndGetResults.
 * Only used by ProfileGPU and FD3D12SubmissionGapRecorder to hold command list start/end timestamp queries currently
 */
class FD3D12LinearQueryHeap final : public FD3D12DeviceChild, public FD3D12SingleNodeGPUObject
{
public:
	enum EHeapState
	{
		HS_Open,
		HS_Closed
	};

	FD3D12LinearQueryHeap(class FD3D12Device* InParent, D3D12_QUERY_HEAP_TYPE InHeapType, int32 InChunkSize);
	~FD3D12LinearQueryHeap();

	/**
	 * Allocate a slot on query heap and queue a BeginQuery command to the given list
	 * @param CmdListHandle - a handle to the command list where BeginQuery will be called
	 * @return index of the allocated query
	 */
	int32 BeginQuery(FD3D12CommandListHandle CmdListHandle);

	/**
	* Allocate a slot on query heap and queue an EndQuery command to the given list
	* @param CmdListHandle - a handle to the command list where EndQuery will be called
	* @return index of the allocated query
	*/
	int32 EndQuery(FD3D12CommandListHandle CmdListHandle);

	/**
	* Resolve new queries and get results for a query batch
	* @param QueryResults - results of the query batch
	* @param Token - a token that identifies the query batch to get results from. Ignored if bWait is true
	* @param bWait - whether to wait for the current resolve and its results
	* @return a new token of the query batch that has just been resolved. INDEX_NONE if bWait is true
	*/
	uint64 ResolveAndGetResults(TArray<uint64>& QueryResults, uint64 Token, bool bWait);

	int32 GetNextFreeIdx() const
	{
		return HeadSlot.Load(EMemoryOrder::Relaxed) - TailSlot;
	}

private:
	static D3D12_QUERY_TYPE HeapTypeToQueryType(D3D12_QUERY_HEAP_TYPE HeapType);

	void GetQueryBatchResults(TArray<uint64>& QueryResults, uint64 Token);

	uint64 StoreQueryBatch(FD3D12CommandListHandle Handle, TRefCountPtr<FD3D12Resource> ResultBuffer, int32 Offset, int32 NumResults);

	/** Returns an index to the allocated heap slot */
	int32 AllocateQueryHeapSlot();
	
	/** Grow the allocator's backing memory */
	void Grow();

	/** Helper to create a new query heap */
	void CreateQueryHeap(int32 NumQueries, ID3D12QueryHeap** OutHeap, FD3D12ResidencyHandle& OutResidencyHandle);

	/** Helper to create a readback buffer used to hold query results */
	void CreateResultBuffer(uint64 SizeInBytes, FD3D12Resource** OutBuffer);

	/** Release all allocated query heaps and detach them from residency manager */
	void ReleaseResources();

	struct FChunk
	{
		TRefCountPtr<ID3D12QueryHeap> QueryHeap;
		FD3D12ResidencyHandle QueryHeapResidencyHandle;
	};

	struct FQueryBatch
	{
		FD3D12CommandListHandle Handle;
		TRefCountPtr<FD3D12Resource> RBuffer;
		uint64 StoredCLGeneration;
		uint64 Token;
		int32 Offset;
		int32 NResults;
	};

	/** Size in bytes of a single query result */
	static constexpr SIZE_T ResultSize = sizeof(uint64);

	const D3D12_QUERY_HEAP_TYPE QueryHeapType;
	const D3D12_QUERY_TYPE QueryType;
	const int32 ChunkSize;
	const int32 SlotToHeapIdxShift;
	TAtomic<int32> HeadSlot;
	int32 TailSlot;
	int32 MaxNumQueries;
	EHeapState HeapState;
	uint64 NextToken;
	TArray<FChunk, TInlineAllocator<2>> AllocatedChunks;
	TArray<FQueryBatch, TInlineAllocator<2>> PendingQueryBatches;
	FCriticalSection CS;
};
