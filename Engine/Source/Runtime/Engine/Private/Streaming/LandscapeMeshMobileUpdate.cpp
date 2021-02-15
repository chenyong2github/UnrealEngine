// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
LandscapeMeshMobileUpdate.cpp: Helpers to stream in and out mobile landscape vertex buffers.
=============================================================================*/

#include "Streaming/LandscapeMeshMobileUpdate.h"
#include "Streaming/TextureStreamingHelpers.h"
#include "LandscapeComponent.h"
#include "LandscapeRenderMobile.h"
#include "Streaming/RenderAssetUpdate.inl"

template class TRenderAssetUpdate<FLandscapeMeshMobileUpdateContext>;

FLandscapeMeshMobileUpdateContext::FLandscapeMeshMobileUpdateContext(const ULandscapeLODStreamingProxy* InLandscapeProxy, EThreadType InCurrentThread)
	: LandscapeProxy(InLandscapeProxy)
	, CurrentThread(InCurrentThread)
{
	check(InLandscapeProxy);
	checkSlow(InCurrentThread != FLandscapeMeshMobileUpdate::TT_Render || IsInRenderingThread());
	RenderData = InLandscapeProxy ? InLandscapeProxy->GetRenderData().Get() : nullptr;
}

FLandscapeMeshMobileUpdateContext::FLandscapeMeshMobileUpdateContext(const UStreamableRenderAsset* InLandscapeProxy, EThreadType InCurrentThread)
	: FLandscapeMeshMobileUpdateContext(CastChecked<ULandscapeLODStreamingProxy>(InLandscapeProxy), InCurrentThread)
{}

FLandscapeMeshMobileUpdate::FLandscapeMeshMobileUpdate(ULandscapeLODStreamingProxy* InLandscapeProxy)
	: TRenderAssetUpdate<FLandscapeMeshMobileUpdateContext>(InLandscapeProxy)
{
	TSharedPtr<FLandscapeMobileRenderData, ESPMode::ThreadSafe> RenderData = InLandscapeProxy->GetRenderData();
	if (!RenderData.IsValid())
	{
		bIsCancelled = true;
	}
}

FLandscapeMeshMobileStreamIn::FLandscapeMeshMobileStreamIn(ULandscapeLODStreamingProxy* InLandscapeProxy)
	: FLandscapeMeshMobileUpdate(InLandscapeProxy)
{
	FMemory::Memset(StagingLODDataArray, 0, sizeof(StagingLODDataArray));
	FMemory::Memset(StagingLODDataSizes, 0, sizeof(StagingLODDataSizes));
}

FLandscapeMeshMobileStreamIn::~FLandscapeMeshMobileStreamIn()
{
	check(!IntermediateVertexBuffer);
}

void FLandscapeMeshMobileStreamIn::ExpandResources(const FContext& Context)
{
	LLM_SCOPE(ELLMTag::Landscape);

	const ULandscapeLODStreamingProxy* LandscapeProxy = Context.LandscapeProxy;
	FLandscapeMobileRenderData* RenderData = Context.RenderData;

	if (!IsCancelled() && LandscapeProxy && RenderData)
	{
		FLandscapeVertexBufferMobile* LandscapeVB = RenderData->VertexBuffer;
		check(LandscapeVB);

		const uint32 OldSize = RenderData->VertexBuffer->VertexBufferRHI->GetSize();
		uint32 NewSize = OldSize;
		for (int32 LODIdx = CurrentFirstLODIdx - 1; LODIdx >= PendingFirstLODIdx; --LODIdx)
		{
			check(StagingLODDataSizes[LODIdx] >= 0);
			NewSize += StagingLODDataSizes[LODIdx];
		}

		if (NewSize == OldSize)
		{
			check(!IntermediateVertexBuffer);
			return;
		}

		FRHIResourceCreateInfo CreateInfo;
		IntermediateVertexBuffer = RHICreateVertexBuffer(NewSize, BUF_Static, CreateInfo);
		uint8* Dest = (uint8*)RHILockVertexBuffer(IntermediateVertexBuffer, OldSize, NewSize - OldSize, RLM_WriteOnly);
		for (int32 LODIdx = CurrentFirstLODIdx - 1; LODIdx >= PendingFirstLODIdx; --LODIdx)
		{
			if (StagingLODDataSizes[LODIdx] > 0)
			{
				check(StagingLODDataArray[LODIdx]);
				FMemory::Memcpy(Dest, StagingLODDataArray[LODIdx], StagingLODDataSizes[LODIdx]);
				Dest += StagingLODDataSizes[LODIdx];
				FMemory::Free(StagingLODDataArray[LODIdx]);
				StagingLODDataArray[LODIdx] = nullptr;
				StagingLODDataSizes[LODIdx] = 0;
			}
		}
		RHIUnlockVertexBuffer(IntermediateVertexBuffer);

		FRHICommandListExecutor::GetImmediateCommandList().CopyBufferRegion(IntermediateVertexBuffer, 0, RenderData->VertexBuffer->VertexBufferRHI, 0, OldSize);
	}
}

