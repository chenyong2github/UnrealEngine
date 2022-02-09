// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXCommands.cpp: AGX RHI commands implementation.
=============================================================================*/

#include "AGXRHIPrivate.h"
#include "AGXShaderTypes.h"
#include "AGXGraphicsPipelineState.h"
#include "AGXComputePipelineState.h"
#include "GlobalShader.h"
#include "OneColorShader.h"
#include "RHICommandList.h"
#include "RHIStaticStates.h"
#include "ShaderParameterUtils.h"
#include "SceneUtils.h"
#include "AGXProfiler.h"
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
		Elements.Add(FVertexElement(0, 0, VET_Float4, 0, sizeof(FVector4f)));
		VertexDeclarationRHI = PipelineStateCache::GetOrCreateVertexDeclaration(Elements);
	}
	virtual void ReleaseRHI() override
	{
		VertexDeclarationRHI.SafeRelease();
	}
};
static TGlobalResource<FVector4VertexDeclaration> FVector4VertexDeclaration;

mtlpp::PrimitiveType AGXTranslatePrimitiveType(uint32 PrimitiveType)
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
			return mtlpp::PrimitiveType::Triangle;
		}
		default:
			METAL_FATAL_ERROR(TEXT("Unsupported primitive type %d"), (int32)PrimitiveType);
			return mtlpp::PrimitiveType::Triangle;
	}
}

static FORCEINLINE EAGXShaderStages GetShaderStage(FRHIGraphicsShader* ShaderRHI)
{
	EAGXShaderStages Stage = EAGXShaderStages::Num;
	switch (ShaderRHI->GetFrequency())
	{
	case SF_Vertex:		Stage = EAGXShaderStages::Vertex; break;
	case SF_Pixel:		Stage = EAGXShaderStages::Pixel; break;
	default:
		checkf(0, TEXT("FRHIShader Type %d is invalid or unsupported!"), (int32)ShaderRHI->GetFrequency());
		NOT_SUPPORTED("RHIShaderStage");
		break;
	}

	return Stage;
}

void FAGXRHICommandContext::RHISetStreamSource(uint32 StreamIndex, FRHIBuffer* VertexBufferRHI,uint32 Offset)
{
	@autoreleasepool {
		FAGXVertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);
		
		FAGXBuffer TheBuffer = nil;
		if(VertexBuffer && !VertexBuffer->Data)
		{
			TheBuffer = VertexBuffer->GetCurrentBuffer();
		}
		
		Context->GetCurrentState().SetVertexStream(StreamIndex, VertexBuffer ? &TheBuffer : nil, VertexBuffer ? VertexBuffer->Data : nil, Offset, VertexBuffer ? VertexBuffer->GetSize() : 0);
	}
}

void FAGXRHICommandContext::RHISetComputeShader(FRHIComputeShader* ComputeShaderRHI)
{
	@autoreleasepool {
	FAGXComputeShader* ComputeShader = ResourceCast(ComputeShaderRHI);

	// cache this for Dispatch
	// sets this compute shader pipeline as the current (this resets all state, so we need to set all resources after calling this)
	Context->GetCurrentState().SetComputeShader(ComputeShader);

	ApplyStaticUniformBuffers(ComputeShader);
	}
}

void FAGXRHICommandContext::RHISetComputePipelineState(FRHIComputePipelineState* ComputePipelineState)
{
	@autoreleasepool {
	FAGXComputePipelineState* ComputePipeline = ResourceCast(ComputePipelineState);
	
	// cache this for Dispatch
	// sets this compute shader pipeline as the current (this resets all state, so we need to set all resources after calling this)
	Context->GetCurrentState().SetComputeShader(ComputePipeline->GetComputeShader());

	ApplyStaticUniformBuffers(ComputePipeline->GetComputeShader());
	}
}

