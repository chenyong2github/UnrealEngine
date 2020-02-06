// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RenderCore/Public/RenderTargetPool.h"

/*
====================================
	Manages GPU and CPU buffers for VT feedback.
	Shared for all views, not per view.

	Should use append buffer but that requires RHI ability to copy
	a GPU structured buffer to a CPU read only version.
====================================
*/
class FVirtualTextureFeedback
{
public:
	FVirtualTextureFeedback();
	~FVirtualTextureFeedback();

	static const uint32 TargetCapacity = 4u;
	static const uint32 MaxRectPerTarget = 4u;

	struct MapResult
	{
		TArray<uint32> Buffer;
		int32 Pitch;
		int32 NumRects;
		FIntRect Rects[MaxRectPerTarget];
	};

	FVertexBufferRHIRef FeedbackBuffer;
	FUnorderedAccessViewRHIRef FeedbackBufferUAV;

	void			CreateResourceGPU(FRHICommandListImmediate& RHICmdList, FIntPoint InSize);
	void			ReleaseResources();

	void			TransferGPUToCPU(FRHICommandListImmediate& RHICmdList, TArrayView<FIntRect> const& ViewRects);

	uint32			GetPendingTargetCount() const { return PendingTargetCount; }
	uint32			GetFeedbackStride() const { return (uint32)Size.X; }

	bool			CanMap(FRHICommandListImmediate& RHICmdList);
	bool			Map(FRHICommandListImmediate& RHICmdList, MapResult& OutResult);

private:
	struct FFeedBackItem
	{
		int32 NumRects;
		FIntRect Rects[MaxRectPerTarget];
		FRHIGPUMask GPUMask;
		FStagingBufferRHIRef ReadbackBuffer;
	};

	FFeedBackItem FeedbackCPU[TargetCapacity];

	class FFeedbackFences* FeedBackFences;

	FIntPoint Size;
	uint32 NumBytes;

	uint32 GPUWriteIndex;
	uint32 CPUReadIndex;
	uint32 PendingTargetCount;
};


/* Dummy resource for VT feedback, used to bypass UB validation errors when VT is disabled */
class FVirtualTextureFeedbackDummyResource : public FRenderResource
{
public:
	FVertexBufferRHIRef			VertexBufferRHI;
	FUnorderedAccessViewRHIRef	UAV;

	virtual void InitRHI() override
	{
		FRHIResourceCreateInfo CreateInfo(TEXT("VTFeedbackDummy"));
		VertexBufferRHI = RHICreateVertexBuffer(4, BUF_Static | BUF_ShaderResource | BUF_UnorderedAccess, CreateInfo);
		UAV = RHICreateUnorderedAccessView(VertexBufferRHI, /*Format=*/ PF_R32_UINT);
	}
	virtual void ReleaseRHI() override
	{
		UAV.SafeRelease();
		VertexBufferRHI.SafeRelease();
	}
};

extern TGlobalResource<FVirtualTextureFeedbackDummyResource> GVirtualTextureFeedbackDummyResource;

