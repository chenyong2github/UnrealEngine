// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualTextureFeedback.h"

#include "ClearQuad.h"
#include "VisualizeTexture.h"

#if PLATFORM_WINDOWS
// Temporary use of Querys until RHI has a good fence on D3D11
#define USE_RHI_FENCES 0
#else
#define USE_RHI_FENCES 1
#endif

#if USE_RHI_FENCES
class FFeedbackFences
{
public:
	FGPUFenceRHIRef Fences[FVirtualTextureFeedback::TargetCapacity];

	void Init(FRHICommandListImmediate& RHICmdList)
	{
	}

	void ReleaseAll()
	{
		for (int i = 0; i < FVirtualTextureFeedback::TargetCapacity; ++i)
		{
			Fences[i].SafeRelease();
		}
	}

	void Allocate(FRHICommandListImmediate& RHICmdList, int32 Index)
	{
		if (!Fences[Index])
		{
			Fences[Index] = RHICmdList.CreateGPUFence(FName(""));
		}
		Fences[Index]->Clear();
	}

	void Write(FRHICommandListImmediate& RHICmdList, int32 Index)
	{
		RHICmdList.WriteGPUFence(Fences[Index]);
	}

	bool Poll(FRHICommandListImmediate& RHICmdList, int32 Index)
	{
		return Fences[Index]->Poll();
	}

	FGPUFenceRHIRef GetMapFence(int32 Index)
	{
		return Fences[Index];
	}

	void Release(int32 Index)
	{
		Fences[Index].SafeRelease();
	}
};
#else
class FFeedbackFences
{
public:
	FRenderQueryPoolRHIRef FenceQueryPool;
	FGPUFenceRHIRef DummyFence;
	FRHIPooledRenderQuery Fences[FVirtualTextureFeedback::TargetCapacity];

	void Init(FRHICommandListImmediate& RHICmdList)
	{
		if (!FenceQueryPool.IsValid())
		{
			FenceQueryPool = RHICreateRenderQueryPool(RQT_AbsoluteTime, FVirtualTextureFeedback::TargetCapacity);
		}

		if (!DummyFence.IsValid())
		{
			DummyFence = RHICmdList.CreateGPUFence(FName());
			RHICmdList.WriteGPUFence(DummyFence);
		}
	}

	void ReleaseAll()
	{
		for (int i = 0; i < FVirtualTextureFeedback::TargetCapacity; ++i)
		{
			if (Fences[i].IsValid())
			{
				Fences[i].ReleaseQuery();
			}
		}

		DummyFence.SafeRelease();

		FenceQueryPool.SafeRelease();
	}

	void Allocate(FRHICommandListImmediate& RHICmdList, int32 Index)
	{
		if (!Fences[Index].IsValid())
		{
			Fences[Index] = FenceQueryPool->AllocateQuery();
		}
	}
	
	void Write(FRHICommandListImmediate& RHICmdList, int32 Index)
	{
		RHICmdList.EndRenderQuery(Fences[Index].GetQuery());
	}

	bool Poll(FRHICommandListImmediate& RHICmdList, int32 Index)
	{
		uint64 Dummy;
		return RHICmdList.GetRenderQueryResult(Fences[Index].GetQuery(), Dummy, false);
	}

	FGPUFenceRHIRef GetMapFence(int32 Index)
	{
		return DummyFence;
	}

	void Release(int32 Index)
	{
		Fences[Index].ReleaseQuery();
	}
};
#endif

#include "RHIGPUReadback.h"

TGlobalResource<FVirtualTextureFeedbackDummyResource> GVirtualTextureFeedbackDummyResource;

FVirtualTextureFeedback::FVirtualTextureFeedback()
	: Size(0, 0)
	, NumBytes(0)
	, GPUWriteIndex(0)
	, CPUReadIndex(0)
	, PendingTargetCount(0)
{
	FeedBackFences = new FFeedbackFences;
}

FVirtualTextureFeedback::~FVirtualTextureFeedback()
{
	delete FeedBackFences;
}

void FVirtualTextureFeedback::ReleaseResources()
{
	FeedbackBufferUAV.SafeRelease();
	FeedbackBuffer.SafeRelease();
	
	for (int i = 0; i < TargetCapacity; ++i)
	{
		FeedbackCPU[i].ReadbackBuffer.SafeRelease();
	}

	FeedBackFences->ReleaseAll();

	CPUReadIndex = 0u;
	GPUWriteIndex = 0u;
	PendingTargetCount = 0u;
}

void FVirtualTextureFeedback::CreateResourceGPU( FRHICommandListImmediate& RHICmdList, FIntPoint InSize)
{
	if (Size != InSize || !FeedbackBuffer.IsValid())
	{
		Size = InSize;
		NumBytes = Size.X * Size.Y * sizeof(uint32);

		FRHIResourceCreateInfo CreateInfo(TEXT("VTFeedbackGPU"));
		FeedbackBuffer = RHICreateVertexBuffer(NumBytes, BUF_Static | BUF_ShaderResource | BUF_UnorderedAccess | BUF_SourceCopy, CreateInfo);
		FeedbackBufferUAV = RHICreateUnorderedAccessView(FeedbackBuffer, /*Format=*/ PF_R32_UINT);
	}
	
	// Clear to default value
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWNoBarrier, EResourceTransitionPipeline::EGfxToCompute, FeedbackBufferUAV);
	RHICmdList.ClearUAVUint(FeedbackBufferUAV.GetReference(), FUintVector4(~0u, ~0u, ~0u, ~0u));
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWNoBarrier, EResourceTransitionPipeline::EComputeToCompute, FeedbackBufferUAV);

	FeedBackFences->Init(RHICmdList);
}

