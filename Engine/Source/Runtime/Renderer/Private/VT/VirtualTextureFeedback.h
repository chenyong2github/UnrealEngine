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

	TRefCountPtr< IPooledRenderTarget >	FeedbackTextureGPU;

	struct MapResult
	{
		int32 MapHandle;
		uint32* RESTRICT Buffer;
		int32 Pitch;
		int32 NumRects;
		FIntRect Rects[MaxRectPerTarget];
	};

	void			CreateResourceGPU(FRHICommandListImmediate& RHICmdList, FIntPoint InSize);
	void			ReleaseResources();
	void			MakeSnapshot(const FVirtualTextureFeedback& SnapshotSource);

	void			TransferGPUToCPU(FRHICommandListImmediate& RHICmdList, TArrayView<FIntRect> const& ViewRects);

	uint32			GetPendingTargetCount() const { return PendingTargetCount; }

	bool			CanMap(FRHICommandListImmediate& RHICmdList);
	bool			Map(FRHICommandListImmediate& RHICmdList, MapResult& OutResult);
	void			Unmap(FRHICommandListImmediate& RHICmdList, int32 MapHandle);

private:
	struct FFeedBackItem
	{
		int32 NumRects;
		FIntRect Rects[MaxRectPerTarget];
		TRefCountPtr< IPooledRenderTarget > TextureCPU;
		FRHIGPUMask GPUMask;
	};

	FFeedBackItem FeedbackTextureCPU[TargetCapacity];

	class FFeedbackFences* FeedBackFences;

	FIntPoint Size;
	uint32 GPUWriteIndex;
	uint32 CPUReadIndex;
	uint32 PendingTargetCount;
};
