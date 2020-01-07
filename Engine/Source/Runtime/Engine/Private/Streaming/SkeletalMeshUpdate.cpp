// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
SkeletalMeshUpdate.cpp: Helpers to stream in and out skeletal mesh LODs.
=============================================================================*/

#include "Streaming/SkeletalMeshUpdate.h"
#include "RenderUtils.h"
#include "Containers/ResourceArray.h"
#include "ContentStreaming.h"
#include "Streaming/TextureStreamingHelpers.h"
#include "HAL/PlatformFilemanager.h"
#include "Serialization/MemoryReader.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Components/SkinnedMeshComponent.h"
#include "Streaming/RenderAssetUpdate.inl"

template class TRenderAssetUpdate<FSkelMeshUpdateContext>;

static constexpr uint32 GSkelMeshMaxNumResourceUpdatesPerLOD = 16;
static constexpr uint32 GSkelMeshMaxNumResourceUpdatesPerBatch = (MAX_MESH_LOD_COUNT - 1) * GSkelMeshMaxNumResourceUpdatesPerLOD;

FSkelMeshUpdateContext::FSkelMeshUpdateContext(USkeletalMesh* InMesh, EThreadType InCurrentThread)
	: Mesh(InMesh)
	, CurrentThread(InCurrentThread)
{
	check(InMesh);
	checkSlow(InCurrentThread != FSkeletalMeshUpdate::TT_Render || IsInRenderingThread());
	RenderData = Mesh->GetResourceForRendering();
}

FSkelMeshUpdateContext::FSkelMeshUpdateContext(UStreamableRenderAsset* InMesh, EThreadType InCurrentThread)
#if UE_BUILD_SHIPPING
	: FSkelMeshUpdateContext(static_cast<USkeletalMesh*>(InMesh), InCurrentThread)
#else
	: FSkelMeshUpdateContext(Cast<USkeletalMesh>(InMesh), InCurrentThread)
#endif
{}

