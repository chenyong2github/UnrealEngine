// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	D3D12Query.cpp: D3D query RHI implementation.
=============================================================================*/

#include "D3D12RHIPrivate.h"

namespace D3D12RHI
{
	/**
	* RHI console variables used by queries.
	*/
	namespace RHIConsoleVariables
	{
		int32 bStablePowerState = 0;
		static FAutoConsoleVariableRef CVarStablePowerState(
			TEXT("D3D12.StablePowerState"),
			bStablePowerState,
			TEXT("If true, enable stable power state. This increases GPU timing measurement accuracy but may decrease overall GPU clock rate."),
			ECVF_Default
			);

		int32 GInsertOuterOcclusionQuery = 0;
		static FAutoConsoleVariableRef CVarInsertOuterOcclusionQuery(
			TEXT("D3D12.InsertOuterOcclusionQuery"),
			GInsertOuterOcclusionQuery,
			TEXT("If true, enable a dummy outer occlusion query around occlusion query batches. Can help performance on some GPU architectures"),
			ECVF_Default
		);
		
#if D3D12_SUBMISSION_GAP_RECORDER
		int32 GAdjustRenderQueryTimestamps = 1;
		static FAutoConsoleVariableRef CVarAdjustRenderQueryTimestamps(
			TEXT("D3D12.AdjustRenderQueryTimestamps"),
			GAdjustRenderQueryTimestamps,
			TEXT("If true, this adjusts render query timings to remove gaps between command list submissions\n"),
			ECVF_Default
		);
#endif
	}
}
using namespace D3D12RHI;

FRenderQueryRHIRef FD3D12DynamicRHI::RHICreateRenderQuery(ERenderQueryType QueryType)
{
	FD3D12Adapter* Adapter = &GetAdapter();

	check(QueryType == RQT_Occlusion || QueryType == RQT_AbsoluteTime);

	return Adapter->CreateLinkedObject<FD3D12RenderQuery>(FRHIGPUMask::All(), [QueryType](FD3D12Device* Device)
	{
		return new FD3D12RenderQuery(Device, QueryType);
	});
}

bool FD3D12DynamicRHI::RHIGetRenderQueryResult(FRHIRenderQuery* QueryRHI, uint64& OutResult, bool bWait, uint32 QueryGPUIndex)
{
	check(IsInRenderingThread());
	FD3D12Adapter& Adapter = GetAdapter();

	// First generate the GPU node mask for of the latest queries.
	FRHIGPUMask RelevantNodeMask = FRHIGPUMask::GPU0();
	if (GNumExplicitGPUsForRendering > 1)
	{
		// If we're not getting results for a specific GPU, use the GPU(s) whose query
		// submitted most recently.
		if (QueryGPUIndex == INDEX_NONE)
		{
			uint32 LatestTimestamp = 0;
			for (FD3D12RenderQuery& Query : *FD3D12DynamicRHI::ResourceCast(QueryRHI))
			{
				if (Query.Timestamp > LatestTimestamp)
				{
					RelevantNodeMask = Query.GetParentDevice()->GetGPUMask();
					LatestTimestamp = Query.Timestamp;
				}
				else if (Query.Timestamp == LatestTimestamp)
				{
					RelevantNodeMask |= Query.GetParentDevice()->GetGPUMask();
				}
			}

			if (LatestTimestamp == 0)
			{
				return false;
			}
		}
		else
		{
			RelevantNodeMask = FRHIGPUMask::FromIndex(QueryGPUIndex);
		}
	}

	bool bSuccess = false;
	OutResult = 0;
	for (uint32 GPUIndex : RelevantNodeMask)
	{
		FD3D12CommandContext& DefaultContext = Adapter.GetDevice(GPUIndex)->GetDefaultCommandContext();
		FD3D12RenderQuery* Query = DefaultContext.RetrieveObject<FD3D12RenderQuery>(QueryRHI);

		if (Query->HeapIndex == INDEX_NONE || !Query->bResolved)
		{
			// This query hasn't seen a begin/end before or hasn't been resolved.
			continue;
		}

		if (!Query->bResultIsCached)
		{
			SCOPE_CYCLE_COUNTER(STAT_RenderQueryResultTime);
			if (Query->GetParentDevice()->GetQueryData(*Query, bWait))
			{
				Query->bResultIsCached = true;
			}
			else
			{
				continue;
			}
		}

		if (Query->Type == RQT_AbsoluteTime)
		{
			uint64 AdjustedTimestamp;
#if D3D12_SUBMISSION_GAP_RECORDER
			if (RHIConsoleVariables::GAdjustRenderQueryTimestamps)
			{
				AdjustedTimestamp = Adapter.SubmissionGapRecorder.AdjustTimestampForSubmissionGaps(Query->FrameSubmitted, Query->Result);
			}
			else
#endif
			{
				AdjustedTimestamp = Query->Result;
			}

			// GetTimingFrequency is the number of ticks per second
			uint64 GPUFrequency = FMath::Max(1llu, FGPUTiming::GetTimingFrequency(GPUIndex));
			double CyclesToMicroseconds = 1e6 / double(GPUFrequency);

			double TimeInMicroseconds = (double)AdjustedTimestamp * CyclesToMicroseconds;
			OutResult = FMath::Max((uint64)TimeInMicroseconds, OutResult);

			bSuccess = true;
		}
		else
		{
			OutResult = FMath::Max<uint64>(Query->Result, OutResult);
			bSuccess = true;
		}
	}
	return bSuccess;
}

