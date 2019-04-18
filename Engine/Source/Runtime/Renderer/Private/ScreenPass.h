// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommonRenderResources.h"
#include "GlobalShader.h"
#include "PipelineStateCache.h"
#include "RenderGraph.h"
#include "PostProcess/SceneFilterRendering.h"
#include "ScenePrivate.h"

// Returns whether a HMD hidden area mask is being used for VR.
bool IsHMDHiddenAreaMaskActive();

// Returns the global engine mini font texture.
const FTextureRHIRef& GetMiniFontTexture();

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

// Describes a viewport rect oriented within a texture.
struct FScreenPassTextureViewport
{
	// Creates a viewport that is downscaled by the requested scale factor.
	static FScreenPassTextureViewport CreateDownscaled(const FScreenPassTextureViewport& Other, uint32 ScaleFactor);

	FScreenPassTextureViewport() = default;

	FScreenPassTextureViewport(FIntRect InRect)
		: Rect(InRect)
		, Extent(InRect.Max)
	{}

	FScreenPassTextureViewport(FIntRect InRect, FIntPoint InExtent)
		: Rect(InRect)
		, Extent(InExtent)
	{}

	FScreenPassTextureViewport(const FScreenPassTextureViewport&) = default;

	bool operator==(const FScreenPassTextureViewport& Other) const;
	bool operator!=(const FScreenPassTextureViewport& Other) const;

	// Returns whether the viewport contains an empty viewport or extent.
	bool IsEmpty() const;

	// The viewport rect, in pixels; defines a sub-set within [0, 0]x(Extent, Extent).
	FIntRect Rect;

	// The texture extent, in pixels; defines a super-set [0, 0]x(Extent, Extent).
	FIntPoint Extent = FIntPoint::ZeroValue;
};

// Describes the set of shader parameters for a screen pass texture viewport.
BEGIN_SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, )
	// Texture extent in pixels.
	SHADER_PARAMETER(FVector2D, Extent)
	SHADER_PARAMETER(FVector2D, ExtentInverse)

	// Scale / Bias factor to convert from [-1, 1] to [ViewportMin, ViewportMax]
	SHADER_PARAMETER(FVector2D, ScreenPosToViewportScale)
	SHADER_PARAMETER(FVector2D, ScreenPosToViewportBias)

	// Texture viewport min / max in pixels.
	SHADER_PARAMETER(FIntPoint, ViewportMin)
	SHADER_PARAMETER(FIntPoint, ViewportMax)

	// Texture viewport size in pixels.
	SHADER_PARAMETER(FVector2D, ViewportSize)
	SHADER_PARAMETER(FVector2D, ViewportSizeInverse)

	// Texture viewport min / max in normalized UV coordinates, with respect to the texture extent.
	SHADER_PARAMETER(FVector2D, UVViewportMin)
	SHADER_PARAMETER(FVector2D, UVViewportMax)

	// Texture viewport size in normalized UV coordinates, with respect to the texture extent.
	SHADER_PARAMETER(FVector2D, UVViewportSize)
	SHADER_PARAMETER(FVector2D, UVViewportSizeInverse)

	// Texture viewport min / max in normalized UV coordinates, with respect to the texture extent,
	// adjusted by a half pixel offset for bilinear filtering. Useful for clamping to avoid sampling
	// pixels on viewport edges; e.g. clamp(UV, UVViewportBilinearMin, UVViewportBilinearMax);
	SHADER_PARAMETER(FVector2D, UVViewportBilinearMin)
	SHADER_PARAMETER(FVector2D, UVViewportBilinearMax)
END_SHADER_PARAMETER_STRUCT()

FScreenPassTextureViewportParameters GetScreenPassTextureViewportParameters(const FScreenPassTextureViewport& InViewport);

/**
 * A class grouping a render graph texture and its shader parameters. The screen pass texture
 * is either in an empty state or a valid state. The default constructor initializes to an empty state.
 * Instantiating an instance from valid state requires using the provided @ref Create functions, which
 * validate that the shader parameter invariant holds.
 */
