// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
StaticMeshUpdate.cpp: Helpers to stream in and out static mesh LODs.
=============================================================================*/

#include "Streaming/StaticMeshUpdate.h"
#include "RenderUtils.h"
#include "Containers/ResourceArray.h"
#include "Streaming/TextureStreamingHelpers.h"
#include "HAL/PlatformFilemanager.h"
#include "Serialization/MemoryReader.h"
#include "Streaming/RenderAssetUpdate.inl"

// Instantiate TRenderAssetUpdate for FStaticMeshUpdateContext
template class TRenderAssetUpdate<FStaticMeshUpdateContext>;

static constexpr uint32 GStaticMeshMaxNumResourceUpdatesPerLOD = 14;
static constexpr uint32 GStaticMeshMaxNumResourceUpdatesPerBatch = (MAX_STATIC_MESH_LODS - 1) * GStaticMeshMaxNumResourceUpdatesPerLOD;

FStaticMeshUpdateContext::FStaticMeshUpdateContext(UStaticMesh* InMesh, EThreadType InCurrentThread)
	: Mesh(InMesh)
	, CurrentThread(InCurrentThread)
{
	check(InMesh);
	checkSlow(InCurrentThread != FStaticMeshUpdate::TT_Render || IsInRenderingThread());
	RenderData = Mesh->RenderData.Get();
}

FStaticMeshUpdateContext::FStaticMeshUpdateContext(UStreamableRenderAsset* InMesh, EThreadType InCurrentThread)
#if UE_BUILD_SHIPPING
	: FStaticMeshUpdateContext(static_cast<UStaticMesh*>(InMesh), InCurrentThread)
#else
	: FStaticMeshUpdateContext(Cast<UStaticMesh>(InMesh), InCurrentThread)
#endif
{}

FStaticMeshUpdate::FStaticMeshUpdate(UStaticMesh* InMesh, int32 InRequestedMips)
	: TRenderAssetUpdate<FStaticMeshUpdateContext>(InMesh, InRequestedMips)
{
	if (InMesh->RenderData)
	{
		CurrentFirstLODIdx = InMesh->RenderData->CurrentFirstLODIdx;
		check(CurrentFirstLODIdx >= 0 && CurrentFirstLODIdx < MAX_MESH_LOD_COUNT);
	}
	else
	{
		RequestedMips = INDEX_NONE;
		PendingFirstMip = INDEX_NONE;
		bIsCancelled = true;
		CurrentFirstLODIdx = INDEX_NONE;
	}
}

void FStaticMeshStreamIn::FIntermediateBuffers::CreateFromCPUData_RenderThread(UStaticMesh* Mesh, FStaticMeshLODResources& LODResource)
{
	LODResource.ConditionalForce16BitIndexBuffer(GMaxRHIShaderPlatform, Mesh);
	FStaticMeshVertexBuffers& VBs = LODResource.VertexBuffers;
	TangentsVertexBuffer = VBs.StaticMeshVertexBuffer.CreateTangentsRHIBuffer_RenderThread();
	TexCoordVertexBuffer = VBs.StaticMeshVertexBuffer.CreateTexCoordRHIBuffer_RenderThread();
	PositionVertexBuffer = VBs.PositionVertexBuffer.CreateRHIBuffer_RenderThread();
	ColorVertexBuffer = VBs.ColorVertexBuffer.CreateRHIBuffer_RenderThread();
	IndexBuffer = LODResource.IndexBuffer.CreateRHIBuffer_RenderThread();
	DepthOnlyIndexBuffer = LODResource.DepthOnlyIndexBuffer.CreateRHIBuffer_RenderThread();


	if (LODResource.AdditionalIndexBuffers)
	{
		ReversedIndexBuffer = LODResource.AdditionalIndexBuffers->ReversedIndexBuffer.CreateRHIBuffer_RenderThread();
		ReversedDepthOnlyIndexBuffer = LODResource.AdditionalIndexBuffers->ReversedDepthOnlyIndexBuffer.CreateRHIBuffer_RenderThread();
		WireframeIndexBuffer = LODResource.AdditionalIndexBuffers->WireframeIndexBuffer.CreateRHIBuffer_RenderThread();
		AdjacencyIndexBuffer = LODResource.AdditionalIndexBuffers->AdjacencyIndexBuffer.CreateRHIBuffer_RenderThread();
	}
}

