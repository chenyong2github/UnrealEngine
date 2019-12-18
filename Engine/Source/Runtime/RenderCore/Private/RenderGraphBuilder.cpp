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

TAutoConsoleVariable<int32> CVarRDGEnableBreakpoint(
	TEXT("r.RDG.EnableBreakpoint"), 0,
	TEXT("Breakpoint in debugger when a warning is raised.\n"),
	ECVF_RenderThreadSafe);

int32 GRDGClobberResources = 0;
FAutoConsoleVariableRef CVarRDGClobberResources(
	TEXT("r.RDG.ClobberResources"),
	GRDGClobberResources,
	TEXT("Clears all render targets and texture / buffer UAVs with the requested clear color at allocation time. Useful for debugging.\n")
	TEXT(" 0:off (default);\n")
	TEXT(" 1: 1000 on RGBA channels;\n")
	TEXT(" 2: NaN on RGBA channels;\n")
	TEXT(" 3: +INFINITY on RGBA channels.\n"),
	ECVF_Cheat | ECVF_RenderThreadSafe);

FLinearColor GetClobberColor()
{
	switch (GRDGClobberResources)
	{
	case 2:
		return FLinearColor(NAN, NAN, NAN, NAN);
		break;
	case 3:
		return FLinearColor(INFINITY, INFINITY, INFINITY, INFINITY);
		break;
	default:
		return FLinearColor(1000, 1000, 1000, 1000);
	}
}

uint32 GetClobberBufferValue()
{
	return 1000;
}

#else

const int32 GRDGImmediateMode = 0;
const int32 GRDGDebug = 0;
const int32 GRDGClobberResources = 0;

#endif

} //! namespace

bool IsRDGClobberResourcesEnabled()
{
	return GRDGClobberResources > 0;
}

bool GetEmitRDGEvents()
{
	check(IsInRenderingThread());
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
#if RDG_ENABLE_DEBUG_WITH_ENGINE
	if (!GRDGDebug)
	{
		return;
	}

	static TSet<FString> GAlreadyEmittedWarnings;

	const bool bEnableBreakpoint = CVarRDGEnableBreakpoint.GetValueOnRenderThread() != 0;

	if (GRDGDebug == kRDGEmitWarningsOnce)
	{
		if (!GAlreadyEmittedWarnings.Contains(WarningMessage))
		{
			GAlreadyEmittedWarnings.Add(WarningMessage);
			UE_LOG(LogRendererCore, Warning, TEXT("%s"), *WarningMessage);

			if (bEnableBreakpoint)
			{
				UE_DEBUG_BREAK();
			}
		}
	}
	else
	{
		UE_LOG(LogRendererCore, Warning, TEXT("%s"), *WarningMessage);

		if (bEnableBreakpoint)
		{
			UE_DEBUG_BREAK();
		}
	}
#endif
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
	ClobberPassOutputs(Pass);

	IF_RDG_ENABLE_DEBUG(Validation.ValidateAddPass(Pass, bInDebugPassScope));

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
	if (!GVisualizeTexture.bEnabled || bInDebugPassScope)
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
				FRDGTextureRef Texture = UAV->Desc.Texture;
				check(Texture);

				int32 CaptureId = GVisualizeTexture.ShouldCapture(Texture->Name);
				if (CaptureId != FVisualizeTexture::kInvalidCaptureId && UAV->Desc.MipLevel == GVisualizeTexture.CustomMip)
				{
					GVisualizeTexture.CreateContentCapturePass(*this, Texture, CaptureId);
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
				const bool bHasStoreAction = DepthStencil.GetDepthStencilAccess().IsAnyWrite();

				if (bHasStoreAction)
				{
					// Depth render target binding can only be done on mip level 0.
					const int32 MipLevel = 0;

					int32 CaptureId = GVisualizeTexture.ShouldCapture(Texture->Name);
					if (CaptureId != FVisualizeTexture::kInvalidCaptureId && MipLevel == GVisualizeTexture.CustomMip)
					{
						GVisualizeTexture.CreateContentCapturePass(*this, Texture, CaptureId);
					}
				}
			}

			const uint32 RenderTargetCount = RenderTargets.Num();

			for (uint32 RenderTargetIndex = 0; RenderTargetIndex < RenderTargetCount; ++RenderTargetIndex)
			{
				const FRenderTargetBinding& RenderTarget = RenderTargets[RenderTargetIndex];

				if (FRDGTextureRef Texture = RenderTarget.GetTexture())
				{
					int32 CaptureId = GVisualizeTexture.ShouldCapture(Texture->Name);
						
					if (CaptureId != FVisualizeTexture::kInvalidCaptureId && RenderTarget.GetMipIndex() == GVisualizeTexture.CustomMip)
					{
						GVisualizeTexture.CreateContentCapturePass(*this, Texture, CaptureId);
					}
				}
				else
				{
					break;
				}
			}
		}
		break;
		}
	}
