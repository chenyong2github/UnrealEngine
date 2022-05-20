// Copyright Epic Games, Inc. All Rights Reserved.

#include "AGXRHIPrivate.h"
#include "AGXRHIRenderQuery.h"
#include "AGXCommandBufferFence.h"

TGlobalResource<TBoundShaderStateHistory<10000>> FAGXRHICommandContext::BoundShaderStateHistory;

FAGXDeviceContext& GetAGXDeviceContext()
{
	FAGXRHICommandContext* Context = static_cast<FAGXRHICommandContext*>(RHIGetDefaultContext());
	check(Context);
	return ((FAGXDeviceContext&)Context->GetInternalContext());
}

void AGXSafeReleaseMetalObject(id Object)
{
	if(GIsAGXInitialized && GDynamicRHI && Object)
	{
		FAGXRHICommandContext* Context = static_cast<FAGXRHICommandContext*>(RHIGetDefaultContext());
		if(Context)
		{
			((FAGXDeviceContext&)Context->GetInternalContext()).ReleaseObject(Object);
			return;
		}
	}
	[Object release];
}

void AGXSafeReleaseMetalTexture(FAGXTexture& Object)
{
	if(GIsAGXInitialized && GDynamicRHI && Object)
	{
		FAGXRHICommandContext* Context = static_cast<FAGXRHICommandContext*>(RHIGetDefaultContext());
		if(Context)
		{
			((FAGXDeviceContext&)Context->GetInternalContext()).ReleaseTexture(Object);
			return;
		}
	}
}

void AGXSafeReleaseMetalBuffer(FAGXBuffer& Buffer)
{
	if(GIsAGXInitialized && GDynamicRHI && Buffer)
	{
		Buffer.SetOwner(nullptr, false);
		FAGXRHICommandContext* Context = static_cast<FAGXRHICommandContext*>(RHIGetDefaultContext());
		if(Context)
		{
			((FAGXDeviceContext&)Context->GetInternalContext()).ReleaseBuffer(Buffer);
		}
	}
}

FAGXRHICommandContext::FAGXRHICommandContext(class FAGXProfiler* InProfiler, FAGXContext* WrapContext)
: Context(WrapContext)
, Profiler(InProfiler)
, PendingVertexDataStride(0)
, PendingIndexDataStride(0)
, PendingPrimitiveType(0)
, PendingNumPrimitives(0)
{
	check(Context);
	GlobalUniformBuffers.AddZeroed(FUniformBufferStaticSlotRegistry::Get().GetSlotCount());
}

FAGXRHICommandContext::~FAGXRHICommandContext()
{
	delete Context;
}

FAGXRHIComputeContext::FAGXRHIComputeContext(class FAGXProfiler* InProfiler, FAGXContext* WrapContext)
: FAGXRHICommandContext(InProfiler, WrapContext)
{
}

FAGXRHIComputeContext::~FAGXRHIComputeContext()
{
}

void FAGXRHIComputeContext::RHISetAsyncComputeBudget(EAsyncComputeBudget Budget)
{
	if (!Context->GetCurrentCommandBuffer())
	{
		Context->InitFrame(false, 0, 0);
	}
	FAGXRHICommandContext::RHISetAsyncComputeBudget(Budget);
}

void FAGXRHIComputeContext::RHISetComputePipelineState(FRHIComputePipelineState* ComputePipelineState)
{
	if (!Context->GetCurrentCommandBuffer())
	{
		Context->InitFrame(false, 0, 0);
	}
	FAGXRHICommandContext::RHISetComputePipelineState(ComputePipelineState);
}

void FAGXRHIComputeContext::RHISubmitCommandsHint()
{
	if (!Context->GetCurrentCommandBuffer())
	{
		Context->InitFrame(false, 0, 0);
	}
	Context->FinishFrame(false);
	
#if ENABLE_METAL_GPUPROFILE
	FAGXContext::MakeCurrent(&GetAGXDeviceContext());
#endif
}

FAGXRHIImmediateCommandContext::FAGXRHIImmediateCommandContext(class FAGXProfiler* InProfiler, FAGXContext* WrapContext)
	: FAGXRHICommandContext(InProfiler, WrapContext)
{
}

void FAGXRHICommandContext::RHIBeginRenderPass(const FRHIRenderPassInfo& InInfo, const TCHAR* InName)
{
	@autoreleasepool {
	bool bHasTarget = (InInfo.DepthStencilRenderTarget.DepthStencilTarget != nullptr || InInfo.GetNumColorRenderTargets() > 0);
	
	if (InInfo.NumOcclusionQueries > 0)
	{
		Context->GetCommandList().SetParallelIndex(0, 0);
	}

	// Ignore any attempt to "clear" the render-targets as that is senseless with the way AGXRHI has to try and coalesce passes.
	if (bHasTarget)
	{
		Context->SetRenderPassInfo(InInfo);

		// Set the viewport to the full size of render target 0.
		if (InInfo.ColorRenderTargets[0].RenderTarget)
		{
			const FRHIRenderPassInfo::FColorEntry& RenderTargetView = InInfo.ColorRenderTargets[0];
			FAGXSurface* RenderTarget = AGXGetMetalSurfaceFromRHITexture(RenderTargetView.RenderTarget);

			uint32 Width = FMath::Max((uint32)(RenderTarget->Texture.GetWidth() >> RenderTargetView.MipIndex), (uint32)1);
			uint32 Height = FMath::Max((uint32)(RenderTarget->Texture.GetHeight() >> RenderTargetView.MipIndex), (uint32)1);

			RHISetViewport(0.0f, 0.0f, 0.0f, (float)Width, (float)Height, 1.0f);
		}
	}
	}
	
	RenderPassInfo = InInfo;
	if (InInfo.NumOcclusionQueries > 0)
	{
		RHIBeginOcclusionQueryBatch(InInfo.NumOcclusionQueries);
	}
}