void FLandscapeMeshMobileStreamIn::DiscardNewLODs(const FContext& Context)
{
	for (int32 Idx = 0; Idx < MAX_MESH_LOD_COUNT; ++Idx)
	{
		FMemory::Free(StagingLODDataArray[Idx]);
		StagingLODDataArray[Idx] = nullptr;
		StagingLODDataSizes[Idx] = 0;
	}
}

void FLandscapeMeshMobileStreamIn::DoFinishUpdate(const FContext& Context)
{
	const ULandscapeLODStreamingProxy* LandscapeProxy = Context.LandscapeProxy;
	FLandscapeMobileRenderData* RenderData = Context.RenderData;

	if (!IsCancelled() && LandscapeProxy && RenderData)
	{
		check(Context.CurrentThread == TT_Render);

		if (IntermediateVertexBuffer)
		{
			FLandscapeVertexBufferMobile* LandscapeVB = RenderData->VertexBuffer;
			check(LandscapeVB);
			FLandscapeVertexBufferMobile::UpdateMemoryStat(IntermediateVertexBuffer->GetSize() - LandscapeVB->VertexBufferRHI->GetSize());

			TRHIResourceUpdateBatcher<1> Batcher;
			Batcher.QueueUpdateRequest(LandscapeVB->VertexBufferRHI, IntermediateVertexBuffer);
		}

		RenderData->CurrentFirstLODIdx = PendingFirstLODIdx;
		MarkAsSuccessfullyFinished();
	}
	
	IntermediateVertexBuffer.SafeRelease();
}

void FLandscapeMeshMobileStreamIn::DoCancel(const FContext& Context)
{
	DiscardNewLODs(Context);
	DoFinishUpdate(Context);
}

FLandscapeMeshMobileStreamOut::FLandscapeMeshMobileStreamOut(ULandscapeLODStreamingProxy* InLandscapeProxy)
	: FLandscapeMeshMobileUpdate(InLandscapeProxy)
{
	PushTask(FContext(InLandscapeProxy, TT_None), TT_Render, SRA_UPDATE_CALLBACK(ShrinkResources), TT_None, nullptr);
}

void FLandscapeMeshMobileStreamOut::ShrinkResources(const FContext& Context)
{
	LLM_SCOPE(ELLMTag::Landscape);
	check(Context.CurrentThread == TT_Render);

	const ULandscapeLODStreamingProxy* LandscapeProxy = Context.LandscapeProxy;
	FLandscapeMobileRenderData* RenderData = Context.RenderData;

	if (!IsCancelled() && LandscapeProxy && RenderData)
	{
		RenderData->CurrentFirstLODIdx = PendingFirstLODIdx;

		const int32 OldSizeNoInline = LandscapeProxy->CalcCumulativeLODSize(ResourceState.NumResidentLODs);
		const int32 NewSizeNoInline = LandscapeProxy->CalcCumulativeLODSize(ResourceState.NumRequestedLODs);
		const int32 SizeDelta = NewSizeNoInline - OldSizeNoInline;
		if (!!SizeDelta)
		{
			check(SizeDelta < 0 && RenderData->VertexBuffer && RenderData->VertexBuffer->VertexBufferRHI);
			FLandscapeVertexBufferMobile::UpdateMemoryStat(SizeDelta);

			FRHIResourceCreateInfo CreateInfo;
			FRHIVertexBuffer* LandsacpeVBRHI = RenderData->VertexBuffer->VertexBufferRHI;
			const int32 NewSize = (int32)LandsacpeVBRHI->GetSize() + SizeDelta;
			FVertexBufferRHIRef IntermediateVertexBuffer = RHICreateVertexBuffer(NewSize, BUF_Static, CreateInfo);
			FRHICommandListExecutor::GetImmediateCommandList().CopyBufferRegion(IntermediateVertexBuffer, 0, LandsacpeVBRHI, 0, NewSize);

			TRHIResourceUpdateBatcher<1> Batcher;
			Batcher.QueueUpdateRequest(LandsacpeVBRHI, IntermediateVertexBuffer);
		}
		MarkAsSuccessfullyFinished();
	}
}

