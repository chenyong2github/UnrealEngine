// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RenderGraphBarrierBatcher.h"
#include "RenderGraphPass.h"

namespace
{

/** Number of entries to reserve in the batch array. */
const uint32 kBatchReservationSize = 8;

/** Returns whether the resource is transitioning to a writable state. */
inline bool IsWriteAccessBegin(FRDGResourceState::EAccess AccessBefore, FRDGResourceState::EAccess AccessAfter)
{
	return AccessBefore == FRDGResourceState::EAccess::Read && AccessAfter == FRDGResourceState::EAccess::Write;
}

/** Returns whether the resource is transitioning from a writable state. */
inline bool IsWriteAccessEnd(FRDGResourceState::EAccess AccessBefore, FRDGResourceState::EAccess AccessAfter)
{
	return AccessBefore == FRDGResourceState::EAccess::Write && AccessAfter == FRDGResourceState::EAccess::Read;
}

inline EResourceTransitionPipeline GetResourceTransitionPipeline(FRDGResourceState::EPipeline PipelineBefore, FRDGResourceState::EPipeline PipelineAfter)
{
	switch (PipelineBefore)
	{
	case FRDGResourceState::EPipeline::Graphics:
		switch (PipelineAfter)
		{
		case FRDGResourceState::EPipeline::Graphics:
			return EResourceTransitionPipeline::EGfxToGfx;
		case FRDGResourceState::EPipeline::Compute:
			return EResourceTransitionPipeline::EGfxToCompute;
		}
		break;
	case FRDGResourceState::EPipeline::Compute:
		switch (PipelineAfter)
		{
		case FRDGResourceState::EPipeline::Graphics:
			return EResourceTransitionPipeline::EComputeToGfx;
		case FRDGResourceState::EPipeline::Compute:
			return EResourceTransitionPipeline::EComputeToCompute;
		}
		break;
	}
	check(false);
	return EResourceTransitionPipeline::EGfxToGfx;
}

inline EResourceTransitionAccess GetResourceTransitionAccess(FRDGResourceState::EAccess AccessAfter)
{
	return AccessAfter == FRDGResourceState::EAccess::Write ? EResourceTransitionAccess::EWritable : EResourceTransitionAccess::EReadable;
}

inline EResourceTransitionAccess GetResourceTransitionAccessForUAV(FRDGResourceState::EAccess AccessBefore, FRDGResourceState::EAccess AccessAfter)
{
	switch (AccessAfter)
	{
	case FRDGResourceState::EAccess::Read:
		return EResourceTransitionAccess::EReadable;
	case FRDGResourceState::EAccess::Write:
		// If doing a Write -> Write transition, we use a UAV barrier.
		if (AccessBefore == FRDGResourceState::EAccess::Write)
		{
			return EResourceTransitionAccess::ERWBarrier;
		}
		else
		{
			return EResourceTransitionAccess::EWritable;
		}
	}
	check(false);
	return EResourceTransitionAccess::EMaxAccess;
}

} //! namespace

FRDGBarrierBatcher::~FRDGBarrierBatcher()
{
	check(!bAllowQueueing);
}

void FRDGBarrierBatcher::Begin()
{
#if RDG_ENABLE_DEBUG
	check(!bAllowQueueing);

	check(!TextureUpdateMultiFrameBegins.Num());
	check(!TextureUpdateMultiFrameEnds.Num());
	check(!TextureBatchMap.Num());

	check(!UAVUpdateMultiFrameBegins.Num());
	check(!UAVUpdateMultiFrameEnds.Num());
	check(!UAVBatchMap.Num());
#endif

	bAllowQueueing = true;
}

void FRDGBarrierBatcher::QueueTransitionTexture(FRDGTexture* Texture, FRDGResourceState StateAfter)
{
	check(Texture);

	const FRDGResourceState StateBefore = Texture->State;

	ValidateTransition(StateBefore, StateAfter, Texture);

	if (StateBefore != StateAfter)
	{
		FRHITexture* RHITexture = Texture->GetRHIUnchecked();

		const bool bIsMultiFrameResource = (Texture->Flags & ERDGResourceFlags::MultiFrame) == ERDGResourceFlags::MultiFrame;

		if (bIsMultiFrameResource && IsWriteAccessBegin(StateBefore.Access, StateAfter.Access))
		{
			TextureUpdateMultiFrameBegins.AddUnique(RHITexture);
		}

		// Add transition to the respective batch bucket.
		{
			FTransitionParameters TransitionParameters;
			TransitionParameters.TransitionAccess = GetResourceTransitionAccess(StateAfter.Access);
			TransitionParameters.TransitionPipeline = EResourceTransitionPipeline::EGfxToGfx; // NOTE: Transition API for textures doesn't currently expose pipeline transitions.

			FTextureBatch& TextureBatch = TextureBatchMap.FindOrAdd(TransitionParameters);
			TextureBatch.Reserve(kBatchReservationSize);

			#if RDG_ENABLE_DEBUG
			{
				// We should have filtered out duplicates in the first branch of this function.
				check(TextureBatch.Find(RHITexture) == INDEX_NONE);
			}
			#endif

			TextureBatch.Add(RHITexture);
		}

		if (bIsMultiFrameResource && IsWriteAccessEnd(StateBefore.Access, StateAfter.Access))
		{
			TextureUpdateMultiFrameEnds.AddUnique(RHITexture);
		}

		Texture->State = StateAfter;
	}
}

