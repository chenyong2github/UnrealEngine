// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraphResources.h"

/** Class that queues up batches of resource barriers and then submits them to a command list. */
class RENDERCORE_API FRDGBarrierBatcher final
{
public:
	/** RAII initialization of batcher. Batcher will flush all queued transitions in the destructor.
	 *  @param Pass The current pass if performing inter-pass barriers. This can be null (e.g. for post-execution barriers).
	 */
	FRDGBarrierBatcher(FRHICommandList& RHICmdList, const FRDGPass* Pass);
	~FRDGBarrierBatcher();

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

	FRHICommandList& RHICmdList;

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