void FLandscapeMeshMobileStreamIn_IO::FCancelIORequestsTask::DoWork()
{
	check(PendingUpdate);
	const ETaskState PreviousTaskState = PendingUpdate->DoLock();
	PendingUpdate->CancelIORequest();
	PendingUpdate->DoUnlock(PreviousTaskState);
}

FLandscapeMeshMobileStreamIn_IO::FLandscapeMeshMobileStreamIn_IO(ULandscapeLODStreamingProxy* InLandscapeProxy, bool bHighPrio)
	: FLandscapeMeshMobileStreamIn(InLandscapeProxy)
	, bHighPrioIORequest(bHighPrio)
{
	FMemory::Memset(IORequests, 0, sizeof(IORequests));
}

void FLandscapeMeshMobileStreamIn_IO::Abort()
{
	if (!IsCancelled() && !IsCompleted())
	{
		FLandscapeMeshMobileStreamIn::Abort();

		if (HasPendingIORequests())
		{
			(new FAsyncCancelIORequestsTask(this))->StartBackgroundTask();
		}
	}
}

bool FLandscapeMeshMobileStreamIn_IO::HasPendingIORequests() const
{
	for (int32 Idx = 0; Idx < MAX_MESH_LOD_COUNT; ++Idx)
	{
		if (!!IORequests[Idx])
		{
			return true;
		}
	}
	return false;
}

FString FLandscapeMeshMobileStreamIn_IO::GetIOFilename(const FContext& Context)
{
	const ULandscapeLODStreamingProxy* LandscapeProxy = Context.LandscapeProxy;

	if (!IsCancelled() && LandscapeProxy)
	{
		FString Filename;
		verify(LandscapeProxy->GetMipDataFilename(PendingFirstLODIdx, Filename));
		return Filename;
	}
	MarkAsCancelled();
	return FString();
}

void FLandscapeMeshMobileStreamIn_IO::SetAsyncFileCallback(const FContext& Context)
{
	AsyncFileCallback = [this, Context](bool bWasCancelled, IBulkDataIORequest* Req)
	{
		TaskSynchronization.Decrement();

		if (bWasCancelled)
		{
			MarkAsCancelled();
		}

#if !UE_BUILD_SHIPPING
		if (FRenderAssetStreamingSettings::ExtraIOLatency > 0 && TaskSynchronization.GetValue() == 0)
		{
			FPlatformProcess::Sleep(FRenderAssetStreamingSettings::ExtraIOLatency * .001f);
		}
#endif

		Tick(FLandscapeMeshMobileUpdate::TT_None);
	};
}

void FLandscapeMeshMobileStreamIn_IO::SetIORequest(const FContext& Context, const FString& IOFilename)
{
	if (IsCancelled())
	{
		return;
	}

	check(PendingFirstLODIdx < CurrentFirstLODIdx);

	const ULandscapeLODStreamingProxy* LandscapeProxy = Context.LandscapeProxy;
	if (LandscapeProxy != nullptr)
	{
		SetAsyncFileCallback(Context);

		for (int32 Index = PendingFirstLODIdx; Index < CurrentFirstLODIdx; ++Index)
		{
			FBulkDataInterface::BulkDataRangeArray BulkDataArray;
#if !LANDSCAPE_LOD_STREAMING_USE_TOKEN && USE_BULKDATA_STREAMING_TOKEN
			FBulkDataStreamingToken StreamingToken = LandscapeProxy->GetStreamingLODBulkData(Index).CreateStreamingToken();
			BulkDataArray.Add(&StreamingToken);
#else
			BulkDataArray.Add(&LandscapeProxy->GetStreamingLODBulkData(Index));
#endif
			if (BulkDataArray[0]->GetBulkDataSize() > 0)
			{
				check(!IOFilename.IsEmpty());
				TaskSynchronization.Increment();
				IORequests[Index] = FBulkDataInterface::CreateStreamingRequestForRange(
					STREAMINGTOKEN_PARAM(IOFilename)
					BulkDataArray,
					bHighPrioIORequest ? AIOP_BelowNormal : AIOP_Low,
					&AsyncFileCallback);
			}
		}
	}
	else
	{
		MarkAsCancelled();
	}
}

