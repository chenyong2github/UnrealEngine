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

void FAGXRHIComputeContext::RHISetComputeShader(FRHIComputeShader* ComputeShader)
{
	if (!Context->GetCurrentCommandBuffer())
	{
		Context->InitFrame(false, 0, 0);
	}
	FAGXRHICommandContext::RHISetComputeShader(ComputeShader);
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
	
	if (InInfo.bOcclusionQueries)
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
	if (InInfo.bOcclusionQueries)
	{
		RHIBeginOcclusionQueryBatch(InInfo.NumOcclusionQueries);
	}
}

void FAGXRHICommandContext::RHIEndRenderPass()
{
	if (RenderPassInfo.bOcclusionQueries)
	{
		RHIEndOcclusionQueryBatch();
	}
	
	for (int32 Index = 0; Index < MaxSimultaneousRenderTargets; ++Index)
	{
		if (!RenderPassInfo.ColorRenderTargets[Index].RenderTarget)
		{
			break;
		}
		if (RenderPassInfo.ColorRenderTargets[Index].ResolveTarget)
		{
			RHICopyToResolveTarget(RenderPassInfo.ColorRenderTargets[Index].RenderTarget, RenderPassInfo.ColorRenderTargets[Index].ResolveTarget, RenderPassInfo.ResolveParameters);
		}
	}
	
	if (RenderPassInfo.DepthStencilRenderTarget.DepthStencilTarget && RenderPassInfo.DepthStencilRenderTarget.ResolveTarget)
	{
		RHICopyToResolveTarget(RenderPassInfo.DepthStencilRenderTarget.DepthStencilTarget, RenderPassInfo.DepthStencilRenderTarget.ResolveTarget, RenderPassInfo.ResolveParameters);
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
