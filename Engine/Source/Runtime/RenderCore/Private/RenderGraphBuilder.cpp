// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RenderGraphBuilder.h"
#include "RenderCore.h"
#include "RenderTargetPool.h"
#include "RenderGraphResourcePool.h"
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

int32 GRDGEmitWarnings = 0;
FAutoConsoleVariableRef CVarEmitWarnings(
	TEXT("r.RDG.EmitWarnings"),
	GRDGEmitWarnings,
	TEXT("Allow to output warnings for inefficiencies found during wiring and execution of the passes.\n")
	TEXT(" 0: disabled;\n")
	TEXT(" 1: emit warning once (default);\n")
	TEXT(" 2: emit warning everytime issue is detected."),
	ECVF_RenderThreadSafe);

#else

const int32 GRDGImmediateMode = 0;
const int32 GRDGEmitWarnings = 0;

#endif
} //! namespace

bool GetEmitRDGEvents()
{
#if RDG_EVENTS != RDG_EVENTS_NONE
	return GetEmitDrawEvents() || GRDGEmitWarnings;
#else
	return false;
#endif
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
		GRDGEmitWarnings = 1;
	}
#endif
}

void EmitRDGWarning(const FString& WarningMessage)
{
	if (!GRDGEmitWarnings)
	{
		return;
	}

	static TSet<FString> GAlreadyEmittedWarnings;

	if (GRDGEmitWarnings == kRDGEmitWarningsOnce)
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

#define EmitRDGWarningf(WarningMessageFormat, ...) \
	EmitRDGWarning(FString::Printf(WarningMessageFormat, ##__VA_ARGS__));

FRDGBuilder::FRDGBuilder(FRHICommandListImmediate& InRHICmdList)
	: RHICmdList(InRHICmdList)
	, MemStack(FMemStack::Get())
	, EventScopeStack(RHICmdList)
	, StatScopeStack(RHICmdList)
{}

FRDGBuilder::~FRDGBuilder()
{
	#if RDG_ENABLE_DEBUG
	{
		checkf(bHasExecuted, TEXT("Render graph execution is required to ensure consistency with immediate mode."));
	}
	#endif
}

void FRDGBuilder::Execute()
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(FRDGBuilder_Execute);

	#if RDG_ENABLE_DEBUG
	{
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

	DestructPasses();

	#if RDG_ENABLE_DEBUG
	{
		bHasExecuted = true;
	}
	#endif
}

void FRDGBuilder::AddPassInternal(FRDGPass* Pass)
{
	#if RDG_ENABLE_DEBUG
	{
		checkf(!bHasExecuted, TEXT("Render graph pass %s needs to be added before the builder execution."), Pass->GetName());

		const void* ParameterStructData = Pass->GetParameters().GetContents();

		/** The lifetime of each pass parameter structure must extend until deferred pass execution; therefore, it needs to be
		 *  allocated with FRDGBuilder::AllocParameters(). Also, all references held by the parameter structure are released
		 *  immediately after pass execution, so a pass parameter struct instance must be 1-to-1 with a pass instance (i.e. one
		 *  per AddPass() call).
		 */
		checkf(
			AllocatedUnusedPassParameters.Contains(ParameterStructData),
			TEXT("The pass parameter structure has not been allocated for correct life time FRDGBuilder::AllocParameters() or has already ")
			TEXT("been used by another previous FRDGBuilder::AddPass()."));

		AllocatedUnusedPassParameters.Remove(ParameterStructData);
	}
	#endif

	Pass->EventScope = EventScopeStack.GetCurrentScope();
	Pass->StatScope = StatScopeStack.GetCurrentScope();
	Passes.Emplace(Pass);

	ValidatePass(Pass);

	if (GRDGImmediateMode)
	{
		ExecutePass(Pass);
	}

	VisualizePassOutputs(Pass);
}

void FRDGBuilder::ValidatePass(const FRDGPass* Pass) const
{
#if RDG_ENABLE_DEBUG
	const FRenderTargetBindingSlots* RenderTargetBindingSlots = nullptr;

	const TCHAR* PassName = Pass->GetName();
	const bool bIsCompute = Pass->IsCompute();
	const bool bRenderTargetSlotsRequired = !bIsCompute;

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
				checkf(Texture->HasBeenProduced(),
					TEXT("Pass %s has a dependency over the texture %s that has never been produced."),
					PassName, Texture->Name);
			}
		}
		break;
		case UBMT_RDG_TEXTURE_SRV:
		{
			if (FRDGTextureSRVRef SRV = Parameter.GetAsTextureSRV())
			{
				FRDGTextureRef Texture = SRV->Desc.Texture;

				checkf(Texture->HasBeenProduced(),
					TEXT("Pass %s has a dependency over the texture %s that has never been produced."),
					PassName, Texture->Name);
			}
		}
		break;
		case UBMT_RDG_TEXTURE_UAV:
		{
			if (FRDGTextureUAVRef UAV = Parameter.GetAsTextureUAV())
			{
				FRDGTextureRef Texture = UAV->Desc.Texture;

				Texture->MarkAsProducedBy(Pass);
			}
		}
		break;
		case UBMT_RDG_BUFFER:
		{
			if (FRDGBufferRef Buffer = Parameter.GetAsBuffer())
			{
				checkf(Buffer->HasBeenProduced(),
					TEXT("Pass %s has a dependency over the buffer %s that has never been produced."),
					PassName, Buffer->Name);
			}
		}
		break;
		case UBMT_RDG_BUFFER_SRV:
		{
			if (FRDGBufferSRVRef SRV = Parameter.GetAsBufferSRV())
			{
				FRDGBufferRef Buffer = SRV->Desc.Buffer;

				checkf(Buffer->HasBeenProduced(),
					TEXT("Pass %s has a dependency over the buffer %s that has never been produced."),
					PassName, SRV->Desc.Buffer->Name);
			}
		}
		break;
		case UBMT_RDG_BUFFER_UAV:
		{
			if (FRDGBufferUAVRef UAV = Parameter.GetAsBufferUAV())
			{
				FRDGBufferRef Buffer = UAV->Desc.Buffer;

				Buffer->MarkAsProducedBy(Pass);
			}
		}
		break;
		case UBMT_RENDER_TARGET_BINDING_SLOTS:
		{
			if (!RenderTargetBindingSlots)
			{
				RenderTargetBindingSlots = &Parameter.GetAsRenderTargetBindingSlots();
			}
			else if (GRDGEmitWarnings)
			{
				EmitRDGWarningf(
					TEXT("Pass %s have duplicated render target binding slots."),
					PassName);
			}
		}
		break;
		default:
			break;
		}
	}

	/** Validate that raster passes have render target binding slots and compute passes don't. */
	if (RenderTargetBindingSlots)
	{
		checkf(!bIsCompute, TEXT("Pass '%s' has render target binding slots but is flagged as 'Compute'."), PassName);
	}
	else
	{
		checkf(bIsCompute, TEXT("Pass '%s' is missing render target binding slots. Add the 'Compute' flag if render targets are not required."), PassName);
	}

	/** Validate render target / depth stencil binding usage. */
	if (RenderTargetBindingSlots)
	{
		const auto& RenderTargets = RenderTargetBindingSlots->Output;

		const uint32 RenderTargetCount = RenderTargets.Num();

		{
			/** Tracks the number of contiguous, non-null textures in the render target output array. */
			uint32 ValidRenderTargetCount = 0;

			for (uint32 RenderTargetIndex = 0; RenderTargetIndex < RenderTargetCount; ++RenderTargetIndex)
			{
				const FRenderTargetBinding& RenderTarget = RenderTargets[RenderTargetIndex];

				if (FRDGTextureRef Texture = RenderTarget.GetTexture())
				{
					/** Validate that load action is correct. */
					const bool bIsLoadAction = RenderTarget.GetLoadAction() == ERenderTargetLoadAction::ELoad;
					const bool bIsLoadActionInvalid = bIsLoadAction && !Texture->HasBeenProduced();
					checkf(
						!bIsLoadActionInvalid,
						TEXT("Can't load a render target '%s' that has never been produced."),
						Texture->Name);

					/** Mark the pass as a producer for render targets with a store action. */
					const bool bIsStoreAction = RenderTarget.GetStoreAction() != ERenderTargetStoreAction::ENoAction;
					if (bIsStoreAction)
					{
						Texture->MarkAsProducedBy(Pass);
					}
				}
				else
				{
					/** Found end of contiguous interval of valid render targets. */
					ValidRenderTargetCount = RenderTargetIndex;
					break;
				}
			}

			/** Validate that no holes exist in the render target output array. Render targets must be bound contiguously. */
			for (uint32 RenderTargetIndex = ValidRenderTargetCount; RenderTargetIndex < RenderTargetCount; ++RenderTargetIndex)
			{
				const FRenderTargetBinding& RenderTarget = RenderTargets[RenderTargetIndex];
				checkf(RenderTarget.GetTexture() == nullptr, TEXT("Render targets must be packed. No empty spaces in the array."));
			}
		}

		const auto IsMipLevelBoundForRead = [ParameterStruct, ParameterCount](FRDGTextureRef Texture, uint32 MipLevel)
		{
			for (uint32 ParameterIndex = 0; ParameterIndex < ParameterCount; ++ParameterIndex)
			{
				FRDGPassParameter Parameter = ParameterStruct.GetParameter(ParameterIndex);

				switch (Parameter.GetType())
				{
				case UBMT_RDG_TEXTURE:
				{
					if (Parameter.GetAsTexture() == Texture)
					{
						/** Full texture view bound. */
						return true;
					}
					break;
				}
				case UBMT_RDG_TEXTURE_SRV:
				{
					if (const FRDGTextureSRVRef InputSRV = Parameter.GetAsTextureSRV())
					{
						const FRDGTextureSRVDesc& InputSRVDesc = InputSRV->Desc;

						if (InputSRVDesc.Texture == Texture)
						{
							return InputSRVDesc.MipLevel == MipLevel;
						}
					}
					break;
				}
				default:
					break;
				}
			}

			return false;
		};

		/** Validate that texture mips are not bound as a render target and SRV at the same time. */
		for (uint32 RenderTargetIndex = 0; RenderTargetIndex < RenderTargetCount; ++RenderTargetIndex)
		{
			const FRenderTargetBinding& RenderTarget = RenderTargets[RenderTargetIndex];

			if (FRDGTextureRef Texture = RenderTarget.GetTexture())
			{
				if (IsMipLevelBoundForRead(Texture, RenderTarget.GetMipIndex()))
				{
					checkf(false, TEXT("Texture '%s', Mip '%d' is bound as a render target and SRV at the same time."), Texture->Name, RenderTarget.GetMipIndex());
				}
			}
			else
			{
				break;
			}
		}
	}