bool FD3D12Device::GetQueryData(FD3D12RenderQuery& Query, bool bWait)
{
	// Wait for the query result to be ready (if requested).
	const FD3D12CLSyncPoint& SyncPoint = Query.GetSyncPoint();
	if (!SyncPoint.IsComplete())
	{
		if (!bWait)
		{
			return false;
		}

		// It's reasonable to wait for things like occlusion query results. But waiting for timestamps should be avoided.
		UE_CLOG(Query.Type == RQT_AbsoluteTime, LogD3D12RHI, Verbose, TEXT("Waiting for a GPU timestamp query's result to be available. This should be avoided when possible."));

		const uint32 IdleStart = FPlatformTime::Cycles();

		if (SyncPoint.IsOpen())
		{
			// We should really try to avoid this!
			UE_LOG(LogD3D12RHI, Verbose, TEXT("Stalling the RHI thread and flushing GPU commands to wait for a RenderQuery that hasn't been submitted to the GPU yet."));

			// The query is on a command list that hasn't been submitted yet.
			// We need to flush, but the RHI thread may be using the default command list...so stall it first.
			check(IsInRenderingThread());
			FScopedRHIThreadStaller StallRHIThread(FRHICommandListExecutor::GetImmediateCommandList());
			GetDefaultCommandContext().FlushCommands();	// Don't wait yet, since we're stalling the RHI thread.

			// We have to make sure all command lists have actually flush and executed here
			CommandListManager->WaitOnExecuteTask();
		}

		SyncPoint.WaitForCompletion();

		GRenderThreadIdle[ERenderThreadIdleTypes::WaitingForGPUQuery] += FPlatformTime::Cycles() - IdleStart;
		GRenderThreadNumIdle[ERenderThreadIdleTypes::WaitingForGPUQuery]++;
	}

	// Read the data from the query's result buffer.
	const uint32 BeginOffset = Query.HeapIndex * sizeof(Query.Result);
	const CD3DX12_RANGE ReadRange(BeginOffset, BeginOffset + sizeof(Query.Result));
	static const CD3DX12_RANGE EmptyRange(0, 0);

	{
		const FD3D12ScopeMap<uint64> MappedData(Query.ResultBuffer, 0, &ReadRange, &EmptyRange /* Not writing any data */);
		Query.Result = MappedData[Query.HeapIndex];
	}

	return true;
}

void FD3D12CommandContext::RHIBeginOcclusionQueryBatch(uint32 NumQueriesInBatch)
{
	GetParentDevice()->GetOcclusionQueryHeap()->StartQueryBatch(*this, NumQueriesInBatch);
	if (RHIConsoleVariables::GInsertOuterOcclusionQuery)
	{
		if (!OuterOcclusionQuery.IsValid())
		{
			OuterOcclusionQuery = GDynamicRHI->RHICreateRenderQuery(RQT_Occlusion);
		}
		
		FD3D12RenderQuery* OuterOcclusionQueryD3D12 = RetrieveObject<FD3D12RenderQuery>(OuterOcclusionQuery.GetReference());
		GetParentDevice()->GetOcclusionQueryHeap()->BeginQuery(*this, OuterOcclusionQueryD3D12);
		bOuterOcclusionQuerySubmitted = true;
	}
}

void FD3D12CommandContext::RHIEndOcclusionQueryBatch()
{
	if (bOuterOcclusionQuerySubmitted)
	{
		check(OuterOcclusionQuery.IsValid());
		FD3D12RenderQuery* OuterOcclusionQueryD3D12 = RetrieveObject<FD3D12RenderQuery>(OuterOcclusionQuery.GetReference());
		check(OuterOcclusionQueryD3D12->HeapIndex != INDEX_NONE);
		GetParentDevice()->GetOcclusionQueryHeap()->EndQuery(*this, OuterOcclusionQueryD3D12);
		bOuterOcclusionQuerySubmitted = false;
	}
	GetParentDevice()->GetOcclusionQueryHeap()->EndQueryBatchAndResolveQueryData(*this);

	// Note: We want to execute this ASAP. The Engine will call RHISubmitCommandHint after this.
	// We'll break up the command list there so that the wait on the previous frame's results don't block.
}

/*=============================================================================
* class FD3D12QueryHeap
*=============================================================================*/

FD3D12QueryHeap::FD3D12QueryHeap(FD3D12Device* InParent, const D3D12_QUERY_TYPE InQueryType, uint32 InQueryHeapCount, uint32 InMaxActiveBatches)
	: FD3D12DeviceChild(InParent)
	, FD3D12SingleNodeGPUObject(InParent->GetGPUMask())
	, LastBatch(InMaxActiveBatches - 1)
	, ActiveAllocatedElementCount(0)
	, LastAllocatedElement(InQueryHeapCount - 1)
	, QueryType(InQueryType)
	, QueryHeapCount(InQueryHeapCount)
	, ActiveQueryHeap(nullptr)
	, ActiveResultBuffer(nullptr)
{
	check(QueryType == D3D12_QUERY_TYPE_OCCLUSION || QueryType == D3D12_QUERY_TYPE_TIMESTAMP);

	CurrentQueryBatch.Clear();

	ActiveQueryBatches.Reserve(InMaxActiveBatches);
	ActiveQueryBatches.AddZeroed(InMaxActiveBatches);

	// Don't Init() until the RHI has created the device
}

void FD3D12QueryHeap::Init()
{
	check(GetParentDevice());
	check(GetParentDevice()->GetDevice());

	CreateQueryHeap();
}

void FD3D12QueryHeap::Destroy()
{
	DestroyQueryHeap();
}

uint32 FD3D12QueryHeap::GetNextElement(uint32 InElement)
{
	// Increment the provided element
	InElement++;

	// See if we need to wrap around to the begining of the heap
	if (InElement >= QueryHeapCount)
	{
		InElement = 0;
	}

	return InElement;
}

uint32 FD3D12QueryHeap::GetNextBatchElement(uint32 InBatchElement)
{
	// Increment the provided element
	InBatchElement++;

	// See if we need to wrap around to the begining of the heap
	if (InBatchElement >= (uint32) ActiveQueryBatches.Num())
	{
		InBatchElement = 0;
	}

	return InBatchElement;
}

