// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetalRHIPrivate.h"
#include "MetalRHIRenderQuery.h"
#include "MetalCommandBufferFence.h"

TGlobalResource<TBoundShaderStateHistory<10000>> FMetalRHICommandContext::BoundShaderStateHistory;

FMetalDeviceContext& GetMetalDeviceContext()
{
	FMetalRHICommandContext* Context = static_cast<FMetalRHICommandContext*>(RHIGetDefaultContext());
	check(Context);
	return ((FMetalDeviceContext&)Context->GetInternalContext());
}

void SafeReleaseMetalObject(id Object)
{
	if(GIsMetalInitialized && GDynamicRHI && Object)
	{
		FMetalRHICommandContext* Context = static_cast<FMetalRHICommandContext*>(RHIGetDefaultContext());
		if(Context)
		{
			((FMetalDeviceContext&)Context->GetInternalContext()).ReleaseObject(Object);
			return;
		}
	}
	[Object release];
}

void SafeReleaseMetalTexture(FMetalTexture& Object)
{
	if(GIsMetalInitialized && GDynamicRHI && Object)
	{
		FMetalRHICommandContext* Context = static_cast<FMetalRHICommandContext*>(RHIGetDefaultContext());
		if(Context)
		{
			((FMetalDeviceContext&)Context->GetInternalContext()).ReleaseTexture(Object);
			return;
		}
	}
}

void SafeReleaseMetalBuffer(FMetalBuffer& Buffer)
{
	if(GIsMetalInitialized && GDynamicRHI && Buffer)
	{
		Buffer.SetOwner(nullptr, false);
		FMetalRHICommandContext* Context = static_cast<FMetalRHICommandContext*>(RHIGetDefaultContext());
		if(Context)
		{
			((FMetalDeviceContext&)Context->GetInternalContext()).ReleaseBuffer(Buffer);
		}
	}
}

void SafeReleaseMetalFence(FMetalFence* Object)
{
	if(GIsMetalInitialized && GDynamicRHI && Object)
	{
		FMetalRHICommandContext* Context = static_cast<FMetalRHICommandContext*>(RHIGetDefaultContext());
		if(Context)
		{
			((FMetalDeviceContext&)Context->GetInternalContext()).ReleaseFence(Object);
			return;
		}
	}
}

FMetalRHICommandContext::FMetalRHICommandContext(class FMetalProfiler* InProfiler, FMetalContext* WrapContext)
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

FMetalRHICommandContext::~FMetalRHICommandContext()
{
	delete Context;
}

FMetalRHIComputeContext::FMetalRHIComputeContext(class FMetalProfiler* InProfiler, FMetalContext* WrapContext)
: FMetalRHICommandContext(InProfiler, WrapContext)
{
	if (FMetalCommandQueue::SupportsFeature(EMetalFeaturesFences) && FApplePlatformMisc::IsOSAtLeastVersion((uint32[]){10, 14, 0}, (uint32[]){12, 0, 0}, (uint32[]){12, 0, 0}))
	{
		WrapContext->GetCurrentRenderPass().SetDispatchType(mtlpp::DispatchType::Concurrent);
	}
}

FMetalRHIComputeContext::~FMetalRHIComputeContext()
{
}

void FMetalRHIComputeContext::RHISetAsyncComputeBudget(EAsyncComputeBudget Budget)
{
	if (!Context->GetCurrentCommandBuffer())
	{
		Context->InitFrame(false, 0, 0);
	}
	FMetalRHICommandContext::RHISetAsyncComputeBudget(Budget);
}

void FMetalRHIComputeContext::RHISetComputeShader(FRHIComputeShader* ComputeShader)
{
	if (!Context->GetCurrentCommandBuffer())
	{
		Context->InitFrame(false, 0, 0);
	}
	FMetalRHICommandContext::RHISetComputeShader(ComputeShader);
}

void FMetalRHIComputeContext::RHISetComputePipelineState(FRHIComputePipelineState* ComputePipelineState)
{
	if (!Context->GetCurrentCommandBuffer())
	{
		Context->InitFrame(false, 0, 0);
	}
	FMetalRHICommandContext::RHISetComputePipelineState(ComputePipelineState);
}

void FMetalRHIComputeContext::RHISubmitCommandsHint()
{
	if (!Context->GetCurrentCommandBuffer())
	{
		Context->InitFrame(false, 0, 0);
	}
	Context->FinishFrame(false);
	
#if ENABLE_METAL_GPUPROFILE
	FMetalContext::MakeCurrent(&GetMetalDeviceContext());
#endif
}

FMetalRHIImmediateCommandContext::FMetalRHIImmediateCommandContext(class FMetalProfiler* InProfiler, FMetalContext* WrapContext)
	: FMetalRHICommandContext(InProfiler, WrapContext)
{
}

void FMetalRHICommandContext::RHIBeginRenderPass(const FRHIRenderPassInfo& InInfo, const TCHAR* InName)
{
	@autoreleasepool {
	bool bHasTarget = (InInfo.DepthStencilRenderTarget.DepthStencilTarget != nullptr || InInfo.GetNumColorRenderTargets() > 0);
	
	if (InInfo.bOcclusionQueries)
	{
		Context->GetCommandList().SetParallelIndex(0, 0);
	}

	// Ignore any attempt to "clear" the render-targets as that is senseless with the way MetalRHI has to try and coalesce passes.
	if (bHasTarget)
	{
		Context->SetRenderPassInfo(InInfo);

		// Set the viewport to the full size of render target 0.
		if (InInfo.ColorRenderTargets[0].RenderTarget)
		{
			const FRHIRenderPassInfo::FColorEntry& RenderTargetView = InInfo.ColorRenderTargets[0];
			FMetalSurface* RenderTarget = GetMetalSurfaceFromRHITexture(RenderTargetView.RenderTarget);

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

void FMetalRHICommandContext::RHIEndRenderPass()
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

void FMetalRHICommandContext::RHINextSubpass()
{
#if PLATFORM_MAC
	if (RenderPassInfo.SubpassHint == ESubpassHint::DepthReadSubpass)
	{
		FMetalRenderPass& RP = Context->GetCurrentRenderPass();
		if (RP.GetCurrentCommandEncoder().IsRenderCommandEncoderActive())
		{
			RP.InsertTextureBarrier();
		}
	}
#endif
}

void FMetalRHICommandContext::RHIBeginRenderQuery(FRHIRenderQuery* QueryRHI)
{
	@autoreleasepool {
		FMetalRHIRenderQuery* Query = ResourceCast(QueryRHI);
		Query->Begin(Context, CommandBufferFence);
	}
}

void FMetalRHICommandContext::RHIEndRenderQuery(FRHIRenderQuery* QueryRHI)
{
	@autoreleasepool {
		FMetalRHIRenderQuery* Query = ResourceCast(QueryRHI);
		Query->End(Context);
	}
}

void FMetalRHICommandContext::RHIBeginOcclusionQueryBatch(uint32 NumQueriesInBatch)
{
	check(!CommandBufferFence.IsValid());
	CommandBufferFence = MakeShareable(new FMetalCommandBufferFence);
}

void FMetalRHICommandContext::RHIEndOcclusionQueryBatch()
{
	check(CommandBufferFence.IsValid());
	Context->InsertCommandBufferFence(*CommandBufferFence);
	CommandBufferFence.Reset();
}
