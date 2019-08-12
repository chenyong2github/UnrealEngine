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

} //! namespace

FRDGBarrierBatcher::FRDGBarrierBatcher(FRHICommandList& InRHICmdList, const FRDGPass* InPass)
	: RHICmdList(InRHICmdList)
	, Pass(InPass)
{
	if (Pass)
	{
		bIsGeneratingMips = Pass->IsGenerateMips();
		Pipeline = Pass->IsCompute() ? FRDGResourceState::EPipeline::Compute : FRDGResourceState::EPipeline::Graphics;
	}
}

FRDGBarrierBatcher::~FRDGBarrierBatcher()
{
	for (FRHITexture* RHITexture : TextureUpdateMultiFrameBegins)
	{
		RHICmdList.BeginUpdateMultiFrameResource(RHITexture);
	}

	for (FRHIUnorderedAccessView* RHIUAV : UAVUpdateMultiFrameBegins)
	{
		RHICmdList.BeginUpdateMultiFrameResource(RHIUAV);
	}

	for (auto& Element : TextureBatchMap)
	{
		FTransitionParameters TransitionParameters = Element.Key;
		FTextureBatch& Batch = Element.Value;
		RHICmdList.TransitionResources(TransitionParameters.TransitionAccess, Batch.GetData(), Batch.Num());
	}

	for (auto& Element : UAVBatchMap)
	{
		FTransitionParameters TransitionParameters = Element.Key;
		FUAVBatch& Batch = Element.Value;
		RHICmdList.TransitionResources(TransitionParameters.TransitionAccess, TransitionParameters.TransitionPipeline, Batch.GetData(), Batch.Num());
	}

	for (FRHITexture* RHITexture : TextureUpdateMultiFrameEnds)
	{
		RHICmdList.EndUpdateMultiFrameResource(RHITexture);
	}

	for (FRHIUnorderedAccessView* RHIUAV : UAVUpdateMultiFrameEnds)
	{
		RHICmdList.EndUpdateMultiFrameResource(RHIUAV);
	}
}

void FRDGBarrierBatcher::QueueTransitionTexture(FRDGTexture* Texture, FRDGResourceState::EAccess AccessAfter)
{
	check(Texture);

	// Texture transitions are ignored when generating mips, since the render target binding call or UAV will
	// perform the subresource transition.
	if (bIsGeneratingMips)
	{
		return;
	}

	const FRDGResourceState StateBefore = Texture->State;
	const FRDGResourceState StateAfter(Pass, Pipeline, AccessAfter);

	ValidateTransition(Texture, StateBefore, StateAfter);

	if (StateBefore != StateAfter)
	{
		FRHITexture* RHITexture = Texture->PooledRenderTarget->GetRenderTargetItem().TargetableTexture;

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
				ensure(TextureBatch.Find(RHITexture) == INDEX_NONE);
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
	FRDGParentResource* ParentResource,
	FRDGResourceState::EAccess AccessAfter)
{
	check(UAV);
	check(ParentResource);

	const FRDGResourceState StateBefore = ParentResource->State;
	const FRDGResourceState StateAfter(Pass, Pipeline, AccessAfter);

	ValidateTransition(ParentResource, StateBefore, StateAfter);

	if (StateBefore != StateAfter)
	{
		const bool bIsMultiFrameResource = (ParentResource->Flags & ERDGResourceFlags::MultiFrame) == ERDGResourceFlags::MultiFrame;

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
				ensure(UAVBatch.Find(UAV) == INDEX_NONE);
			}
			#endif

			UAVBatch.Add(UAV);
		}

		if (bIsMultiFrameResource && IsWriteAccessEnd(StateBefore.Access, StateAfter.Access))
		{
			UAVUpdateMultiFrameEnds.AddUnique(UAV);
		}

		ParentResource->State = StateAfter;
	}
}

void FRDGBarrierBatcher::ValidateTransition(const FRDGParentResource* Resource, FRDGResourceState StateBefore, FRDGResourceState StateAfter)
{
#if RDG_ENABLE_DEBUG
	check(StateAfter.Pipeline != FRDGResourceState::EPipeline::MAX);
	check(StateAfter.Access != FRDGResourceState::EAccess::Unknown);

	if (StateBefore != StateAfter && StateAfter.Pass)
	{
		// We allow duplicate transitions of the same resource within the same pass, but not conflicting ones.
		ensureMsgf(
			StateBefore.Pass != StateAfter.Pass,
			TEXT("Pass %s attempted to transition resource %s to different states. Make sure the resource isn't being used\n")
			TEXT("for both read and write at the same time. This can occur if the resource is used as both an SRV and UAV, or\n")
			TEXT("SRV and Render Target, for example. If this pass is meant to generate mip maps, make sure the GenerateMips flag\n")
			TEXT("is set.\n"), StateAfter.Pass->GetName(), Resource->Name);
	}
#endif
}

EResourceTransitionAccess FRDGBarrierBatcher::GetResourceTransitionAccess(FRDGResourceState::EAccess AccessAfter) const
{
	return AccessAfter == FRDGResourceState::EAccess::Write ? EResourceTransitionAccess::EWritable : EResourceTransitionAccess::EReadable;
}

EResourceTransitionAccess FRDGBarrierBatcher::GetResourceTransitionAccessForUAV(FRDGResourceState::EAccess AccessBefore, FRDGResourceState::EAccess AccessAfter) const
{
	switch (AccessAfter)
	{
	case FRDGResourceState::EAccess::Read:
		return EResourceTransitionAccess::EReadable;

	case FRDGResourceState::EAccess::Write:
		// Mip-map generation uses its own barrier.
		if (bIsGeneratingMips)
		{
			return EResourceTransitionAccess::ERWSubResBarrier;
		}
		// If doing a Write -> Write transition, we use a UAV barrier.
		else if (AccessBefore == FRDGResourceState::EAccess::Write)
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
