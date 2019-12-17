// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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


FVirtualTextureFeedback::FVirtualTextureFeedback()
	: Size( 0, 0 )
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
	GRenderTargetPool.FreeUnusedResource( FeedbackTextureGPU );
	FeedbackTextureGPU.SafeRelease();
	
	for (int i = 0; i < TargetCapacity; ++i)
	{
		if (FeedbackTextureCPU[i].TextureCPU.IsValid())
		{
			GRenderTargetPool.FreeUnusedResource(FeedbackTextureCPU[i].TextureCPU);
			FeedbackTextureCPU[i].TextureCPU.SafeRelease();
		}
	}

	FeedBackFences->ReleaseAll();

	CPUReadIndex = 0u;
	GPUWriteIndex = 0u;
	PendingTargetCount = 0u;
}

void FVirtualTextureFeedback::CreateResourceGPU( FRHICommandListImmediate& RHICmdList, FIntPoint InSize)
{
	Size = InSize;

	FPooledRenderTargetDesc Desc( FPooledRenderTargetDesc::Create2DDesc( Size, PF_R32_UINT, FClearValueBinding::None, TexCreate_None, TexCreate_UAV | TexCreate_ShaderResource, false ) );
	GRenderTargetPool.FindFreeElement( RHICmdList, Desc, FeedbackTextureGPU, TEXT("VTFeedbackGPU") );

	// Clear to default value
	RHICmdList.ClearUAVUint(FeedbackTextureGPU->GetRenderTargetItem().UAV, FUintVector4(~0u, ~0u, ~0u, ~0u));
	RHICmdList.TransitionResource( EResourceTransitionAccess::ERWNoBarrier, EResourceTransitionPipeline::EGfxToGfx, FeedbackTextureGPU->GetRenderTargetItem().UAV );

	FeedBackFences->Init(RHICmdList);
}

void FVirtualTextureFeedback::MakeSnapshot(const FVirtualTextureFeedback& SnapshotSource)
{
	Size = SnapshotSource.Size;
	FeedbackTextureGPU = GRenderTargetPool.MakeSnapshot(SnapshotSource.FeedbackTextureGPU);
	for (int i = 0; i < TargetCapacity; ++i)
	{
		FeedbackTextureCPU[i].TextureCPU = GRenderTargetPool.MakeSnapshot(SnapshotSource.FeedbackTextureCPU[i].TextureCPU);
	}
}

void FVirtualTextureFeedback::TransferGPUToCPU( FRHICommandListImmediate& RHICmdList, TArrayView<FIntRect> const& ViewRects )
{
	RHICmdList.TransitionResource( EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EGfxToGfx, FeedbackTextureGPU->GetRenderTargetItem().UAV );

	GVisualizeTexture.SetCheckPoint( RHICmdList, FeedbackTextureGPU );
	
	if (PendingTargetCount >= TargetCapacity)
	{
		// If we have too many pending transfers, start throwing away the oldest
		// We will need to allocate a new fence, since the previous fence will still be set on the old CopyToResolveTarget command (which we will now ignore/discard)
		FeedBackFences->Release(CPUReadIndex);

		--PendingTargetCount;
		CPUReadIndex = (CPUReadIndex + 1) % TargetCapacity;
	}

	FFeedBackItem& FeedbackEntryCPU = FeedbackTextureCPU[GPUWriteIndex];
	FeedbackEntryCPU.NumRects = FMath::Min((int32)MaxRectPerTarget, ViewRects.Num());
	for (int32 RectIndex = 0; RectIndex < FeedbackEntryCPU.NumRects; ++RectIndex)
	{
		FIntRect const& Rect = ViewRects[RectIndex];
		//todo[vt]: Value of 16 has to match r.vt.FeedbackFactor
		FeedbackEntryCPU.Rects[RectIndex].Min = FIntPoint(Rect.Min.X / 16, Rect.Min.Y / 16);
		FeedbackEntryCPU.Rects[RectIndex].Max = FIntPoint((Rect.Max.X + 15) / 16, (Rect.Max.Y + 15) / 16);
	}

	const FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(Size, PF_R32_UINT, FClearValueBinding::None, TexCreate_CPUReadback | TexCreate_HideInVisualizeTexture, TexCreate_None, false));

	const TCHAR* DebugNames[TargetCapacity] = { TEXT("VTFeedbackCPU_0") , TEXT("VTFeedbackCPU_1") , TEXT("VTFeedbackCPU_2") , TEXT("VTFeedbackCPU_3") };
	GRenderTargetPool.FindFreeElement(RHICmdList, Desc, FeedbackEntryCPU.TextureCPU, DebugNames[GPUWriteIndex]);
	
	FeedBackFences->Allocate(RHICmdList, GPUWriteIndex);

	// We only need to transfer 1 copy of the data, so restrict mask to the first active GPU.
	FeedbackEntryCPU.GPUMask = FRHIGPUMask::FromIndex(RHICmdList.GetGPUMask().GetFirstIndex());
	SCOPED_GPU_MASK(RHICmdList, FeedbackEntryCPU.GPUMask);

	// Transfer memory GPU -> CPU
	RHICmdList.CopyToResolveTarget(
		FeedbackTextureGPU->GetRenderTargetItem().TargetableTexture,
		FeedbackEntryCPU.TextureCPU->GetRenderTargetItem().ShaderResourceTexture,
		FResolveParams());
	
	FeedBackFences->Write(RHICmdList, GPUWriteIndex);

	GPUWriteIndex = (GPUWriteIndex + 1) % TargetCapacity;
	++PendingTargetCount;
}