FSkeletalMeshUpdate::FSkeletalMeshUpdate(USkeletalMesh* InMesh, int32 InRequestedMips)
	: TRenderAssetUpdate<FSkelMeshUpdateContext>(InMesh, InRequestedMips)
{
	FSkeletalMeshRenderData* RenderData = InMesh->GetResourceForRendering();
	if (RenderData)
	{
		CurrentFirstLODIdx = RenderData->CurrentFirstLODIdx;
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

void FSkeletalMeshStreamIn::FIntermediateBuffers::CreateFromCPUData_RenderThread(USkeletalMesh* Mesh, FSkeletalMeshLODRenderData& LODResource)
{
	FStaticMeshVertexBuffers& VBs = LODResource.StaticVertexBuffers;
	TangentsVertexBuffer = VBs.StaticMeshVertexBuffer.CreateTangentsRHIBuffer_RenderThread();
	TexCoordVertexBuffer = VBs.StaticMeshVertexBuffer.CreateTexCoordRHIBuffer_RenderThread();
	PositionVertexBuffer = VBs.PositionVertexBuffer.CreateRHIBuffer_RenderThread();
	ColorVertexBuffer = VBs.ColorVertexBuffer.CreateRHIBuffer_RenderThread();
	LODResource.SkinWeightProfilesData.CreateRHIBuffers_RenderThread(AltSkinWeightVertexBuffers);
	SkinWeightVertexBuffer = LODResource.SkinWeightVertexBuffer.CreateRHIBuffer_RenderThread();
	ClothVertexBuffer = LODResource.ClothVertexBuffer.CreateRHIBuffer_RenderThread();
	IndexBuffer = LODResource.MultiSizeIndexContainer.CreateRHIBuffer_RenderThread();
	AdjacencyIndexBuffer = LODResource.AdjacencyMultiSizeIndexContainer.CreateRHIBuffer_RenderThread();
}

void FSkeletalMeshStreamIn::FIntermediateBuffers::CreateFromCPUData_Async(USkeletalMesh* Mesh, FSkeletalMeshLODRenderData& LODResource)
{
	FStaticMeshVertexBuffers& VBs = LODResource.StaticVertexBuffers;
	TangentsVertexBuffer = VBs.StaticMeshVertexBuffer.CreateTangentsRHIBuffer_Async();
	TexCoordVertexBuffer = VBs.StaticMeshVertexBuffer.CreateTexCoordRHIBuffer_Async();
	PositionVertexBuffer = VBs.PositionVertexBuffer.CreateRHIBuffer_Async();
	ColorVertexBuffer = VBs.ColorVertexBuffer.CreateRHIBuffer_Async();
	LODResource.SkinWeightProfilesData.CreateRHIBuffers_Async(AltSkinWeightVertexBuffers);
	SkinWeightVertexBuffer = LODResource.SkinWeightVertexBuffer.CreateRHIBuffer_Async();
	ClothVertexBuffer = LODResource.ClothVertexBuffer.CreateRHIBuffer_Async();
	IndexBuffer = LODResource.MultiSizeIndexContainer.CreateRHIBuffer_Async();
	AdjacencyIndexBuffer = LODResource.AdjacencyMultiSizeIndexContainer.CreateRHIBuffer_Async();
}

void FSkeletalMeshStreamIn::FIntermediateBuffers::SafeRelease()
{
	TangentsVertexBuffer.SafeRelease();
	TexCoordVertexBuffer.SafeRelease();
	PositionVertexBuffer.SafeRelease();
	ColorVertexBuffer.SafeRelease();
	SkinWeightVertexBuffer.SafeRelease();
	ClothVertexBuffer.SafeRelease();
	IndexBuffer.SafeRelease();
	AdjacencyIndexBuffer.SafeRelease();
	AltSkinWeightVertexBuffers.Empty();
}

template <uint32 MaxNumUpdates>
void FSkeletalMeshStreamIn::FIntermediateBuffers::TransferBuffers(FSkeletalMeshLODRenderData& LODResource, TRHIResourceUpdateBatcher<MaxNumUpdates>& Batcher)
{
	FStaticMeshVertexBuffers& VBs = LODResource.StaticVertexBuffers;
	VBs.StaticMeshVertexBuffer.InitRHIForStreaming(TangentsVertexBuffer, TexCoordVertexBuffer, Batcher);
	VBs.PositionVertexBuffer.InitRHIForStreaming(PositionVertexBuffer, Batcher);
	VBs.ColorVertexBuffer.InitRHIForStreaming(ColorVertexBuffer, Batcher);
	LODResource.SkinWeightVertexBuffer.InitRHIForStreaming(SkinWeightVertexBuffer, Batcher);
	LODResource.ClothVertexBuffer.InitRHIForStreaming(ClothVertexBuffer, Batcher);
	LODResource.MultiSizeIndexContainer.InitRHIForStreaming(IndexBuffer, Batcher);
	LODResource.AdjacencyMultiSizeIndexContainer.InitRHIForStreaming(AdjacencyIndexBuffer, Batcher);
	LODResource.SkinWeightProfilesData.InitRHIForStreaming(AltSkinWeightVertexBuffers, Batcher);
	SafeRelease();
}

void FSkeletalMeshStreamIn::FIntermediateBuffers::CheckIsNull() const
{
	check(!TangentsVertexBuffer
		&& !TexCoordVertexBuffer
		&& !PositionVertexBuffer
		&& !ColorVertexBuffer
		&& !SkinWeightVertexBuffer
		&& !ClothVertexBuffer
		&& !IndexBuffer
		&& !AdjacencyIndexBuffer
		&& !AltSkinWeightVertexBuffers.Num());
}

FSkeletalMeshStreamIn::FSkeletalMeshStreamIn(USkeletalMesh* InMesh, int32 InRequestedMips)
	: FSkeletalMeshUpdate(InMesh, InRequestedMips)
{}

FSkeletalMeshStreamIn::~FSkeletalMeshStreamIn()
{
#if DO_CHECK
	for (int32 Idx = 0; Idx < MAX_MESH_LOD_COUNT; ++Idx)
	{
		IntermediateBuffersArray[Idx].CheckIsNull();
	}
#endif
}

template <bool bRenderThread>
void FSkeletalMeshStreamIn::CreateBuffers_Internal(const FContext& Context)
{
	LLM_SCOPE(ELLMTag::SkeletalMesh);

	USkeletalMesh* Mesh = Context.Mesh;
	FSkeletalMeshRenderData* RenderData = Context.RenderData;
	if (!IsCancelled() && Mesh && RenderData)
	{
		check(CurrentFirstLODIdx == RenderData->CurrentFirstLODIdx && PendingFirstMip < CurrentFirstLODIdx);

		for (int32 LODIdx = PendingFirstMip; LODIdx < CurrentFirstLODIdx; ++LODIdx)
		{
			FSkeletalMeshLODRenderData& LODResource = RenderData->LODRenderData[LODIdx];
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

void FSkeletalMeshStreamIn::CreateBuffers_RenderThread(const FContext& Context)
{
	check(Context.CurrentThread == TT_Render);
	CreateBuffers_Internal<true>(Context);
}

void FSkeletalMeshStreamIn::CreateBuffers_Async(const FContext& Context)
{
	check(Context.CurrentThread == TT_Async);
	CreateBuffers_Internal<false>(Context);
}

void FSkeletalMeshStreamIn::DiscardNewLODs(const FContext& Context)
{
	FSkeletalMeshRenderData* RenderData = Context.RenderData;
	if (RenderData)
	{
		check(CurrentFirstLODIdx == RenderData->CurrentFirstLODIdx && PendingFirstMip < CurrentFirstLODIdx);

		for (int32 LODIdx = PendingFirstMip; LODIdx < CurrentFirstLODIdx; ++LODIdx)
		{
			FSkeletalMeshLODRenderData& LODResource = RenderData->LODRenderData[LODIdx];
			LODResource.ReleaseCPUResources(true);
		}
	}
}

void FSkeletalMeshStreamIn::DoFinishUpdate(const FContext& Context)
{
	USkeletalMesh* Mesh = Context.Mesh;
	FSkeletalMeshRenderData* RenderData = Context.RenderData;
	if (!IsCancelled() && Mesh && RenderData)
	{
		check(Context.CurrentThread == TT_Render
			&& CurrentFirstLODIdx == RenderData->CurrentFirstLODIdx
			&& PendingFirstMip < CurrentFirstLODIdx);
		// Use a scope to flush the batcher before updating CurrentFirstLODIdx
		{
			TRHIResourceUpdateBatcher<GSkelMeshMaxNumResourceUpdatesPerBatch> Batcher;

			for (int32 LODIdx = PendingFirstMip; LODIdx < CurrentFirstLODIdx; ++LODIdx)
			{
				FSkeletalMeshLODRenderData& LODResource = RenderData->LODRenderData[LODIdx];
				LODResource.IncrementMemoryStats(Mesh->bHasVertexColors);
				IntermediateBuffersArray[LODIdx].TransferBuffers(LODResource, Batcher);
			}
		}
		check(Mesh->GetCachedNumResidentLODs() == RenderData->LODRenderData.Num() - RenderData->CurrentFirstLODIdx);
		RenderData->CurrentFirstLODIdx = RenderData->PendingFirstLODIdx = PendingFirstMip;
		Mesh->SetCachedNumResidentLODs(static_cast<uint8>(RenderData->LODRenderData.Num() - PendingFirstMip));
	}
	else
	{
		for (int32 LODIdx = PendingFirstMip; LODIdx < CurrentFirstLODIdx; ++LODIdx)
		{
			IntermediateBuffersArray[LODIdx].SafeRelease();
		}
	}
}

void FSkeletalMeshStreamIn::DoCancel(const FContext& Context)
{
	// TODO: support streaming CPU data for editor builds
	if (!GIsEditor)
	{
		DiscardNewLODs(Context);
	}
	DoFinishUpdate(Context);
}

FSkeletalMeshStreamOut::FSkeletalMeshStreamOut(USkeletalMesh* InMesh, int32 InRequestedMips)
	: FSkeletalMeshUpdate(InMesh, InRequestedMips)
{
	PushTask(FContext(InMesh, TT_None), TT_GameThread, SRA_UPDATE_CALLBACK(DoConditionalMarkComponentsDirty), TT_None, nullptr);
}

void FSkeletalMeshStreamOut::DoConditionalMarkComponentsDirty(const FContext& Context)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FSkeletalMeshStreamOut_DoConditionalMarkComponentsDirty);
	CSV_SCOPED_TIMING_STAT_GLOBAL(SkStreamingMarkDirtyTime);
	check(Context.CurrentThread == TT_GameThread);

	USkeletalMesh* Mesh = Context.Mesh;
	FSkeletalMeshRenderData* RenderData = Context.RenderData;
	if (!IsCancelled() && Mesh && RenderData)
	{
		RenderData->PendingFirstLODIdx = PendingFirstMip;

		TArray<const UPrimitiveComponent*> Comps;
		IStreamingManager::Get().GetTextureStreamingManager().GetAssetComponents(Mesh, Comps, [](const UPrimitiveComponent* Comp)
		{
			return !Comp->IsComponentTickEnabled();
		});
		for (int32 Idx = 0; Idx < Comps.Num(); ++Idx)
		{
			check(Comps[Idx]->IsA<USkinnedMeshComponent>());
			USkinnedMeshComponent* Comp = (USkinnedMeshComponent*)Comps[Idx];
			if (Comp->PredictedLODLevel < PendingFirstMip)
			{
				Comp->PredictedLODLevel = PendingFirstMip;
				Comp->bForceMeshObjectUpdate = true;
				Comp->MarkRenderDynamicDataDirty();
			}
		}
	}
	else
	{
		Abort();
	}
	PushTask(Context, TT_Render, SRA_UPDATE_CALLBACK(DoReleaseBuffers), (EThreadType)Context.CurrentThread, SRA_UPDATE_CALLBACK(DoCancel));
}

void FSkeletalMeshStreamOut::DoReleaseBuffers(const FContext& Context)
{
	check(Context.CurrentThread == TT_Render);
	USkeletalMesh* Mesh = Context.Mesh;
	FSkeletalMeshRenderData* RenderData = Context.RenderData;
	if (!IsCancelled() && Mesh && RenderData)
	{
		check(CurrentFirstLODIdx == RenderData->CurrentFirstLODIdx && PendingFirstMip > CurrentFirstLODIdx);
		check(Mesh->GetCachedNumResidentLODs() == RenderData->LODRenderData.Num() - CurrentFirstLODIdx);
		RenderData->CurrentFirstLODIdx = PendingFirstMip;
		Mesh->SetCachedNumResidentLODs(static_cast<uint8>(RenderData->LODRenderData.Num() - PendingFirstMip));

		TRHIResourceUpdateBatcher<GSkelMeshMaxNumResourceUpdatesPerBatch> Batcher;

		for (int32 LODIdx = CurrentFirstLODIdx; LODIdx < PendingFirstMip; ++LODIdx)
		{
			FSkeletalMeshLODRenderData& LODResource = RenderData->LODRenderData[LODIdx];
			FStaticMeshVertexBuffers& VBs = LODResource.StaticVertexBuffers;
			LODResource.DecrementMemoryStats();
			VBs.StaticMeshVertexBuffer.ReleaseRHIForStreaming(Batcher);
			VBs.PositionVertexBuffer.ReleaseRHIForStreaming(Batcher);
			VBs.ColorVertexBuffer.ReleaseRHIForStreaming(Batcher);
			LODResource.SkinWeightVertexBuffer.ReleaseRHIForStreaming(Batcher);
			LODResource.ClothVertexBuffer.ReleaseRHIForStreaming(Batcher);
			LODResource.MultiSizeIndexContainer.ReleaseRHIForStreaming(Batcher);
			LODResource.AdjacencyMultiSizeIndexContainer.ReleaseRHIForStreaming(Batcher);
			LODResource.SkinWeightProfilesData.ReleaseRHIForStreaming(Batcher);
		}
	}
}

void FSkeletalMeshStreamOut::DoCancel(const FContext& Context)
{
	FSkeletalMeshRenderData* RenderData = Context.RenderData;
	if (RenderData)
	{
		RenderData->PendingFirstLODIdx = CurrentFirstLODIdx;
	}
}

void FSkeletalMeshStreamIn_IO::FCancelIORequestsTask::DoWork()
{
	check(PendingUpdate);
	// Acquire the lock of this object in order to cancel any pending IO.
	// If the object is currently being ticked, wait.
	ETaskState OldState = PendingUpdate->DoLock();
	PendingUpdate->CancelIORequest();
	PendingUpdate->DoUnlock(OldState);
}

FSkeletalMeshStreamIn_IO::FSkeletalMeshStreamIn_IO(USkeletalMesh* InMesh, int32 InRequestedMips, bool bHighPrio)
	: FSkeletalMeshStreamIn(InMesh, InRequestedMips)
	, IORequest(nullptr)
	, bHighPrioIORequest(bHighPrio)
{}

void FSkeletalMeshStreamIn_IO::Abort()
{
	if (!IsCancelled() && !IsCompleted())
	{
		FSkeletalMeshStreamIn::Abort();

		if (IORequest != nullptr)
		{
			// Prevent the update from being considered done before this is finished.
			// By checking that it was not already cancelled, we make sure this doesn't get called twice.
			(new FAsyncCancelIORequestsTask(this))->StartBackgroundTask();
		}
	}
}

FString FSkeletalMeshStreamIn_IO::GetIOFilename(const FContext& Context)
{
	USkeletalMesh* Mesh = Context.Mesh;
	if (!IsCancelled() && Mesh)
	{
		FString Filename;
		verify(Mesh->GetMipDataFilename(PendingFirstMip, Filename));
		return Filename;
	}
	MarkAsCancelled();
	return FString();
}

void FSkeletalMeshStreamIn_IO::SetAsyncFileCallback(const FContext& Context)
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
		Tick(FSkeletalMeshUpdate::TT_None);
	};
}

void FSkeletalMeshStreamIn_IO::SetIORequest(const FContext& Context, const FString& IOFilename)
{
	if (IsCancelled())
	{
		return;
	}
	check(!IORequest && PendingFirstMip < CurrentFirstLODIdx);

	FSkeletalMeshRenderData* RenderData = Context.RenderData;
	FString debugName;
	if (Context.Mesh)
	{
		debugName = Context.Mesh->GetName();
	}

	if (RenderData != nullptr)
	{
		SetAsyncFileCallback(Context);

		FBulkDataInterface::BulkDataRangeArray BulkDataArray;
		for (int32 Index = PendingFirstMip; Index < CurrentFirstLODIdx; ++Index)
		{
			BulkDataArray.Push(&RenderData->LODRenderData[Index].StreamingBulkData);
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

void FSkeletalMeshStreamIn_IO::ClearIORequest(const FContext& Context)
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

void FSkeletalMeshStreamIn_IO::SerializeLODData(const FContext& Context)
{
	LLM_SCOPE(ELLMTag::SkeletalMesh);

	check(!TaskSynchronization.GetValue());
	USkeletalMesh* Mesh = Context.Mesh;
	FSkeletalMeshRenderData* RenderData = Context.RenderData;
	if (!IsCancelled() && Mesh && RenderData)
	{
		check(PendingFirstMip < CurrentFirstLODIdx && CurrentFirstLODIdx == RenderData->CurrentFirstLODIdx);
		check(IORequest->GetSize() >= 0 && IORequest->GetSize() <= TNumericLimits<uint32>::Max());

		TArrayView<uint8> Data(IORequest->GetReadResults(), IORequest->GetSize());
		FMemoryReaderView Ar(Data, true);
		for (int32 LODIdx = PendingFirstMip; LODIdx < CurrentFirstLODIdx; ++LODIdx)
		{
			FSkeletalMeshLODRenderData& LODResource = RenderData->LODRenderData[LODIdx];
			const bool bForceKeepCPUResources = FSkeletalMeshLODRenderData::ShouldForceKeepCPUResources();
			const bool bNeedsCPUAccess = FSkeletalMeshLODRenderData::ShouldKeepCPUResources(Mesh, LODIdx, bForceKeepCPUResources);
			constexpr uint8 DummyStripFlags = 0;
			LODResource.SerializeStreamedData(Ar, Mesh, LODIdx, DummyStripFlags, bNeedsCPUAccess, bForceKeepCPUResources);
		}

		FMemory::Free(Data.GetData()); // Free the memory we took ownership of via IORequest->GetReadResults()
	}
}

void FSkeletalMeshStreamIn_IO::CancelIORequest()
{
	if (IORequest)
	{
		// Calling cancel will trigger the SetAsyncFileCallback() which will also try a tick but will fail.
		IORequest->Cancel();
	}
}

template <bool bRenderThread>
TSkeletalMeshStreamIn_IO<bRenderThread>::TSkeletalMeshStreamIn_IO(USkeletalMesh* InMesh, int32 InRequestedMips, bool bHighPrio)
	: FSkeletalMeshStreamIn_IO(InMesh, InRequestedMips, bHighPrio)
{
	PushTask(FContext(InMesh, TT_None), TT_Async, SRA_UPDATE_CALLBACK(DoInitiateIO), TT_None, nullptr);
}

template <bool bRenderThread>
void TSkeletalMeshStreamIn_IO<bRenderThread>::DoInitiateIO(const FContext& Context)
{
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
void TSkeletalMeshStreamIn_IO<bRenderThread>::DoSerializeLODData(const FContext& Context)
{
	check(Context.CurrentThread == TT_Async);
	SerializeLODData(Context);
	ClearIORequest(Context);
	const EThreadType TThread = bRenderThread ? TT_Render : TT_Async;
	const EThreadType CThread = (EThreadType)Context.CurrentThread;
	PushTask(Context, TThread, SRA_UPDATE_CALLBACK(DoCreateBuffers), CThread, SRA_UPDATE_CALLBACK(DoCancel));
}

template <bool bRenderThread>
void TSkeletalMeshStreamIn_IO<bRenderThread>::DoCreateBuffers(const FContext& Context)
{
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
void TSkeletalMeshStreamIn_IO<bRenderThread>::DoCancelIO(const FContext& Context)
{
	ClearIORequest(Context);
	PushTask(Context, TT_None, nullptr, (EThreadType)Context.CurrentThread, SRA_UPDATE_CALLBACK(DoCancel));
}

template class TSkeletalMeshStreamIn_IO<true>;
template class TSkeletalMeshStreamIn_IO<false>;

#if WITH_EDITOR
FSkeletalMeshStreamIn_DDC::FSkeletalMeshStreamIn_DDC(USkeletalMesh* InMesh, int32 InRequestedMips)
	: FSkeletalMeshStreamIn(InMesh, InRequestedMips)
	, bDerivedDataInvalid(false)
{}

void FSkeletalMeshStreamIn_DDC::LoadNewLODsFromDDC(const FContext& Context)
{
	check(Context.CurrentThread == TT_Async);
	// TODO: support streaming CPU data for editor builds
}

template <bool bRenderThread>
TSkeletalMeshStreamIn_DDC<bRenderThread>::TSkeletalMeshStreamIn_DDC(USkeletalMesh* InMesh, int32 InRequestedMips)
	: FSkeletalMeshStreamIn_DDC(InMesh, InRequestedMips)
{
	PushTask(FContext(InMesh, TT_None), TT_Async, SRA_UPDATE_CALLBACK(DoLoadNewLODsFromDDC), TT_None, nullptr);
}

template <bool bRenderThread>
void TSkeletalMeshStreamIn_DDC<bRenderThread>::DoLoadNewLODsFromDDC(const FContext& Context)
{
	LoadNewLODsFromDDC(Context);
	check(!TaskSynchronization.GetValue());
	const EThreadType TThread = bRenderThread ? TT_Render : TT_Async;
	const EThreadType CThread = (EThreadType)Context.CurrentThread;
	PushTask(Context, TThread, SRA_UPDATE_CALLBACK(DoCreateBuffers), CThread, SRA_UPDATE_CALLBACK(DoCancel));
}

template <bool bRenderThread>
void TSkeletalMeshStreamIn_DDC<bRenderThread>::DoCreateBuffers(const FContext& Context)
{
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

template class TSkeletalMeshStreamIn_DDC<true>;
template class TSkeletalMeshStreamIn_DDC<false>;
#endif
