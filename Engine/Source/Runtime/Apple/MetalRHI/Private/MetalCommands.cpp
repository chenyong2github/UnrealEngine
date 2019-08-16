// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalCommands.cpp: Metal RHI commands implementation.
=============================================================================*/

#include "MetalRHIPrivate.h"

#include "GlobalShader.h"
#include "OneColorShader.h"
#include "RHICommandList.h"
#include "RHIStaticStates.h"
#include "ShaderParameterUtils.h"
#include "SceneUtils.h"
#include "MetalProfiler.h"
#include "MetalCommandBuffer.h"
#include "StaticBoundShaderState.h"
#include "EngineGlobals.h"
#include "PipelineStateCache.h"

static const bool GUsesInvertedZ = true;

/** Vertex declaration for just one FVector4 position. */
class FVector4VertexDeclaration : public FRenderResource
{
public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;
	virtual void InitRHI() override
	{
		FVertexDeclarationElementList Elements;
		Elements.Add(FVertexElement(0, 0, VET_Float4, 0, sizeof(FVector4)));
		VertexDeclarationRHI = PipelineStateCache::GetOrCreateVertexDeclaration(Elements);
	}
	virtual void ReleaseRHI() override
	{
		VertexDeclarationRHI.SafeRelease();
	}
};
static TGlobalResource<FVector4VertexDeclaration> GVector4VertexDeclaration;

mtlpp::PrimitiveType TranslatePrimitiveType(uint32 PrimitiveType)
{
	switch (PrimitiveType)
	{
		case PT_TriangleList:	return mtlpp::PrimitiveType::Triangle;
		case PT_TriangleStrip:	return mtlpp::PrimitiveType::TriangleStrip;
		case PT_LineList:		return mtlpp::PrimitiveType::Line;
		case PT_PointList:		return mtlpp::PrimitiveType::Point;
		// Metal doesn't actually actually draw in control-point patch-lists because of the way the compute shader stage works - it can handle any arbitrary patch size and will output triangles.
		case PT_1_ControlPointPatchList:
		case PT_2_ControlPointPatchList:
		case PT_3_ControlPointPatchList:
		case PT_4_ControlPointPatchList:
		case PT_5_ControlPointPatchList:
		case PT_6_ControlPointPatchList:
		case PT_7_ControlPointPatchList:
		case PT_8_ControlPointPatchList:
		case PT_9_ControlPointPatchList:
		case PT_10_ControlPointPatchList:
		case PT_11_ControlPointPatchList:
		case PT_12_ControlPointPatchList:
		case PT_13_ControlPointPatchList:
		case PT_14_ControlPointPatchList:
		case PT_15_ControlPointPatchList:
		case PT_16_ControlPointPatchList:
		case PT_17_ControlPointPatchList:
		case PT_18_ControlPointPatchList:
		case PT_19_ControlPointPatchList:
		case PT_20_ControlPointPatchList:
		case PT_21_ControlPointPatchList:
		case PT_22_ControlPointPatchList:
		case PT_23_ControlPointPatchList:
		case PT_24_ControlPointPatchList:
		case PT_25_ControlPointPatchList:
		case PT_26_ControlPointPatchList:
		case PT_27_ControlPointPatchList:
		case PT_28_ControlPointPatchList:
		case PT_29_ControlPointPatchList:
		case PT_30_ControlPointPatchList:
		case PT_31_ControlPointPatchList:
		case PT_32_ControlPointPatchList:
		{
			static uint32 Logged = 0;
			if (!Logged)
			{
				Logged = 1;
				UE_LOG(LogMetal, Warning, TEXT("Untested primitive type %d"), (int32)PrimitiveType);
			}
			return mtlpp::PrimitiveType::Triangle;
		}
		default:
			METAL_FATAL_ERROR(TEXT("Unsupported primitive type %d"), (int32)PrimitiveType);
			return mtlpp::PrimitiveType::Triangle;
	}
}

void FMetalRHICommandContext::RHISetStreamSource(uint32 StreamIndex, FRHIVertexBuffer* VertexBufferRHI,uint32 Offset)
{
	@autoreleasepool {
		FMetalVertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);
		Context->GetCurrentState().SetVertexStream(StreamIndex, VertexBuffer ? &VertexBuffer->Buffer : nil, VertexBuffer ? VertexBuffer->Data : nil, Offset, VertexBuffer ? VertexBuffer->GetSize() : 0);
	}
}

void FMetalDynamicRHI::RHISetStreamOutTargets(uint32 NumTargets, FRHIVertexBuffer* const * VertexBuffers, const uint32* Offsets)
{
	NOT_SUPPORTED("RHISetStreamOutTargets");

}

void FMetalRHICommandContext::RHISetComputeShader(FRHIComputeShader* ComputeShaderRHI)
{
	@autoreleasepool {
	FMetalComputeShader* ComputeShader = ResourceCast(ComputeShaderRHI);

	// cache this for Dispatch
	// sets this compute shader pipeline as the current (this resets all state, so we need to set all resources after calling this)
	Context->GetCurrentState().SetComputeShader(ComputeShader);
	}
}

void FMetalRHICommandContext::RHISetComputePipelineState(FRHIComputePipelineState* ComputePipelineState)
{
	@autoreleasepool {
	FMetalComputePipelineState* ComputePipeline = ResourceCast(ComputePipelineState);
	
	// cache this for Dispatch
	// sets this compute shader pipeline as the current (this resets all state, so we need to set all resources after calling this)
	Context->GetCurrentState().SetComputeShader(ComputePipeline->GetComputeShader());
	}
}