uint32 FD3D12QueryHeap::AllocQuery(FD3D12CommandContext& CmdContext)
{
	check(CmdContext.IsDefaultContext());

	// Get the element for this allocation
	const uint32 CurrentElement = GetNextElement(LastAllocatedElement);

	if (QueryType == D3D12_QUERY_TYPE_OCCLUSION)
	{
		check(CurrentQueryBatch.bOpen);
	}
	else
	{
		if (!CurrentQueryBatch.bOpen)
		{
			StartQueryBatch(CmdContext, 256);
			check(CurrentQueryBatch.bOpen && CurrentQueryBatch.ElementCount == 0);
		}

		if (CurrentQueryBatch.StartElement > CurrentElement)
		{
			// We're in the middle of a batch, but we're at the end of the heap
			// We need to split the batch in two and resolve the first piece
			EndQueryBatchAndResolveQueryData(CmdContext);
		}

		// check for the the batch being closed due to wrap and open a new one
		if (!CurrentQueryBatch.bOpen)
		{
			StartQueryBatch(CmdContext, 256);
			check(CurrentQueryBatch.bOpen && CurrentQueryBatch.ElementCount == 0);
		}
	}

	// Increment the count for the current batch
	CurrentQueryBatch.ElementCount++;

	LastAllocatedElement = CurrentElement;
	check(CurrentElement < QueryHeapCount);
	return CurrentElement;
}

void FD3D12QueryHeap::StartQueryBatch(FD3D12CommandContext& CmdContext, uint32 NumQueriesInBatch)
{
	check(!CurrentQueryBatch.bOpen);

	// Clear the current batch
	CurrentQueryBatch.Clear();

	if (ActiveAllocatedElementCount + NumQueriesInBatch > QueryHeapCount)
	{
		DestroyQueryHeap();

		QueryHeapCount = Align(NumQueriesInBatch + QueryHeapCount, 65536 / ResultSize);

		CreateQueryHeap();

		UE_LOG(LogD3D12RHI, Display, TEXT("QueryHeapCount is now %d elements"), QueryHeapCount);

		ActiveAllocatedElementCount = 0;
		LastAllocatedElement = QueryHeapCount - 1;
	}

	// Start a new batch
	CurrentQueryBatch.StartElement = GetNextElement(LastAllocatedElement);
	CurrentQueryBatch.UsedQueryHeap = ActiveQueryHeap;
	CurrentQueryBatch.UsedResultBuffer = ActiveResultBuffer;
	CurrentQueryBatch.bOpen = true;
}

void FD3D12QueryHeap::EndQueryBatchAndResolveQueryData(FD3D12CommandContext& CmdContextIn)
{
	FD3D12CommandContext* CmdContext = &CmdContextIn;
	if (CmdContext->IsAsyncComputeContext())
	{
		CmdContext = &GetParentDevice()->GetDefaultCommandContext();
	}
	check(CmdContext);
	check(CmdContext->IsDefaultContext());

	if (!CurrentQueryBatch.bOpen)
	{
		return;
	}

	check(CurrentQueryBatch.bOpen);

	// Close the current batch
	CurrentQueryBatch.bOpen = false;

	// Discard empty batches
	if (CurrentQueryBatch.ElementCount == 0)
	{
		return;
	}

	// Increment the active element count
	ActiveAllocatedElementCount += CurrentQueryBatch.ElementCount;
	checkf(ActiveAllocatedElementCount <= QueryHeapCount, TEXT("The query heap is too small. Either increase the heap count (larger resource) or decrease MAX_ACTIVE_BATCHES."));

	// Track the current active batches (application is using the data)
	LastBatch = GetNextBatchElement(LastBatch);
	ActiveQueryBatches[LastBatch] = CurrentQueryBatch;
	
	// Update the active element count if still part of this query heap
	QueryBatch& OldestBatch = ActiveQueryBatches[GetNextBatchElement(LastBatch)];
	if (OldestBatch.UsedQueryHeap == ActiveQueryHeap)
	{
		check(ActiveAllocatedElementCount >= OldestBatch.ElementCount);
		ActiveAllocatedElementCount -= OldestBatch.ElementCount;
	}

	CmdContext->otherWorkCounter++;
	if (CurrentQueryBatch.StartElement + CurrentQueryBatch.ElementCount <= QueryHeapCount)
	{
		// Single range
		CmdContext->CommandListHandle->ResolveQueryData(
			ActiveQueryHeap, QueryType, CurrentQueryBatch.StartElement, CurrentQueryBatch.ElementCount,
			ActiveResultBuffer->GetResource(), GetResultBufferOffsetForElement(CurrentQueryBatch.StartElement));
	}
	else
	{
		// Wrapping around heap border, need two resolves for end of heap and beginning of new range
		CmdContext->CommandListHandle->ResolveQueryData(
			ActiveQueryHeap, QueryType, CurrentQueryBatch.StartElement, QueryHeapCount - CurrentQueryBatch.StartElement,
			ActiveResultBuffer->GetResource(), GetResultBufferOffsetForElement(CurrentQueryBatch.StartElement));
		CmdContext->CommandListHandle->ResolveQueryData(
			ActiveQueryHeap, QueryType, 0, CurrentQueryBatch.ElementCount - (QueryHeapCount - CurrentQueryBatch.StartElement),
			ActiveResultBuffer->GetResource(), 0);
	}

	CmdContext->CommandListHandle.UpdateResidency(&ActiveQueryHeapResidencyHandle);
	CmdContext->CommandListHandle.UpdateResidency(ActiveResultBuffer);

	// For each render query used in this batch, update the command list
	// so we know what sync point to wait for. The query's data isn't ready to read until the above ResolveQueryData completes on the GPU.	
	check(CurrentQueryBatch.UsedResultBuffer == ActiveResultBuffer);
	for (int32 i = 0; i < CurrentQueryBatch.RenderQueries.Num(); i++)
	{
		CurrentQueryBatch.RenderQueries[i]->MarkResolved(CmdContext->CommandListHandle, ActiveResultBuffer);
	}
}

void FD3D12QueryHeap::BeginQuery(FD3D12CommandContext& CmdContext, FD3D12RenderQuery* RenderQuery)
{
	check(CmdContext.IsDefaultContext());
	check(CurrentQueryBatch.bOpen);

	RenderQuery->Reset();
	RenderQuery->HeapIndex = AllocQuery(CmdContext);

	CmdContext.otherWorkCounter++;
	CmdContext.CommandListHandle->BeginQuery(ActiveQueryHeap, QueryType, RenderQuery->HeapIndex);

	CmdContext.CommandListHandle.UpdateResidency(&ActiveQueryHeapResidencyHandle);
}

