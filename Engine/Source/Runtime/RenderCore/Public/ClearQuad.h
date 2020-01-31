// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHIDefinitions.h"
#include "RenderResource.h"
#include "RendererInterface.h"

class FRHICommandList;
struct FRWBufferStructured;
struct FRWBuffer;
struct FSceneRenderTargetItem;
class FRHIUnorderedAccessView;
class FGraphicsPipelineStateInitializer;

class FClearVertexBuffer : public FVertexBuffer
{
public:
	/**
	* Initialize the RHI for this rendering resource
	*/
	virtual void InitRHI() override
	{
		// create a static vertex buffer
		FRHIResourceCreateInfo CreateInfo;
		VertexBufferRHI = RHICreateVertexBuffer(sizeof(FVector4) * 4, BUF_Static, CreateInfo);
		void* VoidPtr = RHILockVertexBuffer(VertexBufferRHI, 0, sizeof(FVector4) * 4, RLM_WriteOnly);
		// Generate the vertices used
		FVector4* Vertices = reinterpret_cast<FVector4*>(VoidPtr);
		Vertices[0] = FVector4(-1.0f, 1.0f, 0.0f, 1.0f);
		Vertices[1] = FVector4(1.0f, 1.0f, 0.0f, 1.0f);
		Vertices[2] = FVector4(-1.0f, -1.0f, 0.0f, 1.0f);
		Vertices[3] = FVector4(1.0f, -1.0f, 0.0f, 1.0f);
		RHIUnlockVertexBuffer(VertexBufferRHI);
	}
};
extern RENDERCORE_API TGlobalResource<FClearVertexBuffer> GClearVertexBuffer;

struct FClearQuadCallbacks
{
	TFunction<void(FGraphicsPipelineStateInitializer&)> PSOModifier = nullptr;
	TFunction<void(FRHICommandList&)> PreClear = nullptr;
	TFunction<void(FRHICommandList&)> PostClear = nullptr;
};

UE_DEPRECATED(4.25, "ClearUAV() is deprecated. Use RHICmdList.ClearUAVUint or RHICmdList.ClearUAVFloat instead, and handle any necessary resource transitions.")
inline void ClearUAV(FRHICommandList& RHICmdList, const FRWBufferStructured& StructuredBuffer, uint32 Value)
{
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, StructuredBuffer.UAV);
	RHICmdList.ClearUAVUint(StructuredBuffer.UAV, FUintVector4(Value, Value, Value, Value));
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, StructuredBuffer.UAV);
}

UE_DEPRECATED(4.25, "ClearUAV() is deprecated. Use RHICmdList.ClearUAVUint or RHICmdList.ClearUAVFloat instead, and handle any necessary resource transitions.")
inline void ClearUAV(FRHICommandList& RHICmdList, const FTextureRWBuffer2D& Buffer, FLinearColor Value)
{
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, Buffer.UAV);
	RHICmdList.ClearUAVFloat(Buffer.UAV, FVector4(Value.R, Value.G, Value.B, Value.A));
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, Buffer.UAV);
}

UE_DEPRECATED(4.25, "ClearUAV() is deprecated. Use RHICmdList.ClearUAVUint or RHICmdList.ClearUAVFloat instead, and handle any necessary resource transitions.")
inline void ClearUAV(FRHICommandList& RHICmdList, const FTextureRWBuffer3D& Buffer, FLinearColor Value)
{
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, Buffer.UAV);
	RHICmdList.ClearUAVFloat(Buffer.UAV, FVector4(Value.R, Value.G, Value.B, Value.A));
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, Buffer.UAV);
}

UE_DEPRECATED(4.25, "ClearUAV() is deprecated. Use RHICmdList.ClearUAVUint or RHICmdList.ClearUAVFloat instead, and handle any necessary resource transitions.")
inline void ClearUAV(FRHICommandList& RHICmdList, const FRWBuffer& Buffer, uint32 Value)
{
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, Buffer.UAV);
	RHICmdList.ClearUAVUint(Buffer.UAV, FUintVector4(Value, Value, Value, Value));
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, Buffer.UAV);
}

UE_DEPRECATED(4.25, "ClearUAV() is deprecated. Use RHICmdList.ClearUAVUint or RHICmdList.ClearUAVFloat instead, and handle any necessary resource transitions.")
inline void ClearUAV(FRHICommandList& RHICmdList, FRHIUnorderedAccessView* UAV, uint32 /*unused NumBytes*/, uint32 Value)
{
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, UAV);
	RHICmdList.ClearUAVUint(UAV, FUintVector4(Value, Value, Value, Value));
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, UAV);
}

UE_DEPRECATED(4.25, "ClearUAV() is deprecated. Use RHICmdList.ClearUAVUint or RHICmdList.ClearUAVFloat instead, and handle any necessary resource transitions.")
inline void ClearUAV(FRHICommandList& RHICmdList, FRHITexture* /*unused Texture*/, FRHIUnorderedAccessView* TextureUAV, const float(&ClearValues)[4])
{
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, TextureUAV);
	RHICmdList.ClearUAVFloat(TextureUAV, FVector4(ClearValues[0], ClearValues[1], ClearValues[2], ClearValues[3]));
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, TextureUAV);
}

UE_DEPRECATED(4.25, "ClearUAV() is deprecated. Use RHICmdList.ClearUAVUint or RHICmdList.ClearUAVFloat instead, and handle any necessary resource transitions.")
inline void ClearUAV(FRHICommandList& RHICmdList, FRHITexture* /*unused Texture*/, FRHIUnorderedAccessView* TextureUAV, const uint32(&ClearValues)[4])
{
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, TextureUAV);
	RHICmdList.ClearUAVUint(TextureUAV, FUintVector4(ClearValues[0], ClearValues[1], ClearValues[2], ClearValues[3]));
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, TextureUAV);
}

