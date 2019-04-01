// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "RenderGraph.h"
#include "PostProcess/SceneFilterRendering.h"

// The vertex shader used by DrawScreenPass to draw a rectangle.
class FScreenPassVS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FScreenPassVS);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters&)
	{
		return true;
	}

	FScreenPassVS() = default;
	FScreenPassVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{}
};

// Returns whether a HMD hidden area mask is being used for VR.
bool IsHMDHiddenAreaMaskActive();

// Returns the global engine mini font texture.
const FTextureRHIRef& GetMiniFontTexture();

// Defines the common set of parameters for a screen space pass.
BEGIN_SHADER_PARAMETER_STRUCT(FScreenPassCommonParameters, )
	SHADER_PARAMETER(FIntRect, ViewportRect)
	SHADER_PARAMETER(FVector4, ViewportSize)
	SHADER_PARAMETER(FVector4, ScreenPosToPixelValue)
	SHADER_PARAMETER_STRUCT_REF(FSceneTexturesUniformParameters, SceneUniformBuffer)
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	SHADER_PARAMETER_SAMPLER(SamplerState, BilinearTextureSampler0)
END_SHADER_PARAMETER_STRUCT()

FScreenPassCommonParameters GetScreenPassCommonParameters(FRHICommandListImmediate& RHICmdList, const FViewInfo& View);

// Defines shader parameters for a single texture input to a screen space pass.
BEGIN_SHADER_PARAMETER_STRUCT(FScreenPassInput, )
	SHADER_PARAMETER(FVector4, Size)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Texture)
	SHADER_PARAMETER_SAMPLER(SamplerState, Sampler)
END_SHADER_PARAMETER_STRUCT()

FScreenPassInput GetScreenPassInputParameters(FRDGTextureRef Texture, FSamplerStateRHIParamRef SamplerState);

/**
 * The context used for screen pass operations. Extracts and holds common state required
 * by most screen passes from the provided View instance. Exists to reduce pointer
 * chasing and reduce function parameter sizes as well as share immutable common state.
 */
class FScreenPassContext
{
public:
	// Creates an instance of the context on the stack specifically to survive through the lifetime of the render graph.
	static FScreenPassContext* Create(FRHICommandListImmediate& RHICmdList, const FViewInfo& InView);

	// The current view instance being processed.
	const FViewInfo& View;

	// The current view family instance being processed.
	const FSceneViewFamily& ViewFamily;

	// The viewport rect for the view being processed.
	const FIntRect ViewportRect;

	// VR - Which stereo pass is being rendered.
	const EStereoscopicPass StereoPass;

	// VR - Whether a HMD hidden area mask is being used for VR.
	const bool bHasHMDMask;

	// The global shader map for the current view being processed.
	const TShaderMap<FGlobalShaderType>* ShaderMap;

	// The vertex shader used by draw screen pass. Cached here to avoid many lookups.
	const TShaderMapRef<FScreenPassVS> ScreenPassVS;

	// Common screen space parameters, filled at context creation time.
	const FScreenPassCommonParameters ScreenPassCommonParameters;

private:
	FScreenPassContext(FRHICommandListImmediate& RHICmdList, const FViewInfo& InView);
	FScreenPassContext(const FScreenPassContext&) = delete;
};

using FScreenPassContextRef = const FScreenPassContext*;

/**
 * Draws a full-viewport triangle with the provided pixel shader type. The destination full-viewport triangle
 * and interpolated source UV coordinates are derived from the viewport and texture rectangles, respectively.
 * @param ViewportRect The rectangle, in pixels, of the viewport drawn onto the destination render target.
 * @param TextureRect The rectangle, in pixels, of the source texture mapped to the [0, 1]x[0, 1] UV space.
 * @param TextureSize The total size, in pixels, of the source texture.
 * @param PixelShader The pixel shader instance assigned to the pipeline state.
 * @param PixelShaderParameters The parameter block assigned to the pixel shader prior to draw.
 */
template<typename TPixelShaderType>
void DrawScreenPass(
	FRHICommandListImmediate& RHICmdList,
	FScreenPassContextRef Context,
	FIntRect ViewportRect,
	FIntRect TextureRect,
	FIntPoint TextureSize,
	TPixelShaderType* PixelShader,
	const typename TPixelShaderType::FParameters& PixelShaderParameters)
{
	const FIntPoint ViewportSize = ViewportRect.Size();
	const FPixelShaderRHIParamRef PixelShaderRHI = GETSAFERHISHADER_PIXEL(PixelShader);
	FScreenPassVS* VertexShader = *(Context->ScreenPassVS);

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(VertexShader);
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShaderRHI;
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;

	RHICmdList.SetViewport(ViewportRect.Min.X, ViewportRect.Min.Y, 0.0f, ViewportRect.Max.X, ViewportRect.Max.Y, 1.0f);
	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
	SetShaderParameters(RHICmdList, PixelShader, PixelShaderRHI, PixelShaderParameters);

	DrawPostProcessPass(
		RHICmdList,
		0, 0, ViewportSize.X, ViewportSize.Y,
		TextureRect.Min.X, TextureRect.Min.Y, TextureRect.Width(), TextureRect.Height(),
		ViewportSize,
		TextureSize,
		VertexShader,
		Context->StereoPass,
		Context->bHasHMDMask,
		EDRF_UseTriangleOptimization);
}

/**
 * Adds a new Render Graph pass which internally calls @ref DrawScreenPass.
 *
 * Parameters are forwarded to @ref DrawScreenPass.
 */
template <typename TPixelShaderType>
inline void AddDrawScreenPass(
	FRDGBuilder& GraphBuilder,
	FRDGEventName&& PassName,
	FScreenPassContextRef Context,
	FIntRect ViewportRect,
	FIntRect TextureRect,
	FIntPoint TextureSize,
	TPixelShaderType* PixelShader,
	typename TPixelShaderType::FParameters* PixelShaderParameters)
{
	check(Context);
	check(PixelShader);
	check(PixelShaderParameters);

	ClearUnusedGraphResources(PixelShader, PixelShaderParameters);

	GraphBuilder.AddPass(
		Forward<FRDGEventName>(PassName),
		PixelShaderParameters,
		ERenderGraphPassFlags::None,
		[Context, ViewportRect, TextureRect, TextureSize, PixelShader, PixelShaderParameters]
		(FRHICommandListImmediate& RHICmdList)
	{
		DrawScreenPass(RHICmdList, Context, ViewportRect, TextureRect, TextureSize, PixelShader, *PixelShaderParameters);
	});
}