void FMetalRHICommandContext::RHIDispatchComputeShader(uint32 ThreadGroupCountX, uint32 ThreadGroupCountY, uint32 ThreadGroupCountZ)
{
	@autoreleasepool {
	ThreadGroupCountX = FMath::Max(ThreadGroupCountX, 1u);
	ThreadGroupCountY = FMath::Max(ThreadGroupCountY, 1u);
	ThreadGroupCountZ = FMath::Max(ThreadGroupCountZ, 1u);
	
	Context->Dispatch(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
	}
}

void FMetalRHICommandContext::RHIDispatchIndirectComputeShader(FRHIVertexBuffer* ArgumentBufferRHI, uint32 ArgumentOffset)
{
	@autoreleasepool {
	if (GetMetalDeviceContext().SupportsFeature(EMetalFeaturesIndirectBuffer))
	{
		FMetalVertexBuffer* VertexBuffer = ResourceCast(ArgumentBufferRHI);
		
		Context->DispatchIndirect(VertexBuffer, ArgumentOffset);
	}
	else
	{
		NOT_SUPPORTED("RHIDispatchIndirectComputeShader");
	}
	}
}

void FMetalRHICommandContext::RHISetViewport(uint32 MinX,uint32 MinY,float MinZ,uint32 MaxX,uint32 MaxY,float MaxZ)
{
	@autoreleasepool {
	mtlpp::Viewport Viewport;
	Viewport.originX = MinX;
	Viewport.originY = MinY;
	Viewport.width = MaxX - MinX;
	Viewport.height = MaxY - MinY;
	Viewport.znear = MinZ;
	Viewport.zfar = MaxZ;
	
	Context->GetCurrentState().SetViewport(Viewport);
	}
}

void FMetalRHICommandContext::RHISetStereoViewport(uint32 LeftMinX, uint32 RightMinX, uint32 LeftMinY, uint32 RightMinY, float MinZ, uint32 LeftMaxX, uint32 RightMaxX, uint32 LeftMaxY, uint32 RightMaxY, float MaxZ)
{
	if (FMetalCommandQueue::SupportsFeature(EMetalFeaturesMultipleViewports))
	{
		@autoreleasepool {
		mtlpp::Viewport Viewport[2];
		
		Viewport[0].originX = LeftMinX;
		Viewport[0].originY = LeftMinY;
		Viewport[0].width = LeftMaxX - LeftMinX;
		Viewport[0].height = LeftMaxY - LeftMinY;
		Viewport[0].znear = MinZ;
		Viewport[0].zfar = MaxZ;
		
		Viewport[1].originX = RightMinX;
		Viewport[1].originY = RightMinY;
		Viewport[1].width = RightMaxX - RightMinX;
		Viewport[1].height = RightMaxY - RightMinY;
		Viewport[1].znear = MinZ;
		Viewport[1].zfar = MaxZ;
		
		Context->GetCurrentState().SetViewports(Viewport, 2);
		}
	}
	else
	{
		NOT_SUPPORTED("RHISetStereoViewport");
	}
}

void FMetalRHICommandContext::RHISetMultipleViewports(uint32 Count, const FViewportBounds* Data)
{ 
	NOT_SUPPORTED("RHISetMultipleViewports");
}

void FMetalRHICommandContext::RHISetScissorRect(bool bEnable,uint32 MinX,uint32 MinY,uint32 MaxX,uint32 MaxY)
{
	@autoreleasepool {
	mtlpp::ScissorRect Scissor;
	Scissor.x = MinX;
	Scissor.y = MinY;
	Scissor.width = MaxX - MinX;
	Scissor.height = MaxY - MinY;

	// metal doesn't support 0 sized scissor rect
	if (bEnable == false || Scissor.width == 0 || Scissor.height == 0)
	{
		mtlpp::Viewport const& Viewport = Context->GetCurrentState().GetViewport(0);
		CGSize FBSize = Context->GetCurrentState().GetFrameBufferSize();
		
		Scissor.x = Viewport.originX;
		Scissor.y = Viewport.originY;
		Scissor.width = (Viewport.originX + Viewport.width <= FBSize.width) ? Viewport.width : FBSize.width - Viewport.originX;
		Scissor.height = (Viewport.originY + Viewport.height <= FBSize.height) ? Viewport.height : FBSize.height - Viewport.originY;
	}
	Context->GetCurrentState().SetScissorRect(bEnable, Scissor);
	}
}

void FMetalRHICommandContext::RHISetGraphicsPipelineState(FRHIGraphicsPipelineState* GraphicsState)
{
	@autoreleasepool {
		FMetalGraphicsPipelineState* PipelineState = ResourceCast(GraphicsState);
		if (SafeGetRuntimeDebuggingLevel() >= EMetalDebugLevelResetOnBind && Context->GetCurrentState().GetGraphicsPSO() != PipelineState)
		{
			Context->GetCurrentRenderPass().GetCurrentCommandEncoder().ResetLive();
		}
		Context->GetCurrentState().SetGraphicsPipelineState(PipelineState);
		// Bad Arne!
		RHISetStencilRef(0);
		RHISetBlendFactor(FLinearColor(1.0f, 1.0f, 1.0f));
	}
}

void FMetalRHICommandContext::RHISetUAVParameter(FRHIComputeShader* ComputeShaderRHI, uint32 UAVIndex, FRHIUnorderedAccessView* UAVRHI)
{
	@autoreleasepool {
	FMetalUnorderedAccessView* UAV = ResourceCast(UAVRHI);
	Context->GetCurrentState().SetShaderUnorderedAccessView(EMetalShaderStages::Compute, UAVIndex, UAV);
	}
}

void FMetalRHICommandContext::RHISetUAVParameter(FRHIComputeShader* ComputeShaderRHI,uint32 UAVIndex, FRHIUnorderedAccessView* UAVRHI, uint32 InitialCount)
{
	@autoreleasepool {
		FMetalUnorderedAccessView* UAV = ResourceCast(UAVRHI);
		Context->GetCurrentState().SetShaderUnorderedAccessView(EMetalShaderStages::Compute, UAVIndex, UAV);
	}
}


void FMetalRHICommandContext::RHISetShaderTexture(FRHIVertexShader* VertexShaderRHI, uint32 TextureIndex, FRHITexture* NewTextureRHI)
{
	@autoreleasepool {
	FMetalSurface* Surface = GetMetalSurfaceFromRHITexture(NewTextureRHI);
	if (Surface != nullptr)
	{
        if (Surface->Texture || !(Surface->Flags & TexCreate_Presentable))
        {
            Context->GetCurrentState().SetShaderTexture(EMetalShaderStages::Vertex, Surface->Texture, TextureIndex, (mtlpp::ResourceUsage)(mtlpp::ResourceUsage::Read|mtlpp::ResourceUsage::Sample));
        }
        else
        {
            FMetalTexture Tex = Surface->GetCurrentTexture();
            Context->GetCurrentState().SetShaderTexture(EMetalShaderStages::Vertex, Tex, TextureIndex, (mtlpp::ResourceUsage)(mtlpp::ResourceUsage::Read|mtlpp::ResourceUsage::Sample));
        }
	}
	else
	{
		Context->GetCurrentState().SetShaderTexture(EMetalShaderStages::Vertex, nil, TextureIndex, mtlpp::ResourceUsage(0));
	}
	}
}

void FMetalRHICommandContext::RHISetShaderTexture(FRHIHullShader* HullShader, uint32 TextureIndex, FRHITexture* NewTextureRHI)
{
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
	@autoreleasepool {
	FMetalSurface* Surface = GetMetalSurfaceFromRHITexture(NewTextureRHI);
	if (Surface != nullptr)
    {
        if (Surface->Texture || !(Surface->Flags & TexCreate_Presentable))
        {
            Context->GetCurrentState().SetShaderTexture(EMetalShaderStages::Hull, Surface->Texture, TextureIndex, (mtlpp::ResourceUsage)(mtlpp::ResourceUsage::Read|mtlpp::ResourceUsage::Sample));
        }
        else
        {
            FMetalTexture Tex = Surface->GetCurrentTexture();
            Context->GetCurrentState().SetShaderTexture(EMetalShaderStages::Hull, Tex, TextureIndex, (mtlpp::ResourceUsage)(mtlpp::ResourceUsage::Read|mtlpp::ResourceUsage::Sample));
        }
	}
	else
	{
		Context->GetCurrentState().SetShaderTexture(EMetalShaderStages::Hull, nil, TextureIndex, mtlpp::ResourceUsage(0));
	}
	}
#endif
}

void FMetalRHICommandContext::RHISetShaderTexture(FRHIDomainShader* DomainShader, uint32 TextureIndex, FRHITexture* NewTextureRHI)
{
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
	@autoreleasepool {
	FMetalSurface* Surface = GetMetalSurfaceFromRHITexture(NewTextureRHI);
	if (Surface != nullptr)
    {
        if (Surface->Texture || !(Surface->Flags & TexCreate_Presentable))
        {
            Context->GetCurrentState().SetShaderTexture(EMetalShaderStages::Domain, Surface->Texture, TextureIndex, (mtlpp::ResourceUsage)(mtlpp::ResourceUsage::Read|mtlpp::ResourceUsage::Sample));
        }
        else
        {
            FMetalTexture Tex = Surface->GetCurrentTexture();
            Context->GetCurrentState().SetShaderTexture(EMetalShaderStages::Domain, Tex, TextureIndex, (mtlpp::ResourceUsage)(mtlpp::ResourceUsage::Read|mtlpp::ResourceUsage::Sample));
        }
	}
	else
	{
		Context->GetCurrentState().SetShaderTexture(EMetalShaderStages::Domain, nil, TextureIndex, mtlpp::ResourceUsage(0));
	}
	}
#endif
}

void FMetalRHICommandContext::RHISetShaderTexture(FRHIGeometryShader* GeometryShader, uint32 TextureIndex, FRHITexture* NewTextureRHI)
{
	NOT_SUPPORTED("RHISetShaderTexture-Geometry");

}

void FMetalRHICommandContext::RHISetShaderTexture(FRHIPixelShader* PixelShader, uint32 TextureIndex, FRHITexture* NewTextureRHI)
{
	@autoreleasepool {
	FMetalSurface* Surface = GetMetalSurfaceFromRHITexture(NewTextureRHI);
	if (Surface != nullptr)
    {
        if (Surface->Texture || !(Surface->Flags & TexCreate_Presentable))
        {
            Context->GetCurrentState().SetShaderTexture(EMetalShaderStages::Pixel, Surface->Texture, TextureIndex, (mtlpp::ResourceUsage)(mtlpp::ResourceUsage::Read|mtlpp::ResourceUsage::Sample));
        }
        else
        {
            FMetalTexture Tex = Surface->GetCurrentTexture();
            Context->GetCurrentState().SetShaderTexture(EMetalShaderStages::Pixel, Tex, TextureIndex, (mtlpp::ResourceUsage)(mtlpp::ResourceUsage::Read|mtlpp::ResourceUsage::Sample));
        }
	}
	else
	{
		Context->GetCurrentState().SetShaderTexture(EMetalShaderStages::Pixel, nil, TextureIndex, mtlpp::ResourceUsage(0));
	}
	}
}

void FMetalRHICommandContext::RHISetShaderTexture(FRHIComputeShader* ComputeShader, uint32 TextureIndex, FRHITexture* NewTextureRHI)
{
	@autoreleasepool {
	FMetalSurface* Surface = GetMetalSurfaceFromRHITexture(NewTextureRHI);
	if (Surface != nullptr)
    {
        if (Surface->Texture || !(Surface->Flags & TexCreate_Presentable))
        {
            Context->GetCurrentState().SetShaderTexture(EMetalShaderStages::Compute, Surface->Texture, TextureIndex, (mtlpp::ResourceUsage)(mtlpp::ResourceUsage::Read|mtlpp::ResourceUsage::Sample));
        }
        else
        {
            FMetalTexture Tex = Surface->GetCurrentTexture();
            Context->GetCurrentState().SetShaderTexture(EMetalShaderStages::Compute, Tex, TextureIndex, (mtlpp::ResourceUsage)(mtlpp::ResourceUsage::Read|mtlpp::ResourceUsage::Sample));
        }
	}
	else
	{
		Context->GetCurrentState().SetShaderTexture(EMetalShaderStages::Compute, nil, TextureIndex, mtlpp::ResourceUsage(0));
	}
	}
}


void FMetalRHICommandContext::RHISetShaderResourceViewParameter(FRHIVertexShader* VertexShaderRHI, uint32 TextureIndex, FRHIShaderResourceView* SRVRHI)
{
	@autoreleasepool {
	FMetalShaderResourceView* SRV = ResourceCast(SRVRHI);
	Context->GetCurrentState().SetShaderResourceView(Context, EMetalShaderStages::Vertex, TextureIndex, SRV);
	}
}

void FMetalRHICommandContext::RHISetShaderResourceViewParameter(FRHIHullShader* HullShaderRHI,uint32 TextureIndex, FRHIShaderResourceView* SRVRHI)
{
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
	@autoreleasepool {
	FMetalShaderResourceView* SRV = ResourceCast(SRVRHI);
	Context->GetCurrentState().SetShaderResourceView(Context, EMetalShaderStages::Hull, TextureIndex, SRV);
	}
#endif
}

void FMetalRHICommandContext::RHISetShaderResourceViewParameter(FRHIDomainShader* DomainShaderRHI,uint32 TextureIndex, FRHIShaderResourceView* SRVRHI)
{
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
	@autoreleasepool {
	FMetalShaderResourceView* SRV = ResourceCast(SRVRHI);
	Context->GetCurrentState().SetShaderResourceView(Context, EMetalShaderStages::Domain, TextureIndex, SRV);
	}
#endif
}

void FMetalRHICommandContext::RHISetShaderResourceViewParameter(FRHIGeometryShader* GeometryShaderRHI,uint32 TextureIndex, FRHIShaderResourceView* SRVRHI)
{
	NOT_SUPPORTED("RHISetShaderResourceViewParameter");
}

void FMetalRHICommandContext::RHISetShaderResourceViewParameter(FRHIPixelShader* PixelShaderRHI,uint32 TextureIndex, FRHIShaderResourceView* SRVRHI)
{
	@autoreleasepool {
	FMetalShaderResourceView* SRV = ResourceCast(SRVRHI);
	Context->GetCurrentState().SetShaderResourceView(Context, EMetalShaderStages::Pixel, TextureIndex, SRV);
	}
}

void FMetalRHICommandContext::RHISetShaderResourceViewParameter(FRHIComputeShader* ComputeShaderRHI,uint32 TextureIndex, FRHIShaderResourceView* SRVRHI)
{
	@autoreleasepool {
	FMetalShaderResourceView* SRV = ResourceCast(SRVRHI);
	Context->GetCurrentState().SetShaderResourceView(Context, EMetalShaderStages::Compute, TextureIndex, SRV);
	}
}


void FMetalRHICommandContext::RHISetShaderSampler(FRHIVertexShader* VertexShaderRHI, uint32 SamplerIndex, FRHISamplerState* NewStateRHI)
{
	@autoreleasepool {
	FMetalSamplerState* NewState = ResourceCast(NewStateRHI);

	Context->GetCurrentState().SetShaderSamplerState(EMetalShaderStages::Vertex, NewState, SamplerIndex);
	}
}

void FMetalRHICommandContext::RHISetShaderSampler(FRHIHullShader* HullShader, uint32 SamplerIndex, FRHISamplerState* NewStateRHI)
{
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
	@autoreleasepool {
	FMetalSamplerState* NewState = ResourceCast(NewStateRHI);

	Context->GetCurrentState().SetShaderSamplerState(EMetalShaderStages::Hull, NewState, SamplerIndex);
	}
#endif
}

void FMetalRHICommandContext::RHISetShaderSampler(FRHIDomainShader* DomainShader, uint32 SamplerIndex, FRHISamplerState* NewStateRHI)
{
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
	@autoreleasepool {
	FMetalSamplerState* NewState = ResourceCast(NewStateRHI);

	Context->GetCurrentState().SetShaderSamplerState(EMetalShaderStages::Domain, NewState, SamplerIndex);
	}
#endif
}

void FMetalRHICommandContext::RHISetShaderSampler(FRHIGeometryShader* GeometryShader, uint32 SamplerIndex, FRHISamplerState* NewStateRHI)
{
	NOT_SUPPORTED("RHISetSamplerState-Geometry");
}

void FMetalRHICommandContext::RHISetShaderSampler(FRHIPixelShader* PixelShader, uint32 SamplerIndex, FRHISamplerState* NewStateRHI)
{
	@autoreleasepool {
	FMetalSamplerState* NewState = ResourceCast(NewStateRHI);

	Context->GetCurrentState().SetShaderSamplerState(EMetalShaderStages::Pixel, NewState, SamplerIndex);
	}
}

void FMetalRHICommandContext::RHISetShaderSampler(FRHIComputeShader* ComputeShader, uint32 SamplerIndex, FRHISamplerState* NewStateRHI)
{
	@autoreleasepool {
	FMetalSamplerState* NewState = ResourceCast(NewStateRHI);

	Context->GetCurrentState().SetShaderSamplerState(EMetalShaderStages::Compute, NewState, SamplerIndex);
	}
}

void FMetalRHICommandContext::RHISetShaderParameter(FRHIVertexShader* VertexShaderRHI, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue)
{
	@autoreleasepool {
	Context->GetCurrentState().GetShaderParameters(EMetalShaderStages::Vertex).Set(BufferIndex, BaseIndex, NumBytes, NewValue);
	}
}

void FMetalRHICommandContext::RHISetShaderParameter(FRHIHullShader* HullShaderRHI, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue)
{
	// Just ignore Hull shader parameter sets - none of our Hull shaders have any loose parameters to bind.
	// @todo Whenever we do put a shader parameter into a hull shader we'll need to map it into the vertex-shader parameter buffer so that it can be set on the device.
}

void FMetalRHICommandContext::RHISetShaderParameter(FRHIPixelShader* PixelShaderRHI, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue)
{
	@autoreleasepool {
	Context->GetCurrentState().GetShaderParameters(EMetalShaderStages::Pixel).Set(BufferIndex, BaseIndex, NumBytes, NewValue);
	}
}

void FMetalRHICommandContext::RHISetShaderParameter(FRHIDomainShader* DomainShaderRHI, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue)
{
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
	@autoreleasepool {
	Context->GetCurrentState().GetShaderParameters(EMetalShaderStages::Domain).Set(BufferIndex, BaseIndex, NumBytes, NewValue);
	}
#endif
}

void FMetalRHICommandContext::RHISetShaderParameter(FRHIGeometryShader* GeometryShaderRHI, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue)
{
	NOT_SUPPORTED("RHISetShaderParameter-Geometry");
}

void FMetalRHICommandContext::RHISetShaderParameter(FRHIComputeShader* ComputeShaderRHI,uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue)
{
	@autoreleasepool {
	Context->GetCurrentState().GetShaderParameters(EMetalShaderStages::Compute).Set(BufferIndex, BaseIndex, NumBytes, NewValue);
	}
}

void FMetalRHICommandContext::RHISetShaderUniformBuffer(FRHIVertexShader* VertexShaderRHI, uint32 BufferIndex, FRHIUniformBuffer* BufferRHI)
{
	@autoreleasepool {
	FMetalVertexShader* VertexShader = ResourceCast(VertexShaderRHI);
	Context->GetCurrentState().BindUniformBuffer(EMetalShaderStages::Vertex, BufferIndex, BufferRHI);

	auto& Bindings = VertexShader->Bindings;
	check(BufferIndex < Bindings.NumUniformBuffers);
	if (Bindings.ConstantBuffers & 1 << BufferIndex)
	{
		auto* UB = (FMetalUniformBuffer*)BufferRHI;
		Context->GetCurrentState().SetShaderBuffer(EMetalShaderStages::Vertex, (Bindings.ArgumentBuffers & 1 << BufferIndex) ? UB->GetIAB().IndirectArgumentBuffer : UB->Buffer, UB->Data, 0, UB->GetSize(), BufferIndex, mtlpp::ResourceUsage::Read);
	}
	}
}

void FMetalRHICommandContext::RHISetShaderUniformBuffer(FRHIHullShader* HullShaderRHI, uint32 BufferIndex, FRHIUniformBuffer* BufferRHI)
{
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
	@autoreleasepool {
	FMetalHullShader* HullShader = ResourceCast(HullShaderRHI);
	Context->GetCurrentState().BindUniformBuffer(EMetalShaderStages::Hull, BufferIndex, BufferRHI);

	auto& Bindings = HullShader->Bindings;
	check(BufferIndex < Bindings.NumUniformBuffers);
	if (Bindings.ConstantBuffers & 1 << BufferIndex)
	{
		auto* UB = (FMetalUniformBuffer*)BufferRHI;
		Context->GetCurrentState().SetShaderBuffer(EMetalShaderStages::Hull, (Bindings.ArgumentBuffers & 1 << BufferIndex) ? UB->GetIAB().IndirectArgumentBuffer : UB->Buffer, UB->Data, 0, UB->GetSize(), BufferIndex, mtlpp::ResourceUsage::Read);
	}
	}
#endif
}

void FMetalRHICommandContext::RHISetShaderUniformBuffer(FRHIDomainShader* DomainShaderRHI, uint32 BufferIndex, FRHIUniformBuffer* BufferRHI)
{
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
	@autoreleasepool {
	FMetalDomainShader* DomainShader = ResourceCast(DomainShaderRHI);
	Context->GetCurrentState().BindUniformBuffer(EMetalShaderStages::Domain, BufferIndex, BufferRHI);

	auto& Bindings = DomainShader->Bindings;
	check(BufferIndex < Bindings.NumUniformBuffers);
	if (Bindings.ConstantBuffers & 1 << BufferIndex)
	{
		auto* UB = (FMetalUniformBuffer*)BufferRHI;
		Context->GetCurrentState().SetShaderBuffer(EMetalShaderStages::Domain, (Bindings.ArgumentBuffers & 1 << BufferIndex) ? UB->GetIAB().IndirectArgumentBuffer : UB->Buffer, UB->Data, 0, UB->GetSize(), BufferIndex, mtlpp::ResourceUsage::Read);
	}
	}
#endif
}

void FMetalRHICommandContext::RHISetShaderUniformBuffer(FRHIGeometryShader* GeometryShader, uint32 BufferIndex, FRHIUniformBuffer* BufferRHI)
{
	NOT_SUPPORTED("RHISetShaderUniformBuffer-Geometry");
}

void FMetalRHICommandContext::RHISetShaderUniformBuffer(FRHIPixelShader* PixelShaderRHI, uint32 BufferIndex, FRHIUniformBuffer* BufferRHI)
{
	@autoreleasepool {
	FMetalPixelShader* PixelShader = ResourceCast(PixelShaderRHI);
	Context->GetCurrentState().BindUniformBuffer(EMetalShaderStages::Pixel, BufferIndex, BufferRHI);

	auto& Bindings = PixelShader->Bindings;
	check(BufferIndex < Bindings.NumUniformBuffers);
	if (Bindings.ConstantBuffers & 1 << BufferIndex)
	{
		auto* UB = (FMetalUniformBuffer*)BufferRHI;
		Context->GetCurrentState().SetShaderBuffer(EMetalShaderStages::Pixel, (Bindings.ArgumentBuffers & 1 << BufferIndex) ? UB->GetIAB().IndirectArgumentBuffer : UB->Buffer, UB->Data, 0, UB->GetSize(), BufferIndex, mtlpp::ResourceUsage::Read);
	}
	}
}

void FMetalRHICommandContext::RHISetShaderUniformBuffer(FRHIComputeShader* ComputeShaderRHI, uint32 BufferIndex, FRHIUniformBuffer* BufferRHI)
{
	@autoreleasepool {
	FMetalComputeShader* ComputeShader = ResourceCast(ComputeShaderRHI);
	Context->GetCurrentState().BindUniformBuffer(EMetalShaderStages::Compute, BufferIndex, BufferRHI);

	auto& Bindings = ComputeShader->Bindings;
	check(BufferIndex < Bindings.NumUniformBuffers);
	if (Bindings.ConstantBuffers & 1 << BufferIndex)
	{
		auto* UB = (FMetalUniformBuffer*)BufferRHI;
		Context->GetCurrentState().SetShaderBuffer(EMetalShaderStages::Compute, (Bindings.ArgumentBuffers & 1 << BufferIndex) ? UB->GetIAB().IndirectArgumentBuffer : UB->Buffer, UB->Data, 0, UB->GetSize(), BufferIndex, mtlpp::ResourceUsage::Read);
	}
	}
}


void FMetalRHICommandContext::RHISetStencilRef(uint32 StencilRef)
{
	Context->GetCurrentState().SetStencilRef(StencilRef);
}

void FMetalRHICommandContext::RHISetBlendFactor(const FLinearColor& BlendFactor)
{
	Context->GetCurrentState().SetBlendFactor(BlendFactor);
}

void FMetalRHICommandContext::RHISetRenderTargets(uint32 NumSimultaneousRenderTargets, const FRHIRenderTargetView* NewRenderTargets,
	const FRHIDepthRenderTargetView* NewDepthStencilTargetRHI, uint32 NumUAVs, FRHIUnorderedAccessView* const* UAVs)
{
	@autoreleasepool {
	FMetalContext* Manager = Context;
	FRHIDepthRenderTargetView DepthView;
	if (NewDepthStencilTargetRHI)
	{
		DepthView = *NewDepthStencilTargetRHI;
	}
	else
	{
		DepthView = FRHIDepthRenderTargetView(nullptr, ERenderTargetLoadAction::EClear, ERenderTargetStoreAction::ENoAction);
	}

	FRHISetRenderTargetsInfo Info(NumSimultaneousRenderTargets, NewRenderTargets, DepthView);
	Info.NumUAVs = NumUAVs;
	for (uint32 i = 0; i < NumUAVs; i++)
	{
		Info.UnorderedAccessView[i] = UAVs[i];
	}
	RHISetRenderTargetsAndClear(Info);
	}
}

void FMetalRHICommandContext::RHISetRenderTargetsAndClear(const FRHISetRenderTargetsInfo& RenderTargetsInfo)
{
	@autoreleasepool {
		
	FRHIRenderPassInfo PassInfo;
	bool bHasTarget = (RenderTargetsInfo.DepthStencilRenderTarget.Texture != nullptr);
	FMetalContext* Manager = Context;
	{
		PassInfo.NumUAVs = RenderTargetsInfo.NumUAVs;
		for (uint32 i = 0; i < RenderTargetsInfo.NumUAVs; i++)
		{
			if (IsValidRef(RenderTargetsInfo.UnorderedAccessView[i]))
			{
				PassInfo.UAVs[i] = RenderTargetsInfo.UnorderedAccessView[i];
				bHasTarget = true;
			}
		}
	}
	
	for (uint32 i = 0; i < RenderTargetsInfo.NumColorRenderTargets; i++)
	{
		if (RenderTargetsInfo.ColorRenderTarget[i].Texture)
		{
			PassInfo.ColorRenderTargets[i].RenderTarget = RenderTargetsInfo.ColorRenderTarget[i].Texture;
			PassInfo.ColorRenderTargets[i].ArraySlice = RenderTargetsInfo.ColorRenderTarget[i].ArraySliceIndex;
			PassInfo.ColorRenderTargets[i].MipIndex = RenderTargetsInfo.ColorRenderTarget[i].MipIndex;
			PassInfo.ColorRenderTargets[i].Action = MakeRenderTargetActions(RenderTargetsInfo.ColorRenderTarget[i].LoadAction, RenderTargetsInfo.ColorRenderTarget[i].StoreAction);
		bHasTarget = (RenderTargetsInfo.ColorRenderTarget[i].Texture != nullptr);
			PassInfo.bIsMSAA |= PassInfo.ColorRenderTargets[i].RenderTarget->GetNumSamples() > 1;
		}
	}
		
	if (RenderTargetsInfo.DepthStencilRenderTarget.Texture)
	{
		PassInfo.DepthStencilRenderTarget.DepthStencilTarget = RenderTargetsInfo.DepthStencilRenderTarget.Texture;
		PassInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = RenderTargetsInfo.DepthStencilRenderTarget.GetDepthStencilAccess();
		PassInfo.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(MakeRenderTargetActions(RenderTargetsInfo.DepthStencilRenderTarget.DepthLoadAction, RenderTargetsInfo.DepthStencilRenderTarget.DepthStoreAction), MakeRenderTargetActions(RenderTargetsInfo.DepthStencilRenderTarget.StencilLoadAction, RenderTargetsInfo.DepthStencilRenderTarget.GetStencilStoreAction()));
		PassInfo.bIsMSAA |= RenderTargetsInfo.DepthStencilRenderTarget.Texture->GetNumSamples() > 1;
	}
		
	PassInfo.NumOcclusionQueries = UINT16_MAX;
	PassInfo.bOcclusionQueries = true;

	// Ignore any attempt to "clear" the render-targets as that is senseless with the way MetalRHI has to try and coalesce passes.
	if (bHasTarget)
	{
		Manager->SetRenderPassInfo(PassInfo);

		// Set the viewport to the full size of render target 0.
		if (RenderTargetsInfo.ColorRenderTarget[0].Texture)
		{
			const FRHIRenderTargetView& RenderTargetView = RenderTargetsInfo.ColorRenderTarget[0];
			FMetalSurface* RenderTarget = GetMetalSurfaceFromRHITexture(RenderTargetView.Texture);

			uint32 Width = FMath::Max((uint32)(RenderTarget->Texture.GetWidth() >> RenderTargetView.MipIndex), (uint32)1);
			uint32 Height = FMath::Max((uint32)(RenderTarget->Texture.GetHeight() >> RenderTargetView.MipIndex), (uint32)1);

			RHISetViewport(0, 0, 0.0f, Width, Height, 1.0f);
		}
	}
	}
}


void FMetalRHICommandContext::RHIDrawPrimitive(uint32 BaseVertexIndex, uint32 NumPrimitives, uint32 NumInstances)
{
	@autoreleasepool {
	SCOPE_CYCLE_COUNTER(STAT_MetalDrawCallTime);
		
	uint32 PrimitiveType = Context->GetCurrentState().GetPrimitiveType();
	
		
	//checkf(NumInstances == 1, TEXT("Currently only 1 instance is supported"));
	
	NumInstances = FMath::Max(NumInstances,1u);
	
	RHI_DRAW_CALL_STATS(PrimitiveType,NumInstances*NumPrimitives);

	// how many verts to render
	uint32 NumVertices = GetVertexCountForPrimitiveCount(NumPrimitives, PrimitiveType);
	uint32 VertexCount = GetVertexCountForPrimitiveCount(NumPrimitives,PrimitiveType);
	
	Context->DrawPrimitive(PrimitiveType, BaseVertexIndex, NumPrimitives, NumInstances);
	}
}

void FMetalRHICommandContext::RHIDrawPrimitiveIndirect(FRHIVertexBuffer* VertexBufferRHI, uint32 ArgumentOffset)
{
	@autoreleasepool {
#if PLATFORM_IOS
	NOT_SUPPORTED("RHIDrawPrimitiveIndirect");
#else
	SCOPE_CYCLE_COUNTER(STAT_MetalDrawCallTime);
	uint32 PrimitiveType = Context->GetCurrentState().GetPrimitiveType();
	
	
	RHI_DRAW_CALL_STATS(PrimitiveType,1);
	FMetalVertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);
	
	Context->DrawPrimitiveIndirect(PrimitiveType, VertexBuffer, ArgumentOffset);
#endif
	}
}