class FScreenPassTexture
{
public:
	/**
	 * Creates a screen pass texture with a viewport.
	 * @param InTexture A valid render graph texture instance. Asserts if the texture is null.
	 * @param InViewport A viewport contained within the texture extent.
	 */
	static FScreenPassTexture Create(FRDGTextureRef InTexture, FIntRect InViewport);

	/**
	 * Creates a full-screen pass texture with a viewport covering the full texture.
	 * @param InTexture A valid render graph texture instance. Asserts if the texture is null.
	 */
	static FScreenPassTexture CreateFullscreen(FRDGTextureRef InTexture);

	// Default initializes to empty.
	FScreenPassTexture() = default;

	// Copy construction is permitted.
	FScreenPassTexture(const FScreenPassTexture&) = default;

	inline FRDGTextureRef GetRDGTexture() const
	{
		return Texture;
	}

	inline FScreenPassTextureViewport GetViewport() const
	{
		return Viewport;
	}

	inline FScreenPassTextureViewportParameters GetViewportParameters() const
	{
		return GetScreenPassTextureViewportParameters(Viewport);
	}

	inline const FTextureRHIParamRef GetRHITexture() const
	{
		return Texture ? Texture->GetRHITexture() : nullptr;
	}

	inline bool IsValid() const
	{
		return Texture != nullptr;
	}

	inline bool IsEmpty() const
	{
		return Texture == nullptr;
	}

	inline bool operator==(const FScreenPassTexture& Other) const
	{
		return Texture == Other.Texture && Viewport == Other.Viewport;
	}

	inline bool operator!=(const FScreenPassTexture& Other) const
	{
		return !(*this == Other);
	}

private:
	FRDGTextureRef Texture = nullptr;
	FScreenPassTextureViewport Viewport;
};

/**
 * Contains a transform that maps UV coordinates from one screen pass texture viewport to another.
 * Assumes normalized UV coordinates [0, 0]x[1, 1] where [0, 0] maps to the source view min
 * coordinate and [1, 1] maps to the source view rect max coordinate.
 *
 * Example Usage:
 *    float2 DestinationUV = SourceUV * UVScaleBias.xy + UVScaleBias.zw;
 */
BEGIN_SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportTransform, )
	// A scale / bias factor to apply to the input UV coordinate, converting it to a output UV coordinate.
	SHADER_PARAMETER(FVector2D, Scale)
	SHADER_PARAMETER(FVector2D, Bias)
END_SHADER_PARAMETER_STRUCT()

// Constructs a view transform from source and destination texture viewports.
FScreenPassTextureViewportTransform GetScreenPassTextureViewportTransform(
	const FScreenPassTextureViewportParameters& Source,
	const FScreenPassTextureViewportParameters& Destination);

// Constructs a view transform from source and destination UV offset / extent pairs.
FScreenPassTextureViewportTransform GetScreenPassTextureViewportTransform(
	FVector2D SourceUVOffset,
	FVector2D SourceUVExtent,
	FVector2D DestinationUVOffset,
	FVector2D DestinationUVExtent);

// Defines the common set of parameters for a screen space pass.
BEGIN_SHADER_PARAMETER_STRUCT(FScreenPassCommonParameters, )
	SHADER_PARAMETER_STRUCT_REF(FSceneTexturesUniformParameters, SceneUniformBuffer)
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	SHADER_PARAMETER_SAMPLER(SamplerState, BilinearTextureSampler)
END_SHADER_PARAMETER_STRUCT()

FScreenPassCommonParameters GetScreenPassCommonParameters(FRHICommandListImmediate& RHICmdList, const FViewInfo& View);

/**
 * The context used for screen pass operations. Extracts and holds common state required
 * by most screen passes from the provided View instance. Exists to reduce pointer
 * chasing and reduce function parameter sizes as well as share immutable common state.
 */
class FScreenPassContext
{
public:

	UE_NONCOPYABLE(FScreenPassContext)

	FScreenPassContext(FRHICommandListImmediate& RHICmdList, const FViewInfo& InView);

	// The current view instance being processed.
	const FViewInfo& View;

	// The current view family instance being processed.
	const FSceneViewFamily& ViewFamily;

	// The current view state instance being processed.
	const FSceneViewState* ViewState;