void FAGXRHICommandContext::RHIDispatchComputeShader(uint32 ThreadGroupCountX, uint32 ThreadGroupCountY, uint32 ThreadGroupCountZ)
{
	@autoreleasepool {
	ThreadGroupCountX = FMath::Max(ThreadGroupCountX, 1u);
	ThreadGroupCountY = FMath::Max(ThreadGroupCountY, 1u);
	ThreadGroupCountZ = FMath::Max(ThreadGroupCountZ, 1u);
	
	Context->Dispatch(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
	}
}

void FAGXRHICommandContext::RHIDispatchIndirectComputeShader(FRHIBuffer* ArgumentBufferRHI, uint32 ArgumentOffset)
{
	@autoreleasepool {
	if (GetAGXDeviceContext().SupportsFeature(EAGXFeaturesIndirectBuffer))
	{
		FAGXVertexBuffer* VertexBuffer = ResourceCast(ArgumentBufferRHI);
		
		Context->DispatchIndirect(VertexBuffer, ArgumentOffset);
	}
	else
	{
		NOT_SUPPORTED("RHIDispatchIndirectComputeShader");
	}
	}
}

void FAGXRHICommandContext::RHISetViewport(float MinX, float MinY,float MinZ, float MaxX, float MaxY,float MaxZ)
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

void FAGXRHICommandContext::RHISetStereoViewport(float LeftMinX, float RightMinX, float LeftMinY, float RightMinY, float MinZ, float LeftMaxX, float RightMaxX, float LeftMaxY, float RightMaxY, float MaxZ)
{
	if (FAGXCommandQueue::SupportsFeature(EAGXFeaturesMultipleViewports))
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

void FAGXRHICommandContext::RHISetMultipleViewports(uint32 Count, const FViewportBounds* Data)
{ 
	NOT_SUPPORTED("RHISetMultipleViewports");
}

void FAGXRHICommandContext::RHISetScissorRect(bool bEnable,uint32 MinX,uint32 MinY,uint32 MaxX,uint32 MaxY)
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

void FAGXRHICommandContext::RHISetGraphicsPipelineState(FRHIGraphicsPipelineState* GraphicsState, uint32 StencilRef, bool bApplyAdditionalState)
{
	@autoreleasepool {
		FAGXGraphicsPipelineState* PipelineState = ResourceCast(GraphicsState);
		if (AGXSafeGetRuntimeDebuggingLevel() >= EAGXDebugLevelResetOnBind && Context->GetCurrentState().GetGraphicsPSO() != PipelineState)
		{
			Context->GetCurrentRenderPass().GetCurrentCommandEncoder().ResetLive();
		}
		Context->GetCurrentState().SetGraphicsPipelineState(PipelineState);

		RHISetStencilRef(StencilRef);
		RHISetBlendFactor(FLinearColor(1.0f, 1.0f, 1.0f));

		if (bApplyAdditionalState)
		{
			ApplyStaticUniformBuffers(PipelineState->VertexShader.GetReference());
			ApplyStaticUniformBuffers(PipelineState->PixelShader.GetReference());
		}
	}
}

void FAGXRHICommandContext::RHISetStaticUniformBuffers(const FUniformBufferStaticBindings& InUniformBuffers)
{
	FMemory::Memzero(GlobalUniformBuffers.GetData(), GlobalUniformBuffers.Num() * sizeof(FRHIUniformBuffer*));

	for (int32 Index = 0; Index < InUniformBuffers.GetUniformBufferCount(); ++Index)
	{
		GlobalUniformBuffers[InUniformBuffers.GetSlot(Index)] = InUniformBuffers.GetUniformBuffer(Index);
	}
}

void FAGXRHICommandContext::RHISetUAVParameter(FRHIPixelShader* PixelShaderRHI, uint32 UAVIndex, FRHIUnorderedAccessView* UAVRHI)
{
	@autoreleasepool {
		FAGXUnorderedAccessView* UAV = ResourceCast(UAVRHI);
		Context->GetCurrentState().SetShaderUnorderedAccessView(EAGXShaderStages::Pixel, UAVIndex, UAV);
	}
}

void FAGXRHICommandContext::RHISetUAVParameter(FRHIComputeShader* ComputeShaderRHI, uint32 UAVIndex, FRHIUnorderedAccessView* UAVRHI)
{
	@autoreleasepool {
	FAGXUnorderedAccessView* UAV = ResourceCast(UAVRHI);
	Context->GetCurrentState().SetShaderUnorderedAccessView(EAGXShaderStages::Compute, UAVIndex, UAV);
	}
}

void FAGXRHICommandContext::RHISetUAVParameter(FRHIComputeShader* ComputeShaderRHI,uint32 UAVIndex, FRHIUnorderedAccessView* UAVRHI, uint32 InitialCount)
{
	@autoreleasepool {
		FAGXUnorderedAccessView* UAV = ResourceCast(UAVRHI);
		Context->GetCurrentState().SetShaderUnorderedAccessView(EAGXShaderStages::Compute, UAVIndex, UAV);
	}
}


void FAGXRHICommandContext::RHISetShaderTexture(FRHIGraphicsShader* ShaderRHI, uint32 TextureIndex, FRHITexture* NewTextureRHI)
{
	@autoreleasepool {
	FAGXSurface* Surface = AGXGetMetalSurfaceFromRHITexture(NewTextureRHI);
	EAGXShaderStages Stage = GetShaderStage(ShaderRHI);
	if (Surface != nullptr)
	{
        if (Surface->Texture || !(Surface->Flags & TexCreate_Presentable))
        {
            Context->GetCurrentState().SetShaderTexture(Stage, Surface->Texture, TextureIndex, (mtlpp::ResourceUsage)(mtlpp::ResourceUsage::Read|mtlpp::ResourceUsage::Sample));
        }
        else
        {
            FAGXTexture Tex = Surface->GetCurrentTexture();
            Context->GetCurrentState().SetShaderTexture(Stage, Tex, TextureIndex, (mtlpp::ResourceUsage)(mtlpp::ResourceUsage::Read|mtlpp::ResourceUsage::Sample));
        }
	}
	else
	{
		Context->GetCurrentState().SetShaderTexture(Stage, nil, TextureIndex, mtlpp::ResourceUsage(0));
	}
	}
}

void FAGXRHICommandContext::RHISetShaderTexture(FRHIComputeShader* ComputeShader, uint32 TextureIndex, FRHITexture* NewTextureRHI)
{
	@autoreleasepool {
	FAGXSurface* Surface = AGXGetMetalSurfaceFromRHITexture(NewTextureRHI);
	if (Surface != nullptr)
    {
        if (Surface->Texture || !(Surface->Flags & TexCreate_Presentable))
        {
            Context->GetCurrentState().SetShaderTexture(EAGXShaderStages::Compute, Surface->Texture, TextureIndex, (mtlpp::ResourceUsage)(mtlpp::ResourceUsage::Read|mtlpp::ResourceUsage::Sample));
        }
        else
        {
            FAGXTexture Tex = Surface->GetCurrentTexture();
            Context->GetCurrentState().SetShaderTexture(EAGXShaderStages::Compute, Tex, TextureIndex, (mtlpp::ResourceUsage)(mtlpp::ResourceUsage::Read|mtlpp::ResourceUsage::Sample));
        }
	}
	else
	{
		Context->GetCurrentState().SetShaderTexture(EAGXShaderStages::Compute, nil, TextureIndex, mtlpp::ResourceUsage(0));
	}
	}
}


void FAGXRHICommandContext::RHISetShaderResourceViewParameter(FRHIGraphicsShader* ShaderRHI, uint32 TextureIndex, FRHIShaderResourceView* SRVRHI)
{
	@autoreleasepool {
	FAGXShaderResourceView* SRV = ResourceCast(SRVRHI);
	EAGXShaderStages Stage = GetShaderStage(ShaderRHI);
	Context->GetCurrentState().SetShaderResourceView(Context, Stage, TextureIndex, SRV);
	}
}

void FAGXRHICommandContext::RHISetShaderResourceViewParameter(FRHIComputeShader* ComputeShaderRHI,uint32 TextureIndex, FRHIShaderResourceView* SRVRHI)
{
	@autoreleasepool {
	FAGXShaderResourceView* SRV = ResourceCast(SRVRHI);
	Context->GetCurrentState().SetShaderResourceView(Context, EAGXShaderStages::Compute, TextureIndex, SRV);
	}
}


void FAGXRHICommandContext::RHISetShaderSampler(FRHIGraphicsShader* ShaderRHI, uint32 SamplerIndex, FRHISamplerState* NewStateRHI)
{
	@autoreleasepool {
	FAGXSamplerState* NewState = ResourceCast(NewStateRHI);
	EAGXShaderStages Stage = GetShaderStage(ShaderRHI);
	Context->GetCurrentState().SetShaderSamplerState(Stage, NewState, SamplerIndex);
	}
}

void FAGXRHICommandContext::RHISetShaderSampler(FRHIComputeShader* ComputeShader, uint32 SamplerIndex, FRHISamplerState* NewStateRHI)
{
	@autoreleasepool {
	FAGXSamplerState* NewState = ResourceCast(NewStateRHI);

	Context->GetCurrentState().SetShaderSamplerState(EAGXShaderStages::Compute, NewState, SamplerIndex);
	}
}

void FAGXRHICommandContext::RHISetShaderParameter(FRHIGraphicsShader* ShaderRHI, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue)
{
	@autoreleasepool {
	EAGXShaderStages Stage = GetShaderStage(ShaderRHI);
	Context->GetCurrentState().GetShaderParameters(Stage).Set(BufferIndex, BaseIndex, NumBytes, NewValue);
	}
}

void FAGXRHICommandContext::RHISetShaderParameter(FRHIComputeShader* ComputeShaderRHI,uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue)
{
	@autoreleasepool {
	Context->GetCurrentState().GetShaderParameters(EAGXShaderStages::Compute).Set(BufferIndex, BaseIndex, NumBytes, NewValue);
	}
}

void FAGXRHICommandContext::RHISetStencilRef(uint32 StencilRef)
{
	Context->GetCurrentState().SetStencilRef(StencilRef);
}

void FAGXRHICommandContext::RHISetBlendFactor(const FLinearColor& BlendFactor)
{
	Context->GetCurrentState().SetBlendFactor(BlendFactor);
}

void FAGXRHICommandContext::SetRenderTargets(uint32 NumSimultaneousRenderTargets, const FRHIRenderTargetView* NewRenderTargets,
	const FRHIDepthRenderTargetView* NewDepthStencilTargetRHI)
{
	@autoreleasepool {
	FAGXContext* Manager = Context;
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
	SetRenderTargetsAndClear(Info);
	}
}

void FAGXRHICommandContext::SetRenderTargetsAndClear(const FRHISetRenderTargetsInfo& RenderTargetsInfo)
{
	@autoreleasepool {
		
	FRHIRenderPassInfo PassInfo;
	bool bHasTarget = (RenderTargetsInfo.DepthStencilRenderTarget.Texture != nullptr);
	FAGXContext* Manager = Context;
	
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

	// Ignore any attempt to "clear" the render-targets as that is senseless with the way AGXRHI has to try and coalesce passes.
	if (bHasTarget)
	{
		Manager->SetRenderPassInfo(PassInfo);

		// Set the viewport to the full size of render target 0.
		if (RenderTargetsInfo.ColorRenderTarget[0].Texture)
		{
			const FRHIRenderTargetView& RenderTargetView = RenderTargetsInfo.ColorRenderTarget[0];
			FAGXSurface* RenderTarget = AGXGetMetalSurfaceFromRHITexture(RenderTargetView.Texture);

			uint32 Width = FMath::Max((uint32)(RenderTarget->Texture.GetWidth() >> RenderTargetView.MipIndex), (uint32)1);
			uint32 Height = FMath::Max((uint32)(RenderTarget->Texture.GetHeight() >> RenderTargetView.MipIndex), (uint32)1);

			RHISetViewport(0, 0, 0.0f, Width, Height, 1.0f);
		}
	}
	}
}


void FAGXRHICommandContext::RHIDrawPrimitive(uint32 BaseVertexIndex, uint32 NumPrimitives, uint32 NumInstances)
{
	@autoreleasepool {
	SCOPE_CYCLE_COUNTER(STAT_AGXDrawCallTime);
		
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

void FAGXRHICommandContext::RHIDrawPrimitiveIndirect(FRHIBuffer* ArgumentBufferRHI, uint32 ArgumentOffset)
{
	@autoreleasepool {
        if (GetAGXDeviceContext().SupportsFeature(EAGXFeaturesIndirectBuffer))
        {
            SCOPE_CYCLE_COUNTER(STAT_AGXDrawCallTime);
            uint32 PrimitiveType = Context->GetCurrentState().GetPrimitiveType();
            
            RHI_DRAW_CALL_STATS(PrimitiveType,1);
            FAGXResourceMultiBuffer* ArgumentBuffer = ResourceCast(ArgumentBufferRHI);
            
            Context->DrawPrimitiveIndirect(PrimitiveType, ArgumentBuffer, ArgumentOffset);
        }
        else
        {
            NOT_SUPPORTED("RHIDrawPrimitiveIndirect");
        }
	}
}

void FAGXRHICommandContext::RHIDrawIndexedPrimitive(FRHIBuffer* IndexBufferRHI, int32 BaseVertexIndex, uint32 FirstInstance,
	uint32 NumVertices, uint32 StartIndex, uint32 NumPrimitives, uint32 NumInstances)
{
	@autoreleasepool {
	SCOPE_CYCLE_COUNTER(STAT_AGXDrawCallTime);
	//checkf(NumInstances == 1, TEXT("Currently only 1 instance is supported"));
	checkf(GRHISupportsBaseVertexIndex || BaseVertexIndex == 0, TEXT("BaseVertexIndex must be 0, see GRHISupportsBaseVertexIndex"));
	checkf(GRHISupportsFirstInstance || FirstInstance == 0, TEXT("FirstInstance must be 0, see GRHISupportsFirstInstance"));
	uint32 PrimitiveType = Context->GetCurrentState().GetPrimitiveType();
	
		
	RHI_DRAW_CALL_STATS(PrimitiveType,FMath::Max(NumInstances,1u)*NumPrimitives);

	FAGXResourceMultiBuffer* IndexBuffer = ResourceCast(IndexBufferRHI);
	Context->DrawIndexedPrimitive(IndexBuffer->GetCurrentBuffer(), IndexBuffer->GetStride(), IndexBuffer->IndexType, PrimitiveType, BaseVertexIndex, FirstInstance, NumVertices, StartIndex, NumPrimitives, NumInstances);
	}
}

void FAGXRHICommandContext::RHIDrawIndexedIndirect(FRHIBuffer* IndexBufferRHI, FRHIBuffer* ArgumentsBufferRHI, int32 DrawArgumentsIndex, uint32 NumInstances)
{
	@autoreleasepool {
	if (GetAGXDeviceContext().SupportsFeature(EAGXFeaturesIndirectBuffer))
	{
		check(NumInstances > 1);
		
		SCOPE_CYCLE_COUNTER(STAT_AGXDrawCallTime);
		
		uint32 PrimitiveType = Context->GetCurrentState().GetPrimitiveType();
		

		RHI_DRAW_CALL_STATS(PrimitiveType,1);
		FAGXResourceMultiBuffer* IndexBuffer = ResourceCast(IndexBufferRHI);
		FAGXResourceMultiBuffer* ArgumentsBuffer = ResourceCast(ArgumentsBufferRHI);
		
		Context->DrawIndexedIndirect(IndexBuffer, PrimitiveType, ArgumentsBuffer, DrawArgumentsIndex, NumInstances);
	}
	else
	{
		NOT_SUPPORTED("RHIDrawIndexedIndirect");
	}
	}
}

void FAGXRHICommandContext::RHIDrawIndexedPrimitiveIndirect(FRHIBuffer* IndexBufferRHI, FRHIBuffer* ArgumentBufferRHI, uint32 ArgumentOffset)
{
	@autoreleasepool {
	if (GetAGXDeviceContext().SupportsFeature(EAGXFeaturesIndirectBuffer))
	{
		SCOPE_CYCLE_COUNTER(STAT_AGXDrawCallTime);
		
		uint32 PrimitiveType = Context->GetCurrentState().GetPrimitiveType();
		

		RHI_DRAW_CALL_STATS(PrimitiveType,1);
		FAGXResourceMultiBuffer* IndexBuffer = ResourceCast(IndexBufferRHI);
		FAGXResourceMultiBuffer* ArgumentsBuffer = ResourceCast(ArgumentBufferRHI);
		
		Context->DrawIndexedPrimitiveIndirect(PrimitiveType, IndexBuffer, ArgumentsBuffer, ArgumentOffset);
	}
	else
	{
		NOT_SUPPORTED("RHIDrawIndexedPrimitiveIndirect");
	}
	}
}

void FAGXDynamicRHI::SetupRecursiveResources()
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
		FVector4VertexDeclaration.InitRHI();
		
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
				else if (IsFeatureLevelSupported( GMaxRHIShaderPlatform, ERHIFeatureLevel::SM5 ))
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
				
				// SetGlobalBoundShaderState(RHICmdList, GMaxRHIFeatureLevel, GClearMRTBoundShaderState[NumBuffers - 1][Instanced], FVector4VertexDeclaration.VertexDeclarationRHI, VertexShader, PixelShader);
			}
		}
		
		bSetupResources = true;
	}
	}
    */
}

