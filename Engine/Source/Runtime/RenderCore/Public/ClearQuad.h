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
		FRHIResourceCreateInfo CreateInfo(TEXT("FClearVertexBuffer"));
		VertexBufferRHI = RHICreateVertexBuffer(sizeof(FVector4) * 4, BUF_Static, CreateInfo);
		void* VoidPtr = RHILockBuffer(VertexBufferRHI, 0, sizeof(FVector4) * 4, RLM_WriteOnly);
		// Generate the vertices used
		FVector4* Vertices = reinterpret_cast<FVector4*>(VoidPtr);
		Vertices[0] = FVector4(-1.0f, 1.0f, 0.0f, 1.0f);
		Vertices[1] = FVector4(1.0f, 1.0f, 0.0f, 1.0f);
		Vertices[2] = FVector4(-1.0f, -1.0f, 0.0f, 1.0f);
		Vertices[3] = FVector4(1.0f, -1.0f, 0.0f, 1.0f);
		RHIUnlockBuffer(VertexBufferRHI);
	}
};
extern RENDERCORE_API TGlobalResource<FClearVertexBuffer> GClearVertexBuffer;

struct FClearQuadCallbacks
{
	TFunction<void(FGraphicsPipelineStateInitializer&)> PSOModifier = nullptr;
	TFunction<void(FRHICommandList&)> PreClear = nullptr;
	TFunction<void(FRHICommandList&)> PostClear = nullptr;
};

extern RENDERCORE_API void ClearUAV(FRHICommandList& RHICmdList, FRHIUnorderedAccessView* Buffer, uint32 NumBytes, uint32 Value, bool bBarriers = true);
extern RENDERCORE_API void DrawClearQuadMRT(FRHICommandList& RHICmdList, bool bClearColor, int32 NumClearColors, const FLinearColor* ClearColorArray, bool bClearDepth, float Depth, bool bClearStencil, uint32 Stencil);
extern RENDERCORE_API void DrawClearQuadMRT(FRHICommandList& RHICmdList, bool bClearColor, int32 NumClearColors, const FLinearColor* ClearColorArray, bool bClearDepth, float Depth, bool bClearStencil, uint32 Stencil, FClearQuadCallbacks ClearQuadCallbacks);
extern RENDERCORE_API void DrawClearQuadMRT(FRHICommandList& RHICmdList, bool bClearColor, int32 NumClearColors, const FLinearColor* ClearColorArray, bool bClearDepth, float Depth, bool bClearStencil, uint32 Stencil, FIntPoint ViewSize, FIntRect ExcludeRect);

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