void FVirtualTextureFeedback::TransferGPUToCPU( FRHICommandListImmediate& RHICmdList, TArrayView<FIntRect> const& ViewRects)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_VirtualTextureFeedback_TransferGPUToCPU);

	RHICmdList.TransitionResource( EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EGfxToGfx, FeedbackBufferUAV );

	if (PendingTargetCount >= TargetCapacity)
	{
		// If we have too many pending transfers, start throwing away the oldest
		// We will need to allocate a new fence, since the previous fence will still be set on the old CopyToResolveTarget command (which we will now ignore/discard)
		FeedBackFences->Release(CPUReadIndex);
		--PendingTargetCount;
		CPUReadIndex = (CPUReadIndex + 1) % TargetCapacity;
	}

	FFeedBackItem& FeedbackEntryCPU = FeedbackCPU[GPUWriteIndex];

	FeedbackEntryCPU.NumRects = FMath::Min((int32)MaxRectPerTarget, ViewRects.Num());
	for (int32 RectIndex = 0; RectIndex < FeedbackEntryCPU.NumRects; ++RectIndex)
	{
		FIntRect const& Rect = ViewRects[RectIndex];
		//todo[vt]: Value of 16 has to match r.vt.FeedbackFactor
		FeedbackEntryCPU.Rects[RectIndex].Min = FIntPoint(Rect.Min.X / 16, Rect.Min.Y / 16);
		FeedbackEntryCPU.Rects[RectIndex].Max = FIntPoint((Rect.Max.X + 15) / 16, (Rect.Max.Y + 15) / 16);
	}
	
	FeedBackFences->Allocate(RHICmdList, GPUWriteIndex);

	// We only need to transfer 1 copy of the data, so restrict mask to the first active GPU.
	FeedbackEntryCPU.GPUMask = FRHIGPUMask::FromIndex(RHICmdList.GetGPUMask().GetFirstIndex());
	SCOPED_GPU_MASK(RHICmdList, FeedbackEntryCPU.GPUMask);

	FeedbackEntryCPU.ReadbackBuffer = RHICreateStagingBuffer();
	RHICmdList.CopyToStagingBuffer(FeedbackBuffer, FeedbackEntryCPU.ReadbackBuffer, 0, NumBytes);

	FeedBackFences->Write(RHICmdList, GPUWriteIndex);

	GPUWriteIndex = (GPUWriteIndex + 1) % TargetCapacity;
	++PendingTargetCount;
}

bool FVirtualTextureFeedback::CanMap(FRHICommandListImmediate& RHICmdList)
{
	const FFeedBackItem& FeedbackEntryCPU = FeedbackCPU[CPUReadIndex];
	return 
		PendingTargetCount > 0u && 
		FeedbackEntryCPU.ReadbackBuffer->IsValid() && 
		FeedBackFences->Poll(RHICmdList, CPUReadIndex);
}

bool FVirtualTextureFeedback::Map(FRHICommandListImmediate& RHICmdList, MapResult& OutResult)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_VirtualTextureFeedback_Map);
		
	FFeedBackItem& FeedbackEntryCPU = FeedbackCPU[CPUReadIndex];
		
	if (PendingTargetCount > 0u &&
		FeedbackEntryCPU.ReadbackBuffer->IsValid() &&
		FeedBackFences->Poll(RHICmdList, CPUReadIndex))
	{
		SCOPED_GPU_MASK(RHICmdList, FeedbackEntryCPU.GPUMask);
	
		OutResult.NumRects = FeedbackEntryCPU.NumRects;
		for (int32 i = 0; i < FeedbackEntryCPU.NumRects; ++i)
		{
			OutResult.Rects[i] = FeedbackEntryCPU.Rects[i];
		}
		OutResult.Pitch = Size.X;
		
		void* MappedMem = (uint32*)RHICmdList.LockStagingBuffer(FeedbackEntryCPU.ReadbackBuffer, FeedBackFences->GetMapFence(CPUReadIndex), 0, NumBytes);

		OutResult.Buffer.SetNumUninitialized(NumBytes / sizeof(uint32));
		FMemory::Memcpy(OutResult.Buffer.GetData(), MappedMem, NumBytes);

		RHICmdList.UnlockStagingBuffer(FeedbackEntryCPU.ReadbackBuffer);
		FeedbackEntryCPU.ReadbackBuffer.SafeRelease();
		
		check(PendingTargetCount > 0u);
		--PendingTargetCount;
		CPUReadIndex = (CPUReadIndex + 1) % TargetCapacity;
		return true;
	}

	return false;
}