void FAGXRHICommandContext::RHIClearMRT(bool bClearColor,int32 NumClearColors,const FLinearColor* ClearColorArray,bool bClearDepth,float Depth,bool bClearStencil,uint32 Stencil)
{
	NOT_SUPPORTED("RHIClearMRT");
}

void FAGXDynamicRHI::RHIBlockUntilGPUIdle()
{
	@autoreleasepool {
	ImmediateContext.Context->SubmitCommandBufferAndWait();
	}
}

uint32 FAGXDynamicRHI::RHIGetGPUFrameCycles(uint32 GPUIndex)
{
	check(GPUIndex == 0);
	return GGPUFrameTime;
}

void FAGXDynamicRHI::RHIExecuteCommandList(FRHICommandList* RHICmdList)
{
	NOT_SUPPORTED("RHIExecuteCommandList");
}

void FAGXRHICommandContext::RHISetDepthBounds(float MinDepth, float MaxDepth)
{
	METAL_IGNORED(FAGXRHICommandContextSetDepthBounds);
}

void FAGXRHICommandContext::RHISubmitCommandsHint()
{
	@autoreleasepool {
    Context->SubmitCommandsHint();
	}
}

void FAGXRHICommandContext::RHIDiscardRenderTargets(bool Depth, bool Stencil, uint32 ColorBitMask)
{
	Context->GetCurrentState().DiscardRenderTargets(Depth, Stencil, ColorBitMask);
}

IRHICommandContext* FAGXDynamicRHI::RHIGetDefaultContext()
{
	return &ImmediateContext;
}

IRHIComputeContext* FAGXDynamicRHI::RHIGetDefaultAsyncComputeContext()
{
	@autoreleasepool {
	IRHIComputeContext* ComputeContext = GSupportsEfficientAsyncCompute && AsyncComputeContext ? AsyncComputeContext : RHIGetDefaultContext();
	// On platforms that support non-async compute we set this to the normal context.  It won't be async, but the high level
	// code can be agnostic if it wants to be.
	return ComputeContext;
	}
}

#if PLATFORM_USES_FIXED_RHI_CLASS
#define INTERNAL_DECORATOR(Method) ((FAGXRHICommandContext&)CmdList.GetContext()).FAGXRHICommandContext::Method
#include "RHICommandListCommandExecutes.inl"
#endif