void FRDGBarrierBatcher::QueueTransitionUAV(
	FRHIUnorderedAccessView* UAV,
	FRDGTrackedResource* UnderlyingResource,
	FRDGResourceState StateAfter)
{
	check(UAV);
	check(UnderlyingResource);

	const FRDGResourceState StateBefore = UnderlyingResource->State;

	ValidateTransition(StateBefore, StateAfter, UnderlyingResource);

	if (StateBefore != StateAfter)
	{
		const bool bIsMultiFrameResource = (UnderlyingResource->Flags & ERDGResourceFlags::MultiFrame) == ERDGResourceFlags::MultiFrame;

		if (bIsMultiFrameResource && IsWriteAccessBegin(StateBefore.Access, StateAfter.Access))
		{
			UAVUpdateMultiFrameBegins.AddUnique(UAV);
		}

		// Add transition to the correct batch bucket.
		{
			FTransitionParameters TransitionParameters;
			TransitionParameters.TransitionAccess = GetResourceTransitionAccessForUAV(StateBefore.Access, StateAfter.Access);
			TransitionParameters.TransitionPipeline = GetResourceTransitionPipeline(StateBefore.Pipeline, StateAfter.Pipeline);

			FUAVBatch& UAVBatch = UAVBatchMap.FindOrAdd(TransitionParameters);
			UAVBatch.Reserve(kBatchReservationSize);

			#if RDG_ENABLE_DEBUG
			{
				// We should have filtered out duplicates in the first branch of this function.
				check(UAVBatch.Find(UAV) == INDEX_NONE);
			}
			#endif

			UAVBatch.Add(UAV);
		}

		if (bIsMultiFrameResource && IsWriteAccessEnd(StateBefore.Access, StateAfter.Access))
		{
			UAVUpdateMultiFrameEnds.AddUnique(UAV);
		}

		UnderlyingResource->State = StateAfter;
	}
}

void FRDGBarrierBatcher::End(FRHICommandList& RHICmdList)
{
	check(bAllowQueueing);
	bAllowQueueing = false;

	for (FRHITexture* RHITexture : TextureUpdateMultiFrameBegins)
	{
		RHICmdList.BeginUpdateMultiFrameResource(RHITexture);
	}
	TextureUpdateMultiFrameBegins.Empty();

	for (FRHIUnorderedAccessView* RHIUAV : UAVUpdateMultiFrameBegins)
	{
		RHICmdList.BeginUpdateMultiFrameResource(RHIUAV);
	}
	UAVUpdateMultiFrameBegins.Empty();

	for (auto& Element : TextureBatchMap)
	{
		FTransitionParameters TransitionParameters = Element.Key;
		FTextureBatch& Batch = Element.Value;
		RHICmdList.TransitionResources(TransitionParameters.TransitionAccess, Batch.GetData(), Batch.Num());
	}
	TextureBatchMap.Empty();

	for (auto& Element : UAVBatchMap)
	{
		FTransitionParameters TransitionParameters = Element.Key;
		FUAVBatch& Batch = Element.Value;
		RHICmdList.TransitionResources(TransitionParameters.TransitionAccess, TransitionParameters.TransitionPipeline, Batch.GetData(), Batch.Num());
	}
	UAVBatchMap.Empty();

	for (FRHITexture* RHITexture : TextureUpdateMultiFrameEnds)
	{
		RHICmdList.EndUpdateMultiFrameResource(RHITexture);
	}
	TextureUpdateMultiFrameEnds.Empty();

	for (FRHIUnorderedAccessView* RHIUAV : UAVUpdateMultiFrameEnds)
	{
		RHICmdList.EndUpdateMultiFrameResource(RHIUAV);
	}
	UAVUpdateMultiFrameEnds.Empty();
}

void FRDGBarrierBatcher::ValidateTransition(FRDGResourceState StateBefore, FRDGResourceState StateAfter, const FRDGTrackedResource* Resource)
{
#if RDG_ENABLE_DEBUG
	check(StateAfter.Pipeline != FRDGResourceState::EPipeline::MAX);
	check(StateAfter.Access != FRDGResourceState::EAccess::Unknown);

	if (StateBefore != StateAfter && StateAfter.Pass)
	{
		// We allow duplicate transitions of the same resource within the same pass, but not conflicting ones.
		checkf(
			StateBefore.Pass != StateAfter.Pass,
			TEXT("Pass %s attempted to transition resource %s to different states. Make sure the resource isn't being used\n")
			TEXT("for both read and write at the same time. This can occur if the resource is used as both an SRV and UAV, or\n")
			TEXT("SRV and Render Target, for example.\n"), StateAfter.Pass->GetName(), Resource->Name);
	}
#endif
}