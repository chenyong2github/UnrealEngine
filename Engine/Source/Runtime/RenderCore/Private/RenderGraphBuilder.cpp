// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RenderGraphBuilder.h"
#include "RenderCore.h"
#include "RenderTargetPool.h"
#include "RenderGraphResourcePool.h"
#include "VisualizeTexture.h"
#include "ProfilingDebugging/CsvProfiler.h"

#if RENDER_GRAPH_DEBUGGING

static int32 GRenderGraphImmediateMode = 0;
static FAutoConsoleVariableRef CVarImmediateMode(
	TEXT("r.RDG.ImmediateMode"),
	GRenderGraphImmediateMode,
	TEXT("Executes passes as they get created. Useful to have a callstack of the wiring code when crashing in the pass' lambda."),
	ECVF_RenderThreadSafe);

static int32 GRenderGraphEmitWarnings = 0;
static FAutoConsoleVariableRef CVarEmitWarnings(
	TEXT("r.RDG.EmitWarnings"),
	GRenderGraphEmitWarnings,
	TEXT("Allow to output warnings for inefficiencies found during wiring and execution of the passes.\n")
	TEXT(" 0: disabled;\n")
	TEXT(" 1: emit warning once (default);\n")
	TEXT(" 2: emit warning everytime issue is detected."),
	ECVF_RenderThreadSafe);

#else

static const int32 GRenderGraphImmediateMode = 0;
static const int32 GRenderGraphEmitWarnings = 0;

#endif


void InitRenderGraph()
{
#if RENDER_GRAPH_DEBUGGING && WITH_ENGINE
	if (FParse::Param(FCommandLine::Get(), TEXT("rdgimmediate")))
	{
		GRenderGraphImmediateMode = 1;
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("rdgdebug")))
	{
		GRenderGraphEmitWarnings = 1;
	}
#endif
}

static void EmitRenderGraphWarning(const FString& WarningMessage)
{
	check(GRenderGraphEmitWarnings);

	static TSet<FString> GAlreadyEmittedWarnings;

	if (GRenderGraphEmitWarnings == 2)
	{
		UE_LOG(LogRendererCore, Warning, TEXT("%s"), *WarningMessage);
	}
	else if (!GAlreadyEmittedWarnings.Contains(WarningMessage))
	{
		GAlreadyEmittedWarnings.Add(WarningMessage);
		UE_LOG(LogRendererCore, Warning, TEXT("%s"), *WarningMessage);
	}
}

