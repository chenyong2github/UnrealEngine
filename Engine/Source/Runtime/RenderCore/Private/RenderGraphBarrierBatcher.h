// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraphResources.h"
#include "Containers/SortedMap.h"

/** Class that queues up batches of resource barriers and then submits them to a command list. */
class RENDERCORE_API FRDGBarrierBatcher final
{
public:
	FRDGBarrierBatcher() = default;

	void Begin(const FRDGPass* Pass);
	void End(FRHICommandList& RHICmdList);

	/** Queues a transition of the texture to the requested access state. */
	void QueueTransitionTexture(FRDGTexture* Texture, FRDGResourceState::EAccess AccessAfter);

	/** Queues a transition of the UAV to the requested access state. The current state of the resource is checked and the transition
	 *  is only performed if a change occurs. It is valid to queue the same state multiple times. However, it is invalid to queue a
	 *  resource into multiple states at the same time. The underlying resource is the texture / buffer instance referenced by the view.
	 */
	void QueueTransitionUAV(
		FRHIUnorderedAccessView* UAV,
		FRDGParentResource* ParentResource,
		FRDGResourceState::EAccess AccessAfter,
		bool bGeneratingMips = false,
		FRDGResourceState::EPipeline PipelineAfter = FRDGResourceState::EPipeline::MAX);

#if WITH_MGPU
	void SetNameForTemporalEffect(FName InNameForTemporalEffect)
	{
		NameForTemporalEffect = InNameForTemporalEffect;
	}
#endif

private:
	struct FTransitionParameters
	{
		FTransitionParameters() = default;

		bool operator==(const FTransitionParameters& Other) const
		{
			return TransitionAccess == Other.TransitionAccess && TransitionPipeline == Other.TransitionPipeline;
		}

		bool operator<(const FTransitionParameters& Other) const
		{
			return GetHash() < Other.GetHash();
		}

		uint32 GetHash() const
		{
			return uint32(TransitionAccess) | (uint32(TransitionPipeline) << 8);
		}

		EResourceTransitionAccess TransitionAccess;
		EResourceTransitionPipeline TransitionPipeline = EResourceTransitionPipeline::EGfxToGfx;
	};

	using FUAVBatch = TArray<FRHIUnorderedAccessView*, SceneRenderingAllocator>;
	using FUAVBatchMap = TSortedMap<FTransitionParameters, FUAVBatch, SceneRenderingAllocator>;

	using FTextureBatch = TArray<FRHITexture*, SceneRenderingAllocator>;
	using FTextureBatchMap = TSortedMap<FTransitionParameters, FTextureBatch, SceneRenderingAllocator>;

	FTextureBatch TextureUpdateMultiFrameBegins;
	FTextureBatch TextureUpdateMultiFrameEnds;
	FTextureBatchMap TextureBatchMap;

	FUAVBatch UAVUpdateMultiFrameBegins;
	FUAVBatch UAVUpdateMultiFrameEnds;
	FUAVBatchMap UAVBatchMap;

#if WITH_MGPU
	FName NameForTemporalEffect;
	FTextureBatch TexturesToCopyForTemporalEffect;
#endif

	void ValidateTransition(const FRDGParentResource* Resource, FRDGResourceState StateBefore, FRDGResourceState StateAfter) const;
	void LogTransition(const FRDGParentResource* Resource, FTransitionParameters Parameters) const;

	EResourceTransitionAccess GetResourceTransitionAccess(FRDGResourceState::EAccess AccessAfter) const;
	EResourceTransitionAccess GetResourceTransitionAccessForUAV(FRDGResourceState::EAccess AccessBefore, FRDGResourceState::EAccess AccessAfter, bool bIsGeneratingMips) const;

	const FRDGPass* Pass = nullptr;
	FRDGResourceState::EPipeline Pipeline = FRDGResourceState::EPipeline::Graphics;
};