void FStaticMeshStreamIn::FIntermediateBuffers::CreateFromCPUData_Async(UStaticMesh* Mesh, FStaticMeshLODResources& LODResource)
{
	LODResource.ConditionalForce16BitIndexBuffer(GMaxRHIShaderPlatform, Mesh);
	FStaticMeshVertexBuffers& VBs = LODResource.VertexBuffers;
	TangentsVertexBuffer = VBs.StaticMeshVertexBuffer.CreateTangentsRHIBuffer_Async();
	TexCoordVertexBuffer = VBs.StaticMeshVertexBuffer.CreateTexCoordRHIBuffer_Async();
	PositionVertexBuffer = VBs.PositionVertexBuffer.CreateRHIBuffer_Async();
	ColorVertexBuffer = VBs.ColorVertexBuffer.CreateRHIBuffer_Async();
	IndexBuffer = LODResource.IndexBuffer.CreateRHIBuffer_Async();
	DepthOnlyIndexBuffer = LODResource.DepthOnlyIndexBuffer.CreateRHIBuffer_Async();

	if (LODResource.AdditionalIndexBuffers)
	{
		ReversedIndexBuffer = LODResource.AdditionalIndexBuffers->ReversedIndexBuffer.CreateRHIBuffer_Async();
		ReversedDepthOnlyIndexBuffer = LODResource.AdditionalIndexBuffers->ReversedDepthOnlyIndexBuffer.CreateRHIBuffer_Async();
		WireframeIndexBuffer = LODResource.AdditionalIndexBuffers->WireframeIndexBuffer.CreateRHIBuffer_Async();
		AdjacencyIndexBuffer = LODResource.AdditionalIndexBuffers->AdjacencyIndexBuffer.CreateRHIBuffer_Async();
	}
}

void FStaticMeshStreamIn::FIntermediateBuffers::SafeRelease()
{
	TangentsVertexBuffer.SafeRelease();
	TexCoordVertexBuffer.SafeRelease();
	PositionVertexBuffer.SafeRelease();
	ColorVertexBuffer.SafeRelease();
	IndexBuffer.SafeRelease();
	ReversedIndexBuffer.SafeRelease();
	DepthOnlyIndexBuffer.SafeRelease();
	ReversedDepthOnlyIndexBuffer.SafeRelease();
	WireframeIndexBuffer.SafeRelease();
	AdjacencyIndexBuffer.SafeRelease();
}

template <uint32 MaxNumUpdates>
void FStaticMeshStreamIn::FIntermediateBuffers::TransferBuffers(FStaticMeshLODResources& LODResource, TRHIResourceUpdateBatcher<MaxNumUpdates>& Batcher)
{
	FStaticMeshVertexBuffers& VBs = LODResource.VertexBuffers;
	VBs.StaticMeshVertexBuffer.InitRHIForStreaming(TangentsVertexBuffer, TexCoordVertexBuffer, Batcher);
	VBs.PositionVertexBuffer.InitRHIForStreaming(PositionVertexBuffer, Batcher);
	VBs.ColorVertexBuffer.InitRHIForStreaming(ColorVertexBuffer, Batcher);
	LODResource.IndexBuffer.InitRHIForStreaming(IndexBuffer, Batcher);
	LODResource.DepthOnlyIndexBuffer.InitRHIForStreaming(DepthOnlyIndexBuffer, Batcher);

	if (LODResource.AdditionalIndexBuffers)
	{
		LODResource.AdditionalIndexBuffers->ReversedIndexBuffer.InitRHIForStreaming(ReversedIndexBuffer, Batcher);
		LODResource.AdditionalIndexBuffers->ReversedDepthOnlyIndexBuffer.InitRHIForStreaming(ReversedDepthOnlyIndexBuffer, Batcher);
		LODResource.AdditionalIndexBuffers->WireframeIndexBuffer.InitRHIForStreaming(WireframeIndexBuffer, Batcher);
		LODResource.AdditionalIndexBuffers->AdjacencyIndexBuffer.InitRHIForStreaming(AdjacencyIndexBuffer, Batcher);
	}
	SafeRelease();
}