void FMetalRHICommandContext::RHIDrawIndexedPrimitive(FRHIIndexBuffer* IndexBufferRHI, int32 BaseVertexIndex, uint32 FirstInstance,
	uint32 NumVertices, uint32 StartIndex, uint32 NumPrimitives, uint32 NumInstances)
{
	@autoreleasepool {
	SCOPE_CYCLE_COUNTER(STAT_MetalDrawCallTime);
	//checkf(NumInstances == 1, TEXT("Currently only 1 instance is supported"));
	checkf(GRHISupportsBaseVertexIndex || BaseVertexIndex == 0, TEXT("BaseVertexIndex must be 0, see GRHISupportsBaseVertexIndex"));
	checkf(GRHISupportsFirstInstance || FirstInstance == 0, TEXT("FirstInstance must be 0, see GRHISupportsFirstInstance"));
	uint32 PrimitiveType = Context->GetCurrentState().GetPrimitiveType();
	
		
	RHI_DRAW_CALL_STATS(PrimitiveType,FMath::Max(NumInstances,1u)*NumPrimitives);

	FMetalIndexBuffer* IndexBuffer = ResourceCast(IndexBufferRHI);
	Context->DrawIndexedPrimitive(IndexBuffer->Buffer, IndexBuffer->GetStride(), IndexBuffer->IndexType, PrimitiveType, BaseVertexIndex, FirstInstance, NumVertices, StartIndex, NumPrimitives, NumInstances);
	}
}