#endif // RDG_ENABLE_DEBUG
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

	check(Texture->ReferenceCount > 0 || GRDGImmediateMode);

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

	FRDGBufferRef Buffer = SRV->Desc.Buffer;

	// The underlying buffer have already been allocated by a prior pass through AllocateRHIBufferUAVIfNeeded().
	#if RDG_ENABLE_DEBUG
	{
		check(Buffer->HasBeenProduced());
	}
	#endif

	check(Buffer->PooledBuffer);

	if (Buffer->PooledBuffer->SRVs.Contains(SRV->Desc))
	{
		SRV->CachedRHI.SRV = Buffer->PooledBuffer->SRVs[SRV->Desc];
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

	SRV->CachedRHI.SRV = RHIShaderResourceView;
	Buffer->PooledBuffer->SRVs.Add(SRV->Desc, RHIShaderResourceView);
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
		check(Buffer->ReferenceCount > 0 || GRDGImmediateMode);

		TRefCountPtr<FPooledRDGBuffer>& AllocatedBuffer = AllocatedBuffers.FindOrAdd(Buffer);
		GRenderGraphResourcePool.FindFreeBuffer(RHICmdList, Buffer->Desc, AllocatedBuffer, Buffer->Name);

		Buffer->PooledBuffer = AllocatedBuffer;
	}

	if (Buffer->PooledBuffer->UAVs.Contains(UAV->Desc))
	{
		UAV->CachedRHI.UAV = Buffer->PooledBuffer->UAVs[UAV->Desc];
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

	if (Texture->bWritable != bRequiredWritable || Texture->bCompute != bRequiredCompute)
	{
		FTextureRHIParamRef RHITexture = Texture->PooledRenderTarget->GetRenderTargetItem().ShaderResourceTexture;

		const bool bIsMultiFrameResource = (Texture->Flags & ERDGResourceFlags::MultiFrame) == ERDGResourceFlags::MultiFrame;
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

void FRDGBuilder::TransitionUAV(FUnorderedAccessViewRHIParamRef UAV, FRDGTrackedResource* UnderlyingResource, ERDGResourceFlags ResourceFlags, EResourceTransitionAccess TransitionAccess, bool bRequiredCompute ) const
{
	const bool bRequiredWritable = TransitionAccess != EResourceTransitionAccess::EReadable;

	if (bRequiredWritable && UnderlyingResource->bWritable)
	{
		// Force a RW barrier between UAV write.
		// TODO(RDG): allow to have no barrier in the API when multiple pass write concurrently to same resource.
		EResourceTransitionPipeline TransitionPipeline = CalcTransitionPipeline(UnderlyingResource->bCompute, bRequiredCompute);
		RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, TransitionPipeline, UAV);
	}
	else if(UnderlyingResource->bWritable != bRequiredWritable || UnderlyingResource->bCompute != bRequiredCompute )
	{
	}
	else if(UnderlyingResource->bWritable != bRequiredWritable || UnderlyingResource->bCompute != bRequiredCompute )
	{
		const bool bIsMultiFrameResource = (ResourceFlags & ERDGResourceFlags::MultiFrame) == ERDGResourceFlags::MultiFrame;
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

void FRDGBuilder::ExecutePass(const FRDGPass* Pass)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FRDGBuilder_ExecutePass);

	FRHIRenderPassInfo RPInfo;
	bool bHasRenderTargets = false;

	PrepareResourcesForExecute(Pass, &RPInfo, &bHasRenderTargets);
	
	{
		const bool bAllowAccess = true;
		UpdateAccessGuardForPassResources(Pass, bAllowAccess);
	}

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

	{
		const bool bAllowAccess = false;
		UpdateAccessGuardForPassResources(Pass, bAllowAccess);
	}

	UnmarkUsedResources(Pass);

	// Can't release resources with immediate mode, because don't know if whether they are gonna be used.
	if (!GRDGImmediateMode)
	{
		ReleaseUnreferencedResources(Pass);
	}
}

void FRDGBuilder::PrepareResourcesForExecute(const FRDGPass* Pass, struct FRHIRenderPassInfo* OutRPInfo, bool* bOutHasRenderTargets)
{
	OutRPInfo->NumUAVs = 0;
	OutRPInfo->UAVIndex = 0;

	const bool bIsCompute = Pass->IsCompute();
	const bool bGeneratingMips = (Pass->GetFlags() & ERDGPassFlags::GenerateMips) == ERDGPassFlags::GenerateMips;

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
				// The underlying texture have already been allocated by a prior pass.
				#if RDG_ENABLE_DEBUG
				{
					check(Texture->HasBeenProduced());
					Texture->PassAccessCount++;
				}
				#endif

				check(Texture->PooledRenderTarget);
				check(Texture->CachedRHI.Resource);
				TransitionTexture(Texture, EResourceTransitionAccess::EReadable, bIsCompute);
			}
		}
		break;
		case UBMT_RDG_TEXTURE_SRV:
		{
			if (FRDGTextureSRVRef SRV = Parameter.GetAsTextureSRV())
			{
				FRDGTextureRef Texture = SRV->Desc.Texture;

				// The underlying texture have already been allocated by a prior pass.
				#if RDG_ENABLE_DEBUG
				{
					check(Texture->HasBeenProduced());
					Texture->PassAccessCount++;
				}
				#endif

				// Might be the first time using this render graph SRV, so need to setup the cached rhi resource.
				if (!SRV->CachedRHI.SRV)
				{
					SRV->CachedRHI.SRV = Texture->PooledRenderTarget->GetRenderTargetItem().MipSRVs[SRV->Desc.MipLevel];
				}

				TransitionTexture(Texture, EResourceTransitionAccess::EReadable, bIsCompute);
			}
		}
		break;
		case UBMT_RDG_TEXTURE_UAV:
		{
			if (FRDGTextureUAVRef UAV = Parameter.GetAsTextureUAV())
			{
				FRDGTextureRef Texture = UAV->Desc.Texture;

				if (!bIsCompute)
				{
					OutRPInfo->UAVs[OutRPInfo->NumUAVs++] = UAV->CachedRHI.UAV;	// Bind UAVs in declaration order
				}

				#if RDG_ENABLE_DEBUG
				{
					Texture->PassAccessCount++;
				}
				#endif

				AllocateRHITextureUAVIfNeeded(UAV, bIsCompute);
				TransitionUAV(UAV->CachedRHI.UAV, Texture, Texture->Flags, EResourceTransitionAccess::EWritable, bIsCompute);
			}
		}
		break;
		case UBMT_RDG_BUFFER:
		{
			if (FRDGBufferRef Buffer = Parameter.GetAsBuffer())
			{
				// The underlying buffer have already been allocated by a prior pass through AllocateRHIBufferUAVIfNeeded().
				#if RDG_ENABLE_DEBUG
				{
					check(Buffer->HasBeenProduced());
					Buffer->PassAccessCount++;
				}
				#endif

				// TODO(RDG): super hacky, find the UAV and transition it. Hopefully there is one...
				check(Buffer->PooledBuffer);
				check(Buffer->PooledBuffer->UAVs.Num() == 1);
				FUnorderedAccessViewRHIParamRef UAV = Buffer->PooledBuffer->UAVs.CreateIterator().Value();
				TransitionUAV(UAV, Buffer, Buffer->Flags, EResourceTransitionAccess::EReadable, bIsCompute);
			}
		}
		break;
		case UBMT_RDG_BUFFER_SRV:
		{
			if (FRDGBufferSRVRef SRV = Parameter.GetAsBufferSRV())
			{
				FRDGBufferRef Buffer = SRV->Desc.Buffer;

				// The underlying buffer have already been allocated by a prior pass through AllocateRHIBufferUAVIfNeeded().
				#if RDG_ENABLE_DEBUG
				{
					check(Buffer->HasBeenProduced());
					Buffer->PassAccessCount++;
				}
				#endif

				// TODO(RDG): super hacky, find the UAV and transition it. Hopefully there is one...
				check(Buffer->PooledBuffer);
				check(Buffer->PooledBuffer->UAVs.Num() == 1);
				FUnorderedAccessViewRHIParamRef UAV = Buffer->PooledBuffer->UAVs.CreateIterator().Value();

				AllocateRHIBufferSRVIfNeeded(SRV, bIsCompute);
				TransitionUAV(UAV, Buffer, Buffer->Flags, EResourceTransitionAccess::EReadable, bIsCompute);
			}
		}
		break;
		case UBMT_RDG_BUFFER_UAV:
		{
			if (FRDGBufferUAVRef UAV = Parameter.GetAsBufferUAV())
			{
				FRDGBufferRef Buffer = UAV->Desc.Buffer;

				if (!bIsCompute)
				{
					OutRPInfo->UAVs[OutRPInfo->NumUAVs++] = UAV->CachedRHI.UAV;	// Bind UAVs in declaration order
				}
				
				#if RDG_ENABLE_DEBUG
				{
					Buffer->PassAccessCount++;
				}
				#endif

				AllocateRHIBufferUAVIfNeeded(UAV, bIsCompute);
				TransitionUAV(UAV->CachedRHI.UAV, Buffer, Buffer->Flags, EResourceTransitionAccess::EWritable, bIsCompute);
			}
		}
		break;
		case UBMT_RENDER_TARGET_BINDING_SLOTS:
		{
			check(!bIsCompute);

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
					AllocateRHITextureIfNeeded(Texture, bIsCompute);

					auto& OutRenderTarget = OutRPInfo->ColorRenderTargets[RenderTargetIndex];

					// TODO(RDG): should force TargetableTexture == ShaderResourceTexture with MSAA, and instead have an explicit MSAA resolve pass.
					OutRenderTarget.RenderTarget = Texture->PooledRenderTarget->GetRenderTargetItem().TargetableTexture;
					OutRenderTarget.ResolveTarget = nullptr;
					OutRenderTarget.ArraySlice = -1;
					OutRenderTarget.MipIndex = RenderTarget.GetMipIndex();
					OutRenderTarget.Action = MakeRenderTargetActions(RenderTarget.GetLoadAction(), RenderTarget.GetStoreAction());

					if (!bGeneratingMips)
					{
						// Implicit assurance the RHI will do the correct transitions
						TransitionTexture(Texture, EResourceTransitionAccess::EWritable, bIsCompute);
					}

					SampleCount |= OutRenderTarget.RenderTarget->GetNumSamples();
					ValidRenderTargetCount++;

					#if RDG_ENABLE_DEBUG
					{
						Texture->PassAccessCount++;
					}
					#endif
				}
				else
				{
					break;
				}
			}

			OutRPInfo->UAVIndex = ValidRenderTargetCount;

			if (FRDGTextureRef Texture = DepthStencil.GetTexture())

			{
				AllocateRHITextureIfNeeded(Texture, false);

				auto& OutDepthStencil = OutRPInfo->DepthStencilRenderTarget;

				OutDepthStencil.DepthStencilTarget = Texture->PooledRenderTarget->GetRenderTargetItem().TargetableTexture;
				OutDepthStencil.ResolveTarget = nullptr;
				OutDepthStencil.Action = MakeDepthStencilTargetActions(
					MakeRenderTargetActions(DepthStencil.GetDepthLoadAction(), DepthStencil.GetDepthStoreAction()),
					MakeRenderTargetActions(DepthStencil.GetStencilLoadAction(), DepthStencil.GetStencilStoreAction()));
				OutDepthStencil.ExclusiveDepthStencil = DepthStencil.GetDepthStencilAccess();

				TransitionTexture(Texture, EResourceTransitionAccess::EWritable, bIsCompute);

				SampleCount |= OutDepthStencil.DepthStencilTarget->GetNumSamples();
				ValidDepthStencilCount++;

				#if RDG_ENABLE_DEBUG
				{
					Texture->PassAccessCount++;
				}
				#endif
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

void FRDGBuilder::UpdateAccessGuardForPassResources(const FRDGPass* Pass, bool bAllowAccess)
{
#if RDG_ENABLE_DEBUG
	FRDGPassParameterStruct ParameterStruct = Pass->GetParameters();

	const uint32 ParameterCount = ParameterStruct.GetParameterCount();

	for (uint32 ParameterIndex = 0; ParameterIndex < ParameterCount; ++ParameterIndex)
	{
		FRDGPassParameter Parameter = ParameterStruct.GetParameter(ParameterIndex);

		if (Parameter.IsResource())
		{
			if (FRDGResourceRef Resource = Parameter.GetAsResource())
			{
				Resource->bAllowRHIAccess = bAllowAccess;
			}
		}
		else if (Parameter.GetType() == UBMT_RENDER_TARGET_BINDING_SLOTS)
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
					Texture->bAllowRHIAccess = bAllowAccess;
				}
				else
				{
					break;
				}
			}
			
			if (FRDGTextureRef Texture = DepthStencil.GetTexture())
			{
				Texture->bAllowRHIAccess = bAllowAccess;
			}
		}
	}