bool FVirtualTextureFeedback::CanMap(FRHICommandListImmediate& RHICmdList)
{
	const FFeedBackItem& FeedbackEntryCPU = FeedbackTextureCPU[CPUReadIndex];
	return (
		PendingTargetCount > 0u &&
		FeedbackEntryCPU.TextureCPU.IsValid() &&
		FeedBackFences->Poll(RHICmdList, CPUReadIndex));
}

bool FVirtualTextureFeedback::Map(FRHICommandListImmediate& RHICmdList, MapResult& OutResult)
{
	const FFeedBackItem& FeedbackEntryCPU = FeedbackTextureCPU[CPUReadIndex];
	if (PendingTargetCount > 0u &&
		FeedbackEntryCPU.TextureCPU.IsValid() &&
		FeedBackFences->Poll(RHICmdList, CPUReadIndex))
	{
		SCOPED_GPU_MASK(RHICmdList, FeedbackEntryCPU.GPUMask);

		OutResult.MapHandle = CPUReadIndex;
		
		OutResult.NumRects = FeedbackEntryCPU.NumRects;
		for (int32 i = 0; i < FeedbackEntryCPU.NumRects; ++i)
		{
			OutResult.Rects[i] = FeedbackEntryCPU.Rects[i];
		}

		int32 LockHeight = 0;
		RHICmdList.MapStagingSurface(FeedbackEntryCPU.TextureCPU->GetRenderTargetItem().ShaderResourceTexture, FeedBackFences->GetMapFence(CPUReadIndex), *(void**)&OutResult.Buffer, OutResult.Pitch, LockHeight);

		check(PendingTargetCount > 0u);
		--PendingTargetCount;
		CPUReadIndex = (CPUReadIndex + 1) % TargetCapacity;

		return true;
	}

	return false;
}

void FVirtualTextureFeedback::Unmap( FRHICommandListImmediate& RHICmdList, int32 MapHandle )
{
	check(FeedbackTextureCPU[MapHandle].TextureCPU.IsValid());

	SCOPED_GPU_MASK(RHICmdList, FeedbackTextureCPU[MapHandle].GPUMask);
	RHICmdList.UnmapStagingSurface(FeedbackTextureCPU[MapHandle].TextureCPU->GetRenderTargetItem().ShaderResourceTexture);
	GRenderTargetPool.FreeUnusedResource(FeedbackTextureCPU[MapHandle].TextureCPU);
}
