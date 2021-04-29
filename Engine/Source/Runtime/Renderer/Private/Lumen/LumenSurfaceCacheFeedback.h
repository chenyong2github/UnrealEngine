// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenSurfaceCacheFeedback.h
=============================================================================*/


#pragma once

#include "CoreMinimal.h"
#include "RenderGraphResources.h"
#include "RHIGPUReadback.h"

class FLumenSceneData;
class FViewInfo;

namespace Lumen
{
	uint32 GetFeedbackBufferTileSize();
	uint32 GetFeedbackBufferTileWrapMask();
}

class FLumenSurfaceCacheFeedback : public FRenderResource
{
public:
	FLumenSurfaceCacheFeedback();
	~FLumenSurfaceCacheFeedback();

	class FFeedbackResources
	{
	public:
		FRDGBufferRef BufferAllocator = nullptr;
		FRDGBufferRef Buffer = nullptr;
		uint32 BufferSize = 0;
	};

	void AllocateFeedbackResources(FRDGBuilder& GraphBuilder, FFeedbackResources& Resouces) const;
	FRDGBufferUAVRef GetDummyFeedbackAllocatorUAV(FRDGBuilder& GraphBuilder) const;
	FRDGBufferUAVRef GetDummyFeedbackUAV(FRDGBuilder& GraphBuilder) const;
	void SubmitFeedbackBuffer(const FViewInfo& View, FRDGBuilder& GraphBuilder, FLumenSurfaceCacheFeedback::FFeedbackResources& FeedbackResources);

	FRHIGPUBufferReadback* GetLatestReadbackBuffer();
	FIntPoint GetFeedbackBufferTileJitter() const;
	uint32 GetFrameIndex() const { return FrameIndex; }

private:

	uint32 FrameIndex = 0;

	const int32 MaxReadbackBuffers = 4;
	int32 ReadbackBuffersWriteIndex = 0;
	int32 ReadbackBuffersNumPending = 0;
	TArray<FRHIGPUBufferReadback*> ReadbackBuffers;
};