void FLandscapeMeshMobileStreamIn_IO::GetIORequestResults(const FContext& Context)
{
	LLM_SCOPE(ELLMTag::Landscape);
	check(!TaskSynchronization.GetValue());

	const ULandscapeLODStreamingProxy* LandscapeProxy = Context.LandscapeProxy;
	FLandscapeMobileRenderData* RenderData = Context.RenderData;

	if (!IsCancelled() && LandscapeProxy && RenderData)
	{
		check(PendingFirstLODIdx < CurrentFirstLODIdx && CurrentFirstLODIdx == RenderData->CurrentFirstLODIdx);

		for (int32 Idx = PendingFirstLODIdx; Idx < CurrentFirstLODIdx; ++Idx)
		{
			IBulkDataIORequest* IORequest = IORequests[Idx];
			if (IORequest)
			{
				IORequests[Idx] = nullptr;

				// Temporary workaround for FORT-245343
				while (!IORequest->PollCompletion())
				{
					FPlatformProcess::Sleep(0.000001f);
				}

				check(IORequest->GetSize() >= 0 && IORequest->GetSize() <= TNumericLimits<uint32>::Max());
				check(!StagingLODDataArray[Idx] && !StagingLODDataSizes[Idx]);

				StagingLODDataArray[Idx] = IORequest->GetReadResults();
				StagingLODDataSizes[Idx] = IORequest->GetSize();
				delete IORequest;
			}
		}
	}
}

void FLandscapeMeshMobileStreamIn_IO::ClearIORequest(const FContext& Context)
{
	for (int32 Idx = PendingFirstLODIdx; Idx < CurrentFirstLODIdx; ++Idx)
	{
		IBulkDataIORequest*& IORequest = IORequests[Idx];
		if (IORequest != nullptr)
		{
			if (!IORequest->PollCompletion())
			{
				IORequest->Cancel();
				IORequest->WaitCompletion();
			}
			delete IORequest;
			IORequest = nullptr;
		}
	}
}

void FLandscapeMeshMobileStreamIn_IO::CancelIORequest()
{
	for (int32 Idx = 0; Idx < MAX_MESH_LOD_COUNT; ++Idx)
	{
		IBulkDataIORequest* IORequest = IORequests[Idx];
		if (IORequest)
		{
			IORequest->Cancel();
		}
	}
}

FLandscapeMeshMobileStreamIn_IO_AsyncReallocate::FLandscapeMeshMobileStreamIn_IO_AsyncReallocate(ULandscapeLODStreamingProxy* InLandscapeProxy, bool bHighPrio)
	: FLandscapeMeshMobileStreamIn_IO(InLandscapeProxy, bHighPrio)
{
	PushTask(FContext(InLandscapeProxy, TT_None), TT_Async, SRA_UPDATE_CALLBACK(DoInitiateIO), TT_None, nullptr);
}

void FLandscapeMeshMobileStreamIn_IO_AsyncReallocate::DoInitiateIO(const FContext& Context)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("LSMMStreamInIOAsyncRealloc_DoInitiateIO"), STAT_LSMMStreamInIOAsyncRealloc_DoInitiateIO, STATGROUP_StreamingDetails);
	check(Context.CurrentThread == TT_Async);
	const FString IOFilename = GetIOFilename(Context);
	SetIORequest(Context, IOFilename);
	PushTask(Context, TT_Async, SRA_UPDATE_CALLBACK(DoGetIORequestResults), TT_Async, SRA_UPDATE_CALLBACK(DoCancelIO));
}

