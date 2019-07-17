// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RenderTargetPool.h: Scene render target pool manager.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RenderResource.h"
#include "RendererInterface.h"
#include "RenderGraphResources.h"


/**
 * Pools all resources for the render graph.
 */
class RENDERCORE_API FRenderGraphResourcePool : public FRenderResource
{
public:
	FRenderGraphResourcePool();

	/** Allocate a buffer from a given descriptor. */
	void FindFreeBuffer(
		FRHICommandList& RHICmdList,
		const FRDGBufferDesc& Desc,
		TRefCountPtr<FPooledRDGBuffer>& Out,
		const TCHAR* InDebugName);

	/** Free renderer resources */
	virtual void ReleaseDynamicRHI() override;

	/** Call once per frame to trim elements from the pool. */
	void TickPoolElements();

private:
	/** Elements can be 0, we compact the buffer later. */
	TArray<TRefCountPtr<FPooledRDGBuffer>> AllocatedBuffers;

	uint32 FrameCounter = 0;
};

/** The global render targets for easy shading. */
extern RENDERCORE_API TGlobalResource<FRenderGraphResourcePool> GRenderGraphResourcePool;