UE_DEPRECATED(4.25, "ClearUAV() is deprecated. Use RHICmdList.ClearUAVUint or RHICmdList.ClearUAVFloat instead, and handle any necessary resource transitions.")
inline void ClearUAV(FRHICommandList& RHICmdList, FRHITexture* /*unused Texture*/, FRHIUnorderedAccessView* TextureUAV, const FLinearColor& ClearColor)
{
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, TextureUAV);
	RHICmdList.ClearUAVFloat(TextureUAV, FVector4(ClearColor.R, ClearColor.G, ClearColor.B, ClearColor.A));
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, TextureUAV);
}

UE_DEPRECATED(4.25, "ClearUAV() is deprecated. Use RHICmdList.ClearUAVUint or RHICmdList.ClearUAVFloat instead, and handle any necessary resource transitions.")
inline void ClearUAV(FRHICommandList& RHICmdList, FRHIUnorderedAccessView* UAV, uint32 /*unused Width*/, uint32 /*unused Height*/, const FLinearColor& ClearColor)
{
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, UAV);
	RHICmdList.ClearUAVFloat(UAV, FVector4(ClearColor.R, ClearColor.G, ClearColor.B, ClearColor.A));
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, UAV);
}

UE_DEPRECATED(4.25, "ClearUAV() is deprecated. Use RHICmdList.ClearUAVUint or RHICmdList.ClearUAVFloat instead, and handle any necessary resource transitions.")
inline void ClearUAV(FRHICommandList& RHICmdList, const FSceneRenderTargetItem& RenderTargetItem, const float(&ClearValues)[4])
{
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, RenderTargetItem.UAV);
	RHICmdList.ClearUAVFloat(RenderTargetItem.UAV, FVector4(ClearValues[0], ClearValues[1], ClearValues[2], ClearValues[3]));
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, RenderTargetItem.UAV);
}

UE_DEPRECATED(4.25, "ClearUAV() is deprecated. Use RHICmdList.ClearUAVUint or RHICmdList.ClearUAVFloat instead, and handle any necessary resource transitions.")
inline void ClearUAV(FRHICommandList& RHICmdList, const FSceneRenderTargetItem& RenderTargetItem, const uint32(&ClearValues)[4])
{
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, RenderTargetItem.UAV);
	RHICmdList.ClearUAVUint(RenderTargetItem.UAV, FUintVector4(ClearValues[0], ClearValues[1], ClearValues[2], ClearValues[3]));
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, RenderTargetItem.UAV);
}

UE_DEPRECATED(4.25, "ClearUAV() is deprecated. Use RHICmdList.ClearUAVUint or RHICmdList.ClearUAVFloat instead, and handle any necessary resource transitions.")
inline void ClearUAV(FRHICommandList& RHICmdList, const FSceneRenderTargetItem& RenderTargetItem, const FLinearColor& ClearColor)
{
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, RenderTargetItem.UAV);
	RHICmdList.ClearUAVFloat(RenderTargetItem.UAV, FVector4(ClearColor.R, ClearColor.G, ClearColor.B, ClearColor.A));
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, RenderTargetItem.UAV);
}

extern RENDERCORE_API void DrawClearQuadMRT(FRHICommandList& RHICmdList, bool bClearColor, int32 NumClearColors, const FLinearColor* ClearColorArray, bool bClearDepth, float Depth, bool bClearStencil, uint32 Stencil );
extern RENDERCORE_API void DrawClearQuadMRT(FRHICommandList& RHICmdList, bool bClearColor, int32 NumClearColors, const FLinearColor* ClearColorArray, bool bClearDepth, float Depth, bool bClearStencil, uint32 Stencil, FClearQuadCallbacks ClearQuadCallbacks);
extern RENDERCORE_API void DrawClearQuadMRT(FRHICommandList& RHICmdList, bool bClearColor, int32 NumClearColors, const FLinearColor* ClearColorArray, bool bClearDepth, float Depth, bool bClearStencil, uint32 Stencil, FIntPoint ViewSize, FIntRect ExcludeRect );

inline void DrawClearQuad(FRHICommandList& RHICmdList, bool bClearColor, const FLinearColor& Color, bool bClearDepth, float Depth, bool bClearStencil, uint32 Stencil)
{
	DrawClearQuadMRT(RHICmdList, bClearColor, 1, &Color, bClearDepth, Depth, bClearStencil, Stencil);
}

inline void DrawClearQuad(FRHICommandList& RHICmdList, bool bClearColor, const FLinearColor& Color, bool bClearDepth, float Depth, bool bClearStencil, uint32 Stencil, FIntPoint ViewSize, FIntRect ExcludeRect)
{
	DrawClearQuadMRT(RHICmdList, bClearColor, 1, &Color, bClearDepth, Depth, bClearStencil, Stencil, ViewSize, ExcludeRect);
}

inline void DrawClearQuad(FRHICommandList& RHICmdList, const FLinearColor& Color)
{
	DrawClearQuadMRT(RHICmdList, true, 1, &Color, false, 0, false, 0);
}

inline void DrawClearQuad(FRHICommandList& RHICmdList, const FLinearColor& Color, FClearQuadCallbacks ClearQuadCallbacks)
{
	DrawClearQuadMRT(RHICmdList, true, 1, &Color, false, 0, false, 0, ClearQuadCallbacks);
}