void FLandscapeMeshMobileStreamIn_IO_AsyncReallocate::DoGetIORequestResults(const FContext& Context)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("LSMMStreamInIOAsyncRealloc_DoGetIORequestResults"), STAT_LSMMStreamInIOAsyncRealloc_DoGetIORequestResults, STATGROUP_StreamingDetails);
	check(Context.CurrentThread == TT_Async);
	GetIORequestResults(Context);
	ClearIORequest(Context);
	PushTask(Context, TT_Render, SRA_UPDATE_CALLBACK(DoExpandResources), (EThreadType)Context.CurrentThread, SRA_UPDATE_CALLBACK(DoCancel));
}

void FLandscapeMeshMobileStreamIn_IO_AsyncReallocate::DoExpandResources(const FContext& Context)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("LSMMStreamInIOAsyncRealloc_DoExpandResources"), STAT_LSMMStreamInIOAsyncRealloc_DoExpandResources, STATGROUP_StreamingDetails);
	ExpandResources(Context);
	check(!TaskSynchronization.GetValue());
	PushTask(Context, TT_Render, SRA_UPDATE_CALLBACK(DoFinishUpdate), (EThreadType)Context.CurrentThread, SRA_UPDATE_CALLBACK(DoCancel));
}

void FLandscapeMeshMobileStreamIn_IO_AsyncReallocate::DoCancelIO(const FContext& Context)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("LSMMStreamInIOAsyncRealloc_DoCancelIO"), STAT_LSMMStreamInIOAsyncRealloc_DoCancelIO, STATGROUP_StreamingDetails);
	ClearIORequest(Context);
	PushTask(Context, TT_None, nullptr, (EThreadType)Context.CurrentThread, SRA_UPDATE_CALLBACK(DoCancel));
}

#if WITH_EDITOR
FLandscapeMeshMobileStreamIn_GPUDataOnly::FLandscapeMeshMobileStreamIn_GPUDataOnly(ULandscapeLODStreamingProxy* InLandscapeProxy)
	: FLandscapeMeshMobileStreamIn(InLandscapeProxy)
{
	PushTask(FContext(InLandscapeProxy, TT_None), TT_GameThread, SRA_UPDATE_CALLBACK(DoGetStagingData), TT_None, nullptr);
}

void FLandscapeMeshMobileStreamIn_GPUDataOnly::GetStagingData(const FContext& Context)
{
	LLM_SCOPE(ELLMTag::Landscape);
	const ULandscapeLODStreamingProxy* LandscapeProxy = Context.LandscapeProxy;

	if (!IsCancelled() && LandscapeProxy)
	{
		for (int32 Idx = PendingFirstLODIdx; Idx < CurrentFirstLODIdx; ++Idx)
		{
			ULandscapeLODStreamingProxy::BulkDataType& BulkData = LandscapeProxy->GetStreamingLODBulkData(Idx);
			if (BulkData.GetBulkDataSize() > 0)
			{
				StagingLODDataSizes[Idx] = BulkData.GetBulkDataSize();
				BulkData.GetCopy(&StagingLODDataArray[Idx], false);
			}
		}
	}
}

void FLandscapeMeshMobileStreamIn_GPUDataOnly::DoGetStagingData(const FContext& Context)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("LSMMStreamInGPUDataOnly_DoGetStagingData"), STAT_LSMMStreamInGPUDataOnly_DoGetStagingData, STATGROUP_StreamingDetails);
	GetStagingData(Context);
	PushTask(Context, TT_Render, SRA_UPDATE_CALLBACK(DoExpandResources), (EThreadType)Context.CurrentThread, SRA_UPDATE_CALLBACK(DoCancel));
}

void FLandscapeMeshMobileStreamIn_GPUDataOnly::DoExpandResources(const FContext& Context)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("LSMMStreamInGPUDataOnly_DoExpandResources"), STAT_LSMMStreamInGPUDataOnly_DoExpandResources, STATGROUP_StreamingDetails);
	ExpandResources(Context);
	PushTask(Context, TT_Render, SRA_UPDATE_CALLBACK(DoFinishUpdate), (EThreadType)Context.CurrentThread, SRA_UPDATE_CALLBACK(DoCancel));
}
#endif