void FStaticMeshStreamIn::FIntermediateBuffers::CheckIsNull() const
{
	check(!TangentsVertexBuffer
		&& !TexCoordVertexBuffer
		&& !PositionVertexBuffer
		&& !ColorVertexBuffer
		&& !IndexBuffer
		&& !ReversedIndexBuffer
		&& !DepthOnlyIndexBuffer
		&& !ReversedDepthOnlyIndexBuffer
		&& !WireframeIndexBuffer
		&& !AdjacencyIndexBuffer);
}

FStaticMeshStreamIn::FStaticMeshStreamIn(UStaticMesh* InMesh, int32 InRequestedMips)
	: FStaticMeshUpdate(InMesh, InRequestedMips)
{}

FStaticMeshStreamIn::~FStaticMeshStreamIn()
{
#if DO_CHECK
	for (int32 Idx = 0; Idx < MAX_MESH_LOD_COUNT; ++Idx)
	{
		IntermediateBuffersArray[Idx].CheckIsNull();
	}
#endif
}

template <bool bRenderThread>
void FStaticMeshStreamIn::CreateBuffers_Internal(const FContext& Context)
{
	LLM_SCOPE(ELLMTag::StaticMesh);
	
	UStaticMesh* Mesh = Context.Mesh;
	FStaticMeshRenderData* RenderData = Context.RenderData;
	if (!IsCancelled() && Mesh && RenderData)
	{
		check(CurrentFirstLODIdx == RenderData->CurrentFirstLODIdx && PendingFirstMip < CurrentFirstLODIdx);

		for (int32 LODIdx = PendingFirstMip; LODIdx < CurrentFirstLODIdx; ++LODIdx)
		{
			FStaticMeshLODResources& LODResource = RenderData->LODResources[LODIdx];
			if (bRenderThread)
			{
				IntermediateBuffersArray[LODIdx].CreateFromCPUData_RenderThread(Mesh, LODResource);
			}
			else
			{
				IntermediateBuffersArray[LODIdx].CreateFromCPUData_Async(Mesh, LODResource);
			}
		}
	}
}

void FStaticMeshStreamIn::CreateBuffers_RenderThread(const FContext& Context)
{
	check(Context.CurrentThread == TT_Render);
	CreateBuffers_Internal<true>(Context);
}

void FStaticMeshStreamIn::CreateBuffers_Async(const FContext& Context)
{
	check(Context.CurrentThread == TT_Async);
	CreateBuffers_Internal<false>(Context);
}

void FStaticMeshStreamIn::DiscardNewLODs(const FContext& Context)
{
	FStaticMeshRenderData* RenderData = Context.RenderData;
	if (RenderData)
	{
		check(CurrentFirstLODIdx == RenderData->CurrentFirstLODIdx && PendingFirstMip < CurrentFirstLODIdx);

		for (int32 LODIdx = PendingFirstMip; LODIdx < CurrentFirstLODIdx; ++LODIdx)
		{
			FStaticMeshLODResources& LODResource = RenderData->LODResources[LODIdx];
			LODResource.DiscardCPUData();
		}
	}
}

