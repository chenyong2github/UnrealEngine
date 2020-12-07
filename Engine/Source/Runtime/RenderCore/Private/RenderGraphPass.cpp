// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderGraphPass.h"
#include "RenderGraphPrivate.h"
#include "RenderGraphUtils.h"

static FRDGPassHandlesByPipeline GetPassesByPipeline(const FRDGPass* Pass)
{
	check(Pass);
	FRDGPassHandlesByPipeline Passes;
	Passes[Pass->GetPipeline()] = Pass->GetHandle();
	return Passes;
}

FUniformBufferStaticBindings FRDGParameterStruct::GetGlobalUniformBuffers() const
{
	FUniformBufferStaticBindings GlobalUniformBuffers;

	for (uint32 Index = 0, Count = Layout->UniformBuffers.Num(); Index < Count; ++Index)
	{
		const uint32 MemberOffset = Layout->UniformBuffers[Index].MemberOffset;
		const FUniformBufferBinding& UniformBuffer = *reinterpret_cast<const FUniformBufferBinding*>(const_cast<uint8*>(Contents + MemberOffset));

		if (UniformBuffer && UniformBuffer.IsStatic())
		{
			GlobalUniformBuffers.AddUniformBuffer(UniformBuffer.GetUniformBuffer());
		}
	}

	EnumerateUniformBuffers([&](FRDGUniformBufferBinding UniformBuffer)
	{
		if (UniformBuffer.IsStatic())
		{
			GlobalUniformBuffers.AddUniformBuffer(UniformBuffer->GetRHI());
		}
	});

	return GlobalUniformBuffers;
}

FRHIRenderPassInfo FRDGParameterStruct::GetRenderPassInfo() const
{
	const FRenderTargetBindingSlots& RenderTargets = GetRenderTargets();

	FRHIRenderPassInfo RenderPassInfo;
	uint32 SampleCount = 0;
	uint32 RenderTargetIndex = 0;

	RenderTargets.Enumerate([&](FRenderTargetBinding RenderTarget)
	{
		FRDGTextureRef Texture = RenderTarget.GetTexture();
		FRDGTextureRef ResolveTexture = RenderTarget.GetResolveTexture();
		ERenderTargetStoreAction StoreAction = EnumHasAnyFlags(Texture->Desc.Flags, TexCreate_Memoryless) ? ERenderTargetStoreAction::ENoAction : ERenderTargetStoreAction::EStore;

		if (ResolveTexture)
		{
			// Silently skip the resolve if the resolve texture is the same as the render target texture.
			if (ResolveTexture != Texture)
			{
				StoreAction = ERenderTargetStoreAction::EMultisampleResolve;
			}
			else
			{
				ResolveTexture = nullptr;
			}
		}

		auto& ColorRenderTarget = RenderPassInfo.ColorRenderTargets[RenderTargetIndex];
		ColorRenderTarget.RenderTarget = Texture->GetRHI();
		ColorRenderTarget.ResolveTarget = ResolveTexture ? ResolveTexture->GetRHI() : nullptr;
		ColorRenderTarget.ArraySlice = RenderTarget.GetArraySlice();
		ColorRenderTarget.MipIndex = RenderTarget.GetMipIndex();
		ColorRenderTarget.Action = MakeRenderTargetActions(RenderTarget.GetLoadAction(), StoreAction);

		SampleCount |= ColorRenderTarget.RenderTarget->GetNumSamples();
		++RenderTargetIndex;
	});

	const FDepthStencilBinding& DepthStencil = RenderTargets.DepthStencil;

	if (FRDGTextureRef Texture = DepthStencil.GetTexture())
	{
		const FExclusiveDepthStencil ExclusiveDepthStencil = DepthStencil.GetDepthStencilAccess();
		const ERenderTargetStoreAction StoreAction = EnumHasAnyFlags(Texture->Desc.Flags, TexCreate_Memoryless) ? ERenderTargetStoreAction::ENoAction : ERenderTargetStoreAction::EStore;
		const ERenderTargetStoreAction DepthStoreAction = ExclusiveDepthStencil.IsUsingDepth() ? StoreAction : ERenderTargetStoreAction::ENoAction;
		const ERenderTargetStoreAction StencilStoreAction = ExclusiveDepthStencil.IsUsingStencil() ? StoreAction : ERenderTargetStoreAction::ENoAction;

		auto& DepthStencilTarget = RenderPassInfo.DepthStencilRenderTarget;
		DepthStencilTarget.DepthStencilTarget = Texture->GetRHI();
		DepthStencilTarget.Action = MakeDepthStencilTargetActions(
			MakeRenderTargetActions(DepthStencil.GetDepthLoadAction(), DepthStoreAction),
			MakeRenderTargetActions(DepthStencil.GetStencilLoadAction(), StencilStoreAction));
		DepthStencilTarget.ExclusiveDepthStencil = ExclusiveDepthStencil;

		SampleCount |= DepthStencilTarget.DepthStencilTarget->GetNumSamples();
	}

	RenderPassInfo.bIsMSAA = SampleCount > 1;
	RenderPassInfo.ResolveParameters = RenderTargets.ResolveRect;
	RenderPassInfo.ResolveParameters.SourceAccessFinal = ERHIAccess::RTV;
	RenderPassInfo.ResolveParameters.DestAccessFinal = ERHIAccess::ResolveDst;
	RenderPassInfo.NumOcclusionQueries = RenderTargets.NumOcclusionQueries;
	RenderPassInfo.bOcclusionQueries = RenderTargets.NumOcclusionQueries > 0;
	RenderPassInfo.SubpassHint = RenderTargets.SubpassHint;
	RenderPassInfo.MultiViewCount = RenderTargets.MultiViewCount;
	RenderPassInfo.FoveationTexture = RenderTargets.FoveationTexture ? RenderTargets.FoveationTexture->GetRHI() : nullptr;

	return RenderPassInfo;
}

