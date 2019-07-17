// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RenderGraphBuilder.h"
#include "RenderCore.h"
#include "RenderTargetPool.h"
#include "RenderGraphResourcePool.h"
#include "RenderGraphBarrierBatcher.h"
#include "VisualizeTexture.h"
#include "ProfilingDebugging/CsvProfiler.h"

namespace
{
const int32 kRDGEmitWarningsOnce = 1;

#if RDG_ENABLE_DEBUG

int32 GRDGImmediateMode = 0;
FAutoConsoleVariableRef CVarImmediateMode(
	TEXT("r.RDG.ImmediateMode"),
	GRDGImmediateMode,
	TEXT("Executes passes as they get created. Useful to have a callstack of the wiring code when crashing in the pass' lambda."),
	ECVF_RenderThreadSafe);

int32 GRDGDebug = 0;
FAutoConsoleVariableRef CVarRDGDebug(
	TEXT("r.RDG.Debug"),
	GRDGDebug,
	TEXT("Allow to output warnings for inefficiencies found during wiring and execution of the passes.\n")
	TEXT(" 0: disabled;\n")
	TEXT(" 1: emit warning once (default);\n")
	TEXT(" 2: emit warning everytime issue is detected."),
	ECVF_RenderThreadSafe);

#else

const int32 GRDGImmediateMode = 0;
const int32 GRDGDebug = 0;

#endif
} //! namespace

bool GetEmitRDGEvents()
{
#if RDG_EVENTS != RDG_EVENTS_NONE
	return GetEmitDrawEvents() || GRDGDebug;
#else
	return false;
#endif
}

bool IsRDGDebugEnabled()
{
	return GRDGDebug != 0;
}

bool IsRDGImmediateModeEnabled()
{
	return GRDGImmediateMode != 0;
}

void InitRenderGraph()
{
#if RDG_ENABLE_DEBUG_WITH_ENGINE
	if (FParse::Param(FCommandLine::Get(), TEXT("rdgimmediate")))
	{
		GRDGImmediateMode = 1;
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("rdgdebug")))
	{
		GRDGDebug = 1;
	}
#endif
}

void EmitRDGWarning(const FString& WarningMessage)
{
	if (!GRDGDebug)
	{
		return;
	}

	static TSet<FString> GAlreadyEmittedWarnings;

	if (GRDGDebug == kRDGEmitWarningsOnce)
	{
		if (!GAlreadyEmittedWarnings.Contains(WarningMessage))
		{
			GAlreadyEmittedWarnings.Add(WarningMessage);
			UE_LOG(LogRendererCore, Warning, TEXT("%s"), *WarningMessage);
		}
	}
	else
	{
		UE_LOG(LogRendererCore, Warning, TEXT("%s"), *WarningMessage);
	}
}

void FRDGBuilder::TickPoolElements()
{
	GRenderGraphResourcePool.TickPoolElements();
}

FRDGBuilder::FRDGBuilder(FRHICommandListImmediate& InRHICmdList)
	: RHICmdList(InRHICmdList)
	, MemStack(FMemStack::Get())
	, EventScopeStack(RHICmdList)
	, StatScopeStack(RHICmdList)
{}

void FRDGBuilder::Execute()
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(FRDGBuilder_Execute);

	IF_RDG_ENABLE_DEBUG(Validation.ValidateExecuteBegin());

	EventScopeStack.BeginExecute();
	StatScopeStack.BeginExecute();

	if (!GRDGImmediateMode)
	{
		WalkGraphDependencies();

		QUICK_SCOPE_CYCLE_COUNTER(STAT_FRDGBuilder_Execute);
		for (const FRDGPass* Pass : Passes)
		{
			ExecutePass(Pass);
		}
	}

	EventScopeStack.EndExecute();
	StatScopeStack.EndExecute();

	ProcessDeferredInternalResourceQueries();

	IF_RDG_ENABLE_DEBUG(Validation.ValidateExecuteEnd());

	DestructPasses();
}