void FStaticMeshStreamIn::DoFinishUpdate(const FContext& Context)
{
	UStaticMesh* Mesh = Context.Mesh;
	FStaticMeshRenderData* RenderData = Context.RenderData;
	if (!IsCancelled() && Mesh && RenderData)
	{
		check(Context.CurrentThread == TT_Render
			&& CurrentFirstLODIdx == RenderData->CurrentFirstLODIdx
			&& PendingFirstMip < CurrentFirstLODIdx);
		// Use a scope to flush the batcher before updating CurrentFirstLODIdx
		{
			TRHIResourceUpdateBatcher<GStaticMeshMaxNumResourceUpdatesPerBatch> Batcher;

			for (int32 LODIdx = PendingFirstMip; LODIdx < CurrentFirstLODIdx; ++LODIdx)
			{
				FStaticMeshLODResources& LODResource = RenderData->LODResources[LODIdx];
				LODResource.IncrementMemoryStats();
				IntermediateBuffersArray[LODIdx].TransferBuffers(LODResource, Batcher);
			}
		}
		check(Mesh->GetCachedNumResidentLODs() == RenderData->LODResources.Num() - RenderData->CurrentFirstLODIdx);
		RenderData->CurrentFirstLODIdx = PendingFirstMip;
		Mesh->SetCachedNumResidentLODs(static_cast<uint8>(RenderData->LODResources.Num() - PendingFirstMip));
	}
	else
	{
		for (int32 LODIdx = PendingFirstMip; LODIdx < CurrentFirstLODIdx; ++LODIdx)
		{
			IntermediateBuffersArray[LODIdx].SafeRelease();
		}
	}
}

void FStaticMeshStreamIn::DoCancel(const FContext& Context)
{
	// TODO: support streaming CPU data for editor builds
	if (!GIsEditor)
	{
		DiscardNewLODs(Context);
	}
	DoFinishUpdate(Context);
}

FStaticMeshStreamOut::FStaticMeshStreamOut(UStaticMesh* InMesh, int32 InRequestedMips)
	: FStaticMeshUpdate(InMesh, InRequestedMips)
{
	PushTask(FContext(InMesh, TT_None), TT_Render, SRA_UPDATE_CALLBACK(DoReleaseBuffers), TT_None, nullptr);
}

void FStaticMeshStreamOut::DoReleaseBuffers(const FContext& Context)
{
	check(Context.CurrentThread == TT_Render);
	UStaticMesh* Mesh = Context.Mesh;
	FStaticMeshRenderData* RenderData = Context.RenderData;
	if (!IsCancelled() && Mesh && RenderData)
	{
		check(CurrentFirstLODIdx == RenderData->CurrentFirstLODIdx && PendingFirstMip > CurrentFirstLODIdx);
		check(Mesh->GetCachedNumResidentLODs() == RenderData->LODResources.Num() - RenderData->CurrentFirstLODIdx);

		RenderData->CurrentFirstLODIdx = PendingFirstMip;
		Mesh->SetCachedNumResidentLODs(static_cast<uint8>(RenderData->LODResources.Num() - PendingFirstMip));

		TRHIResourceUpdateBatcher<GStaticMeshMaxNumResourceUpdatesPerBatch> Batcher;

		for (int32 LODIdx = CurrentFirstLODIdx; LODIdx < PendingFirstMip; ++LODIdx)
		{
			FStaticMeshLODResources& LODResource = RenderData->LODResources[LODIdx];
			FStaticMeshVertexBuffers& VBs = LODResource.VertexBuffers;
			LODResource.DecrementMemoryStats();
			VBs.StaticMeshVertexBuffer.ReleaseRHIForStreaming(Batcher);
			VBs.PositionVertexBuffer.ReleaseRHIForStreaming(Batcher);
			VBs.ColorVertexBuffer.ReleaseRHIForStreaming(Batcher);
			// Index buffers don't need to update SRV so we can reuse ReleaseRHI
			LODResource.IndexBuffer.ReleaseRHIForStreaming(Batcher);
			LODResource.DepthOnlyIndexBuffer.ReleaseRHIForStreaming(Batcher);

			if (LODResource.AdditionalIndexBuffers)
			{
				LODResource.AdditionalIndexBuffers->ReversedIndexBuffer.ReleaseRHIForStreaming(Batcher);
				LODResource.AdditionalIndexBuffers->ReversedDepthOnlyIndexBuffer.ReleaseRHIForStreaming(Batcher);
				LODResource.AdditionalIndexBuffers->WireframeIndexBuffer.ReleaseRHIForStreaming(Batcher);
				LODResource.AdditionalIndexBuffers->AdjacencyIndexBuffer.ReleaseRHIForStreaming(Batcher);
			}
		}
	}
}