#define EmitRenderGraphWarningf(WarningMessageFormat, ...) \
	EmitRenderGraphWarning(FString::Printf(WarningMessageFormat, ##__VA_ARGS__));

static bool IsBoundAsReadable(const FRDGTexture* Texture, FShaderParameterStructRef ParameterStruct)
{
	for (int ResourceIndex = 0, Num = ParameterStruct.Layout->Resources.Num(); ResourceIndex < Num; ResourceIndex++)
	{
		EUniformBufferBaseType Type = ParameterStruct.Layout->Resources[ResourceIndex].MemberType;
		uint16 Offset = ParameterStruct.Layout->Resources[ResourceIndex].MemberOffset;

		switch (Type)
		{
		case UBMT_RDG_TEXTURE:
		{
			const FRDGTexture* InputTexture = *ParameterStruct.GetMemberPtrAtOffset<const FRDGTexture*>(Offset);
			if (Texture == InputTexture)
			{
				return true;
			}
		}
		break;
		case UBMT_RDG_TEXTURE_SRV:
		{
			const FRDGTextureSRV* InputSRV = *ParameterStruct.GetMemberPtrAtOffset<const FRDGTextureSRV*>(Offset);
			if (InputSRV && Texture == InputSRV->Desc.Texture)
			{
				return true;
			}
		}
		break;
		default:
			break;
		}
	}

	return false;
}


#if RENDER_GRAPH_DRAW_EVENTS == 2

FRDGEventName::FRDGEventName(const TCHAR* EventFormat, ...)
{
	if (GetEmitDrawEvents() || GRenderGraphEmitWarnings)
	{
		va_list ptr;
		va_start(ptr, EventFormat);
		TCHAR TempStr[256];
		// Build the string in the temp buffer
		FCString::GetVarArgs(TempStr, ARRAY_COUNT(TempStr), EventFormat, ptr);
		va_end(ptr);

		EventNameStorage = TempStr;
		EventName = *EventNameStorage;
	}
	else
	{
		EventName = TEXT("!!!Unavailable RDG event name: try r.RDG.EmitWarnings=1 or -rdgdebug!!!");
	}
}

#endif


void FRDGBuilder::Execute()
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(FRDGBuilder_Execute);
	#if RENDER_GRAPH_DEBUGGING
	{
		/** The usage need RDG_EVENT_SCOPE() needs to happen in inner scope of the one containing FRDGBuilder because of
		 *  FStackRDGEventScopeRef's destructor modifying this FRDGBuilder instance.
		 * 
		 *
		 *  FRDGBuilder GraphBuilder(RHICmdList);
		 *  {
		 *  	RDG_EVENT_SCOPE(GraphBuilder, "MyEventScope");
		 *  	// ...
		 *  }
		 *  GraphBuilder.Execute();
		 */
		checkf(CurrentScope == nullptr, TEXT("Render graph needs to have all scopes ended to execute."));

		checkf(!bHasExecuted, TEXT("Render graph execution should only happen once to ensure consistency with immediate mode."));

		/** FRDGBuilder::AllocParameters() allocates shader parameter structure for the life time until pass execution.
		 * But they are allocated on a FMemStack for CPU performance reason, and have their destructor called right after
		 * the pass execution. Therefore allocating pass parameter unused by a FRDGBuilder::AddPass() can lead on a memory
		 * leak of RHI resource that have been reference in the parameter structure.
		 */
		checkf(
			AllocatedUnusedPassParameters.Num() == 0,
			TEXT("%i pass parameter structure has been allocated with FRDGBuilder::AllocParameters(), but has not be used by a ")
			TEXT("FRDGBuilder::AddPass() that can cause RHI resource leak."), AllocatedUnusedPassParameters.Num());
	}
	#endif

	if (!GRenderGraphImmediateMode)
	{
		WalkGraphDependencies();

		QUICK_SCOPE_CYCLE_COUNTER(STAT_FRDGBuilder_Execute);
		for (const FRenderGraphPass* Pass : Passes)
		{
			ExecutePass(Pass);
		}
	}

	// Pops remaining scopes
	if (RENDER_GRAPH_DRAW_EVENTS)
	{
		if (GetEmitDrawEvents())
		{
			for (int32 i = 0; i < kMaxScopeCount; i++)
			{
				if (!ScopesStack[i])
					break;
				RHICmdList.PopEvent();
			}
		}
	}

	ProcessDeferredInternalResourceQueries();

	DestructPasses();

	#if RENDER_GRAPH_DEBUGGING
	{
		bHasExecuted = true;
	}
	#endif
}

void FRDGBuilder::DebugPass(const FRenderGraphPass* Pass)
{
#if RENDER_GRAPH_DEBUGGING
	// Verify all the settings of the pass make sense.
	ValidatePass(Pass);

	// Executes the pass immediatly as they get added mode, to have callstack of wiring code when crashing within the pass.
	if (GRenderGraphImmediateMode)
	{
		ExecutePass(Pass);
	}
#endif

#if WITH_ENGINE && SUPPORTS_VISUALIZE_TEXTURE
	// If visualizing a texture, look for any output of the pass. This must be done after the
	// GRenderGraphImmediateMode's ExecutePass() because this will actually create a capturing
	// pass if needed that would have to be executed right away as well.
	if (GVisualizeTexture.bEnabled)
	{
		CaptureAnyInterestingPassOutput(Pass);
	}
#endif
}

void FRDGBuilder::ValidatePass(const FRenderGraphPass* Pass) const
{
#if RENDER_GRAPH_DEBUGGING
	FRenderTargetBindingSlots* RESTRICT RenderTargets = nullptr;
	FShaderParameterStructRef ParameterStruct = Pass->GetParameters();

	bool bIsCompute = Pass->IsCompute();
	bool bCanUseUAVs = bIsCompute;
	bool bRequiresRenderTargetSlots = !bIsCompute;

	for (int ResourceIndex = 0, Num = ParameterStruct.Layout->Resources.Num(); ResourceIndex < Num; ResourceIndex++)
	{
		EUniformBufferBaseType Type = ParameterStruct.Layout->Resources[ResourceIndex].MemberType;
		uint16 Offset = ParameterStruct.Layout->Resources[ResourceIndex].MemberOffset;

		switch (Type)
		{
		case UBMT_RDG_TEXTURE:
		{
			FRDGTexture* RESTRICT Texture = *ParameterStruct.GetMemberPtrAtOffset<FRDGTexture*>(Offset);
			checkf(!Texture || Texture->bHasEverBeenProduced,
				TEXT("Pass %s has a dependency over the texture %s that has never been produced."),
				Pass->GetName(), Texture->Name);
		}
		break;
		case UBMT_RDG_TEXTURE_SRV:
		{
			FRDGTextureSRV* RESTRICT SRV = *ParameterStruct.GetMemberPtrAtOffset<FRDGTextureSRV*>(Offset);
			if (SRV)
			{
				checkf(SRV->Desc.Texture->bHasEverBeenProduced,
					TEXT("Pass %s has a dependency over the texture %s that has never been produced."),
					Pass->GetName(), SRV->Desc.Texture->Name);
			}
		}
		break;
		case UBMT_RDG_TEXTURE_UAV:
		{
			FRDGTextureUAV* RESTRICT UAV = *ParameterStruct.GetMemberPtrAtOffset<FRDGTextureUAV*>(Offset);
			if (UAV)
			{
				if (!UAV->Desc.Texture->bHasEverBeenProduced)
				{
					UAV->Desc.Texture->bHasEverBeenProduced = true;
					UAV->Desc.Texture->DebugFirstProducer = Pass;
				}

				if (!bCanUseUAVs && GRenderGraphEmitWarnings)
				{
					EmitRenderGraphWarningf(
						TEXT("UAV can only been bound to compute shaders, therefore UAV %s is certainly useless for pass %s."),
						UAV->Name, Pass->GetName());
				}
			}
		}
		break;
		case UBMT_RDG_BUFFER:
		{
			FRDGBuffer* RESTRICT Buffer = *ParameterStruct.GetMemberPtrAtOffset<FRDGBuffer*>(Offset);
			checkf(!Buffer || Buffer->bHasEverBeenProduced,
				TEXT("Pass %s has a dependency over the buffer %s that has never been produced."),
				Pass->GetName(), Buffer->Name);
		}
		break;
		case UBMT_RDG_BUFFER_SRV:
		{
			FRDGBufferSRV* RESTRICT SRV = *ParameterStruct.GetMemberPtrAtOffset<FRDGBufferSRV*>(Offset);
			if (SRV)
			{
				checkf(SRV->Desc.Buffer->bHasEverBeenProduced,
					TEXT("Pass %s has a dependency over the buffer %s that has never been produced."),
					Pass->GetName(), SRV->Desc.Buffer->Name);
			}
		}
		break;
		case UBMT_RDG_BUFFER_UAV:
		{
			FRDGBufferUAV* RESTRICT UAV = *ParameterStruct.GetMemberPtrAtOffset<FRDGBufferUAV*>(Offset);
			if (UAV)
			{
				if (!UAV->Desc.Buffer->bHasEverBeenProduced)
				{
					UAV->Desc.Buffer->bHasEverBeenProduced = true;
					UAV->Desc.Buffer->DebugFirstProducer = Pass;
				}

				if (!bCanUseUAVs && GRenderGraphEmitWarnings)
				{
					EmitRenderGraphWarningf(
						TEXT("UAV can only been bound to compute shaders, therefore UAV %s is certainly useless for pass %s."),
						UAV->Name, Pass->GetName());
				}
			}
		}
		break;
		case UBMT_RENDER_TARGET_BINDING_SLOTS:
		{
			if (!RenderTargets)
			{
				RenderTargets = ParameterStruct.GetMemberPtrAtOffset<FRenderTargetBindingSlots>(Offset);
			}
			else if (GRenderGraphEmitWarnings)
			{
				EmitRenderGraphWarningf(
					TEXT("Pass %s have duplicated render target binding slots."),
					Pass->GetName());
			}
		}
		break;
		default:
			break;
		}
	}

	if (RenderTargets)
	{
		checkf(bRequiresRenderTargetSlots, TEXT("Render pass %s does not need render target binging slots"), Pass->GetName());

		const bool bGeneratingMips = (Pass->GetFlags() & ERenderGraphPassFlags::GenerateMips) == ERenderGraphPassFlags::GenerateMips;
		bool bFoundRTBound = false;

		int32 NumRenderTargets = 0;
		for (int32 i = 0; i < RenderTargets->Output.Num(); i++)
		{
			const FRenderTargetBinding& RenderTarget = RenderTargets->Output[i];
			FRDGTexture* Texture = RenderTarget.GetTexture();

			if (!Texture)
			{
				NumRenderTargets = i;
				break;
			}

			if (!Texture->bHasEverBeenProduced)
			{
				checkf(RenderTarget.GetLoadAction() != ERenderTargetLoadAction::ELoad,
					TEXT("Can't load a render target %s that has never been produced."),
					Texture->Name);

				// TODO(RDG): should only be done when there is a store action.
				Texture->bHasEverBeenProduced = true;
				Texture->DebugFirstProducer = Pass;
			}

			bFoundRTBound = bFoundRTBound || IsBoundAsReadable(Texture, ParameterStruct);
		}
		for (int32 i = NumRenderTargets; i < RenderTargets->Output.Num(); i++)
		{
			const FRenderTargetBinding& RenderTarget = RenderTargets->Output[i];
			checkf(RenderTarget.GetTexture() == nullptr, TEXT("Render targets must be packed. No empty spaces in the array."));
		}
		ensureMsgf(!bGeneratingMips || bFoundRTBound, TEXT("GenerateMips enabled but no RT found as source!"));

		FRDGTexture* Texture = RenderTargets->DepthStencil.GetTexture();
		if (Texture && !Texture->bHasEverBeenProduced)
		{
			checkf(RenderTargets->DepthStencil.GetDepthLoadAction() != ERenderTargetLoadAction::ELoad,
				TEXT("Can't load depth from render target %s that has never been produced."),
				Texture->Name);

			checkf(RenderTargets->DepthStencil.GetStencilLoadAction() != ERenderTargetLoadAction::ELoad,
				TEXT("Can't load stencil from render target %s that has never been produced."),
				Texture->Name);

			Texture->bHasEverBeenProduced = true;
			Texture->DebugFirstProducer = Pass;
		}
	}
	else
	{
		checkf(!bRequiresRenderTargetSlots, TEXT("Render pass %s requires render target binging slots"), Pass->GetName());
	}
#endif // RENDER_GRAPH_DEBUGGING
}

void FRDGBuilder::CaptureAnyInterestingPassOutput(const FRenderGraphPass* Pass)
{
#if SUPPORTS_VISUALIZE_TEXTURE
	FShaderParameterStructRef ParameterStruct = Pass->GetParameters();
	for (int ResourceIndex = 0, Num = ParameterStruct.Layout->Resources.Num(); ResourceIndex < Num; ResourceIndex++)
	{
		EUniformBufferBaseType Type = ParameterStruct.Layout->Resources[ResourceIndex].MemberType;
		uint16 Offset = ParameterStruct.Layout->Resources[ResourceIndex].MemberOffset;

		switch (Type)
		{
		case UBMT_RDG_TEXTURE_UAV:
		{
			FRDGTextureUAV* RESTRICT UAV = *ParameterStruct.GetMemberPtrAtOffset<FRDGTextureUAV*>(Offset);
			if (UAV && GVisualizeTexture.ShouldCapture(UAV->Desc.Texture->Name))
			{
				GVisualizeTexture.CreateContentCapturePass(*this, UAV->Desc.Texture);
			}
		}
		break;
		case UBMT_RENDER_TARGET_BINDING_SLOTS:
		{
			FRenderTargetBindingSlots* RESTRICT RenderTargets = ParameterStruct.GetMemberPtrAtOffset<FRenderTargetBindingSlots>(Offset);
			if (RenderTargets->DepthStencil.GetTexture() && 
				(RenderTargets->DepthStencil.GetDepthStoreAction() != ERenderTargetStoreAction::ENoAction || RenderTargets->DepthStencil.GetStencilStoreAction() != ERenderTargetStoreAction::ENoAction) &&
				GVisualizeTexture.ShouldCapture(RenderTargets->DepthStencil.GetTexture()->Name))
			{
				GVisualizeTexture.CreateContentCapturePass(*this, RenderTargets->DepthStencil.GetTexture());
			}
			for (int32 i = 0; i < RenderTargets->Output.Num(); i++)
			{
				const FRenderTargetBinding& RenderTarget = RenderTargets->Output[i];
				if (RenderTarget.GetTexture() &&
					RenderTarget.GetStoreAction() != ERenderTargetStoreAction::ENoAction &&
					GVisualizeTexture.ShouldCapture(RenderTarget.GetTexture()->Name))
				{
					GVisualizeTexture.CreateContentCapturePass(*this, RenderTarget.GetTexture());
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
#endif // SUPPORTS_VISUALIZE_TEXTURE
}

void FRDGBuilder::WalkGraphDependencies()
{
	for (const FRenderGraphPass* Pass : Passes)
	{
		FShaderParameterStructRef ParameterStruct = Pass->GetParameters();

		/** Increments all the FRDGResource::ReferenceCount. */
		for (int ResourceIndex = 0, Num = ParameterStruct.Layout->Resources.Num(); ResourceIndex < Num; ResourceIndex++)
		{
			EUniformBufferBaseType Type = ParameterStruct.Layout->Resources[ResourceIndex].MemberType;
			uint16 Offset = ParameterStruct.Layout->Resources[ResourceIndex].MemberOffset;

			switch (Type)
			{
			case UBMT_RDG_TEXTURE:
			case UBMT_RDG_BUFFER:
			{
				FRDGResource* RESTRICT Resource = *ParameterStruct.GetMemberPtrAtOffset<FRDGResource*>(Offset);
				if (Resource)
				{
					Resource->ReferenceCount++;
				}
			}
			break;
			case UBMT_RDG_TEXTURE_SRV:
			{
				FRDGTextureSRV* RESTRICT SRV = *ParameterStruct.GetMemberPtrAtOffset<FRDGTextureSRV*>(Offset);
				if (SRV)
				{
					SRV->Desc.Texture->ReferenceCount++;
				}
			}
			break;
			case UBMT_RDG_TEXTURE_UAV:
			{
				FRDGTextureUAV* RESTRICT UAV = *ParameterStruct.GetMemberPtrAtOffset<FRDGTextureUAV*>(Offset);
				if (UAV)
				{
					UAV->Desc.Texture->ReferenceCount++;
				}
			}
			break;
			case UBMT_RDG_BUFFER_SRV:
			{
				FRDGBufferSRV* RESTRICT SRV = *ParameterStruct.GetMemberPtrAtOffset<FRDGBufferSRV*>(Offset);
				if (SRV)
				{
					SRV->Desc.Buffer->ReferenceCount++;
				}
			}
			break;
			case UBMT_RDG_BUFFER_UAV:
			{
				FRDGBufferUAV* RESTRICT UAV = *ParameterStruct.GetMemberPtrAtOffset<FRDGBufferUAV*>(Offset);
				if (UAV)
				{
					UAV->Desc.Buffer->ReferenceCount++;
				}
			}
			break;
			case UBMT_RENDER_TARGET_BINDING_SLOTS:
			{
				FRenderTargetBindingSlots* RESTRICT RenderTargets = ParameterStruct.GetMemberPtrAtOffset<FRenderTargetBindingSlots>(Offset);

				for (int32 i = 0; i < RenderTargets->Output.Num(); i++)
				{
					const FRenderTargetBinding& RenderTarget = RenderTargets->Output[i];
					if (RenderTarget.GetTexture())
					{
						RenderTarget.GetTexture()->ReferenceCount++;
					}
					else
					{
						break;
					}
				}

				const FDepthStencilBinding& DepthStencil = RenderTargets->DepthStencil;
				if (DepthStencil.GetTexture())
				{
					DepthStencil.GetTexture()->ReferenceCount++;
				}
			}
			break;
			default:
				break;
			}
		}
	} // for (const FRenderGraphPass* Pass : Passes)

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
			Pair.Key->CachedRHI.Resource = nullptr;
		}
	}

	// Release external buffers that have ReferenceCount == 0 and yet are already allocated.
	for (auto Pair : AllocatedBuffers)
	{
		if (Pair.Key->ReferenceCount == 0)
		{
			Pair.Value = nullptr;
			Pair.Key->PooledBuffer = nullptr;
			Pair.Key->CachedRHI.Resource = nullptr;
		}
	}
}

void FRDGBuilder::AllocateRHITextureIfNeeded(FRDGTexture* Texture, bool bComputePass)
{
	check(Texture);

	if (Texture->PooledRenderTarget)
	{
		return;
	}

	check(Texture->ReferenceCount > 0 || GRenderGraphImmediateMode);

	// TODO(RDG): should avoid bDoWritableBarrier = true
	TRefCountPtr<IPooledRenderTarget>& PooledRenderTarget = AllocatedTextures.FindOrAdd(Texture);
	GRenderTargetPool.FindFreeElement(RHICmdList, Texture->Desc, PooledRenderTarget, Texture->Name, /* bDoWritableBarrier = */ true);

	Texture->PooledRenderTarget = PooledRenderTarget;
	Texture->CachedRHI.Texture = PooledRenderTarget->GetRenderTargetItem().ShaderResourceTexture;
	check(Texture->CachedRHI.Resource);
}

void FRDGBuilder::AllocateRHITextureUAVIfNeeded(FRDGTextureUAV* UAV, bool bComputePass)
{
	check(UAV);

	if (UAV->CachedRHI.UAV)
	{
		return;
	}

	AllocateRHITextureIfNeeded(UAV->Desc.Texture, bComputePass);

	UAV->CachedRHI.UAV = UAV->Desc.Texture->PooledRenderTarget->GetRenderTargetItem().MipUAVs[UAV->Desc.MipLevel];
}

void FRDGBuilder::AllocateRHIBufferSRVIfNeeded(FRDGBufferSRV* SRV, bool bComputePass)
{
	check(SRV);

	if (SRV->CachedRHI.SRV)
	{
		return;
	}
	
	// The underlying buffer have already been allocated by a prior pass through AllocateRHIBufferUAVIfNeeded().
	#if RENDER_GRAPH_DEBUGGING
	{
		check(SRV->Desc.Buffer->bHasEverBeenProduced);
	}	
	#endif
	check(SRV->Desc.Buffer->PooledBuffer);

	if (SRV->Desc.Buffer->PooledBuffer->SRVs.Contains(SRV->Desc))
	{
		SRV->CachedRHI.SRV = SRV->Desc.Buffer->PooledBuffer->SRVs[SRV->Desc];
		return;
	}

	FShaderResourceViewRHIRef RHIShaderResourceView;

	if (SRV->Desc.Buffer->Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::VertexBuffer)
	{
		RHIShaderResourceView = RHICreateShaderResourceView(SRV->Desc.Buffer->PooledBuffer->VertexBuffer, SRV->Desc.BytesPerElement, SRV->Desc.Format);
	}
	else if (SRV->Desc.Buffer->Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::StructuredBuffer)
	{
		RHIShaderResourceView = RHICreateShaderResourceView(SRV->Desc.Buffer->PooledBuffer->StructuredBuffer);
	}
	else
	{
		check(0);
	}

	SRV->CachedRHI.SRV = RHIShaderResourceView;
	SRV->Desc.Buffer->PooledBuffer->SRVs.Add(SRV->Desc, RHIShaderResourceView);
}

void FRDGBuilder::AllocateRHIBufferUAVIfNeeded(FRDGBufferUAV* UAV, bool bComputePass)
{
	check(UAV);

	if (UAV->CachedRHI.UAV)
	{
		return;
	}
	
	FRDGBufferRef Buffer = UAV->Desc.Buffer;

	// Allocate a buffer resource.
	if (!Buffer->PooledBuffer)
	{
		check(Buffer->ReferenceCount > 0 || GRenderGraphImmediateMode);

		TRefCountPtr<FPooledRDGBuffer>& AllocatedBuffer = AllocatedBuffers.FindOrAdd(Buffer);
		GRenderGraphResourcePool.FindFreeBuffer(RHICmdList, Buffer->Desc, AllocatedBuffer, Buffer->Name);

		Buffer->PooledBuffer = AllocatedBuffer;
	}

	if (Buffer->PooledBuffer->UAVs.Contains(UAV->Desc))
	{
		UAV->CachedRHI.UAV = Buffer->PooledBuffer->UAVs[UAV->Desc];
		return;
	}

	// Hack to make sure only one UAVs is arround.
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

	UAV->CachedRHI.UAV = RHIUnorderedAccessView;
	Buffer->PooledBuffer->UAVs.Add(UAV->Desc, RHIUnorderedAccessView);
}

static EResourceTransitionPipeline CalcTransitionPipeline(bool bCurrentCompute, bool bTargetCompute)
{
	// TODO(RDG) convert table to math
	uint32 Bits;
	Bits  = (uint32)bCurrentCompute;
	Bits |= (uint32)bTargetCompute << 1;

	EResourceTransitionPipeline Table[] = {
		EResourceTransitionPipeline::EGfxToGfx,
		EResourceTransitionPipeline::EComputeToGfx,
		EResourceTransitionPipeline::EGfxToCompute,
		EResourceTransitionPipeline::EComputeToCompute
	};
	
	return static_cast< EResourceTransitionPipeline >( Table[ Bits ] );
}

void FRDGBuilder::TransitionTexture(FRDGTexture* Texture, EResourceTransitionAccess TransitionAccess, bool bRequiredCompute) const
{
	const bool bRequiredWritable = TransitionAccess != EResourceTransitionAccess::EReadable;

	if( Texture->bWritable != bRequiredWritable || Texture->bCompute != bRequiredCompute )
	{
		FTextureRHIParamRef RHITexture = Texture->PooledRenderTarget->GetRenderTargetItem().ShaderResourceTexture;

		bool bIsMultiFrameResource = uint32(Texture->Flags) & uint32(ERDGResourceFlags::MultiFrame);
		if (bIsMultiFrameResource && bRequiredWritable && !Texture->bWritable)
		{
			RHICmdList.BeginUpdateMultiFrameResource(RHITexture);
		}

		RHICmdList.TransitionResource( TransitionAccess, RHITexture );
		
		if (bIsMultiFrameResource && !bRequiredWritable && Texture->bWritable)
		{
			RHICmdList.EndUpdateMultiFrameResource(RHITexture);
		}
		
		Texture->bWritable = bRequiredWritable;
		Texture->bCompute = bRequiredCompute;
	}
}

void FRDGBuilder::TransitionUAV(FUnorderedAccessViewRHIParamRef UAV, FRDGResource* UnderlyingResource, ERDGResourceFlags ResourceFlags, EResourceTransitionAccess TransitionAccess, bool bRequiredCompute ) const
{
	const bool bRequiredWritable = TransitionAccess != EResourceTransitionAccess::EReadable;

	if (bRequiredWritable && UnderlyingResource->bWritable)
	{
		// Force a RW barrier between UAV write.
		// TODO(RDG): allow to have no barriere in the API when multiple pass write concurrently to same resource.
		EResourceTransitionPipeline TransitionPipeline = CalcTransitionPipeline(UnderlyingResource->bCompute, bRequiredCompute);
		RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, TransitionPipeline, UAV);
	}
	else if(UnderlyingResource->bWritable != bRequiredWritable || UnderlyingResource->bCompute != bRequiredCompute )
	{
		bool bIsMultiFrameResource = uint32(ResourceFlags) & uint32(ERDGResourceFlags::MultiFrame);
		if (bIsMultiFrameResource && bRequiredWritable && !UnderlyingResource->bWritable)
		{
			RHICmdList.BeginUpdateMultiFrameResource(UAV);
		}

		EResourceTransitionPipeline TransitionPipeline = CalcTransitionPipeline(UnderlyingResource->bCompute, bRequiredCompute );
		RHICmdList.TransitionResource( TransitionAccess, TransitionPipeline, UAV);
		
		if (bIsMultiFrameResource && !bRequiredWritable && UnderlyingResource->bWritable)
		{
			RHICmdList.EndUpdateMultiFrameResource(UAV);
		}
		
		UnderlyingResource->bWritable = bRequiredWritable;
		UnderlyingResource->bCompute = bRequiredCompute;
	}
}

void FRDGBuilder::PushDrawEventStack(const FRenderGraphPass* Pass)
{
	// Push the scope event.
	{
		// Find out how many scope events needs to be poped.
		TStaticArray<const FRDGEventScope*, kMaxScopeCount> TraversedScopes;
		int32 CommonScopeId = -1;
		int32 TraversedScopeCount = 0;
		const FRDGEventScope* PassParentScope = Pass->ParentScope;
		while (PassParentScope)
		{
			TraversedScopes[TraversedScopeCount] = PassParentScope;

			for (int32 i = 0; i < ScopesStack.Num(); i++)
			{
				if (ScopesStack[i] == PassParentScope)
				{
					CommonScopeId = i;
					break;
				}
			}

			if (CommonScopeId != -1)
			{
				break;
			}

			TraversedScopeCount++;
			PassParentScope = PassParentScope->ParentScope;
		}

		// Pop no longer used scopes
		for (int32 i = CommonScopeId + 1; i < kMaxScopeCount; i++)
		{
			if (!ScopesStack[i])
				break;

			RHICmdList.PopEvent();
			ScopesStack[i] = nullptr;
		}

		// Push new scopes
		const FColor ScopeColor(0);
		for (int32 i = TraversedScopeCount - 1; i >= 0; i--)
		{
			RHICmdList.PushEvent(TraversedScopes[i]->Name.GetTCHAR(), ScopeColor);
			CommonScopeId++;
			ScopesStack[CommonScopeId] = TraversedScopes[i];
		}
	}

	// Push the pass's event with some color.
	{
		FColor Color(0, 0, 0);

		if (Pass->IsCompute())
		{
			// Green for compute.
			Color = FColor(128, 255, 128);
		}
		else
		{
			// Ref for rasterizer.
			Color = FColor(255, 128, 128);
		}

		RHICmdList.PushEvent(Pass->GetName(), Color);
	}
}

void FRDGBuilder::ExecutePass( const FRenderGraphPass* Pass )
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FRDGBuilder_ExecutePass);

	FRHIRenderPassInfo RPInfo;
	bool bHasRenderTargets = false;

	AllocateAndTransitionPassResources(Pass, &RPInfo, &bHasRenderTargets);
	
	if (RENDER_GRAPH_DEBUGGING)
	{
		UpdateAccessGuardForPassResources(Pass, /** bAllowAccess */ true);
	}

	if (RENDER_GRAPH_DRAW_EVENTS)
	{
		if (GetEmitDrawEvents())
		{
			PushDrawEventStack(Pass);
		}
	}

	if( !Pass->IsCompute())
	{
		check(bHasRenderTargets);
		RHICmdList.BeginRenderPass( RPInfo, Pass->GetName() );
	}
	else
	{
		UnbindRenderTargets(RHICmdList);
	}
	
	// The name of the pass just for debuging convenience when crashing in the Execute().
	const TCHAR* PassName = Pass->GetName();

	Pass->Execute(RHICmdList);

	if( bHasRenderTargets )
	{
		RHICmdList.EndRenderPass();
	}

	if (RENDER_GRAPH_DRAW_EVENTS)
	{
		if (GetEmitDrawEvents())
		{
			RHICmdList.PopEvent();
		}
	}

	if (RENDER_GRAPH_DEBUGGING)
	{
		UpdateAccessGuardForPassResources(Pass, /** bAllowAccess */ false);
		WarnForUselessPassDependencies(Pass);
	}

	// Can't release resources with immediate mode, because don't know if whether they are gonna be used.
	if (!GRenderGraphImmediateMode)
	{
		ReleaseUnecessaryResources(Pass);
	}
}

void FRDGBuilder::AllocateAndTransitionPassResources(const FRenderGraphPass* Pass, struct FRHIRenderPassInfo* OutRPInfo, bool* bOutHasRenderTargets)
{
	bool bIsCompute = Pass->IsCompute();
	FShaderParameterStructRef ParameterStruct = Pass->GetParameters();

	const bool bGeneratingMips = (Pass->GetFlags() & ERenderGraphPassFlags::GenerateMips) == ERenderGraphPassFlags::GenerateMips;
	for (int ResourceIndex = 0, Num = ParameterStruct.Layout->Resources.Num(); ResourceIndex < Num; ResourceIndex++)
	{
		EUniformBufferBaseType Type = ParameterStruct.Layout->Resources[ResourceIndex].MemberType;
		uint16 Offset = ParameterStruct.Layout->Resources[ResourceIndex].MemberOffset;

		switch (Type)
		{
		case UBMT_RDG_TEXTURE:
		{
			FRDGTexture* RESTRICT Texture = *ParameterStruct.GetMemberPtrAtOffset<FRDGTexture*>(Offset);
			if (Texture)
			{
				// The underlying texture have already been allocated by a prior pass.
				#if RENDER_GRAPH_DEBUGGING
				{
					check(Texture->bHasEverBeenProduced);
				}	
				#endif
				check(Texture->PooledRenderTarget);
				check(Texture->CachedRHI.Resource);
				TransitionTexture(Texture, EResourceTransitionAccess::EReadable, bIsCompute);

				#if RENDER_GRAPH_DEBUGGING
				{
					Texture->DebugPassAccessCount++;
				}
				#endif
			}
		}
		break;
		case UBMT_RDG_TEXTURE_SRV:
		{
			FRDGTextureSRV* RESTRICT SRV = *ParameterStruct.GetMemberPtrAtOffset<FRDGTextureSRV*>(Offset);
			if (SRV)
			{
				// The underlying texture have already been allocated by a prior pass.
				check(SRV->Desc.Texture);
				#if RENDER_GRAPH_DEBUGGING
				{
					check(SRV->Desc.Texture->bHasEverBeenProduced);
				}	
				#endif
				check(SRV->Desc.Texture->PooledRenderTarget);

				// Might be the first time using this render graph SRV, so need to setup the cached rhi resource.
				if (!SRV->CachedRHI.SRV)
				{
					SRV->CachedRHI.SRV = SRV->Desc.Texture->PooledRenderTarget->GetRenderTargetItem().MipSRVs[SRV->Desc.MipLevel];
				}

				TransitionTexture(SRV->Desc.Texture, EResourceTransitionAccess::EReadable, bIsCompute);

				#if RENDER_GRAPH_DEBUGGING
				{
					SRV->Desc.Texture->DebugPassAccessCount++;
				}
				#endif
			}
		}
		break;
		case UBMT_RDG_TEXTURE_UAV:
		{
			FRDGTextureUAV* RESTRICT UAV = *ParameterStruct.GetMemberPtrAtOffset<FRDGTextureUAV*>(Offset);
			if (UAV)
			{
				AllocateRHITextureUAVIfNeeded(UAV, bIsCompute);
				TransitionUAV(UAV->CachedRHI.UAV, UAV->Desc.Texture, UAV->Desc.Texture->Flags, EResourceTransitionAccess::EWritable, bIsCompute);

				#if RENDER_GRAPH_DEBUGGING
				{
					UAV->Desc.Texture->DebugPassAccessCount++;
				}
				#endif
			}
		}
		break;
		case UBMT_RDG_BUFFER:
		{
			FRDGBuffer* RESTRICT Buffer = *ParameterStruct.GetMemberPtrAtOffset<FRDGBuffer*>(Offset);
			if (Buffer)
			{
				// The underlying buffer have already been allocated by a prior pass through AllocateRHIBufferUAVIfNeeded().
				#if RENDER_GRAPH_DEBUGGING
				{
					check(Buffer->bHasEverBeenProduced);
				}
				#endif
				check(Buffer->PooledBuffer);

				// TODO(RDG): supper hacky, find the UAV and transition it. Hopefully there is one...
				check(Buffer->PooledBuffer->UAVs.Num() == 1);
				FUnorderedAccessViewRHIParamRef UAV = Buffer->PooledBuffer->UAVs.CreateIterator().Value();
				TransitionUAV(UAV, Buffer, Buffer->Flags, EResourceTransitionAccess::EReadable, bIsCompute);

				#if RENDER_GRAPH_DEBUGGING
				{
					Buffer->DebugPassAccessCount++;
				}
				#endif
			}
		}
		break;
		case UBMT_RDG_BUFFER_SRV:
		{
			FRDGBufferSRV* RESTRICT SRV = *ParameterStruct.GetMemberPtrAtOffset<FRDGBufferSRV*>(Offset);
			if (SRV)
			{
				// The underlying buffer have already been allocated by a prior pass through AllocateRHIBufferUAVIfNeeded().
				check(SRV->Desc.Buffer);
				#if RENDER_GRAPH_DEBUGGING
				{
					check(SRV->Desc.Buffer->bHasEverBeenProduced);
				}
				#endif
				check(SRV->Desc.Buffer->PooledBuffer);
				
				AllocateRHIBufferSRVIfNeeded(SRV, bIsCompute);

				// TODO(RDG): supper hacky, find the UAV and transition it. Hopefully there is one...
				check(SRV->Desc.Buffer->PooledBuffer->UAVs.Num() == 1);
				FUnorderedAccessViewRHIParamRef UAV = SRV->Desc.Buffer->PooledBuffer->UAVs.CreateIterator().Value();
				TransitionUAV(UAV, SRV->Desc.Buffer, SRV->Desc.Buffer->Flags, EResourceTransitionAccess::EReadable, bIsCompute);

				#if RENDER_GRAPH_DEBUGGING
				{
					SRV->Desc.Buffer->DebugPassAccessCount++;
				}
				#endif
			}
		}
		break;
		case UBMT_RDG_BUFFER_UAV:
		{
			FRDGBufferUAV* RESTRICT UAV = *ParameterStruct.GetMemberPtrAtOffset<FRDGBufferUAV*>(Offset);
			if (UAV)
			{
				AllocateRHIBufferUAVIfNeeded(UAV, bIsCompute);
				TransitionUAV(UAV->CachedRHI.UAV, UAV->Desc.Buffer, UAV->Desc.Buffer->Flags, EResourceTransitionAccess::EWritable, bIsCompute);

				#if RENDER_GRAPH_DEBUGGING
				{
					UAV->Desc.Buffer->DebugPassAccessCount++;
				}
				#endif
			}
		}
		break;
		case UBMT_RENDER_TARGET_BINDING_SLOTS:
		{
			check(!bIsCompute);

			FRenderTargetBindingSlots* RESTRICT RenderTargets = ParameterStruct.GetMemberPtrAtOffset<FRenderTargetBindingSlots>(Offset);

			uint32 NumRenderTargets = 0;
			uint32 NumDepthStencilTargets = 0;
			uint32 NumSamples = 0;

			for (int32 i = 0; i < RenderTargets->Output.Num(); i++)
			{
				const FRenderTargetBinding& RenderTarget = RenderTargets->Output[i];
				if (RenderTarget.GetTexture())
				{
					AllocateRHITextureIfNeeded(RenderTarget.GetTexture(), false);

					// TODO(RDG): should force TargetableTexture == ShaderResourceTexture with MSAA, and instead have an explicit MSAA resolve pass.
					OutRPInfo->ColorRenderTargets[i].RenderTarget = RenderTarget.GetTexture()->PooledRenderTarget->GetRenderTargetItem().TargetableTexture;
					OutRPInfo->ColorRenderTargets[i].ResolveTarget = nullptr;
					OutRPInfo->ColorRenderTargets[i].ArraySlice = -1;
					OutRPInfo->ColorRenderTargets[i].MipIndex = RenderTarget.GetMipIndex();
					OutRPInfo->ColorRenderTargets[i].Action = MakeRenderTargetActions(RenderTarget.GetLoadAction(), RenderTarget.GetStoreAction());

					if (!bGeneratingMips)
					{
						// Implicit assurance the RHI will do the correct transitions
						TransitionTexture(RenderTarget.GetTexture(), EResourceTransitionAccess::EWritable, false);
					}

					NumSamples |= OutRPInfo->ColorRenderTargets[i].RenderTarget->GetNumSamples();
					NumRenderTargets++;
					
					#if RENDER_GRAPH_DEBUGGING
					{
						RenderTarget.GetTexture()->DebugPassAccessCount++;
					}
					#endif
				}
				else
				{
					break;
				}
			}

			const FDepthStencilBinding& DepthStencil = RenderTargets->DepthStencil;
			if (DepthStencil.GetTexture())
			{
				AllocateRHITextureIfNeeded(DepthStencil.GetTexture(), false);

				OutRPInfo->DepthStencilRenderTarget.DepthStencilTarget = DepthStencil.GetTexture()->PooledRenderTarget->GetRenderTargetItem().TargetableTexture;
				OutRPInfo->DepthStencilRenderTarget.ResolveTarget = nullptr;
				OutRPInfo->DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(
					MakeRenderTargetActions(DepthStencil.GetDepthLoadAction(), DepthStencil.GetDepthStoreAction()),
					MakeRenderTargetActions(DepthStencil.GetStencilLoadAction(), DepthStencil.GetStencilStoreAction()));
				OutRPInfo->DepthStencilRenderTarget.ExclusiveDepthStencil = DepthStencil.GetDepthStencilAccess();

				TransitionTexture(DepthStencil.GetTexture(), EResourceTransitionAccess::EWritable, false);

				NumSamples |= OutRPInfo->DepthStencilRenderTarget.DepthStencilTarget->GetNumSamples();
				NumDepthStencilTargets++;
					
				#if RENDER_GRAPH_DEBUGGING
				{
					DepthStencil.GetTexture()->DebugPassAccessCount++;
				}
				#endif
			}

			OutRPInfo->bIsMSAA = NumSamples > 1;

			*bOutHasRenderTargets = NumRenderTargets + NumDepthStencilTargets > 0;
		}
		break;
		default:
			break;
		}
	}

	OutRPInfo->bGeneratingMips = bGeneratingMips;
}

// static
void FRDGBuilder::UpdateAccessGuardForPassResources(const FRenderGraphPass* Pass, bool bAllowAccess)
{
#if RENDER_GRAPH_DEBUGGING
	FShaderParameterStructRef ParameterStruct = Pass->GetParameters();

	for (int ResourceIndex = 0, Num = ParameterStruct.Layout->Resources.Num(); ResourceIndex < Num; ResourceIndex++)
	{
		EUniformBufferBaseType Type = ParameterStruct.Layout->Resources[ResourceIndex].MemberType;
		uint16 Offset = ParameterStruct.Layout->Resources[ResourceIndex].MemberOffset;

		if (IsRDGResourceReferenceShaderParameterType(Type))
		{
			FRDGResource* RESTRICT Resource = *ParameterStruct.GetMemberPtrAtOffset<FRDGResource*>(Offset);
			if (Resource)
				Resource->bAllowAccessToRHIResource = bAllowAccess;
		}
		else if (Type == UBMT_RENDER_TARGET_BINDING_SLOTS)
		{
			FRenderTargetBindingSlots* RESTRICT RenderTargets = ParameterStruct.GetMemberPtrAtOffset<FRenderTargetBindingSlots>(Offset);

			for (int32 i = 0; i < RenderTargets->Output.Num(); i++)
			{
				FRDGTexture* Texture = RenderTargets->Output[i].GetTexture();
				if (Texture)
					Texture->bAllowAccessToRHIResource = bAllowAccess;
				else
					break;
			}
			
			const FDepthStencilBinding& DepthStencil = RenderTargets->DepthStencil;
			if (DepthStencil.GetTexture())
			{
				DepthStencil.GetTexture()->bAllowAccessToRHIResource = bAllowAccess;
			}
		}
	}
#endif
}

// static 
void FRDGBuilder::WarnForUselessPassDependencies(const FRenderGraphPass* Pass)
{
	if (!GRenderGraphEmitWarnings)
	{
		return;
	}

	FShaderParameterStructRef ParameterStruct = Pass->GetParameters();

	int32 TrackedResourceCount = 0;
	int32 UsedResourceCount = 0;

	// First pass to count resources.
	for (int ResourceIndex = 0, Num = ParameterStruct.Layout->Resources.Num(); ResourceIndex < Num; ResourceIndex++)
	{
		EUniformBufferBaseType Type = ParameterStruct.Layout->Resources[ResourceIndex].MemberType;
		uint16 Offset = ParameterStruct.Layout->Resources[ResourceIndex].MemberOffset;

		if (!IsRDGResourceReferenceShaderParameterType(Type))
			continue;

		const FRDGResource* Resource = *ParameterStruct.GetMemberPtrAtOffset<const FRDGResource*>(Offset);

		if (!Resource)
			continue;

		TrackedResourceCount++;
		UsedResourceCount += Resource->bIsActuallyUsedByPass ? 1 : 0;
	}

	if (TrackedResourceCount != UsedResourceCount)
	{
		FString WarningMessage = FString::Printf(
			TEXT("%i of the %i resources of the pass %s where not actually used."),
			TrackedResourceCount - UsedResourceCount, TrackedResourceCount, Pass->GetName());

		for (int ResourceIndex = 0, Num = ParameterStruct.Layout->Resources.Num(); ResourceIndex < Num; ResourceIndex++)
		{
			EUniformBufferBaseType Type = ParameterStruct.Layout->Resources[ResourceIndex].MemberType;
			uint16 Offset = ParameterStruct.Layout->Resources[ResourceIndex].MemberOffset;

			if (!IsRDGResourceReferenceShaderParameterType(Type))
				continue;

			const FRDGResource* Resource = *ParameterStruct.GetMemberPtrAtOffset<const FRDGResource*>(Offset);

			if (!Resource)
				continue;

			if (!Resource->bIsActuallyUsedByPass)
			{
				WarningMessage += FString::Printf(TEXT("\n    %s"), Resource->Name);
			}
		}

		EmitRenderGraphWarning(WarningMessage);
	}

	// Last pass to clean the bIsActuallyUsedByPass flags.
	for (int ResourceIndex = 0, Num = ParameterStruct.Layout->Resources.Num(); ResourceIndex < Num; ResourceIndex++)
	{
		EUniformBufferBaseType Type = ParameterStruct.Layout->Resources[ResourceIndex].MemberType;
		uint16 Offset = ParameterStruct.Layout->Resources[ResourceIndex].MemberOffset;

		if (!IsRDGResourceReferenceShaderParameterType(Type))
			continue;

		FRDGResource* Resource = *ParameterStruct.GetMemberPtrAtOffset<FRDGResource*>(Offset);

		if (!Resource)
			continue;

		Resource->bIsActuallyUsedByPass = false;
	}
}

void FRDGBuilder::ReleaseRHITextureIfPossible(FRDGTexture* Texture)
{
	check(Texture->ReferenceCount > 0);
	Texture->ReferenceCount--;

	if (Texture->ReferenceCount == 0)
	{
		Texture->PooledRenderTarget = nullptr;
		Texture->CachedRHI.Resource = nullptr;
		AllocatedTextures.FindChecked(Texture) = nullptr;
	}
}

void FRDGBuilder::ReleaseRHIBufferIfPossible(FRDGBuffer* Buffer)
{
	check(Buffer->ReferenceCount > 0);
	Buffer->ReferenceCount--;

	if (Buffer->ReferenceCount == 0)
	{
		Buffer->PooledBuffer = nullptr;
		Buffer->CachedRHI.Resource = nullptr;
		AllocatedBuffers.FindChecked(Buffer) = nullptr;
	}
}

void FRDGBuilder::ReleaseUnecessaryResources(const FRenderGraphPass* Pass)
{
	FShaderParameterStructRef ParameterStruct = Pass->GetParameters();

	/** Increments all the FRDGResource::ReferenceCount. */
	// TODO(RDG): Investigate the cost of branch miss-prediction.
	for (int ResourceIndex = 0, Num = ParameterStruct.Layout->Resources.Num(); ResourceIndex < Num; ResourceIndex++)
	{
		EUniformBufferBaseType Type = ParameterStruct.Layout->Resources[ResourceIndex].MemberType;
		uint16 Offset = ParameterStruct.Layout->Resources[ResourceIndex].MemberOffset;

		switch (Type)
		{
		case UBMT_RDG_TEXTURE:
		{
			FRDGTexture* RESTRICT Texture = *ParameterStruct.GetMemberPtrAtOffset<FRDGTexture*>(Offset);
			if (Texture)
			{
				ReleaseRHITextureIfPossible(Texture);
			}
		}
		break;
		case UBMT_RDG_TEXTURE_SRV:
		{
			FRDGTextureSRV* RESTRICT SRV = *ParameterStruct.GetMemberPtrAtOffset<FRDGTextureSRV*>(Offset);
			if (SRV)
			{
				ReleaseRHITextureIfPossible(SRV->Desc.Texture);
			}
		}
		break;
		case UBMT_RDG_TEXTURE_UAV:
		{
			FRDGTextureUAV* RESTRICT UAV = *ParameterStruct.GetMemberPtrAtOffset<FRDGTextureUAV*>(Offset);
			if (UAV)
			{
				ReleaseRHITextureIfPossible(UAV->Desc.Texture);
			}
		}
		break;
		case UBMT_RDG_BUFFER:
		{
			FRDGBuffer* RESTRICT Buffer = *ParameterStruct.GetMemberPtrAtOffset<FRDGBuffer*>(Offset);
			if (Buffer)
			{
				ReleaseRHIBufferIfPossible(Buffer);
			}
		}
		break;
		case UBMT_RDG_BUFFER_SRV:
		{
			FRDGBufferSRV* RESTRICT SRV = *ParameterStruct.GetMemberPtrAtOffset<FRDGBufferSRV*>(Offset);
			if (SRV)
			{
				ReleaseRHIBufferIfPossible(SRV->Desc.Buffer);
			}
		}
		break;
		case UBMT_RDG_BUFFER_UAV:
		{
			FRDGBufferUAV* RESTRICT UAV = *ParameterStruct.GetMemberPtrAtOffset<FRDGBufferUAV*>(Offset);
			if (UAV)
			{
				ReleaseRHIBufferIfPossible(UAV->Desc.Buffer);
			}
		}
		break;
		case UBMT_RENDER_TARGET_BINDING_SLOTS:
		{
			FRenderTargetBindingSlots* RESTRICT RenderTargets = ParameterStruct.GetMemberPtrAtOffset<FRenderTargetBindingSlots>(Offset);

			for (int32 i = 0; i < RenderTargets->Output.Num(); i++)
			{
				const FRenderTargetBinding& RenderTarget = RenderTargets->Output[i];
				if (RenderTarget.GetTexture())
				{
					ReleaseRHITextureIfPossible(RenderTarget.GetTexture());
				}
				else
				{
					break;
				}
			}
			if (RenderTargets->DepthStencil.GetTexture())
			{
				ReleaseRHITextureIfPossible(RenderTargets->DepthStencil.GetTexture());
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
	for (const auto& Query : DeferredInternalTextureQueries)
	{
		check(Query.Texture->PooledRenderTarget);

		if (Query.bTransitionToRead)
		{
			TransitionTexture(Query.Texture, EResourceTransitionAccess::EReadable, /* bRequiredCompute = */ false);
		}

		*Query.OutTexturePtr = AllocatedTextures.FindChecked(Query.Texture);
		
		#if RENDER_GRAPH_DEBUGGING
		{
			// Increment the number of times the texture has been accessed to avoid warning on produced but never used resources that were produced
			// only to be extracted for the graph.
			Query.Texture->DebugPassAccessCount += 1;
		}
		#endif

		// No need to manually release in immediate mode, since it is done directly when emptying AllocatedTextures in DestructPasses().
		if (!GRenderGraphImmediateMode)
		{
			ReleaseRHITextureIfPossible(Query.Texture);
		}
	}

	for (const auto& Query : DeferredInternalBufferQueries)
	{
		*Query.OutBufferPtr = AllocatedBuffers.FindChecked(Query.Buffer);

#if RENDER_GRAPH_DEBUGGING
		{
			// Increment the number of times the buffer has been accessed to avoid warning on produced but never used resources that were produced
			// only to be extracted for the graph.
			Query.Buffer->DebugPassAccessCount += 1;
		}
#endif

		// No need to manually release in immediate mode, since it is done directly when emptying AllocatedBuffer in DestructPasses().
		if (!GRenderGraphImmediateMode)
		{
			ReleaseRHIBufferIfPossible(Query.Buffer);
		}
	}
}

void FRDGBuilder::DestructPasses()
{
	#if RENDER_GRAPH_DRAW_EVENTS == 2
	{
		// Event scopes are allocated on FMemStack, so need to call their destructor because have a FString within them.
		for (FRDGEventScope* EventScope : EventScopes)
		{
			EventScope->~FRDGEventScope();
		}
		EventScopes.Empty();
	}
	#endif

	#if RENDER_GRAPH_DEBUGGING
	{
		// Make sure all resource references have been released to ensure no leaks happen,
		// and emit warning if produced resource has not been used.
		for (const FRDGResource* Resource : Resources)
		{
			check(Resource->ReferenceCount == 0);

			if (GRenderGraphEmitWarnings && Resource->DebugPassAccessCount == 1 && Resource->DebugFirstProducer)
			{
				check(Resource->bHasEverBeenProduced);

				EmitRenderGraphWarningf(
					TEXT("Resources %s has been produced by the pass %s, but never used by another pass."),
					Resource->Name, Resource->DebugFirstProducer->GetName());
			}
		}
		Resources.Empty();
	}
	#endif

	// Passes are allocated on FMemStack, so need to call destructor manually.
	for (FRenderGraphPass* Pass : Passes)
	{
		Pass->~FRenderGraphPass();
	}

	Passes.Empty();
	DeferredInternalTextureQueries.Empty();
	DeferredInternalBufferQueries.Empty();
	AllocatedTextures.Empty();
	AllocatedBuffers.Empty();
}

FRDGBuilder::~FRDGBuilder()
{
	// Owned objects must be destroyed in reverse allocation order to ensure that children are destroyed before parents.
	while(OwnedObjects.Num())
	{
		FOwnedObjectPtr& Obj = OwnedObjects.Last();
		Obj.Deleter(Obj.Ptr);
		OwnedObjects.Pop(false);
	}

	#if RENDER_GRAPH_DEBUGGING
	{
		checkf(bHasExecuted, TEXT("Render graph execution is required to ensure consistency with immediate mode."));
	}
	#endif
}