void FRDGBuilder::AddPassInternal(FRDGPass* Pass)
{
	IF_RDG_ENABLE_DEBUG(Validation.ValidateAddPass(Pass));

	Pass->EventScope = EventScopeStack.GetCurrentScope();
	Pass->StatScope = StatScopeStack.GetCurrentScope();
	Passes.Emplace(Pass);

	if (GRDGImmediateMode)
	{
		ExecutePass(Pass);
	}

	VisualizePassOutputs(Pass);
}

void FRDGBuilder::VisualizePassOutputs(const FRDGPass* Pass)
{
#if SUPPORTS_VISUALIZE_TEXTURE
	if (!GVisualizeTexture.bEnabled)
	{
		return;
	}

	FRDGPassParameterStruct ParameterStruct = Pass->GetParameters();

	const uint32 ParameterCount = ParameterStruct.GetParameterCount();

	for (uint32 ParameterIndex = 0; ParameterIndex < ParameterCount; ++ParameterIndex)
	{
		FRDGPassParameter Parameter = ParameterStruct.GetParameter(ParameterIndex);

		switch (Parameter.GetType())
		{
		case UBMT_RDG_TEXTURE_UAV:
		{
			if (FRDGTextureUAVRef UAV = Parameter.GetAsTextureUAV())
			{
				if (FRDGTextureRef Texture = UAV->Desc.Texture)
				{
					if (GVisualizeTexture.ShouldCapture(Texture->Name))
					{
						GVisualizeTexture.CreateContentCapturePass(*this, Texture);
					}
				}
			}
		}
		break;
		case UBMT_RENDER_TARGET_BINDING_SLOTS:
		{
			const FRenderTargetBindingSlots& RenderTargetBindingSlots = Parameter.GetAsRenderTargetBindingSlots();
			const auto& DepthStencil = RenderTargetBindingSlots.DepthStencil;
			const auto& RenderTargets = RenderTargetBindingSlots.Output;

			if (FRDGTextureRef Texture = DepthStencil.GetTexture())
			{
				const bool bHasStoreAction = DepthStencil.GetDepthStoreAction() != ERenderTargetStoreAction::ENoAction || DepthStencil.GetStencilStoreAction() != ERenderTargetStoreAction::ENoAction;

				if (bHasStoreAction && GVisualizeTexture.ShouldCapture(Texture->Name))
				{
					GVisualizeTexture.CreateContentCapturePass(*this, Texture);
				}
			}

			const uint32 RenderTargetCount = RenderTargets.Num();

			for (uint32 RenderTargetIndex = 0; RenderTargetIndex < RenderTargetCount; ++RenderTargetIndex)
			{
				const FRenderTargetBinding& RenderTarget = RenderTargets[RenderTargetIndex];

				if (FRDGTextureRef Texture = RenderTarget.GetTexture())
				{
					const bool bHasStoreAction = RenderTarget.GetStoreAction() != ERenderTargetStoreAction::ENoAction;

					if (bHasStoreAction && GVisualizeTexture.ShouldCapture(Texture->Name))
					{
						GVisualizeTexture.CreateContentCapturePass(*this, Texture);
					}
				}
				else
				{
					break;
				}
			}
		}
		break;
		default:
			break;
		}
	}
#endif
}