void FD3D12QueryHeap::EndQuery(FD3D12CommandContext& CmdContext, FD3D12RenderQuery* RenderQuery)
{
	check(CmdContext.IsDefaultContext());

	if (QueryType == D3D12_QUERY_TYPE_OCCLUSION)
	{
		check(CurrentQueryBatch.bOpen);
	}
	else
	{
		RenderQuery->Reset();
		FD3D12Adapter* Adapter = nullptr;
		if (GetParentDevice())
		{
			Adapter = GetParentDevice()->GetParentAdapter();
			if (Adapter)
			{
				RenderQuery->FrameSubmitted = Adapter->GetFrameCount();
			}
		}
		RenderQuery->HeapIndex = AllocQuery(CmdContext);
	}

	CmdContext.otherWorkCounter++;
	CmdContext.CommandListHandle->EndQuery(ActiveQueryHeap, QueryType, RenderQuery->HeapIndex);

	CmdContext.CommandListHandle.UpdateResidency(&ActiveQueryHeapResidencyHandle);

	// Track which render queries are used in this batch.
	CurrentQueryBatch.RenderQueries.Push(RenderQuery);
}

void FD3D12QueryHeap::CreateQueryHeap()
{
	// Setup the query heap desc
	D3D12_QUERY_HEAP_DESC QueryHeapDesc;
	QueryHeapDesc.Type = (QueryType == D3D12_QUERY_TYPE_OCCLUSION)? D3D12_QUERY_HEAP_TYPE_OCCLUSION : D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
	QueryHeapDesc.Count = QueryHeapCount;
	QueryHeapDesc.NodeMask = GetGPUMask().GetNative();

	// Create the upload heap
	VERIFYD3D12RESULT(GetParentDevice()->GetDevice()->CreateQueryHeap(&QueryHeapDesc, IID_PPV_ARGS(ActiveQueryHeap.GetInitReference())));
	SetName(ActiveQueryHeap, L"Query Heap");

#if ENABLE_RESIDENCY_MANAGEMENT
	D3DX12Residency::Initialize(ActiveQueryHeapResidencyHandle, ActiveQueryHeap, ResultSize * QueryHeapDesc.Count);
	D3DX12Residency::BeginTrackingObject(GetParentDevice()->GetResidencyManager(), ActiveQueryHeapResidencyHandle);
#endif

	FD3D12Adapter* Adapter = GetParentDevice()->GetParentAdapter();

	const D3D12_HEAP_PROPERTIES ResultBufferHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK, GetGPUMask().GetNative(), GetVisibilityMask().GetNative());
	const D3D12_RESOURCE_DESC ResultBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(ResultSize * QueryHeapDesc.Count); // Each query's result occupies ResultSize bytes.

	// Create the readback heap
	VERIFYD3D12RESULT(Adapter->CreateCommittedResource(
		ResultBufferDesc,
		GetGPUMask(),
		ResultBufferHeapProperties,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		ActiveResultBuffer.GetInitReference(),
		TEXT("Query Heap Result Buffer")));
}

void FD3D12QueryHeap::DestroyQueryHeap()
{
#if ENABLE_RESIDENCY_MANAGEMENT
	if (D3DX12Residency::IsInitialized(ActiveQueryHeapResidencyHandle))
	{
		D3DX12Residency::EndTrackingObject(GetParentDevice()->GetResidencyManager(), ActiveQueryHeapResidencyHandle);
		ActiveQueryHeapResidencyHandle = {};
	}
#endif

	ActiveQueryHeap = nullptr;
	ActiveResultBuffer = nullptr;
}

/*=============================================================================
* class FD3D12LinearQueryHeap
*=============================================================================*/

FD3D12LinearQueryHeap::FD3D12LinearQueryHeap(FD3D12Device* InParent, D3D12_QUERY_HEAP_TYPE InHeapType, int32 InChunkSize)
	: FD3D12DeviceChild(InParent)
	, FD3D12SingleNodeGPUObject(InParent->GetGPUMask())
	, QueryHeapType(InHeapType)
	, QueryType(HeapTypeToQueryType(InHeapType))
	, ChunkSize(InChunkSize)
	, SlotToHeapIdxShift(FPlatformMath::CountBits(ChunkSize - 1))
	, HeadSlot(0)
	, TailSlot(0)
	, MaxNumQueries(0)
	, HeapState(HS_Open)
	, NextToken(0)
{
	check(ChunkSize > 0 && !(ChunkSize & (ChunkSize - 1)));
}

FD3D12LinearQueryHeap::~FD3D12LinearQueryHeap()
{
	ReleaseResources();
}

int32 FD3D12LinearQueryHeap::BeginQuery(FD3D12CommandListHandle CmdListHandle)
{
	const int32 SlotIdx = AllocateQueryHeapSlot();
	const int32 HeapIdx = SlotIdx >> SlotToHeapIdxShift;
	const int32 Offset = SlotIdx & (ChunkSize - 1);

	FChunk& Chunk = AllocatedChunks[HeapIdx];
	CmdListHandle->BeginQuery(Chunk.QueryHeap, QueryType, Offset);
	CmdListHandle.UpdateResidency(&Chunk.QueryHeapResidencyHandle);
	FD3D12CommandContext* Context = CmdListHandle.GetCurrentOwningContext();
	if (Context)
	{
		++Context->otherWorkCounter;
	}
	return SlotIdx - TailSlot;
}

int32 FD3D12LinearQueryHeap::EndQuery(FD3D12CommandListHandle CmdListHandle)
{
	const int32 SlotIdx = AllocateQueryHeapSlot();
	const int32 HeapIdx = SlotIdx >> SlotToHeapIdxShift;
	const int32 Offset = SlotIdx & (ChunkSize - 1);

	FChunk& Chunk = AllocatedChunks[HeapIdx];
	CmdListHandle->EndQuery(Chunk.QueryHeap, QueryType, Offset);
	CmdListHandle.UpdateResidency(&Chunk.QueryHeapResidencyHandle);
	FD3D12CommandContext* Context = CmdListHandle.GetCurrentOwningContext();
	if (Context)
	{
		++Context->otherWorkCounter;
	}
	return SlotIdx - TailSlot;
}