void FMetalRHICommandContext::RHIDrawIndexedIndirect(FRHIIndexBuffer* IndexBufferRHI, FRHIStructuredBuffer* VertexBufferRHI, int32 DrawArgumentsIndex, uint32 NumInstances)
{
	@autoreleasepool {
	if (GetMetalDeviceContext().SupportsFeature(EMetalFeaturesIndirectBuffer))
	{
		check(NumInstances > 1);
		
		SCOPE_CYCLE_COUNTER(STAT_MetalDrawCallTime);
		
		uint32 PrimitiveType = Context->GetCurrentState().GetPrimitiveType();
		

		RHI_DRAW_CALL_STATS(PrimitiveType,1);
		FMetalIndexBuffer* IndexBuffer = ResourceCast(IndexBufferRHI);
		FMetalStructuredBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);
		
		Context->DrawIndexedIndirect(IndexBuffer, PrimitiveType, VertexBuffer, DrawArgumentsIndex, NumInstances);
	}
	else
	{
		NOT_SUPPORTED("RHIDrawIndexedIndirect");
	}
	}
}

void FMetalRHICommandContext::RHIDrawIndexedPrimitiveIndirect(FRHIIndexBuffer* IndexBufferRHI, FRHIVertexBuffer* VertexBufferRHI,uint32 ArgumentOffset)
{
	@autoreleasepool {
	if (GetMetalDeviceContext().SupportsFeature(EMetalFeaturesIndirectBuffer))
	{
		SCOPE_CYCLE_COUNTER(STAT_MetalDrawCallTime);
		
		uint32 PrimitiveType = Context->GetCurrentState().GetPrimitiveType();
		

		RHI_DRAW_CALL_STATS(PrimitiveType,1);
		FMetalIndexBuffer* IndexBuffer = ResourceCast(IndexBufferRHI);
		FMetalVertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);
		
		Context->DrawIndexedPrimitiveIndirect(PrimitiveType, IndexBuffer, VertexBuffer, ArgumentOffset);
	}
	else
	{
		NOT_SUPPORTED("RHIDrawIndexedPrimitiveIndirect");
	}
	}
}