void FRDGBuilder::WalkGraphDependencies()
{
	for (const FRDGPass* Pass : Passes)
	{
		FRDGPassParameterStruct ParameterStruct = Pass->GetParameters();

		const uint32 ParameterCount = ParameterStruct.GetParameterCount();

		for (uint32 ParameterIndex = 0; ParameterIndex < ParameterCount; ++ParameterIndex)
		{
			FRDGPassParameter Parameter = ParameterStruct.GetParameter(ParameterIndex);

			switch (Parameter.GetType())
			{
			case UBMT_RDG_TEXTURE:
			case UBMT_RDG_BUFFER:
			{
				if (FRDGTrackedResourceRef Resource = Parameter.GetAsTrackedResource())
				{
					Resource->ReferenceCount++;
				}
			}
			break;
			case UBMT_RDG_TEXTURE_SRV:
			{
				if (FRDGTextureSRVRef SRV = Parameter.GetAsTextureSRV())
				{
					SRV->Desc.Texture->ReferenceCount++;
				}
			}
			break;
			case UBMT_RDG_TEXTURE_UAV:
			{
				if (FRDGTextureUAVRef UAV = Parameter.GetAsTextureUAV())
				{
					UAV->Desc.Texture->ReferenceCount++;
				}
			}
			break;
			case UBMT_RDG_BUFFER_SRV:
			{
				if (FRDGBufferSRVRef SRV = Parameter.GetAsBufferSRV())
				{
					SRV->Desc.Buffer->ReferenceCount++;
				}
			}
			break;
			case UBMT_RDG_BUFFER_UAV:
			{
				if (FRDGBufferUAVRef UAV = Parameter.GetAsBufferUAV())
				{
					UAV->Desc.Buffer->ReferenceCount++;
				}
			}
			break;
			case UBMT_RENDER_TARGET_BINDING_SLOTS:
			{
				const FRenderTargetBindingSlots& RenderTargetBindingSlots = Parameter.GetAsRenderTargetBindingSlots();
				const auto& DepthStencil = RenderTargetBindingSlots.DepthStencil;
				const auto& RenderTargets = RenderTargetBindingSlots.Output;
				const uint32 RenderTargetCount = RenderTargets.Num();

				for (uint32 RenderTargetIndex = 0; RenderTargetIndex < RenderTargetCount; ++RenderTargetIndex)
				{
					const FRenderTargetBinding& RenderTarget = RenderTargets[RenderTargetIndex];

					if (FRDGTextureRef Texture = RenderTarget.GetTexture())
					{
						Texture->ReferenceCount++;
					}
					else
					{
						break;
					}
				}

				if (FRDGTextureRef Texture = DepthStencil.GetTexture())
				{
					Texture->ReferenceCount++;
				}
			}
			break;
			default:
				break;
			}
		}
	} 

	// Add additional dependencies from deferred queries.
	for (const auto& Query : DeferredInternalTextureQueries)
	{
		Query.Texture->ReferenceCount++;
	}
	for (const auto& Query : DeferredInternalBufferQueries)
	{
		Query.Buffer->ReferenceCount++;
	}

	// Release external texture that have ReferenceCount == 0 and yet are already allocated.
	for (auto Pair : AllocatedTextures)
	{
		if (Pair.Key->ReferenceCount == 0)
		{
			Pair.Value = nullptr;
			Pair.Key->PooledRenderTarget = nullptr;
			Pair.Key->ResourceRHI = nullptr;
		}
	}

	// Release external buffers that have ReferenceCount == 0 and yet are already allocated.
	for (auto Pair : AllocatedBuffers)
	{
		if (Pair.Key->ReferenceCount == 0)
		{
			Pair.Value = nullptr;
			Pair.Key->PooledBuffer = nullptr;
			Pair.Key->ResourceRHI = nullptr;
		}
	}
}

void FRDGBuilder::AllocateRHITextureIfNeeded(FRDGTexture* Texture)
{
	check(Texture);

	if (Texture->PooledRenderTarget)
	{
		return;
	}

	check(Texture->ReferenceCount > 0 || GRDGImmediateMode);

	TRefCountPtr<IPooledRenderTarget>& PooledRenderTarget = AllocatedTextures.FindOrAdd(Texture);

	const bool bDoWriteBarrier = false;
	GRenderTargetPool.FindFreeElement(RHICmdList, Texture->Desc, PooledRenderTarget, Texture->Name, bDoWriteBarrier);

	Texture->PooledRenderTarget = PooledRenderTarget;
	Texture->ResourceRHI = PooledRenderTarget->GetRenderTargetItem().ShaderResourceTexture;
	check(Texture->ResourceRHI);
}

void FRDGBuilder::AllocateRHITextureUAVIfNeeded(FRDGTextureUAV* UAV)
{
	check(UAV);

	if (UAV->ResourceRHI)
	{
		return;
	}

	AllocateRHITextureIfNeeded(UAV->Desc.Texture);

	UAV->ResourceRHI = UAV->Desc.Texture->PooledRenderTarget->GetRenderTargetItem().MipUAVs[UAV->Desc.MipLevel];
}