void FAGXRHICommandContext::RHIEndRenderPass()
{
	if (RenderPassInfo.NumOcclusionQueries > 0)
	{
		RHIEndOcclusionQueryBatch();
	}

	UE::RHICore::ResolveRenderPassTargets(RenderPassInfo, [this](UE::RHICore::FResolveTextureInfo Info)
	{
		ResolveTexture(Info);
	});
}

void FAGXRHICommandContext::ResolveTexture(UE::RHICore::FResolveTextureInfo Info)
{
	@autoreleasepool {
	FAGXSurface* Source = AGXGetMetalSurfaceFromRHITexture(Info.SourceTexture);
	FAGXSurface* Destination = AGXGetMetalSurfaceFromRHITexture(Info.DestTexture);

	const FRHITextureDesc& SourceDesc = Source->GetDesc();
	const FRHITextureDesc& DestinationDesc = Destination->GetDesc();

	const bool bDepthStencil = SourceDesc.Format == PF_DepthStencil;
	const bool bSupportsMSAADepthResolve = GetAGXDeviceContext().SupportsFeature(EAGXFeaturesMSAADepthResolve);
	const bool bSupportsMSAAStoreAndResolve = GetAGXDeviceContext().SupportsFeature(EAGXFeaturesMSAAStoreAndResolve);
	// Resolve required - Device must support this - Using Shader for resolve not supported amd NumSamples should be 1
	check((!bDepthStencil && bSupportsMSAAStoreAndResolve) || (bDepthStencil && bSupportsMSAADepthResolve));

	mtlpp::Origin Origin(0, 0, 0);
	mtlpp::Size Size(0, 0, 1);

	if (Info.ResolveRect.IsValid())
	{
		Origin.x    = Info.ResolveRect.X1;
		Origin.y    = Info.ResolveRect.Y1;
		Size.width  = Info.ResolveRect.X2 - Info.ResolveRect.X1;
		Size.height = Info.ResolveRect.Y2 - Info.ResolveRect.Y1;
	}
	else
	{
		Size.width  = FMath::Max<uint32>(1, SourceDesc.Extent.X >> Info.MipLevel);
		Size.height = FMath::Max<uint32>(1, SourceDesc.Extent.Y >> Info.MipLevel);
	}

	if (Profiler)
	{
		Profiler->RegisterGPUWork();
	}

	int32 ArraySliceBegin = Info.ArraySlice;
	int32 ArraySliceEnd   = Info.ArraySlice + 1;

	if (Info.ArraySlice < 0)
	{
		ArraySliceBegin = 0;
		ArraySliceEnd   = SourceDesc.ArraySize;
	}

	for (int32 ArraySlice = ArraySliceBegin; ArraySlice < ArraySliceEnd; ArraySlice++)
	{
		Context->CopyFromTextureToTexture(Source->MSAAResolveTexture, ArraySlice, Info.MipLevel, Origin, Size, Destination->Texture, ArraySlice, Info.MipLevel, Origin);

#if PLATFORM_MAC
		if ((Destination->GPUReadback & FAGXSurface::EAGXGPUReadbackFlags::ReadbackRequested) != 0)
		{
			Context->GetCurrentRenderPass().SynchronizeTexture(Destination->Texture, ArraySlice, Info.MipLevel);
		}
#endif
	}
	}
}

void FAGXRHICommandContext::RHINextSubpass()
{
#if PLATFORM_MAC
	if (RenderPassInfo.SubpassHint == ESubpassHint::DepthReadSubpass)
	{
		FAGXRenderPass& RP = Context->GetCurrentRenderPass();
		if (RP.GetCurrentCommandEncoder().IsRenderCommandEncoderActive())
		{
			RP.InsertTextureBarrier();
		}
	}
#endif
}

void FAGXRHICommandContext::RHIBeginRenderQuery(FRHIRenderQuery* QueryRHI)
{
	@autoreleasepool {
		FAGXRHIRenderQuery* Query = ResourceCast(QueryRHI);
		Query->Begin(Context, CommandBufferFence);
	}
}

void FAGXRHICommandContext::RHIEndRenderQuery(FRHIRenderQuery* QueryRHI)
{
	@autoreleasepool {
		FAGXRHIRenderQuery* Query = ResourceCast(QueryRHI);
		Query->End(Context);
	}
}

void FAGXRHICommandContext::RHIBeginOcclusionQueryBatch(uint32 NumQueriesInBatch)
{
	check(!CommandBufferFence.IsValid());
	CommandBufferFence = MakeShareable(new FAGXCommandBufferFence);
}

void FAGXRHICommandContext::RHIEndOcclusionQueryBatch()
{
	check(CommandBufferFence.IsValid());
	Context->InsertCommandBufferFence(*CommandBufferFence);
	CommandBufferFence.Reset();
}