void FMetalDynamicRHI::SetupRecursiveResources()
{
    /*
	@autoreleasepool {
	static bool bSetupResources = false;
	if (GRHISupportsRHIThread && !bSetupResources)
	{
		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
		extern int32 GCreateShadersOnLoad;
		TGuardValue<int32> Guard(GCreateShadersOnLoad, 1);
		auto ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<TOneColorVS<true> > DefaultVertexShader(ShaderMap);
		TShaderMapRef<TOneColorVS<true, true> > LayeredVertexShader(ShaderMap);
		GVector4VertexDeclaration.InitRHI();
		
		for (uint32 Instanced = 0; Instanced < 2; Instanced++)
		{
			FShader* VertexShader = !Instanced ? (FShader*)*DefaultVertexShader : (FShader*)*LayeredVertexShader;
			
			for (int32 NumBuffers = 1; NumBuffers <= MaxSimultaneousRenderTargets; NumBuffers++)
			{
				FOneColorPS* PixelShader = NULL;
				
				// Set the shader to write to the appropriate number of render targets
				// On AMD PC hardware, outputting to a color index in the shader without a matching render target set has a significant performance hit
				if (NumBuffers <= 1)
				{
					TShaderMapRef<TOneColorPixelShaderMRT<1> > MRTPixelShader(ShaderMap);
					PixelShader = *MRTPixelShader;
				}
				else if (IsFeatureLevelSupported( GMaxRHIShaderPlatform, ERHIFeatureLevel::SM4 ))
				{
					if (NumBuffers == 2)
					{
						TShaderMapRef<TOneColorPixelShaderMRT<2> > MRTPixelShader(ShaderMap);
						PixelShader = *MRTPixelShader;
					}
					else if (NumBuffers== 3)
					{
						TShaderMapRef<TOneColorPixelShaderMRT<3> > MRTPixelShader(ShaderMap);
						PixelShader = *MRTPixelShader;
					}
					else if (NumBuffers == 4)
					{
						TShaderMapRef<TOneColorPixelShaderMRT<4> > MRTPixelShader(ShaderMap);
						PixelShader = *MRTPixelShader;
					}
					else if (NumBuffers == 5)
					{
						TShaderMapRef<TOneColorPixelShaderMRT<5> > MRTPixelShader(ShaderMap);
						PixelShader = *MRTPixelShader;
					}
					else if (NumBuffers == 6)
					{
						TShaderMapRef<TOneColorPixelShaderMRT<6> > MRTPixelShader(ShaderMap);
						PixelShader = *MRTPixelShader;
					}
					else if (NumBuffers == 7)
					{
						TShaderMapRef<TOneColorPixelShaderMRT<7> > MRTPixelShader(ShaderMap);
						PixelShader = *MRTPixelShader;
					}
					else if (NumBuffers == 8)
					{
						TShaderMapRef<TOneColorPixelShaderMRT<8> > MRTPixelShader(ShaderMap);
						PixelShader = *MRTPixelShader;
					}
				}
				
				// SetGlobalBoundShaderState(RHICmdList, GMaxRHIFeatureLevel, GClearMRTBoundShaderState[NumBuffers - 1][Instanced], GVector4VertexDeclaration.VertexDeclarationRHI, VertexShader, PixelShader);
			}
		}
		
		bSetupResources = true;
	}
	}
    */
}