void FRDGBuilder::AllocateRHIBufferIfNeeded(FRDGBuffer* Buffer)
{
	check(Buffer);

	if (Buffer->PooledBuffer)
	{
		return;
	}

	check(Buffer->ReferenceCount > 0 || GRDGImmediateMode);

	TRefCountPtr<FPooledRDGBuffer>& AllocatedBuffer = AllocatedBuffers.FindOrAdd(Buffer);
	GRenderGraphResourcePool.FindFreeBuffer(RHICmdList, Buffer->Desc, AllocatedBuffer, Buffer->Name);
	check(AllocatedBuffer);
	Buffer->PooledBuffer = AllocatedBuffer;
}

void FRDGBuilder::AllocateRHIBufferSRVIfNeeded(FRDGBufferSRV* SRV)
{
	check(SRV);

	if (SRV->ResourceRHI)
	{
		return;
	}

	FRDGBufferRef Buffer = SRV->Desc.Buffer;

	check(Buffer->PooledBuffer);

	if (Buffer->PooledBuffer->SRVs.Contains(SRV->Desc))
	{
		SRV->ResourceRHI = Buffer->PooledBuffer->SRVs[SRV->Desc];
		return;
	}

	FShaderResourceViewRHIRef RHIShaderResourceView;

	if (Buffer->Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::VertexBuffer)
	{
		RHIShaderResourceView = RHICreateShaderResourceView(Buffer->PooledBuffer->VertexBuffer, SRV->Desc.BytesPerElement, SRV->Desc.Format);
	}
	else if (Buffer->Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::StructuredBuffer)
	{
		RHIShaderResourceView = RHICreateShaderResourceView(Buffer->PooledBuffer->StructuredBuffer);
	}
	else
	{
		check(0);
	}

	SRV->ResourceRHI = RHIShaderResourceView;
	Buffer->PooledBuffer->SRVs.Add(SRV->Desc, RHIShaderResourceView);
}

void FRDGBuilder::AllocateRHIBufferUAVIfNeeded(FRDGBufferUAV* UAV)
{
	check(UAV);

	if (UAV->ResourceRHI)
	{
		return;
	}
	
	FRDGBufferRef Buffer = UAV->Desc.Buffer;
	AllocateRHIBufferIfNeeded(Buffer);

	if (Buffer->PooledBuffer->UAVs.Contains(UAV->Desc))
	{
		UAV->ResourceRHI = Buffer->PooledBuffer->UAVs[UAV->Desc];
		return;
	}

	// Hack to make sure only one UAVs is around.
	Buffer->PooledBuffer->UAVs.Empty();

	FUnorderedAccessViewRHIRef RHIUnorderedAccessView;

	if (Buffer->Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::VertexBuffer)
	{
		RHIUnorderedAccessView = RHICreateUnorderedAccessView(Buffer->PooledBuffer->VertexBuffer, UAV->Desc.Format);
	}
	else if (Buffer->Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::StructuredBuffer)
	{
		RHIUnorderedAccessView = RHICreateUnorderedAccessView(Buffer->PooledBuffer->StructuredBuffer, UAV->Desc.bSupportsAtomicCounter, UAV->Desc.bSupportsAppendBuffer);
	}
	else
	{
		check(0);
	}

	UAV->ResourceRHI = RHIUnorderedAccessView;
	Buffer->PooledBuffer->UAVs.Add(UAV->Desc, RHIUnorderedAccessView);
}

void FRDGBuilder::ExecutePass(const FRDGPass* Pass)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FRDGBuilder_ExecutePass);

	IF_RDG_ENABLE_DEBUG(Validation.ValidateExecutePassBegin(Pass));

	FRHIRenderPassInfo RPInfo;
	bool bHasRenderTargets = false;

	PrepareResourcesForExecute(Pass, &RPInfo, &bHasRenderTargets);
	
	EventScopeStack.BeginExecutePass(Pass);
	StatScopeStack.BeginExecutePass(Pass);

	if (!Pass->IsCompute())
	{
		check(bHasRenderTargets);
		RHICmdList.BeginRenderPass( RPInfo, Pass->GetName() );
	}
	else
	{
		UnbindRenderTargets(RHICmdList);
	}
	
	Pass->Execute(RHICmdList);

	if (bHasRenderTargets)
	{
		RHICmdList.EndRenderPass();
	}

	EventScopeStack.EndExecutePass();

	IF_RDG_ENABLE_DEBUG(Validation.ValidateExecutePassEnd(Pass));

	// Can't release resources with immediate mode, because don't know if whether they are gonna be used.
	if (!GRDGImmediateMode)
	{
		ReleaseUnreferencedResources(Pass);
	}
}