void FStaticMeshStreamIn_IO::FCancelIORequestsTask::DoWork()
{
	check(PendingUpdate);
	// Acquire the lock of this object in order to cancel any pending IO.
	// If the object is currently being ticked, wait.
	const ETaskState PreviousTaskState = PendingUpdate->DoLock();
	PendingUpdate->CancelIORequest();
	PendingUpdate->DoUnlock(PreviousTaskState);
}

FStaticMeshStreamIn_IO::FStaticMeshStreamIn_IO(UStaticMesh* InMesh, int32 InRequestedMips, bool bHighPrio)
	: FStaticMeshStreamIn(InMesh, InRequestedMips)
	, IORequest(nullptr)
	, bHighPrioIORequest(bHighPrio)
{}

void FStaticMeshStreamIn_IO::Abort()
{
	if (!IsCancelled() && !IsCompleted())
	{
		FStaticMeshStreamIn::Abort();

		if (IORequest != nullptr)
		{
			// Prevent the update from being considered done before this is finished.
			// By checking that it was not already cancelled, we make sure this doesn't get called twice.
			(new FAsyncCancelIORequestsTask(this))->StartBackgroundTask();
		}
	}
}

FString FStaticMeshStreamIn_IO::GetIOFilename(const FContext& Context)
{
	UStaticMesh* Mesh = Context.Mesh;
	if (!IsCancelled() && Mesh)
	{
		FString Filename;
		verify(Mesh->GetMipDataFilename(PendingFirstMip, Filename));
		return Filename;
	}
	MarkAsCancelled();
	return FString();
}

void FStaticMeshStreamIn_IO::SetAsyncFileCallback(const FContext& Context)
{
	AsyncFileCallback = [this, Context](bool bWasCancelled, IBulkDataIORequest*)
	{
		// At this point task synchronization would hold the number of pending requests.
		TaskSynchronization.Decrement();

		if (bWasCancelled)
		{
			MarkAsCancelled();
		}

#if !UE_BUILD_SHIPPING
		// On some platforms the IO is too fast to test cancelation requests timing issues.
		if (FRenderAssetStreamingSettings::ExtraIOLatency > 0 && TaskSynchronization.GetValue() == 0)
		{
			FPlatformProcess::Sleep(FRenderAssetStreamingSettings::ExtraIOLatency * .001f); // Slow down the streaming.
		}
#endif

		// The tick here is intended to schedule the success or cancel callback.
		// Using TT_None ensure gets which could create a dead lock.
		Tick(FStaticMeshUpdate::TT_None);
	};
}

void FStaticMeshStreamIn_IO::SetIORequest(const FContext& Context, const FString& IOFilename)
{
	if (IsCancelled())
	{
		return;
	}

	check(!IORequest && PendingFirstMip < CurrentFirstLODIdx);

	FStaticMeshRenderData* RenderData = Context.RenderData;
	if (RenderData != nullptr)
	{
		SetAsyncFileCallback(Context);

		FBulkDataInterface::BulkDataRangeArray BulkDataArray;
		for (int32 Index = PendingFirstMip; Index < CurrentFirstLODIdx; ++Index)
		{
			BulkDataArray.Push(&RenderData->LODResources[Index].StreamingBulkData);
		}

		// Increment as we push the request. If a request complete immediately, then it will call the callback
		// but that won't do anything because the tick would not try to acquire the lock since it is already locked.
		TaskSynchronization.Increment();

		IORequest = FBulkDataInterface::CreateStreamingRequestForRange(
			STREAMINGTOKEN_PARAM(IOFilename)
			BulkDataArray,
			bHighPrioIORequest ? AIOP_BelowNormal : AIOP_Low,
			&AsyncFileCallback);
	}
	else
	{
		MarkAsCancelled();
	}
} 