#endif
}

void FRDGBuilder::ClobberPassOutputs(const FRDGPass* Pass)
{
#if RDG_ENABLE_DEBUG
	if (!IsRDGClobberResourcesEnabled())
	{
		return;
	}

	if (bInDebugPassScope)
	{
		return;
	}
	bInDebugPassScope = true;

	const auto TryMarkForClobber = [](FRDGParentResourceRef Resource) -> bool
	{
		const bool bClobber = !Resource->bHasBeenClobbered && !Resource->IsExternal();

		if (bClobber)
		{
			Resource->bHasBeenClobbered = true;
		}

		return bClobber;
	};

	const FLinearColor ClobberColor = GetClobberColor();

	FRDGPassParameterStruct ParameterStruct = Pass->GetParameters();

	const uint32 ParameterCount = ParameterStruct.GetParameterCount();

	for (uint32 ParameterIndex = 0; ParameterIndex < ParameterCount; ++ParameterIndex)
	{
		FRDGPassParameter Parameter = ParameterStruct.GetParameter(ParameterIndex);

		switch (Parameter.GetType())
		{
		case UBMT_RDG_BUFFER_UAV:
		{
			if (FRDGBufferUAVRef UAV = Parameter.GetAsBufferUAV())
			{
				FRDGBufferRef Buffer = UAV->GetParent();

				if (TryMarkForClobber(Buffer))
				{
					AddClearUAVPass(*this, UAV, GetClobberBufferValue());
				}
			}
		}
		break;
		case UBMT_RDG_TEXTURE_UAV:
		{
			if (FRDGTextureUAVRef UAV = Parameter.GetAsTextureUAV())
			{
				FRDGTextureRef Texture = UAV->GetParent();

				if (TryMarkForClobber(Texture))
				{
					AddClearUAVPass(*this, UAV, ClobberColor);
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
				if (TryMarkForClobber(Texture))
				{
					// These are arbitrarily chosen to be something unusual.
					const float ClobberDepth = 0.56789f;
					const uint8 ClobberStencil = 123;

					AddClearDepthStencilPass(*this, Texture, true, ClobberDepth, true, ClobberStencil);
				}
			}

			const uint32 RenderTargetCount = RenderTargets.Num();

			for (uint32 RenderTargetIndex = 0; RenderTargetIndex < RenderTargetCount; ++RenderTargetIndex)
			{
				const FRenderTargetBinding& RenderTarget = RenderTargets[RenderTargetIndex];

				if (FRDGTextureRef Texture = RenderTarget.GetTexture())
				{
					if (TryMarkForClobber(Texture))
					{
						AddClearRenderTargetPass(*this, Texture, ClobberColor);
					}
				}
				else
				{
					break;
				}
			}
		}
		break;
		}
	}
	bInDebugPassScope = false;
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

			if (Parameter.IsParentResource())
			{
				if (FRDGParentResourceRef Resource = Parameter.GetAsParentResource())
				{
					Resource->ReferenceCount++;
				}
			}
			else if (Parameter.IsChildResource())
			{
				if (FRDGChildResourceRef Resource = Parameter.GetAsChildResource())
				{
					Resource->GetParent()->ReferenceCount++;
				}
			}
			else if (Parameter.IsRenderTargetBindingSlots())
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

void FRDGBuilder::AllocateRHITextureSRVIfNeeded(FRDGTextureSRV* SRV)
{
	check(SRV);

	if (SRV->ResourceRHI)
	{
		return;
	}

	FRDGTextureRef Texture = SRV->Desc.Texture;
	check(Texture->PooledRenderTarget);
	FSceneRenderTargetItem& RenderTarget = Texture->PooledRenderTarget->GetRenderTargetItem();

	if (SRV->Desc.MetaData == ERDGTextureMetaDataAccess::HTile)
	{
		check(GRHISupportsExplicitHTile);
		if (!RenderTarget.HTileSRV)
		{
			RenderTarget.HTileSRV = RHICreateShaderResourceViewHTile((FTexture2DRHIRef&)RenderTarget.TargetableTexture);
		}
		SRV->ResourceRHI = RenderTarget.HTileSRV;
		check(SRV->ResourceRHI);
		return;
	}

	if (RenderTarget.SRVs.Contains(SRV->Desc))
	{
		SRV->ResourceRHI = RenderTarget.SRVs[SRV->Desc];
		return;
	}

	FShaderResourceViewRHIRef RHIShaderResourceView = RHICreateShaderResourceView(RenderTarget.ShaderResourceTexture, SRV->Desc);

	SRV->ResourceRHI = RHIShaderResourceView;
	RenderTarget.SRVs.Add(SRV->Desc, RHIShaderResourceView);
}

void FRDGBuilder::AllocateRHITextureUAVIfNeeded(FRDGTextureUAV* UAV)
{
	check(UAV);

	if (UAV->ResourceRHI)
	{
		return;
	}

	AllocateRHITextureIfNeeded(UAV->Desc.Texture);

	FRDGTextureRef Texture = UAV->Desc.Texture;
	check(Texture->PooledRenderTarget);
	FSceneRenderTargetItem& RenderTarget = Texture->PooledRenderTarget->GetRenderTargetItem();

	if (UAV->Desc.MetaData == ERDGTextureMetaDataAccess::HTile)
	{
		check(GRHISupportsExplicitHTile);
		if (!RenderTarget.HTileUAV)
		{
			RenderTarget.HTileUAV = RHICreateUnorderedAccessViewHTile((FTexture2DRHIRef&)RenderTarget.TargetableTexture);
		}
		UAV->ResourceRHI = RenderTarget.HTileUAV;
		check(UAV->ResourceRHI);
		return;
	}

	UAV->ResourceRHI = RenderTarget.MipUAVs[UAV->Desc.MipLevel];
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

	switch (Buffer->Desc.UnderlyingType)
	{
	case FRDGBufferDesc::EUnderlyingType::VertexBuffer:
		Buffer->ResourceRHI = AllocatedBuffer->VertexBuffer;
		break;
	case FRDGBufferDesc::EUnderlyingType::IndexBuffer:
		Buffer->ResourceRHI = AllocatedBuffer->IndexBuffer;
		break;
	case FRDGBufferDesc::EUnderlyingType::StructuredBuffer:
		Buffer->ResourceRHI = AllocatedBuffer->StructuredBuffer;
		break;
	}
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
	PrepareResourcesForExecute(Pass, &RPInfo);
	
	EventScopeStack.BeginExecutePass(Pass);
	StatScopeStack.BeginExecutePass(Pass);

	if (Pass->IsRaster())
	{
		RHICmdList.BeginRenderPass(RPInfo, Pass->GetName());
	}
	else
	{
		UnbindRenderTargets(RHICmdList);
	}

	Pass->Execute(RHICmdList);

	if (Pass->IsRaster())
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

void FRDGBuilder::PrepareResourcesForExecute(const FRDGPass* Pass, struct FRHIRenderPassInfo* OutRPInfo)
{
	check(Pass);

	const bool bIsCompute = Pass->IsCompute();

	FRDGBarrierBatcher BarrierBatcher(RHICmdList, Pass);

#if WITH_MGPU
	BarrierBatcher.SetNameForTemporalEffect(NameForTemporalEffect);
#endif

	FRDGPassParameterStruct ParameterStruct = Pass->GetParameters();

	const uint32 ParameterCount = ParameterStruct.GetParameterCount();

	// List all RDG texture being read and modified by the pass.
	TSet<FRDGTextureRef, DefaultKeyFuncs<FRDGTextureRef>, SceneRenderingSetAllocator> ReadTextures;
	TSet<FRDGTextureRef, DefaultKeyFuncs<FRDGTextureRef>, SceneRenderingSetAllocator> ModifiedTextures;
	for (uint32 ParameterIndex = 0; ParameterIndex < ParameterCount; ++ParameterIndex)
	{
		FRDGPassParameter Parameter = ParameterStruct.GetParameter(ParameterIndex);

		switch (Parameter.GetType())
		{
		case UBMT_RDG_TEXTURE:
		{
			if (FRDGTextureRef Texture = Parameter.GetAsTexture())
			{
				ReadTextures.Add(Texture);
			}
		}
		break;
		case UBMT_RDG_TEXTURE_SRV:
		{
			if (FRDGTextureSRVRef SRV = Parameter.GetAsTextureSRV())
			{
				ReadTextures.Add(SRV->GetParent());
			}
		}
		break;
		case UBMT_RDG_TEXTURE_UAV:
		{
			if (FRDGTextureUAVRef UAV = Parameter.GetAsTextureUAV())
			{
				ModifiedTextures.Add(UAV->GetParent());
			}
		}
		break;
		case UBMT_RDG_TEXTURE_COPY_DEST:
		{
			if (FRDGTextureRef Texture = Parameter.GetAsTexture())
			{
				ModifiedTextures.Add(Texture);
			}
		}
		break;
		case UBMT_RENDER_TARGET_BINDING_SLOTS:
		{
			const FRenderTargetBindingSlots& RenderTargetBindingSlots = Parameter.GetAsRenderTargetBindingSlots();
			const auto& RenderTargets = RenderTargetBindingSlots.Output;
			const uint32 RenderTargetCount = RenderTargets.Num();

			for (uint32 RenderTargetIndex = 0; RenderTargetIndex < RenderTargetCount; RenderTargetIndex++)
			{
				if (FRDGTextureRef Texture = RenderTargets[RenderTargetIndex].GetTexture())
				{
					ModifiedTextures.Add(Texture);
				}
				else
				{
					break;
				}
			}

			if (FRDGTextureRef Texture = RenderTargetBindingSlots.DepthStencil.GetTexture())
			{
				if (RenderTargetBindingSlots.DepthStencil.GetDepthStencilAccess().IsAnyWrite())
					ModifiedTextures.Add(Texture);
			}
		}
		break;
		default:
			break;
		}
	}

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

				check(!ModifiedTextures.Contains(Texture));
				BarrierBatcher.QueueTransitionTexture(Texture, FRDGResourceState::EAccess::Read);
			}
		}
		break;
		case UBMT_RDG_TEXTURE_SRV:
		{
			if (FRDGTextureSRVRef SRV = Parameter.GetAsTextureSRV())
			{
				FRDGTextureRef Texture = SRV->Desc.Texture;

				AllocateRHITextureSRVIfNeeded(SRV);

				if (ModifiedTextures.Contains(Texture))
				{
					// If it is bound to a RT, no need for ERWSubResBarrier.
					// If it is bound as a UAV, the UAV is going to issue the ERWSubResBarrier.
				}
				else
				{
					BarrierBatcher.QueueTransitionTexture(Texture, FRDGResourceState::EAccess::Read);
				}
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

				bool bGeneratingMips = ReadTextures.Contains(Texture);

				BarrierBatcher.QueueTransitionUAV(UAVRHI, Texture, FRDGResourceState::EAccess::Write, bGeneratingMips);
			}
		}
		break;
		case UBMT_RDG_TEXTURE_COPY_DEST:
		{
			if (FRDGTextureRef Texture = Parameter.GetAsTexture())
			{
				AllocateRHITextureIfNeeded(Texture);

				check(!ReadTextures.Contains(Texture));
				BarrierBatcher.QueueTransitionTexture(Texture, FRDGResourceState::EAccess::Write);
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

				check(Buffer->PooledBuffer);

				// TODO(RDG): super hacky, find the UAV and transition it. Hopefully there is one...
				if (Buffer->PooledBuffer->UAVs.Num() > 0)
				{
					check(Buffer->PooledBuffer->UAVs.Num() == 1);
					FRHIUnorderedAccessView* UAVRHI = Buffer->PooledBuffer->UAVs.CreateIterator().Value();

					BarrierBatcher.QueueTransitionUAV(UAVRHI, Buffer, FRDGResourceState::EAccess::Read);
				}
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

				BarrierBatcher.QueueTransitionUAV(UAVRHI, Buffer, FRDGResourceState::EAccess::Write);
			}
		}
		break;
		case UBMT_RDG_BUFFER_COPY_DEST:
		{
			if (FRDGBufferRef Buffer = Parameter.GetAsBuffer())
			{
				#if RDG_ENABLE_DEBUG
				{
					Buffer->PassAccessCount++;
				}
				#endif

				AllocateRHIBufferIfNeeded(Buffer);
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

					// TODO(RDG): The load store action could actually be optimised by render graph for tile hardware when there is multiple
					// consecutive rasterizer passes that have RDG resource as render target, a bit like resource transitions.
					ERenderTargetStoreAction StoreAction = ERenderTargetStoreAction::EStore;

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

			if (FRDGTextureRef Texture = DepthStencil.GetTexture())
			{
				AllocateRHITextureIfNeeded(Texture);

				auto& OutDepthStencil = OutRPInfo->DepthStencilRenderTarget;

				FExclusiveDepthStencil ExclusiveDepthStencil = DepthStencil.GetDepthStencilAccess();

				ERenderTargetStoreAction DepthStoreAction = ExclusiveDepthStencil.IsDepthWrite() ? ERenderTargetStoreAction::EStore : ERenderTargetStoreAction::ENoAction;
				ERenderTargetStoreAction StencilStoreAction = ExclusiveDepthStencil.IsStencilWrite() ? ERenderTargetStoreAction::EStore : ERenderTargetStoreAction::ENoAction;

				OutDepthStencil.DepthStencilTarget = Texture->PooledRenderTarget->GetRenderTargetItem().TargetableTexture;
				OutDepthStencil.ResolveTarget = nullptr;
				OutDepthStencil.Action = MakeDepthStencilTargetActions(
					MakeRenderTargetActions(DepthStencil.GetDepthLoadAction(), DepthStoreAction),
					MakeRenderTargetActions(DepthStencil.GetStencilLoadAction(), StencilStoreAction));
				OutDepthStencil.ExclusiveDepthStencil = ExclusiveDepthStencil;

				BarrierBatcher.QueueTransitionTexture(Texture,
					ExclusiveDepthStencil.IsAnyWrite() ?
					FRDGResourceState::EAccess::Write :
					FRDGResourceState::EAccess::Read);

				SampleCount |= OutDepthStencil.DepthStencilTarget->GetNumSamples();
				ValidDepthStencilCount++;
			}

			OutRPInfo->bIsMSAA = SampleCount > 1;
		}
		break;
		default:
			break;
		}
	}

	OutRPInfo->bGeneratingMips = Pass->IsGenerateMips();
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
		case UBMT_RDG_TEXTURE_COPY_DEST:
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
		case UBMT_RDG_BUFFER_COPY_DEST:
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

#if WITH_MGPU
	BarrierBatcher.SetNameForTemporalEffect(NameForTemporalEffect);
#endif

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
		check(Query.Buffer->PooledBuffer);

		for (TMap<FRDGBufferUAVDesc, FUnorderedAccessViewRHIRef, FDefaultSetAllocator, TMapRDGBufferUAVFuncs<FRDGBufferUAVDesc, FUnorderedAccessViewRHIRef>>::TIterator It(Query.Buffer->PooledBuffer->UAVs); It; ++It)
		{
			BarrierBatcher.QueueTransitionUAV(It.Value(), Query.Buffer, Query.DestinationAccess, /* bGeneratingMips = */ false, Query.DestinationPipeline);
		}

		*Query.OutBufferPtr = AllocatedBuffers.FindChecked(Query.Buffer);

		// No need to manually release in immediate mode, since it is done directly when emptying AllocatedBuffer in DestructPasses().
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