uint64 FD3D12LinearQueryHeap::ResolveAndGetResults(TArray<uint64>& QueryResults, uint64 Token, bool bWait)
{
	HeapState = HS_Closed;
	
	if (!bWait)
	{
		GetQueryBatchResults(QueryResults, Token);
	}

	FD3D12CommandContext& Context = GetParentDevice()->GetDefaultCommandContext();
	const int32 LocalHead = HeadSlot.Load(EMemoryOrder::Relaxed);
	const int32 NumActiveQueries = LocalHead - TailSlot;
	uint64 NewToken = INDEX_NONE;

	if (NumActiveQueries > 0)
	{
		const uint64 ResultBuffSize = ResultSize * NumActiveQueries;
		TRefCountPtr<FD3D12Resource> ResultBuff;
		CreateResultBuffer(ResultBuffSize, ResultBuff.GetInitReference());

		++Context.otherWorkCounter;
		const int32 StartHeapIdx = TailSlot >> SlotToHeapIdxShift;
		const int32 EndHeapIdx = (LocalHead - 1) >> SlotToHeapIdxShift;

		for (int32 HeapIdx = StartHeapIdx; HeapIdx <= EndHeapIdx; ++HeapIdx)
		{
			const int32 HeapStart = HeapIdx << SlotToHeapIdxShift;
			const int32 Offset = FMath::Max(HeapStart, TailSlot) - HeapStart;
			const int32 NumQueries = FMath::Min((HeapIdx + 1) << SlotToHeapIdxShift, LocalHead) - (HeapStart + Offset);
			check(NumQueries <= ChunkSize);
			FChunk& Chunk = AllocatedChunks[HeapIdx];
			Context.CommandListHandle->ResolveQueryData(Chunk.QueryHeap, QueryType, Offset, NumQueries, ResultBuff->GetResource(), ResultSize * (HeapStart + Offset - TailSlot));
			Context.CommandListHandle.UpdateResidency(&Chunk.QueryHeapResidencyHandle);
			Context.CommandListHandle.UpdateResidency(ResultBuff);
		}

		NewToken = StoreQueryBatch(Context.CommandListHandle, ResultBuff, TailSlot, NumActiveQueries);
		TailSlot = LocalHead;
	}

	if (bWait && NewToken != INDEX_NONE)
	{
		Context.FlushCommands(true);
		GetQueryBatchResults(QueryResults, NewToken);
		NewToken = INDEX_NONE;
	}

	HeapState = HS_Open;
	return NewToken;
}

uint64 FD3D12LinearQueryHeap::StoreQueryBatch(FD3D12CommandListHandle Handle, TRefCountPtr<FD3D12Resource> ResultBuffer, int32 Offset, int32 NumResults)
{
	FQueryBatch& QueryBatch = PendingQueryBatches[PendingQueryBatches.AddDefaulted()];
	QueryBatch.Handle = Handle;
	QueryBatch.RBuffer = ResultBuffer;
	QueryBatch.StoredCLGeneration = Handle.CurrentGeneration();
	QueryBatch.Token = NextToken++;
	QueryBatch.Offset = Offset;
	QueryBatch.NResults = NumResults;

	UE_LOG(LogD3D12GapRecorder, VeryVerbose, TEXT("Storing Query NumResults %d"), QueryBatch.NResults);
	return QueryBatch.Token;
}

void FD3D12LinearQueryHeap::GetQueryBatchResults(TArray<uint64>& QueryResults, uint64 Token)
{
	FQueryBatch* QueryBatch = nullptr;
	for (int32 Idx = 0; Idx < PendingQueryBatches.Num(); ++Idx)
	{
		if (PendingQueryBatches[Idx].Token == Token)
		{
			QueryBatch = &PendingQueryBatches[Idx];
			break;
		}
	}

	if (QueryBatch)
	{
		if (!QueryBatch->Handle.IsComplete(QueryBatch->StoredCLGeneration))
		{
			QueryBatch->Handle.WaitForCompletion(QueryBatch->StoredCLGeneration);
		}

		const uint64 ResultBuffSize = ResultSize * QueryBatch->NResults;
		QueryResults.Empty(QueryBatch->NResults);
		QueryResults.AddUninitialized(QueryBatch->NResults);

		UE_LOG(LogD3D12GapRecorder, VeryVerbose, TEXT("Result Buffer NResults %lu Buffer Size %lu"), QueryBatch->NResults, ResultBuffSize);

		void* MappedResult = nullptr;
		VERIFYD3D12RESULT(QueryBatch->RBuffer->GetResource()->Map(0, nullptr, &MappedResult));
		FMemory::Memcpy(QueryResults.GetData(), MappedResult, ResultBuffSize);

		UE_LOG(LogD3D12GapRecorder, VeryVerbose, TEXT("Query Results Length %d"), QueryResults.Num());

		QueryBatch->RBuffer->GetResource()->Unmap(0, nullptr);

		int32 TmpSlot = 0;
		for (int32 Idx = PendingQueryBatches.Num() - 1; Idx >= 0; --Idx)
		{
			const FQueryBatch& Batch = PendingQueryBatches[Idx];
			if (Batch.Token <= Token)
			{
				TmpSlot = FMath::Max(TmpSlot, Batch.Offset + Batch.NResults);
				PendingQueryBatches.RemoveAt(Idx);
			}
		}

		const int32 TmpHeapIdx = TmpSlot >> SlotToHeapIdxShift;
		if (TmpHeapIdx > 0)
		{
			const int32 NumActiveChunks = AllocatedChunks.Num() - TmpHeapIdx;
			TArray<TAlignedBytes<sizeof(FChunk), 1>, TInlineAllocator<1>> TmpStorage;
			TmpStorage.AddUninitialized(TmpHeapIdx);
			FMemory::Memcpy(TmpStorage.GetData(), AllocatedChunks.GetData(), TmpHeapIdx * sizeof(FChunk));
			FMemory::Memmove(AllocatedChunks.GetData(), AllocatedChunks.GetData() + TmpHeapIdx, NumActiveChunks * sizeof(FChunk));
			FMemory::Memcpy(AllocatedChunks.GetData() + NumActiveChunks, TmpStorage.GetData(), TmpHeapIdx * sizeof(FChunk));

			const int32 Offset = TmpHeapIdx << SlotToHeapIdxShift;
			TailSlot -= Offset;
			HeadSlot -= Offset;
			for (int32 Idx = 0; Idx < PendingQueryBatches.Num(); ++Idx)
			{
				PendingQueryBatches[Idx].Offset -= Offset;
			}
		}
	}
}