void FStaticMeshStreamIn_IO::ClearIORequest(const FContext& Context)
{
	if (IORequest != nullptr)
	{
		// If clearing requests not yet completed, cancel and wait.
		if (!IORequest->PollCompletion())
		{
			IORequest->Cancel();
			IORequest->WaitCompletion();
		}
		delete IORequest;
		IORequest = nullptr;
	}
}

void FStaticMeshStreamIn_IO::SerializeLODData(const FContext& Context)
{
	LLM_SCOPE(ELLMTag::StaticMesh);

	check(!TaskSynchronization.GetValue());
	UStaticMesh* Mesh = Context.Mesh;
	FStaticMeshRenderData* RenderData = Context.RenderData;
	if (!IsCancelled() && Mesh && RenderData)
	{
		check(PendingFirstMip < CurrentFirstLODIdx && CurrentFirstLODIdx == RenderData->CurrentFirstLODIdx);
		check(IORequest->GetSize() >= 0 && IORequest->GetSize() <= TNumericLimits<uint32>::Max());

		TArrayView<uint8> Data(IORequest->GetReadResults(), IORequest->GetSize());

		FMemoryReaderView Ar(Data, true);
		for (int32 LODIdx = PendingFirstMip; LODIdx < CurrentFirstLODIdx; ++LODIdx)
		{
			FStaticMeshLODResources& LODResource = RenderData->LODResources[LODIdx];
			constexpr uint8 DummyStripFlags = 0;
			typename FStaticMeshLODResources::FStaticMeshBuffersSize DummyBuffersSize;
			LODResource.SerializeBuffers(Ar, Mesh, DummyStripFlags, DummyBuffersSize);
			check(DummyBuffersSize.CalcBuffersSize() == LODResource.BuffersSize);
		}
		
		FMemory::Free(Data.GetData());// Free the memory we took ownership of via IORequest->GetReadResults()
	}
}

void FStaticMeshStreamIn_IO::CancelIORequest()
{
	if (IORequest)
	{
		// Calling cancel will trigger the SetAsyncFileCallback() which will also try a tick but will fail.
		IORequest->Cancel();
	}
}

template <bool bRenderThread>
TStaticMeshStreamIn_IO<bRenderThread>::TStaticMeshStreamIn_IO(UStaticMesh* InMesh, int32 InRequestedMips, bool bHighPrio)
	: FStaticMeshStreamIn_IO(InMesh, InRequestedMips, bHighPrio)
{
	PushTask(FContext(InMesh, TT_None), TT_Async, SRA_UPDATE_CALLBACK(DoInitiateIO), TT_None, nullptr);
}

template <bool bRenderThread>
void TStaticMeshStreamIn_IO<bRenderThread>::DoInitiateIO(const FContext& Context)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("TStaticMeshStreamIn_IO::DoInitiateIO"), STAT_StaticMeshStreamInIO_DoInitiateIO, STATGROUP_StreamingDetails);
	check(Context.CurrentThread == TT_Async);

#if USE_BULKDATA_STREAMING_TOKEN
	const FString IOFilename = GetIOFilename(Context);
	SetIORequest(Context, IOFilename);
#else
	SetIORequest(Context, FString());
#endif

	PushTask(Context, TT_Async, SRA_UPDATE_CALLBACK(DoSerializeLODData), TT_Async, SRA_UPDATE_CALLBACK(DoCancelIO));
}

template <bool bRenderThread>
void TStaticMeshStreamIn_IO<bRenderThread>::DoSerializeLODData(const FContext& Context)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("TStaticMeshStreamIn_IO::DoSerializeLODData"), STAT_StaticMeshStreamInIO_DoSerializeLODData, STATGROUP_StreamingDetails);
	check(Context.CurrentThread == TT_Async);
	SerializeLODData(Context);
	ClearIORequest(Context);
	const EThreadType TThread = bRenderThread ? TT_Render : TT_Async;
	const EThreadType CThread = (EThreadType)Context.CurrentThread;
	PushTask(Context, TThread, SRA_UPDATE_CALLBACK(DoCreateBuffers), CThread, SRA_UPDATE_CALLBACK(DoCancel));
}