#endif
}

void FRDGBuilder::UnmarkUsedResources(const FRDGPass* Pass)
{
#if RDG_ENABLE_DEBUG
	FRDGPassParameterStruct ParameterStruct = Pass->GetParameters();

	const uint32 ParameterCount = ParameterStruct.GetParameterCount();

	if (GRDGEmitWarnings)
	{
		uint32 TrackedResourceCount = 0;
		uint32 UsedResourceCount = 0;

		for (uint32 ParameterIndex = 0; ParameterIndex < ParameterCount; ++ParameterIndex)
		{
			FRDGPassParameter Parameter = ParameterStruct.GetParameter(ParameterIndex);

			if (Parameter.IsResource())
			{
				if (FRDGResourceRef Resource = Parameter.GetAsResource())
				{
					TrackedResourceCount++;
					UsedResourceCount += Resource->bIsActuallyUsedByPass ? 1 : 0;
				}
			}
		}

		if (TrackedResourceCount != UsedResourceCount)
		{
			FString WarningMessage = FString::Printf(
				TEXT("'%d' of the '%d' resources of the pass '%s' where not actually used."),
				TrackedResourceCount - UsedResourceCount, TrackedResourceCount, Pass->GetName());

			for (uint32 ParameterIndex = 0; ParameterIndex < ParameterCount; ++ParameterIndex)
			{
				FRDGPassParameter Parameter = ParameterStruct.GetParameter(ParameterIndex);

				if (Parameter.IsResource())
				{
					if (const FRDGResourceRef Resource = Parameter.GetAsResource())
					{
						if (!Resource->bIsActuallyUsedByPass)
						{
							WarningMessage += FString::Printf(TEXT("\n    %s"), Resource->Name);
						}
					}
				}
			}

			EmitRDGWarning(WarningMessage);
		}
	}

	for (uint32 ParameterIndex = 0; ParameterIndex < ParameterCount; ++ParameterIndex)
	{
		FRDGPassParameter Parameter = ParameterStruct.GetParameter(ParameterIndex);

		if (Parameter.IsResource())
		{
			if (const FRDGResourceRef Resource = Parameter.GetAsResource())
			{
				Resource->bIsActuallyUsedByPass = false;
			}
		}
	}
#endif
}