D3D12_QUERY_TYPE FD3D12LinearQueryHeap::HeapTypeToQueryType(D3D12_QUERY_HEAP_TYPE HeapType)
{
	switch (HeapType)
	{
	case D3D12_QUERY_HEAP_TYPE_OCCLUSION:
		return D3D12_QUERY_TYPE_OCCLUSION;
	case D3D12_QUERY_HEAP_TYPE_TIMESTAMP:
		return D3D12_QUERY_TYPE_TIMESTAMP;
	default:
		check(false);
		return (D3D12_QUERY_TYPE)-1;
	}
}

int32 FD3D12LinearQueryHeap::AllocateQueryHeapSlot()
{
	check(HeapState == HS_Open);
	const int32 SlotIdx = HeadSlot++;

	if (SlotIdx >= MaxNumQueries)
	{
		FScopeLock Lock(&CS);
		while (SlotIdx >= MaxNumQueries)
		{
			Grow();
		}
	}
	return SlotIdx;
}

void FD3D12LinearQueryHeap::Grow()
{
	FChunk& NewChunk = AllocatedChunks[AllocatedChunks.AddDefaulted()];
	CreateQueryHeap(ChunkSize, NewChunk.QueryHeap.GetInitReference(), NewChunk.QueryHeapResidencyHandle);
	MaxNumQueries += ChunkSize;
}

void FD3D12LinearQueryHeap::CreateQueryHeap(int32 NumQueries, ID3D12QueryHeap** OutHeap, FD3D12ResidencyHandle& OutResidencyHandle)
{
	D3D12_QUERY_HEAP_DESC Desc;
	Desc.Type = QueryHeapType;
	Desc.Count = static_cast<uint32>(NumQueries);
	Desc.NodeMask = GetGPUMask().GetNative();
	VERIFYD3D12RESULT(GetParentDevice()->GetDevice()->CreateQueryHeap(&Desc, IID_PPV_ARGS(OutHeap)));
	SetName(*OutHeap, TEXT("FD3D12LinearQueryHeap"));

#if ENABLE_RESIDENCY_MANAGEMENT
	D3DX12Residency::Initialize(OutResidencyHandle, *OutHeap, ResultSize * Desc.Count);
	D3DX12Residency::BeginTrackingObject(GetParentDevice()->GetResidencyManager(), OutResidencyHandle);
#endif
}

void FD3D12LinearQueryHeap::CreateResultBuffer(uint64 SizeInBytes, FD3D12Resource** OutBuffer)
{
	FD3D12Adapter* Adapter = GetParentDevice()->GetParentAdapter();
	const D3D12_HEAP_PROPERTIES ResultBufferHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK, GetGPUMask().GetNative(), GetVisibilityMask().GetNative());
	const D3D12_RESOURCE_DESC ResultBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(SizeInBytes);

	VERIFYD3D12RESULT(Adapter->CreateCommittedResource(
		ResultBufferDesc,
		GetGPUMask(),
		ResultBufferHeapProperties,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		OutBuffer,
		TEXT("FD3D12LinearQueryHeap Result Buffer")));
}

void FD3D12LinearQueryHeap::ReleaseResources()
{
#if ENABLE_RESIDENCY_MANAGEMENT
	for (int32 Idx = 0; Idx < AllocatedChunks.Num(); ++Idx)
	{
		FChunk& Chunk = AllocatedChunks[Idx];
		if (D3DX12Residency::IsInitialized(Chunk.QueryHeapResidencyHandle))
		{
			D3DX12Residency::EndTrackingObject(GetParentDevice()->GetResidencyManager(), Chunk.QueryHeapResidencyHandle);
			Chunk.QueryHeapResidencyHandle = {};
		}
	}
#endif
}

/*=============================================================================
 * class FD3D12BufferedGPUTiming
 *=============================================================================*/

 /**
  * Constructor.
  *
  * @param InD3DRHI			RHI interface
  * @param InBufferSize		Number of buffered measurements
  */
FD3D12BufferedGPUTiming::FD3D12BufferedGPUTiming(FD3D12Device* InParent, int32 InBufferSize)
	: FD3D12DeviceChild(InParent)
	, BufferSize(InBufferSize)
	, CurrentTimestamp(-1)
	, NumIssuedTimestamps(0)
	, TimestampQueryHeap(nullptr)
	, TimestampQueryHeapBuffer(nullptr)
	, bIsTiming(false)
	, bStablePowerState(false)
{
}

/**
 * Initializes the static variables, if necessary.
 */
void FD3D12BufferedGPUTiming::PlatformStaticInitialize(void* UserData)
{
	// Are the static variables initialized?
	check(!GAreGlobalsInitialized);

	FD3D12Adapter* ParentAdapter = (FD3D12Adapter*)UserData;
	CalibrateTimers(ParentAdapter);
}

void FD3D12BufferedGPUTiming::CalibrateTimers(FD3D12Adapter* ParentAdapter)
{
	for (uint32 GPUIndex : FRHIGPUMask::All())
	{
		uint64 TimingFrequency = 0;
		VERIFYD3D12RESULT(ParentAdapter->GetDevice(GPUIndex)->GetCommandListManager().GetTimestampFrequency(&TimingFrequency));
		SetTimingFrequency(TimingFrequency, GPUIndex);
		FGPUTimingCalibrationTimestamp CalibrationTimestamp = ParentAdapter->GetDevice(GPUIndex)->GetCommandListManager().GetCalibrationTimestamp();
		SetCalibrationTimestamp(CalibrationTimestamp, GPUIndex);
	}
}

void FD3D12DynamicRHI::RHICalibrateTimers()
{
	check(IsInRenderingThread());

	FScopedRHIThreadStaller StallRHIThread(FRHICommandListExecutor::GetImmediateCommandList());

	FD3D12Adapter& Adapter = GetAdapter();
	FD3D12BufferedGPUTiming::CalibrateTimers(&Adapter);
}

