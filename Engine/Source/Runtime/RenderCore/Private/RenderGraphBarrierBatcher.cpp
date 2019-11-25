// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RenderGraphBarrierBatcher.h"
#include "RenderGraphPass.h"

namespace
{
	TAutoConsoleVariable<int32> CVarRDGTransitionLogEnable(
		TEXT("r.RDG.TransitionLog.Enable"), 0,
		TEXT("Logs resource transitions to the console.\n"),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarRDGTransitionLogEnableBreakpoint(
		TEXT("r.RDG.TransitionLog.EnableBreakpoint"), 0,
		TEXT("Breaks on a transition log event (set filters first!).\n"),
		ECVF_RenderThreadSafe);

	// TODO: String CVars don't support ECVF_RenderThreadSafe. Use with caution.
	TAutoConsoleVariable<FString> CVarRDGLogTransitionsPassFilter(
		TEXT("r.RDG.TransitionLog.PassFilter"), TEXT(""),
		TEXT("Filters logs to passes with names containing the filter string.\n"),
		ECVF_Default);

	TAutoConsoleVariable<FString> CVarRDGLogTransitionsResourceFilter(
		TEXT("r.RDG.TransitionLog.ResourceFilter"), TEXT(""),
		TEXT("Filters logs to resources with names containing the filter string.\n"),
		ECVF_Default);

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

	inline const TCHAR* GetTransitionPipelineName(EResourceTransitionPipeline Pipeline)
	{
		switch (Pipeline)
		{
		case EResourceTransitionPipeline::EGfxToCompute:
			return TEXT("GfxToCompute");
		case EResourceTransitionPipeline::EComputeToGfx:
			return TEXT("ComputeToGfx");
		case EResourceTransitionPipeline::EGfxToGfx:
			return TEXT("GfxToGfx");
		case EResourceTransitionPipeline::EComputeToCompute:
			return TEXT("ComputeToCompute");
		}
		check(false);
		return TEXT("");
	}

	inline const TCHAR* GetTransitionAccessName(EResourceTransitionAccess Access)
	{
		switch (Access)
		{
		case EResourceTransitionAccess::EReadable:
			return TEXT("Readable");
		case EResourceTransitionAccess::EWritable:
			return TEXT("Writable");
		case EResourceTransitionAccess::ERWBarrier:
			return TEXT("RWBarrier");
		case EResourceTransitionAccess::ERWNoBarrier:
			return TEXT("RWNoBarrier");
		case EResourceTransitionAccess::ERWSubResBarrier:
			return TEXT("RWSubResBarrier");
		case EResourceTransitionAccess::EMetaData:
			return TEXT("MetaData");
		}
		check(false);
		return TEXT("");
	}
} //! namespace

FRDGBarrierBatcher::FRDGBarrierBatcher(FRHICommandList& InRHICmdList, const FRDGPass* InPass)
	: RHICmdList(InRHICmdList)
	, Pass(InPass)
{
	if (Pass)
	{
		Pipeline = Pass->IsCompute() ? FRDGResourceState::EPipeline::Compute : FRDGResourceState::EPipeline::Graphics;
	}
}

FRDGBarrierBatcher::~FRDGBarrierBatcher()
{
#if WITH_MGPU
	// Wait for the temporal effect before executing the first pass in the graph. This
	// will be a no-op for every pass after the first since we don't broadcast in
	// between passes.
	if (Pass != nullptr && NameForTemporalEffect != NAME_None)
	{
		RHICmdList.WaitForTemporalEffect(NameForTemporalEffect);
	}
#endif

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

#if WITH_MGPU
	// Broadcast all multi-frame resources when processing deferred resource queries.
	if (Pass == nullptr && NameForTemporalEffect != NAME_None)
	{
		RHICmdList.BroadcastTemporalEffect(NameForTemporalEffect, TexturesToCopyForTemporalEffect);
	}
#endif
}

void FRDGBarrierBatcher::QueueTransitionTexture(FRDGTexture* Texture, FRDGResourceState::EAccess AccessAfter)
{
	check(Texture);

	const FRDGResourceState StateBefore = Texture->State;
	const FRDGResourceState StateAfter(Pass, Pipeline, AccessAfter);

	ValidateTransition(Texture, StateBefore, StateAfter);

	if (StateBefore != StateAfter)
	{
		FRHITexture* RHITexture = Texture->PooledRenderTarget->GetRenderTargetItem().TargetableTexture;

		// This particular texture does not have a targetable texture. It's effectively read-only.
		if (!RHITexture)
		{
			return;
		}

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

			LogTransition(Texture, TransitionParameters);

			TextureBatch.Add(RHITexture);
		}

		if (bIsMultiFrameResource && IsWriteAccessEnd(StateBefore.Access, StateAfter.Access))
		{
			TextureUpdateMultiFrameEnds.AddUnique(RHITexture);
		}

#if WITH_MGPU
		// Broadcast all multi-frame resources when processing deferred resource queries.
		if (bIsMultiFrameResource && Pass == nullptr)
		{
			TexturesToCopyForTemporalEffect.AddUnique(RHITexture);
		}
#endif

		Texture->State = StateAfter;
	}
}

