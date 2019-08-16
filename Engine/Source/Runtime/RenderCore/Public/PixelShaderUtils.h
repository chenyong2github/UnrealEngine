// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PixelShaderUtils.h: Utilities for pixel shaders.
=============================================================================*/

#pragma once

#include "RenderResource.h"
#include "RenderGraphUtils.h"
#include "CommonRenderResources.h"
#include "RHI.h"
#include "RHIStaticStates.h"
#include "PipelineStateCache.h"


/** All utils for pixel shaders. */
struct RENDERCORE_API FPixelShaderUtils
{
	/** Draw a single triangle on the entire viewport. */
	static void DrawFullscreenTriangle(FRHICommandList& RHICmdList, uint32 InstanceCount = 1);

	/** Draw a two triangle on the entire viewport. */
	static void DrawFullscreenQuad(FRHICommandList& RHICmdList, uint32 InstanceCount = 1);

	/** Initialize a pipeline state object initializer with almost all the basics required to do a full viewport pass. */
	static void InitFullscreenPipelineState(
		FRHICommandList& RHICmdList,
		const TShaderMap<FGlobalShaderType>* GlobalShaderMap,
		const FShader* PixelShader,
		FGraphicsPipelineStateInitializer& GraphicsPSOInit);

	/** Dispatch a full screen pixel shader to rhi command list with its parameters. */
	template<typename TShaderClass>
	static inline void DrawFullscreenPixelShader(
		FRHICommandList& RHICmdList, 
		const TShaderMap<FGlobalShaderType>* GlobalShaderMap,
		const TShaderClass* PixelShader,
		const typename TShaderClass::FParameters& Parameters,
		const FIntRect& Viewport,
		FRHIBlendState* BlendState = nullptr,
		FRHIRasterizerState* RasterizerState = nullptr,
		FRHIDepthStencilState* DepthStencilState = nullptr)
	{
		check(PixelShader);
		RHICmdList.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.0f, Viewport.Max.X, Viewport.Max.Y, 1.0f);
		
		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		InitFullscreenPipelineState(RHICmdList, GlobalShaderMap, PixelShader, /* out */ GraphicsPSOInit);
		GraphicsPSOInit.BlendState = BlendState ? BlendState : GraphicsPSOInit.BlendState;
		GraphicsPSOInit.RasterizerState = RasterizerState ? RasterizerState : GraphicsPSOInit.RasterizerState;
		GraphicsPSOInit.DepthStencilState = DepthStencilState ? DepthStencilState : GraphicsPSOInit.DepthStencilState;

		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		SetShaderParameters(RHICmdList, PixelShader, PixelShader->GetPixelShader(), Parameters);

		DrawFullscreenTriangle(RHICmdList);
	}

	/** Dispatch a pixel shader to render graph builder with its parameters. */
	template<typename TShaderClass>
	static inline void AddFullscreenPass(
		FRDGBuilder& GraphBuilder,
		const TShaderMap<FGlobalShaderType>* GlobalShaderMap,
		FRDGEventName&& PassName,
		const TShaderClass* PixelShader,
		typename TShaderClass::FParameters* Parameters,
		const FIntRect& Viewport,
		FRHIBlendState* BlendState = nullptr,
		FRHIRasterizerState* RasterizerState = nullptr,
		FRHIDepthStencilState* DepthStencilState = nullptr)
	{
		check(PixelShader);
		ClearUnusedGraphResources(PixelShader, Parameters);

		GraphBuilder.AddPass(
			Forward<FRDGEventName>(PassName),
			Parameters,
			ERDGPassFlags::Raster,
			[Parameters, GlobalShaderMap, PixelShader, Viewport, BlendState, RasterizerState, DepthStencilState](FRHICommandList& RHICmdList)
		{
			FPixelShaderUtils::DrawFullscreenPixelShader(RHICmdList, GlobalShaderMap, PixelShader, *Parameters, Viewport, 
				BlendState, RasterizerState, DepthStencilState);
		});
	}
};
