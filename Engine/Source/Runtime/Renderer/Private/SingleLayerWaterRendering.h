// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraph.h"
#include "PixelShaderUtils.h"
#include "ScreenSpaceRayTracing.h"

class FViewInfo;

struct FSceneWithoutWaterTextures
{
	struct FView
	{
		FIntRect ViewRect;
		FVector4 MinMaxUV;
	};

	FRDGTextureRef ColorTexture = nullptr;
	FRDGTextureRef DepthTexture = nullptr;
	TArray<FView> Views;
	float RefractionDownsampleFactor = 1.0f;
};

bool ShouldRenderSingleLayerWater(TArrayView<const FViewInfo> Views);
bool ShouldRenderSingleLayerWaterSkippedRenderEditorNotification(TArrayView<const FViewInfo> Views);

class FWaterTileVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FWaterTileVS);
	SHADER_USE_PARAMETER_STRUCT(FWaterTileVS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, TileListData)
	END_SHADER_PARAMETER_STRUCT()

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);
};

template<typename TPixelShaderClass, typename TPassParameters>
void SingleLayerWaterAddTiledFullscreenPass(
	FRDGBuilder& GraphBuilder,
	const FGlobalShaderMap* GlobalShaderMap,
	FRDGEventName&& PassName,
	TShaderRefBase<TPixelShaderClass, FShaderMapPointerTable> PixelShader,
	TPassParameters* PassParameters,
	const TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformBuffer,
	const FIntRect& Viewport,
	FTiledScreenSpaceReflection* TiledScreenSpaceReflection = nullptr,
	FRHIBlendState* BlendState = nullptr,
	FRHIRasterizerState* RasterizerState = nullptr,
	FRHIDepthStencilState* DepthStencilState = nullptr,
	uint32 StencilRef = 0)
{
	PassParameters->IndirectDrawParameter = TiledScreenSpaceReflection ? TiledScreenSpaceReflection->DispatchIndirectParametersBuffer : nullptr;

	PassParameters->VS.ViewUniformBuffer = ViewUniformBuffer;
	PassParameters->VS.TileListData = TiledScreenSpaceReflection ? TiledScreenSpaceReflection->TileListStructureBufferSRV : nullptr;

	ValidateShaderParameters(PixelShader, PassParameters->PS);
	ClearUnusedGraphResources(PixelShader, &PassParameters->PS);

	const bool bRunTiled = TiledScreenSpaceReflection != nullptr;
	if (bRunTiled)
	{
		FWaterTileVS::FPermutationDomain PermutationVector;
		TShaderMapRef<FWaterTileVS> VertexShader(GlobalShaderMap, PermutationVector);

		ValidateShaderParameters(VertexShader, PassParameters->VS);
		ClearUnusedGraphResources(VertexShader, &PassParameters->VS);

		GraphBuilder.AddPass(
			Forward<FRDGEventName>(PassName),
			PassParameters,
			ERDGPassFlags::Raster,
			[PassParameters, GlobalShaderMap, Viewport, TiledScreenSpaceReflection, VertexShader, PixelShader, BlendState, RasterizerState, DepthStencilState, StencilRef](FRHICommandList& RHICmdList)
		{
			RHICmdList.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.0f, Viewport.Max.X, Viewport.Max.Y, 1.0f);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			FPixelShaderUtils::InitFullscreenPipelineState(RHICmdList, GlobalShaderMap, PixelShader, GraphicsPSOInit);

			GraphicsPSOInit.PrimitiveType = GRHISupportsRectTopology ? PT_RectList : PT_TriangleList;
			GraphicsPSOInit.BlendState = BlendState ? BlendState : GraphicsPSOInit.BlendState;
			GraphicsPSOInit.RasterizerState = RasterizerState ? RasterizerState : GraphicsPSOInit.RasterizerState;
			GraphicsPSOInit.DepthStencilState = DepthStencilState ? DepthStencilState : GraphicsPSOInit.DepthStencilState;
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			RHICmdList.SetStencilRef(StencilRef);

			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), PassParameters->VS);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters->PS);

			RHICmdList.DrawPrimitiveIndirect(PassParameters->IndirectDrawParameter->GetIndirectRHICallBuffer(), 0);
		});
	}
	else
	{
		GraphBuilder.AddPass(
			Forward<FRDGEventName>(PassName),
			PassParameters,
			ERDGPassFlags::Raster,
			[PassParameters, GlobalShaderMap, Viewport, PixelShader, BlendState, RasterizerState, DepthStencilState, StencilRef](FRHICommandList& RHICmdList)
		{
			RHICmdList.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.0f, Viewport.Max.X, Viewport.Max.Y, 1.0f);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			FPixelShaderUtils::InitFullscreenPipelineState(RHICmdList, GlobalShaderMap, PixelShader, GraphicsPSOInit);

			GraphicsPSOInit.BlendState = BlendState ? BlendState : GraphicsPSOInit.BlendState;
			GraphicsPSOInit.RasterizerState = RasterizerState ? RasterizerState : GraphicsPSOInit.RasterizerState;
			GraphicsPSOInit.DepthStencilState = DepthStencilState ? DepthStencilState : GraphicsPSOInit.DepthStencilState;

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			RHICmdList.SetStencilRef(StencilRef);

			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters->PS);

			FPixelShaderUtils::DrawFullscreenTriangle(RHICmdList);
		});
	}
}