void FRDGBarrierBatcher::QueueTransitionUAV(
	FRHIUnorderedAccessView* UAV,
	FRDGParentResource* ParentResource,
	FRDGResourceState::EAccess AccessAfter,
	bool bIsGeneratingMips,
	FRDGResourceState::EPipeline PipelineAfter)
{
	check(UAV);
	check(ParentResource);

	const FRDGResourceState StateBefore = ParentResource->State;
	const FRDGResourceState StateAfter(Pass, PipelineAfter == FRDGResourceState::EPipeline::MAX ? Pipeline : PipelineAfter, AccessAfter);

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
			TransitionParameters.TransitionAccess = GetResourceTransitionAccessForUAV(StateBefore.Access, StateAfter.Access, bIsGeneratingMips);
			TransitionParameters.TransitionPipeline = GetResourceTransitionPipeline(StateBefore.Pipeline, StateAfter.Pipeline);

			FUAVBatch& UAVBatch = UAVBatchMap.FindOrAdd(TransitionParameters);
			UAVBatch.Reserve(kBatchReservationSize);

			#if RDG_ENABLE_DEBUG
			{
				// We should have filtered out duplicates in the first branch of this function.
				ensure(UAVBatch.Find(UAV) == INDEX_NONE);
			}
			#endif

			LogTransition(ParentResource, TransitionParameters);

			UAVBatch.Add(UAV);
		}

		if (bIsMultiFrameResource && IsWriteAccessEnd(StateBefore.Access, StateAfter.Access))
		{
			UAVUpdateMultiFrameEnds.AddUnique(UAV);
		}

		ParentResource->State = StateAfter;
	}
}

void FRDGBarrierBatcher::ValidateTransition(const FRDGParentResource* Resource, FRDGResourceState StateBefore, FRDGResourceState StateAfter) const
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

void FRDGBarrierBatcher::LogTransition(const FRDGParentResource* Resource, FTransitionParameters Parameters) const
{
#if RDG_ENABLE_DEBUG
	if (CVarRDGTransitionLogEnable.GetValueOnRenderThread() != 0)
	{
		const FString PassName = Pass ? Pass->GetName() : TEXT("None");
		const FString PassFilterText = CVarRDGLogTransitionsPassFilter.GetValueOnRenderThread();

		if (PassFilterText.IsEmpty() || PassName.Contains(*PassFilterText))
		{
			const FString ResourceName = Resource->Name;
			const FString ResourceFilterText = CVarRDGLogTransitionsResourceFilter.GetValueOnRenderThread();

			if (ResourceFilterText.IsEmpty() || ResourceName.Contains(*ResourceFilterText))
			{
				const TCHAR* PipeName = GetTransitionPipelineName(Parameters.TransitionPipeline);
				const TCHAR* AccessName = GetTransitionAccessName(Parameters.TransitionAccess);
				UE_LOG(LogRendererCore, Display, TEXT("RDG Transition:\tPass('%s'), Resource('%s'), Access(%s), Pipe(%s)"), *PassName, *ResourceName, AccessName, PipeName);

				if (CVarRDGTransitionLogEnableBreakpoint.GetValueOnRenderThread() != 0)
				{
					UE_DEBUG_BREAK();
				}
			}
		}
	}
#endif
}

EResourceTransitionAccess FRDGBarrierBatcher::GetResourceTransitionAccess(FRDGResourceState::EAccess AccessAfter) const
{
	return AccessAfter == FRDGResourceState::EAccess::Write ? EResourceTransitionAccess::EWritable : EResourceTransitionAccess::EReadable;
}

EResourceTransitionAccess FRDGBarrierBatcher::GetResourceTransitionAccessForUAV(FRDGResourceState::EAccess AccessBefore, FRDGResourceState::EAccess AccessAfter, bool bIsGeneratingMips) const
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