/**
 * Initializes all D3D resources and if necessary, the static variables.
 */
void FD3D12BufferedGPUTiming::InitDynamicRHI()
{
	FD3D12Device* Device = GetParentDevice();
	FD3D12Adapter* Adapter = Device->GetParentAdapter();
	ID3D12Device* D3DDevice = Device->GetDevice();
	const FRHIGPUMask Node = FRHIGPUMask::All();

	// StaticInitialize operates on all devices so only call it once.
	static bool bStaticInitialized = false;
	if (!bStaticInitialized)
	{
		StaticInitialize(Adapter, PlatformStaticInitialize);
		bStaticInitialized = true;
	}

	CurrentTimestamp = 0;
	NumIssuedTimestamps = 0;
	bIsTiming = false;

	// Now initialize the queries and backing buffers for this timing object.
	if (GIsSupported)
	{
		D3D12_QUERY_HEAP_DESC QueryHeapDesc = {};
		QueryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
		QueryHeapDesc.Count = BufferSize * 2;	// Space for each Start + End pair.

		TimestampQueryHeap = Adapter->CreateLinkedObject<QueryHeap>(Device->GetGPUMask(), [&] (FD3D12Device* Device)
		{
			QueryHeap* NewHeap = new QueryHeap(Device);
			QueryHeapDesc.NodeMask = Device->GetGPUMask().GetNative();
			VERIFYD3D12RESULT(D3DDevice->CreateQueryHeap(&QueryHeapDesc, IID_PPV_ARGS(NewHeap->Heap.GetInitReference())));
			SetName(NewHeap->Heap, L"FD3D12BufferedGPUTiming: Timestamp Query Heap");

		#if ENABLE_RESIDENCY_MANAGEMENT
			D3DX12Residency::Initialize(NewHeap->ResidencyHandle, NewHeap->Heap.GetReference(), 8ull * QueryHeapDesc.Count);
			D3DX12Residency::BeginTrackingObject(Device->GetResidencyManager(), NewHeap->ResidencyHandle);
		#endif

			return NewHeap;
		});


		const uint64 Size = 8ull * QueryHeapDesc.Count; // Each timestamp query occupies 8 bytes.
		Adapter->CreateBuffer(D3D12_HEAP_TYPE_READBACK, Device->GetGPUMask(), Node, D3D12_RESOURCE_STATE_COPY_DEST, ED3D12ResourceStateMode::Default, Size, TimestampQueryHeapBuffer.GetInitReference(), TEXT("FD3D12BufferedGPUTiming: Timestamp Query Result Buffer"));

		TimestampListHandles.AddZeroed(QueryHeapDesc.Count);
	}
}

/**
 * Releases all D3D resources.
 */
void FD3D12BufferedGPUTiming::ReleaseDynamicRHI()
{
#if ENABLE_RESIDENCY_MANAGEMENT
	if (D3DX12Residency::IsInitialized(TimestampQueryHeap->ResidencyHandle))
	{
		D3DX12Residency::EndTrackingObject(GetParentDevice()->GetResidencyManager(), TimestampQueryHeap->ResidencyHandle);
	}
#endif

	delete(TimestampQueryHeap);
	TimestampQueryHeap = nullptr;
	TimestampQueryHeapBuffer = nullptr;

	TimestampListHandles.Reset();
}

/**
 * Start a GPU timing measurement.
 */
void FD3D12BufferedGPUTiming::StartTiming()
{
	FD3D12Device* Device = GetParentDevice();
	ID3D12Device* D3DDevice = Device->GetDevice();

	// Issue a timestamp query for the 'start' time.
	if (GIsSupported && !bIsTiming)
	{
		// Check to see if stable power state cvar has changed
		const bool bStablePowerStateCVar = RHIConsoleVariables::bStablePowerState != 0;
		if (bStablePowerState != bStablePowerStateCVar)
		{
			if (SUCCEEDED(D3DDevice->SetStablePowerState(bStablePowerStateCVar)))
			{
				// SetStablePowerState succeeded. Update timing frequency.
				uint64 TimingFrequency;
				VERIFYD3D12RESULT(Device->GetCommandListManager().GetTimestampFrequency(&TimingFrequency));
				SetTimingFrequency(TimingFrequency, Device->GetGPUIndex());
				bStablePowerState = bStablePowerStateCVar;
			}
			else
			{
				// SetStablePowerState failed. This can occur if SDKLayers is not present on the system.
				RHIConsoleVariables::CVarStablePowerState->Set(0, ECVF_SetByConsole);
			}
		}

		CurrentTimestamp = (CurrentTimestamp + 1) % BufferSize;

		const uint32 QueryStartIndex = GetStartTimestampIndex(CurrentTimestamp);

		FD3D12CommandContext& CmdContext = Device->GetDefaultCommandContext();

		CmdContext.otherWorkCounter++;

		QueryHeap* CurrentQH = CmdContext.RetrieveObject<QueryHeap>(TimestampQueryHeap);
		CmdContext.CommandListHandle->EndQuery(CurrentQH->Heap, D3D12_QUERY_TYPE_TIMESTAMP, QueryStartIndex);
		CmdContext.CommandListHandle.UpdateResidency(&CurrentQH->ResidencyHandle);

		TimestampListHandles[QueryStartIndex] = CmdContext.CommandListHandle;
		bIsTiming = true;
	}
}

/**
 * End a GPU timing measurement.
 * The timing for this particular measurement will be resolved at a later time by the GPU.
 */
