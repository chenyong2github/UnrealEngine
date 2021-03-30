// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderGraphPass.h"
#include "RenderGraphPrivate.h"
#include "RenderGraphUtils.h"

FUniformBufferStaticBindings FRDGParameterStruct::GetGlobalUniformBuffers() const
{
	FUniformBufferStaticBindings GlobalUniformBuffers;

	for (uint32 Index = 0, Count = Layout->UniformBuffers.Num(); Index < Count; ++Index)
	{
		const uint32 MemberOffset = Layout->UniformBuffers[Index].MemberOffset;
		FRHIUniformBuffer* UniformBufferPtr = *reinterpret_cast<FUniformBufferRHIRef*>(const_cast<uint8*>(Contents + MemberOffset));

		if (UniformBufferPtr && UniformBufferPtr->IsGlobal())
		{
			GlobalUniformBuffers.AddUniformBuffer(UniformBufferPtr);
		}
	}

	EnumerateUniformBuffers([&](FRDGUniformBufferRef UniformBuffer)
	{
		if (UniformBuffer->IsGlobal())
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
	RenderPassInfo.ShadingRateTexture = RenderTargets.ShadingRateTexture ? RenderTargets.ShadingRateTexture->GetRHI() : nullptr;
	// @todo: should define this as a state that gets passed through? Max seems appropriate for now.
	RenderPassInfo.ShadingRateTextureCombiner = RenderPassInfo.ShadingRateTexture.IsValid() ? VRSRB_Max : VRSRB_Passthrough;

	return RenderPassInfo;
}

FRDGBarrierBatch::FRDGBarrierBatch(const FRDGPass* InPass, const TCHAR* InName)
	: Pipeline(InPass->GetPipeline())
#if RDG_ENABLE_DEBUG
	, Pass(InPass)
	, Name(InName)
#endif
{}

void FRDGBarrierBatch::SetSubmitted()
{
	check(!bSubmitted);
	bSubmitted = true;
}

FString FRDGBarrierBatch::GetName() const
{
#if RDG_ENABLE_DEBUG
	return FString::Printf(TEXT("[%s(%s)]: %s"), Pass->GetName(), *GetRHIPipelineName(Pass->GetPipeline()), Name);
#else
	return {};
#endif
}

FRDGBarrierBatchBegin::FRDGBarrierBatchBegin(const FRDGPass* InPass, const TCHAR* InName, TOptional<ERHIPipeline> InOverridePipelineToEnd)
	: FRDGBarrierBatch(InPass, InName)
	, OverridePipelineToEnd(InOverridePipelineToEnd)
{}

FRDGBarrierBatchBegin::~FRDGBarrierBatchBegin()
{
#if RDG_ENABLE_DEBUG
	checkf(!Resources.Num() && !Transitions.Num(), TEXT("Begin barrier batch has unsubmitted transitions."));
	checkf(!Transition, TEXT("Begin barrier batch %s is currently active and was never ended."), *GetName());
#endif
}

void FRDGBarrierBatchBegin::AddTransition(FRDGParentResourceRef Resource, const FRHITransitionInfo& Info)
{
	check(Resource && Info.Resource);

	checkf(!IsSubmitted(), TEXT("Attempting to add transition for resource '%s' into begin batch '%s', when the begin batch has already been submitted."), Resource->Name, *GetName());
	Transitions.Add(Info);

#if STATS
	GRDGStatTransitionCount++;
#endif

#if RDG_ENABLE_DEBUG
	Resources.Add(Resource);
#endif
}

void FRDGBarrierBatchBegin::Submit(FRHIComputeCommandList& RHICmdList)
{
	SetSubmitted();

	check(!Transition);
	if (Transitions.Num() || bUseCrossPipelineFence)
	{
		const ERHIPipeline PassPipeline = GetPipeline();

		ERHICreateTransitionFlags Flags = ERHICreateTransitionFlags::NoFence;

		if (bUseCrossPipelineFence)
		{
			Flags = ERHICreateTransitionFlags::None;
		}

		const ERHIPipeline DstPipeline = OverridePipelineToEnd ? OverridePipelineToEnd.GetValue() : PassPipeline;
		Transition = RHICreateTransition(PassPipeline, DstPipeline, Flags, Transitions);

		RHICmdList.BeginTransitions(MakeArrayView(&Transition, 1));

		Transitions.Empty();
#if RDG_ENABLE_DEBUG
		Resources.Empty();
#endif

#if STATS
		GRDGStatTransitionBatchCount++;
#endif
	}
}

FRDGBarrierBatchEnd::~FRDGBarrierBatchEnd()
{
	checkf(!Dependencies.Num(), TEXT("End barrier batch has unsubmitted dependencies."));
}

void FRDGBarrierBatchEnd::ReserveMemory(uint32 ExpectedDependencyCount)
{
	Dependencies.Reserve(ExpectedDependencyCount);
}

void FRDGBarrierBatchEnd::AddDependency(FRDGBarrierBatchBegin* BeginBatch)
{
	check(BeginBatch);
	checkf(!IsSubmitted(), TEXT("Attempting to add a dependency on begin batch '%s' into end batch '%s', when the end batch has already been submitted."), *BeginBatch->GetName(), *GetName());
	Dependencies.AddUnique(BeginBatch);
}

void FRDGBarrierBatchEnd::Submit(FRHIComputeCommandList& RHICmdList)
{
	SetSubmitted();

	TArray<const FRHITransition*, SceneRenderingAllocator> Transitions;
	Transitions.Reserve(Dependencies.Num());

	// Process dependencies with cross-pipeline fences first.
	for (FRDGBarrierBatchBegin* Dependent : Dependencies)
	{
		check(Dependent->IsSubmitted());

		if (Dependent->Transition && Dependent->bUseCrossPipelineFence)
		{
			Transitions.Add(Dependent->Transition);
			Dependent->Transition = nullptr;
		}
	}

	for (FRDGBarrierBatchBegin* Dependent : Dependencies)
	{
		if (Dependent->Transition)
		{
			Transitions.Add(Dependent->Transition);
			Dependent->Transition = nullptr;
		}
	}

	Dependencies.Empty();

	if (Transitions.Num())
	{
		RHICmdList.EndTransitions(Transitions);
	}
}

FRDGBarrierBatchBegin& FRDGPass::GetPrologueBarriersToBegin(FRDGAllocator& Allocator)
{
	if (!PrologueBarriersToBegin)
	{
		PrologueBarriersToBegin = Allocator.AllocNoDestruct<FRDGBarrierBatchBegin>(this, TEXT("Prologue"));
	}
	return *PrologueBarriersToBegin;
}

FRDGBarrierBatchEnd& FRDGPass::GetPrologueBarriersToEnd(FRDGAllocator& Allocator)
{
	if (!PrologueBarriersToEnd)
	{
		PrologueBarriersToEnd = Allocator.AllocNoDestruct<FRDGBarrierBatchEnd>(this, TEXT("Prologue"));
	}
	return *PrologueBarriersToEnd;
}

FRDGBarrierBatchBegin& FRDGPass::GetEpilogueBarriersToBeginForGraphics(FRDGAllocator& Allocator)
{
	if (!EpilogueBarriersToBeginForGraphics)
	{
		EpilogueBarriersToBeginForGraphics = Allocator.AllocNoDestruct<FRDGBarrierBatchBegin>(this, TEXT("Epilogue (For Graphics)"), ERHIPipeline::Graphics);
	}
	return *EpilogueBarriersToBeginForGraphics;
}

FRDGBarrierBatchBegin& FRDGPass::GetEpilogueBarriersToBeginForAsyncCompute(FRDGAllocator& Allocator)
{
	if (!EpilogueBarriersToBeginForAsyncCompute)
	{
		EpilogueBarriersToBeginForAsyncCompute = Allocator.AllocNoDestruct<FRDGBarrierBatchBegin>(this, TEXT("Epilogue (For AsyncCompute)"), ERHIPipeline::AsyncCompute);
	}
	return *EpilogueBarriersToBeginForAsyncCompute;
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