void FMetalRHICommandContext::RHIClearMRT(bool bClearColor,int32 NumClearColors,const FLinearColor* ClearColorArray,bool bClearDepth,float Depth,bool bClearStencil,uint32 Stencil)
{
	NOT_SUPPORTED("RHIClearMRT");
}

void FMetalDynamicRHI::RHIBlockUntilGPUIdle()
{
	@autoreleasepool {
	ImmediateContext.Context->SubmitCommandBufferAndWait();
	}
}

uint32 FMetalDynamicRHI::RHIGetGPUFrameCycles()
{
	return GGPUFrameTime;
}

void FMetalRHICommandContext::RHIAutomaticCacheFlushAfterComputeShader(bool bEnable)
{
	METAL_IGNORED(FMetalRHICommandContextRHIAutomaticCacheFlushAfterComputeShader);
}

void FMetalRHICommandContext::RHIFlushComputeShaderCache()
{
	METAL_IGNORED(FMetalRHICommandContextRHIFlushComputeShaderCache);
}

void FMetalDynamicRHI::RHIExecuteCommandList(FRHICommandList* RHICmdList)
{
	NOT_SUPPORTED("RHIExecuteCommandList");
}

void FMetalRHICommandContext::RHISetDepthBounds(float MinDepth, float MaxDepth)
{
	METAL_IGNORED(FMetalRHICommandContextSetDepthBounds);
}

void FMetalRHICommandContext::RHISubmitCommandsHint()
{
	@autoreleasepool {
    Context->SubmitCommandsHint();
	}
}

void FMetalRHICommandContext::RHIDiscardRenderTargets(bool Depth, bool Stencil, uint32 ColorBitMask)
{
	Context->GetCurrentState().DiscardRenderTargets(Depth, Stencil, ColorBitMask);
}

IRHICommandContext* FMetalDynamicRHI::RHIGetDefaultContext()
{
	return &ImmediateContext;
}

IRHIComputeContext* FMetalDynamicRHI::RHIGetDefaultAsyncComputeContext()
{
	@autoreleasepool {
	IRHIComputeContext* ComputeContext = GSupportsEfficientAsyncCompute && AsyncComputeContext ? AsyncComputeContext : RHIGetDefaultContext();
	// On platforms that support non-async compute we set this to the normal context.  It won't be async, but the high level
	// code can be agnostic if it wants to be.
	return ComputeContext;
	}
}