uint32 GetTypeHash(FRDGBarrierBatchBeginId Id)
{
	static_assert(sizeof(Id.Passes) == 4, "Hash expects the Passes array to be 4 bytes (2 uint16's).");
	uint32 Hash = *(const uint32*)Id.Passes.GetData();
	return (Hash << GetRHIPipelineCount()) | uint32(Id.PipelinesAfter);
}

FRDGTransitionQueue::FRDGTransitionQueue(uint32 ReservedCount)
{
	Queue.Reserve(ReservedCount);
}

void FRDGTransitionQueue::Insert(const FRHITransition* Transition, ERHICreateTransitionFlags TransitionFlags)
{
	if (!EnumHasAnyFlags(TransitionFlags, ERHICreateTransitionFlags::NoFence))
	{
		QueueWithFences.Add(Transition);
	}
	else
	{
		Queue.Add(Transition);
	}
}

void FRDGTransitionQueue::Begin(FRHIComputeCommandList& RHICmdList)
{
	if (Queue.Num() || QueueWithFences.Num())
	{
		// Fence signals happen last.
		Queue.Append(QueueWithFences);
		RHICmdList.BeginTransitions(Queue);
		Queue.Empty();
		QueueWithFences.Empty();
	}
}

void FRDGTransitionQueue::End(FRHIComputeCommandList& RHICmdList)
{
	if (Queue.Num() || QueueWithFences.Num())
	{
		// Fence waits happen first.
		Queue.Insert(QueueWithFences, 0);
		RHICmdList.EndTransitions(Queue);
		Queue.Empty();
		QueueWithFences.Empty();
	}
}

FRDGBarrierBatchBegin::FRDGBarrierBatchBegin(ERHIPipeline InPipelinesToBegin, ERHIPipeline InPipelinesToEnd, const TCHAR* InDebugName, const FRDGPass* InDebugPass)
	: FRDGBarrierBatchBegin(InPipelinesToBegin, InPipelinesToEnd, InDebugName, GetPassesByPipeline(InDebugPass))
{}

FRDGBarrierBatchBegin::FRDGBarrierBatchBegin(ERHIPipeline InPipelinesToBegin, ERHIPipeline InPipelinesToEnd, const TCHAR* InDebugName, FRDGPassHandlesByPipeline InDebugPasses)
	: PipelinesToBegin(InPipelinesToBegin)
	, PipelinesToEnd(InPipelinesToEnd)
#if RDG_ENABLE_DEBUG
	, DebugPasses(InDebugPasses)
	, DebugName(InDebugName)
	, DebugPipelinesToBegin(InPipelinesToBegin)
	, DebugPipelinesToEnd(InPipelinesToEnd)
#endif
{
#if RDG_ENABLE_DEBUG
	for (ERHIPipeline Pipeline : GetRHIPipelines())
	{
		// We should have provided corresponding debug passes to match the pipeline flags.
		check(InDebugPasses[Pipeline].IsValid() == EnumHasAllFlags(InPipelinesToBegin, Pipeline));
	}
#endif
}

void FRDGBarrierBatchBegin::AddTransition(FRDGParentResourceRef Resource, const FRHITransitionInfo& Info)
{
	Transitions.Add(Info);
	bTransitionNeeded = true;

#if STATS
	GRDGStatTransitionCount++;
#endif

#if RDG_ENABLE_DEBUG
	DebugResources.Add(Resource);
#endif
}

void FRDGBarrierBatchBegin::Submit(FRHIComputeCommandList& RHICmdList, ERHIPipeline Pipeline, FRDGTransitionQueue& TransitionsToBegin)
{
	// Submit may be called once for each pipe. The first to submit creates the transition.
	if (!Transition && bTransitionNeeded)
	{
		Transition = RHICreateTransition(PipelinesToBegin, PipelinesToEnd, TransitionFlags, Transitions);
	}

	if (Transition)
	{
		check(EnumHasAnyFlags(PipelinesToBegin, Pipeline));
		EnumRemoveFlags(PipelinesToBegin, Pipeline);
		TransitionsToBegin.Insert(Transition, TransitionFlags);
	}

#if STATS
	GRDGStatTransitionBatchCount++;
#endif
}

