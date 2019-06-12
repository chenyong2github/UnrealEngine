// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PixelShaderUtils.cpp: Implementations of utilities for pixel shaders.
=============================================================================*/

#include "PixelShaderUtils.h"
#include "CommonRenderResources.h"
#include "RenderGraph.h"


// static
void FPixelShaderUtils::DrawFullscreenTriangle(FRHICommandList& RHICmdList, uint32 InstanceCount)
{
	RHICmdList.SetStreamSource(0, GScreenRectangleVertexBuffer.VertexBufferRHI, 0);

	RHICmdList.DrawIndexedPrimitive(
		GScreenRectangleIndexBuffer.IndexBufferRHI,
		/*BaseVertexIndex=*/ 0,
		/*MinIndex=*/ 0,
		/*NumVertices=*/ 3,
		/*StartIndex=*/ 6,
		/*NumPrimitives=*/ 1,
		/*NumInstances=*/ InstanceCount);
}

// static
void FPixelShaderUtils::DrawFullscreenQuad(FRHICommandList& RHICmdList, uint32 InstanceCount)
{
	RHICmdList.SetStreamSource(0, GScreenRectangleVertexBuffer.VertexBufferRHI, 0);

	RHICmdList.DrawIndexedPrimitive(
		GScreenRectangleIndexBuffer.IndexBufferRHI,
		/*BaseVertexIndex=*/ 0,
		/*MinIndex=*/ 0,
		/*NumVertices=*/ 4,
		/*StartIndex=*/ 0,
		/*NumPrimitives=*/ 2,
		/*NumInstances=*/ InstanceCount);
}

// static
void FPixelShaderUtils::InitFullscreenPipelineState(
	FRHICommandList& RHICmdList,
	const TShaderMap<FGlobalShaderType>* GlobalShaderMap,
	const FShader* PixelShader,
	FGraphicsPipelineStateInitializer& GraphicsPSOInit)
{
	TShaderMapRef<FScreenVertexShaderVS> VertexShader(GlobalShaderMap);
		
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(PixelShader);
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;
}
