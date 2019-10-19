// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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

	TRefCountPtr< IPooledRenderTarget >	FeedbackTextureGPU;

	struct MapResult
	{
		int32 MapHandle;
		uint32* RESTRICT Buffer;
		FIntRect Rect;
		int32 Pitch;
	};

	void			CreateResourceGPU(FRHICommandListImmediate& RHICmdList, FIntPoint InSize);
	void			ReleaseResources();
	void			MakeSnapshot(const FVirtualTextureFeedback& SnapshotSource);

	void			TransferGPUToCPU(FRHICommandListImmediate& RHICmdList, FIntRect const& Rect);

	uint32			GetPendingTargetCount() const { return PendingTargetCount; }

	bool			CanMap(FRHICommandListImmediate& RHICmdList);
	bool			Map(FRHICommandListImmediate& RHICmdList, MapResult& OutResult);
	void			Unmap(FRHICommandListImmediate& RHICmdList, int32 MapHandle);

private:
	struct FFeedBackItem
	{
		FIntRect Rect;
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