void FD3D12BufferedGPUTiming::EndTiming()
{
	// Issue a timestamp query for the 'end' time.
	if (GIsSupported && bIsTiming)
	{
		check(CurrentTimestamp >= 0 && CurrentTimestamp < BufferSize);
		const uint32 QueryStartIndex = GetStartTimestampIndex(CurrentTimestamp);
		const uint32 QueryEndIndex = GetEndTimestampIndex(CurrentTimestamp);
		check(QueryEndIndex == QueryStartIndex + 1);	// Make sure they're adjacent indices.

		FD3D12CommandContext& CmdContext = GetParentDevice()->GetDefaultCommandContext();

		CmdContext.otherWorkCounter += 2;

		QueryHeap* CurrentQH = CmdContext.RetrieveObject<QueryHeap>(TimestampQueryHeap);

		CmdContext.CommandListHandle->EndQuery(CurrentQH->Heap, D3D12_QUERY_TYPE_TIMESTAMP, QueryEndIndex);
		CmdContext.CommandListHandle->ResolveQueryData(CurrentQH->Heap, D3D12_QUERY_TYPE_TIMESTAMP, QueryStartIndex, 2, TimestampQueryHeapBuffer->GetResource(), 8 * QueryStartIndex);
		CmdContext.CommandListHandle.UpdateResidency(&CurrentQH->ResidencyHandle);
		CmdContext.CommandListHandle.UpdateResidency(TimestampQueryHeapBuffer.GetReference());

		TimestampListHandles[QueryEndIndex] = CmdContext.CommandListHandle;
		NumIssuedTimestamps = FMath::Min<int32>(NumIssuedTimestamps + 1, BufferSize);
		bIsTiming = false;
	}
}

/**
 * Retrieves the most recently resolved timing measurement.
 * The unit is the same as for FPlatformTime::Cycles(). Returns 0 if there are no resolved measurements.
 *
 * @return	Value of the most recently resolved timing, or 0 if no measurements have been resolved by the GPU yet.
 */
uint64 FD3D12BufferedGPUTiming::GetTiming(bool bGetCurrentResultsAndBlock)
{
	FD3D12Device* Device = GetParentDevice();

	if (GIsSupported)
	{
		check(CurrentTimestamp >= 0 && CurrentTimestamp < BufferSize);
		uint64 StartTime, EndTime;
		static const CD3DX12_RANGE EmptyRange(0, 0);

		FD3D12CommandListManager& CommandListManager = Device->GetCommandListManager();

		int32 TimestampIndex = CurrentTimestamp;
		if (!bGetCurrentResultsAndBlock)
		{
			// Quickly check the most recent measurements to see if any of them has been resolved.  Do not flush these queries.
			for (int32 IssueIndex = 1; IssueIndex < NumIssuedTimestamps; ++IssueIndex)
			{
				const uint32 QueryStartIndex = GetStartTimestampIndex(TimestampIndex);
				const uint32 QueryEndIndex = GetEndTimestampIndex(TimestampIndex);
				const FD3D12CLSyncPoint& StartQuerySyncPoint = TimestampListHandles[QueryStartIndex];
				const FD3D12CLSyncPoint& EndQuerySyncPoint = TimestampListHandles[QueryEndIndex];
				if (EndQuerySyncPoint.IsComplete() && StartQuerySyncPoint.IsComplete())
				{
					// Scope map the result range for read.
					const CD3DX12_RANGE ReadRange(QueryStartIndex * sizeof(uint64), (QueryEndIndex + 1) * sizeof(uint64));
					const FD3D12ScopeMap<uint64> MappedTimestampData(TimestampQueryHeapBuffer, 0, &ReadRange, &EmptyRange /* Not writing any data */);
					StartTime = MappedTimestampData[QueryStartIndex];
					EndTime = MappedTimestampData[QueryEndIndex];
					
					if (EndTime > StartTime)
					{
						const uint64 Bubble = Device->GetGPUProfiler().CalculateIdleTime(StartTime, EndTime);
						const uint64 ElapsedTime = EndTime - StartTime;
						return ElapsedTime >= Bubble ? ElapsedTime - Bubble : 0;
					}
				}

				TimestampIndex = (TimestampIndex + BufferSize - 1) % BufferSize;
			}
		}

		if (NumIssuedTimestamps > 0 || bGetCurrentResultsAndBlock)
		{
			// None of the (NumIssuedTimestamps - 1) measurements were ready yet,
			// so check the oldest measurement more thoroughly.
			// This really only happens if occlusion and frame sync event queries are disabled, otherwise those will block until the GPU catches up to 1 frame behind

			const bool bBlocking = (NumIssuedTimestamps == BufferSize) || bGetCurrentResultsAndBlock;
			const uint32 IdleStart = FPlatformTime::Cycles();

			SCOPE_CYCLE_COUNTER(STAT_RenderQueryResultTime);

			const uint32 QueryStartIndex = GetStartTimestampIndex(TimestampIndex);
			const uint32 QueryEndIndex = GetEndTimestampIndex(TimestampIndex);

			if (bBlocking)
			{
				const FD3D12CLSyncPoint& StartQuerySyncPoint = TimestampListHandles[QueryStartIndex];
				const FD3D12CLSyncPoint& EndQuerySyncPoint = TimestampListHandles[QueryEndIndex];
				if (EndQuerySyncPoint.IsOpen() || StartQuerySyncPoint.IsOpen())
				{
					// Need to submit the open command lists.
					Device->GetDefaultCommandContext().FlushCommands();
				}

				// CPU wait for query results to be ready.
				StartQuerySyncPoint.WaitForCompletion();
				EndQuerySyncPoint.WaitForCompletion();
			}

			GRenderThreadIdle[ERenderThreadIdleTypes::WaitingForGPUQuery] += FPlatformTime::Cycles() - IdleStart;
			GRenderThreadNumIdle[ERenderThreadIdleTypes::WaitingForGPUQuery]++;

			// Scope map the result range for read.
			const CD3DX12_RANGE ReadRange(QueryStartIndex * sizeof(uint64), (QueryEndIndex + 1) * sizeof(uint64));
			const FD3D12ScopeMap<uint64> MappedTimestampData(TimestampQueryHeapBuffer, 0, &ReadRange, &EmptyRange /* Not writing any data */);
			StartTime = MappedTimestampData[QueryStartIndex];
			EndTime = MappedTimestampData[QueryEndIndex];

			if (EndTime > StartTime)
			{
				const uint64 Bubble = Device->GetGPUProfiler().CalculateIdleTime(StartTime, EndTime);
				const uint64 ElapsedTime = EndTime - StartTime;
				return ElapsedTime >= Bubble ? ElapsedTime - Bubble : 0;
			}
		}
	}

	return 0;
}