	// VR - Which stereo pass is being rendered.
	const EStereoscopicPass StereoPass;

	// VR - Whether a HMD hidden area mask is being used for VR.
	const bool bHasHMDMask;

	// Whether screen passes should use compute.
	const bool bUseComputePasses;

	// The global shader map for the current view being processed.
	const TShaderMap<FGlobalShaderType>* ShaderMap;

	// The vertex shader used by draw screen pass. Cached here to avoid many lookups.
	const TShaderMapRef<FScreenPassVS> ScreenPassVS;

	// Common screen space parameters, filled at context creation time.
	const FScreenPassCommonParameters ScreenPassCommonParameters;
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
	FIntRect OutputRect,
	FIntRect InputRect,
	FIntPoint InputSize,
	TPixelShaderType* PixelShader,
	const typename TPixelShaderType::FParameters& PixelShaderParameters)
{
	const FIntPoint OutputSize = OutputRect.Size();
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

	RHICmdList.SetViewport(OutputRect.Min.X, OutputRect.Min.Y, 0.0f, OutputRect.Max.X, OutputRect.Max.Y, 1.0f);
	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
	SetShaderParameters(RHICmdList, PixelShader, PixelShaderRHI, PixelShaderParameters);

	DrawPostProcessPass(
		RHICmdList,
		0, 0, OutputSize.X, OutputSize.Y,
		InputRect.Min.X, InputRect.Min.Y, InputRect.Width(), InputRect.Height(),
		OutputSize,
		InputSize,
		VertexShader,
		Context->StereoPass,
		Context->bHasHMDMask,
		EDRF_UseTriangleOptimization);
}

/**
 * Helper variant of @ref DrawScreenPass. All other parameters are forwarded.
 * @param DestinationViewDesc The destination texture view descriptor, for viewport generation.
 * @param SourceViewDesc The source view descriptor, for UV generation.
 */
template <typename TPixelShaderType>
inline void DrawScreenPass(
	FRHICommandListImmediate& RHICmdList,
	FScreenPassContextRef Context,
	const FScreenPassTextureViewport& OutputViewport,
	const FScreenPassTextureViewport& InputViewport,
	TPixelShaderType* PixelShader,
	const typename TPixelShaderType::FParameters& PixelShaderParameters)
{
	DrawScreenPass(
		RHICmdList,
		Context,
		OutputViewport.Rect,
		InputViewport.Rect,
		InputViewport.Extent,
		PixelShader,
		PixelShaderParameters);
}

/**
 * Adds a new Render Graph pass which internally calls @ref DrawScreenPass.
 * Parameters are forwarded to @ref DrawScreenPass.
 */
template <typename TPixelShaderType>
inline void AddDrawScreenPass(
	FRDGBuilder& GraphBuilder,
	FRDGEventName&& PassName,
	FScreenPassContextRef Context,
	FIntRect PixelOutputRect,
	FIntRect PixelInputRect,
	FIntPoint PixelInputSize,
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
		[Context, PixelOutputRect, PixelInputRect, PixelInputSize, PixelShader, PixelShaderParameters]
		(FRHICommandListImmediate& RHICmdList)
	{
		DrawScreenPass(
			RHICmdList,
			Context,
			PixelOutputRect,
			PixelInputRect,
			PixelInputSize,
			PixelShader,
			*PixelShaderParameters);
	});
}

/**
 * Helper variants of @ref AddDrawScreenPass. All other parameters are forwarded.
 */
template <typename TPixelShaderType>
inline void AddDrawScreenPass(
	FRDGBuilder& GraphBuilder,
	FRDGEventName&& PassName,
	FScreenPassContextRef Context,
	const FScreenPassTextureViewport& OutputViewport,
	const FScreenPassTextureViewport& InputViewport,
	TPixelShaderType* PixelShader,
	typename TPixelShaderType::FParameters* PixelShaderParameters)
{
	AddDrawScreenPass(
		GraphBuilder,
		Forward<FRDGEventName>(PassName),
		Context,
		OutputViewport.Rect,
		InputViewport.Rect,
		InputViewport.Extent,
		PixelShader,
		PixelShaderParameters);
}