void FRDGBuilder::ReleaseRHITextureIfUnreferenced(FRDGTexture* Texture)
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

void FRDGBuilder::ReleaseRHIBufferIfUnreferenced(FRDGBuffer* Buffer)
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
	for (const auto& Query : DeferredInternalTextureQueries)
	{
		check(Query.Texture->PooledRenderTarget);

		if (Query.bTransitionToRead)
		{
			TransitionTexture(Query.Texture, EResourceTransitionAccess::EReadable, /* bRequiredCompute = */ false);
		}

		*Query.OutTexturePtr = AllocatedTextures.FindChecked(Query.Texture);
		
		#if RDG_ENABLE_DEBUG
		{
			// Increment the number of times the texture has been accessed to avoid warning on produced but never used resources that were produced
			// only to be extracted for the graph.
			Query.Texture->PassAccessCount += 1;
		}
		#endif

		// No need to manually release in immediate mode, since it is done directly when emptying AllocatedTextures in DestructPasses().
		if (!GRDGImmediateMode)
		{
			ReleaseRHITextureIfUnreferenced(Query.Texture);
		}
	}

	for (const auto& Query : DeferredInternalBufferQueries)
	{
		*Query.OutBufferPtr = AllocatedBuffers.FindChecked(Query.Buffer);

		#if RDG_ENABLE_DEBUG
		{
			// Increment the number of times the buffer has been accessed to avoid warning on produced but never used resources that were produced
			// only to be extracted for the graph.
			Query.Buffer->PassAccessCount += 1;
		}
		#endif

		// No need to manually release in immediate mode, since it is done directly when emptying AllocatedBuffer in DestructPasses().
		if (!GRDGImmediateMode)
		{
			ReleaseRHIBufferIfUnreferenced(Query.Buffer);
		}
	}
}

void FRDGBuilder::DestructPasses()
{
	#if RDG_ENABLE_DEBUG
	{
		const bool bEmitWarnings = GRDGEmitWarnings;

		for (const FRDGTrackedResourceRef Resource : TrackedResources)
		{
			check(Resource->ReferenceCount == 0);

			const bool bProducedButNeverUsed = Resource->PassAccessCount == 1 && Resource->FirstProducer;

			if (bEmitWarnings && bProducedButNeverUsed)
			{
				check(Resource->HasBeenProduced());

				EmitRDGWarningf(
					TEXT("Resources %s has been produced by the pass %s, but never used by another pass."),
					Resource->Name, Resource->FirstProducer->GetName());
			}
		}

		TrackedResources.Empty();
	}
	#endif

	for (int32 PassIndex = Passes.Num() - 1; PassIndex >= 0; --PassIndex)
	{
		Passes[PassIndex]->~FRDGPass();
	}
	Passes.Empty();

	DeferredInternalTextureQueries.Empty();
	DeferredInternalBufferQueries.Empty();
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