void FRDGBuilder::PrepareResourcesForExecute(const FRDGPass* Pass, struct FRHIRenderPassInfo* OutRPInfo, bool* bOutHasRenderTargets)
{
	check(Pass);

	OutRPInfo->NumUAVs = 0;
	OutRPInfo->UAVIndex = 0;

	const bool bIsCompute = Pass->IsCompute();

	FRDGBarrierBatcher BarrierBatcher(RHICmdList, Pass);

	// NOTE: When generating mips, we don't perform any transitions on textures. They are done implicitly by the RHI.
	const bool bGeneratingMips = Pass->IsGenerateMips();

	FRDGPassParameterStruct ParameterStruct = Pass->GetParameters();

	const uint32 ParameterCount = ParameterStruct.GetParameterCount();

	for (uint32 ParameterIndex = 0; ParameterIndex < ParameterCount; ++ParameterIndex)
	{
		FRDGPassParameter Parameter = ParameterStruct.GetParameter(ParameterIndex);

		switch (Parameter.GetType())
		{
		case UBMT_RDG_TEXTURE:
		{
			if (FRDGTextureRef Texture = Parameter.GetAsTexture())
			{
				check(Texture->PooledRenderTarget);
				check(Texture->ResourceRHI);

				BarrierBatcher.QueueTransitionTexture(Texture, FRDGResourceState::EAccess::Read);
			}
		}
		break;
		case UBMT_RDG_TEXTURE_SRV:
		{
			if (FRDGTextureSRVRef SRV = Parameter.GetAsTextureSRV())
			{
				FRDGTextureRef Texture = SRV->Desc.Texture;

				// Might be the first time using this render graph SRV, so need to setup the cached rhi resource.
				if (!SRV->ResourceRHI)
				{
					SRV->ResourceRHI = Texture->PooledRenderTarget->GetRenderTargetItem().MipSRVs[SRV->Desc.MipLevel];
				}

				BarrierBatcher.QueueTransitionTexture(Texture, FRDGResourceState::EAccess::Read);
			}
		}
		break;
		case UBMT_RDG_TEXTURE_UAV:
		{
			if (FRDGTextureUAVRef UAV = Parameter.GetAsTextureUAV())
			{
				FRDGTextureRef Texture = UAV->Desc.Texture;
	
				AllocateRHITextureUAVIfNeeded(UAV);

				FRHIUnorderedAccessView* UAVRHI = UAV->GetRHI();

				if (!bIsCompute)
				{
					OutRPInfo->UAVs[OutRPInfo->NumUAVs++] = UAVRHI;	// Bind UAVs in declaration order
				}

				BarrierBatcher.QueueTransitionUAV(UAVRHI, Texture, FRDGResourceState::EAccess::Write);
			}
		}
		break;
		case UBMT_RDG_BUFFER:
		{
			if (FRDGBufferRef Buffer = Parameter.GetAsBuffer())
			{
				// TODO(RDG): super hacky, find the UAV and transition it. Hopefully there is one...
				check(Buffer->PooledBuffer);
				check(Buffer->PooledBuffer->UAVs.Num() == 1);
				FRHIUnorderedAccessView* UAVRHI = Buffer->PooledBuffer->UAVs.CreateIterator().Value();

				BarrierBatcher.QueueTransitionUAV(UAVRHI, Buffer, FRDGResourceState::EAccess::Read);
			}
		}
		break;
		case UBMT_RDG_BUFFER_SRV:
		{
			if (FRDGBufferSRVRef SRV = Parameter.GetAsBufferSRV())
			{
				FRDGBufferRef Buffer = SRV->Desc.Buffer;

				AllocateRHIBufferSRVIfNeeded(SRV);

				// TODO(RDG): super hacky, find the UAV and transition it. Hopefully there is one...
				check(Buffer->PooledBuffer);
				check(Buffer->PooledBuffer->UAVs.Num() == 1);
				FRHIUnorderedAccessView* UAVRHI = Buffer->PooledBuffer->UAVs.CreateIterator().Value();

				BarrierBatcher.QueueTransitionUAV(UAVRHI, Buffer, FRDGResourceState::EAccess::Read);
			}
		}
		break;
		case UBMT_RDG_BUFFER_UAV:
		{
			if (FRDGBufferUAVRef UAV = Parameter.GetAsBufferUAV())
			{
				FRDGBufferRef Buffer = UAV->Desc.Buffer;

				AllocateRHIBufferUAVIfNeeded(UAV);

				FRHIUnorderedAccessView* UAVRHI = UAV->GetRHI();

				if (!bIsCompute)
				{
					OutRPInfo->UAVs[OutRPInfo->NumUAVs++] = UAVRHI;	// Bind UAVs in declaration order
				}

				BarrierBatcher.QueueTransitionUAV(UAVRHI, Buffer, FRDGResourceState::EAccess::Write);
			}
		}
		break;
		case UBMT_RENDER_TARGET_BINDING_SLOTS:
		{
			const FRenderTargetBindingSlots& RenderTargetBindingSlots = Parameter.GetAsRenderTargetBindingSlots();
			const auto& RenderTargets = RenderTargetBindingSlots.Output;
			const auto& DepthStencil = RenderTargetBindingSlots.DepthStencil;
			const uint32 RenderTargetCount = RenderTargets.Num();

			uint32 ValidRenderTargetCount = 0;
			uint32 ValidDepthStencilCount = 0;
			uint32 SampleCount = 0;

			for (uint32 RenderTargetIndex = 0; RenderTargetIndex < RenderTargetCount; RenderTargetIndex++)
			{
				const FRenderTargetBinding& RenderTarget = RenderTargets[RenderTargetIndex];

				if (FRDGTextureRef Texture = RenderTarget.GetTexture())
				{
					AllocateRHITextureIfNeeded(Texture);

					auto& OutRenderTarget = OutRPInfo->ColorRenderTargets[RenderTargetIndex];

					// TODO(RDG): Clean up this legacy hack of the FPooledRenderTarget that can have TargetableTexture != ShaderResourceTexture
					// for MSAA texture. Instead the two texture should be independent FRDGTexture explicitly handled by the user code.
					FRHITexture* TargetableTexture = Texture->PooledRenderTarget->GetRenderTargetItem().TargetableTexture;
					FRHITexture* ShaderResourceTexture = Texture->PooledRenderTarget->GetRenderTargetItem().ShaderResourceTexture;

					// TODO(RDG): Looks like the store action on FRenderTargetBinding is not necessary, because: if want to bind a RT,
					// that is most certainly to modify it as oposed to depth-stencil that might be for read only purposes. And if modify
					// this resource, that certainly for being used by another pass. Otherwise this pass should be culled.
					//
					// TODO(RDG): The load store action could actually be optimised by render graph for tile hardware when there is multiple
					// consecutive rasterizer passes that have RDG resource as render target, a bit like resource transitions.
					ERenderTargetStoreAction StoreAction = RenderTarget.GetStoreAction();

					// Automatically switch the store action to MSAA resolve when there is MSAA texture.
					if (TargetableTexture != ShaderResourceTexture && Texture->Desc.NumSamples > 1 && StoreAction == ERenderTargetStoreAction::EStore)
					{
						StoreAction = ERenderTargetStoreAction::EMultisampleResolve;
					}

					// TODO(RDG): should force TargetableTexture == ShaderResourceTexture with MSAA, and instead have an explicit MSAA resolve pass.
					OutRenderTarget.RenderTarget = TargetableTexture;
					OutRenderTarget.ResolveTarget = ShaderResourceTexture != TargetableTexture ? ShaderResourceTexture : nullptr;
					OutRenderTarget.ArraySlice = -1;
					OutRenderTarget.MipIndex = RenderTarget.GetMipIndex();
					OutRenderTarget.Action = MakeRenderTargetActions(RenderTarget.GetLoadAction(), StoreAction);

					BarrierBatcher.QueueTransitionTexture(Texture, FRDGResourceState::EAccess::Write);

					SampleCount |= OutRenderTarget.RenderTarget->GetNumSamples();
					ValidRenderTargetCount++;
				}
				else
				{
					break;
				}
			}

			OutRPInfo->UAVIndex = ValidRenderTargetCount;

			if (FRDGTextureRef Texture = DepthStencil.GetTexture())
			{
				AllocateRHITextureIfNeeded(Texture);

				auto& OutDepthStencil = OutRPInfo->DepthStencilRenderTarget;

				// TODO(RDG): Addresses the TODO of the color scene render target.
				ensureMsgf(Texture->Desc.NumSamples == 1, TEXT("MSAA dept-stencil render target not yet supported."));
				OutDepthStencil.DepthStencilTarget = Texture->PooledRenderTarget->GetRenderTargetItem().TargetableTexture;
				OutDepthStencil.ResolveTarget = nullptr;
				OutDepthStencil.Action = MakeDepthStencilTargetActions(
					MakeRenderTargetActions(DepthStencil.GetDepthLoadAction(), DepthStencil.GetDepthStoreAction()),
					MakeRenderTargetActions(DepthStencil.GetStencilLoadAction(), DepthStencil.GetStencilStoreAction()));
				OutDepthStencil.ExclusiveDepthStencil = DepthStencil.GetDepthStencilAccess();

				BarrierBatcher.QueueTransitionTexture(Texture,
					DepthStencil.GetDepthStencilAccess().IsAnyWrite() ?
					FRDGResourceState::EAccess::Write :
					FRDGResourceState::EAccess::Read);

				SampleCount |= OutDepthStencil.DepthStencilTarget->GetNumSamples();
				ValidDepthStencilCount++;
			}

			OutRPInfo->bIsMSAA = SampleCount > 1;

			*bOutHasRenderTargets = ValidRenderTargetCount + ValidDepthStencilCount > 0;
		}
		break;
		default:
			break;
		}
	}

	OutRPInfo->bGeneratingMips = bGeneratingMips;
}