template <bool bRenderThread>
void TStaticMeshStreamIn_IO<bRenderThread>::DoCreateBuffers(const FContext& Context)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("TStaticMeshStreamIn_IO::DoCreateBuffers"), STAT_StaticMeshStreamInIO_DoCreateBuffers, STATGROUP_StreamingDetails);
	if (bRenderThread)
	{
		CreateBuffers_RenderThread(Context);
	}
	else
	{
		CreateBuffers_Async(Context);
	}
	check(!TaskSynchronization.GetValue());
	PushTask(Context, TT_Render, SRA_UPDATE_CALLBACK(DoFinishUpdate), (EThreadType)Context.CurrentThread, SRA_UPDATE_CALLBACK(DoCancel));
}

template <bool bRenderThread>
void TStaticMeshStreamIn_IO<bRenderThread>::DoCancelIO(const FContext& Context)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("TStaticMeshStreamIn_IO::DoCancelIO"), STAT_StaticMeshStreamInIO_DoCancelIO, STATGROUP_StreamingDetails);
	ClearIORequest(Context);
	PushTask(Context, TT_None, nullptr, (EThreadType)Context.CurrentThread, SRA_UPDATE_CALLBACK(DoCancel));
}

template class TStaticMeshStreamIn_IO<true>;
template class TStaticMeshStreamIn_IO<false>;

#if WITH_EDITOR
FStaticMeshStreamIn_DDC::FStaticMeshStreamIn_DDC(UStaticMesh* InMesh, int32 InRequestedMips)
	: FStaticMeshStreamIn(InMesh, InRequestedMips)
	, bDerivedDataInvalid(false)
{}

void FStaticMeshStreamIn_DDC::LoadNewLODsFromDDC(const FContext& Context)
{
	check(Context.CurrentThread == TT_Async);
	// TODO: support streaming CPU data for editor builds
}

template <bool bRenderThread>
TStaticMeshStreamIn_DDC<bRenderThread>::TStaticMeshStreamIn_DDC(UStaticMesh* InMesh, int32 InRequestedMips)
	: FStaticMeshStreamIn_DDC(InMesh, InRequestedMips)
{
	PushTask(FContext(InMesh, TT_None), TT_Async, SRA_UPDATE_CALLBACK(DoLoadNewLODsFromDDC), TT_None, nullptr);
}

template <bool bRenderThread>
void TStaticMeshStreamIn_DDC<bRenderThread>::DoLoadNewLODsFromDDC(const FContext& Context)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("TStaticMeshStreamInDDC::DoLoadNewLODsFromDDC"), STAT_StaticMeshStreamInDDC_DoLoadNewLODsFromDDC, STATGROUP_StreamingDetails);
	LoadNewLODsFromDDC(Context);
	check(!TaskSynchronization.GetValue());
	const EThreadType TThread = bRenderThread ? TT_Render : TT_Async;
	const EThreadType CThread = (EThreadType)Context.CurrentThread;
	PushTask(Context, TThread, SRA_UPDATE_CALLBACK(DoCreateBuffers), CThread, SRA_UPDATE_CALLBACK(DoCancel));
}

template <bool bRenderThread>
void TStaticMeshStreamIn_DDC<bRenderThread>::DoCreateBuffers(const FContext& Context)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("TStaticMeshStreamInDDC::DoCreateBuffers"), STAT_StaticMeshStreamInDDC_DoCreateBuffers, STATGROUP_StreamingDetails);
	if (bRenderThread)
	{
		CreateBuffers_RenderThread(Context);
	}
	else
	{
		CreateBuffers_Async(Context);
	}
	check(!TaskSynchronization.GetValue());
	PushTask(Context, TT_Render, SRA_UPDATE_CALLBACK(DoFinishUpdate), (EThreadType)Context.CurrentThread, SRA_UPDATE_CALLBACK(DoCancel));
}

template class TStaticMeshStreamIn_DDC<true>;
template class TStaticMeshStreamIn_DDC<false>;
#endif
