// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraphResources.h"

/** Class that queues up batches of resource barriers and then submits them to a command list. */
class RENDERCORE_API FRDGBarrierBatcher final
{
public:
	FRDGBarrierBatcher() = default;
	~FRDGBarrierBatcher();

	void Begin();

	void QueueTransitionTexture(FRDGTexture* Texture, FRDGResourceState StateAfter);

	void QueueTransitionUAV(
		FRHIUnorderedAccessView* UAV,
		FRDGTrackedResource* UnderlyingResource,
		FRDGResourceState StateAfter);

	/** Submits and clears all queued transitions to the provided RHI command list. */
	void End(FRHICommandList& RHICmdList);

private:
	static void ValidateTransition(FRDGResourceState StateBefore, FRDGResourceState StateAfter, const FRDGTrackedResource* Resource);

	struct FTransitionParameters
	{
		FTransitionParameters() = default;

		bool operator==(const FTransitionParameters& Other) const
		{
			return TransitionAccess == Other.TransitionAccess && TransitionPipeline == Other.TransitionPipeline;
		}

		uint32 GetHash() const
		{
			return uint32(TransitionAccess) | (uint32(TransitionPipeline) << 8);
		}

		EResourceTransitionAccess TransitionAccess;
		EResourceTransitionPipeline TransitionPipeline = EResourceTransitionPipeline::EGfxToGfx;
	};

	template <typename TBatchType>
	struct TBatchMapKeyFuncs : public TDefaultMapKeyFuncs<FTransitionParameters, TBatchType, /** bAllowDuplicateKeys */ false>
	{
		static uint32 GetKeyHash(FTransitionParameters Key)
		{
			return Key.GetHash();
		}
	};

	using FUAVBatch = TArray<FRHIUnorderedAccessView*, SceneRenderingAllocator>;
	using FUAVBatchMap = TMap<FTransitionParameters, FUAVBatch, SceneRenderingSetAllocator, TBatchMapKeyFuncs<FUAVBatch>>;

	using FTextureBatch = TArray<FRHITexture*, SceneRenderingAllocator>;
	using FTextureBatchMap = TMap<FTransitionParameters, FTextureBatch, SceneRenderingSetAllocator, TBatchMapKeyFuncs<FTextureBatch>>;

	FTextureBatch TextureUpdateMultiFrameBegins;
	FTextureBatch TextureUpdateMultiFrameEnds;
	FTextureBatchMap TextureBatchMap;

	FUAVBatch UAVUpdateMultiFrameBegins;
	FUAVBatch UAVUpdateMultiFrameEnds;
	FUAVBatchMap UAVBatchMap;

	bool bAllowQueueing = false;
};