void FRDGBuilder::ReleaseRHITextureIfUnreferenced(FRDGTexture* Texture)
{
	check(Texture->ReferenceCount > 0);
	Texture->ReferenceCount--;

	if (Texture->ReferenceCount == 0)
	{
		Texture->PooledRenderTarget = nullptr;
		Texture->ResourceRHI = nullptr;
		AllocatedTextures.FindChecked(Texture) = nullptr;
	}
}

void FRDGBuilder::ReleaseRHIBufferIfUnreferenced(FRDGBuffer* Buffer)
{
	check(Buffer->ReferenceCount > 0);
	Buffer->ReferenceCount--;

	if (Buffer->ReferenceCount == 0)
	{
		Buffer->PooledBuffer = nullptr;
		Buffer->ResourceRHI = nullptr;
		AllocatedBuffers.FindChecked(Buffer) = nullptr;
	}
}

void FRDGBuilder::ReleaseUnreferencedResources(const FRDGPass* Pass)
{
	FRDGPassParameterStruct ParameterStruct = Pass->GetParameters();

	const uint32 ParameterCount = ParameterStruct.GetParameterCount();

	for (uint32 ParameterIndex = 0; ParameterIndex < ParameterCount; ++ParameterIndex)
	{
		FRDGPassParameter Parameter = ParameterStruct.GetParameter(ParameterIndex);

		switch (Parameter.GetType())
		{
		case UBMT_RDG_TEXTURE:
		{
			if (FRDGTextureRef Texture = Parameter.GetAsTexture())
			{
				ReleaseRHITextureIfUnreferenced(Texture);
			}
		}
		break;
		case UBMT_RDG_TEXTURE_SRV:
		{
			if (FRDGTextureSRVRef SRV = Parameter.GetAsTextureSRV())
			{
				ReleaseRHITextureIfUnreferenced(SRV->Desc.Texture);
			}
		}
		break;
		case UBMT_RDG_TEXTURE_UAV:
		{
			if (FRDGTextureUAVRef UAV = Parameter.GetAsTextureUAV())
			{
				ReleaseRHITextureIfUnreferenced(UAV->Desc.Texture);
			}
		}
		break;
		case UBMT_RDG_BUFFER:
		{
			if (FRDGBufferRef Buffer = Parameter.GetAsBuffer())
			{
				ReleaseRHIBufferIfUnreferenced(Buffer);
			}
		}
		break;
		case UBMT_RDG_BUFFER_SRV:
		{
			if (FRDGBufferSRVRef SRV = Parameter.GetAsBufferSRV())
			{
				ReleaseRHIBufferIfUnreferenced(SRV->Desc.Buffer);
			}
		}
		break;
		case UBMT_RDG_BUFFER_UAV:
		{
			if (FRDGBufferUAVRef UAV = Parameter.GetAsBufferUAV())
			{
				ReleaseRHIBufferIfUnreferenced(UAV->Desc.Buffer);
			}
		}
		break;
		case UBMT_RENDER_TARGET_BINDING_SLOTS:
		{
			const FRenderTargetBindingSlots& RenderTargetBindingSlots = Parameter.GetAsRenderTargetBindingSlots();
			const auto& RenderTargets = RenderTargetBindingSlots.Output;
			const auto& DepthStencil = RenderTargetBindingSlots.DepthStencil;
			const uint32 RenderTargetCount = RenderTargets.Num();

			for (uint32 RenderTargetIndex = 0; RenderTargetIndex < RenderTargetCount; RenderTargetIndex++)
			{
				const FRenderTargetBinding& RenderTarget = RenderTargets[RenderTargetIndex];

				if (FRDGTextureRef Texture = RenderTarget.GetTexture())
				{
					ReleaseRHITextureIfUnreferenced(Texture);
				}
				else
				{
					break;
				}
			}

			if (FRDGTextureRef Texture = DepthStencil.GetTexture())
			{
				ReleaseRHITextureIfUnreferenced(Texture);
			}
		}
		break;
		default:
			break;
		}
	}
}