void FRDGBarrierBatchBegin::Submit(FRHIComputeCommandList& RHICmdList, ERHIPipeline Pipeline)
{
	FRDGTransitionQueue TransitionsToBegin;
	Submit(RHICmdList, Pipeline, TransitionsToBegin);
	TransitionsToBegin.Begin(RHICmdList);
}

void FRDGBarrierBatchEnd::AddDependency(FRDGBarrierBatchBegin* BeginBatch)
{
	Dependencies.AddUnique(BeginBatch);
}

void FRDGBarrierBatchEnd::Submit(FRHIComputeCommandList& RHICmdList, ERHIPipeline Pipeline)
{
	FRDGTransitionQueue Transitions(Dependencies.Num());

	for (FRDGBarrierBatchBegin* Dependent : Dependencies)
	{
		if (EnumHasAnyFlags(Dependent->PipelinesToEnd, Pipeline))
		{
			check(Dependent->Transition);
			EnumRemoveFlags(Dependent->PipelinesToEnd, Pipeline);
			Transitions.Insert(Dependent->Transition, Dependent->TransitionFlags);
		}
	}

	Transitions.End(RHICmdList);
}

FRDGBarrierBatchBegin& FRDGPass::GetPrologueBarriersToBegin(FRDGAllocator& Allocator)
{
	if (!PrologueBarriersToBegin)
	{
		PrologueBarriersToBegin = Allocator.AllocNoDestruct<FRDGBarrierBatchBegin>(Pipeline, Pipeline, TEXT("Prologue"), this);
	}
	return *PrologueBarriersToBegin;
}

FRDGBarrierBatchBegin& FRDGPass::GetEpilogueBarriersToBeginForGraphics(FRDGAllocator& Allocator)
{
	if (!EpilogueBarriersToBeginForGraphics)
	{
		EpilogueBarriersToBeginForGraphics = Allocator.AllocNoDestruct<FRDGBarrierBatchBegin>(Pipeline, ERHIPipeline::Graphics, GetEpilogueBarriersToBeginDebugName(ERHIPipeline::Graphics), this);
	}
	return *EpilogueBarriersToBeginForGraphics;
}

FRDGBarrierBatchBegin& FRDGPass::GetEpilogueBarriersToBeginForAsyncCompute(FRDGAllocator& Allocator)
{
	if (!EpilogueBarriersToBeginForAsyncCompute)
	{
		EpilogueBarriersToBeginForAsyncCompute = Allocator.AllocNoDestruct<FRDGBarrierBatchBegin>(Pipeline, ERHIPipeline::AsyncCompute, GetEpilogueBarriersToBeginDebugName(ERHIPipeline::AsyncCompute), this);
	}
	return *EpilogueBarriersToBeginForAsyncCompute;
}

FRDGBarrierBatchBegin& FRDGPass::GetEpilogueBarriersToBeginForAll(FRDGAllocator& Allocator)
{
	if (!EpilogueBarriersToBeginForAll)
	{
		EpilogueBarriersToBeginForAll = Allocator.AllocNoDestruct<FRDGBarrierBatchBegin>(Pipeline, ERHIPipeline::All, GetEpilogueBarriersToBeginDebugName(ERHIPipeline::AsyncCompute), this);
	}
	return *EpilogueBarriersToBeginForAll;
}

FRDGBarrierBatchEnd& FRDGPass::GetPrologueBarriersToEnd(FRDGAllocator& Allocator)
{
	if (!PrologueBarriersToEnd)
	{
		PrologueBarriersToEnd = Allocator.AllocNoDestruct<FRDGBarrierBatchEnd>();
	}
	return *PrologueBarriersToEnd;
}

FRDGBarrierBatchEnd& FRDGPass::GetEpilogueBarriersToEnd(FRDGAllocator& Allocator)
{
	if (!EpilogueBarriersToEnd)
	{
		EpilogueBarriersToEnd = Allocator.AllocNoDestruct<FRDGBarrierBatchEnd>();
	}
	return *EpilogueBarriersToEnd;
}

FRDGPass::FRDGPass(
	FRDGEventName&& InName,
	FRDGParameterStruct InParameterStruct,
	ERDGPassFlags InFlags)
	: Name(Forward<FRDGEventName&&>(InName))
	, ParameterStruct(InParameterStruct)
	, Flags(InFlags)
	, Pipeline(EnumHasAnyFlags(Flags, ERDGPassFlags::AsyncCompute) ? ERHIPipeline::AsyncCompute : ERHIPipeline::Graphics)
{}

#if RDG_ENABLE_DEBUG
const TCHAR* FRDGPass::GetName() const
{
	// When in debug runtime mode, use the full path name.
	if (!FullPathIfDebug.IsEmpty())
	{
		return *FullPathIfDebug;
	}
	else
	{
		return Name.GetTCHAR();
	}
}
#endif

void FRDGPass::Execute(FRHIComputeCommandList& RHICmdList)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FRDGPass_Execute);
	RHICmdList.SetGlobalUniformBuffers(ParameterStruct.GetGlobalUniformBuffers());
	ExecuteImpl(RHICmdList);
}