void FRDGBuilder::ProcessDeferredInternalResourceQueries()
{
	FRDGBarrierBatcher BarrierBatcher(RHICmdList, nullptr);

	for (const auto& Query : DeferredInternalTextureQueries)
	{
		check(Query.Texture->PooledRenderTarget);

		if (Query.bTransitionToRead)
		{
			BarrierBatcher.QueueTransitionTexture(Query.Texture, FRDGResourceState::EAccess::Read);
		}

		*Query.OutTexturePtr = AllocatedTextures.FindChecked(Query.Texture);

		if (!GRDGImmediateMode)
		{
			ReleaseRHITextureIfUnreferenced(Query.Texture);
		}
	}

	for (const auto& Query : DeferredInternalBufferQueries)
	{
		*Query.OutBufferPtr = AllocatedBuffers.FindChecked(Query.Buffer);

		// No need to manually release in immediate mode, since it is done directly when emptying AllocatedTextures in DestructPasses().
		if (!GRDGImmediateMode)
		{
			ReleaseRHIBufferIfUnreferenced(Query.Buffer);
		}
	}
}

void FRDGBuilder::DestructPasses()
{
	for (int32 PassIndex = Passes.Num() - 1; PassIndex >= 0; --PassIndex)
	{
		Passes[PassIndex]->~FRDGPass();
	}
	Passes.Empty();

	DeferredInternalTextureQueries.Empty();
	DeferredInternalBufferQueries.Empty();
	ExternalTextures.Empty();
	ExternalBuffers.Empty();
	AllocatedTextures.Empty();
	AllocatedBuffers.Empty();
}

void FRDGBuilder::BeginEventScope(FRDGEventName&& ScopeName)
{
	EventScopeStack.BeginScope(Forward<FRDGEventName&&>(ScopeName));
}

void FRDGBuilder::EndEventScope()
{
	EventScopeStack.EndScope();
}

void FRDGBuilder::BeginStatScope(const FName& Name, const FName& StatName)
{
	StatScopeStack.BeginScope(Name, StatName);
}

void FRDGBuilder::EndStatScope()
{
	StatScopeStack.EndScope();
}