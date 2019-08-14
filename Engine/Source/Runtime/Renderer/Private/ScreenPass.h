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
class FScreenPassTextureViewport
{
public:
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

	FScreenPassTextureViewport(FIntRect InRect, FRDGTextureRef InTexture)
		: Rect(InRect)
		, Extent(InTexture->Desc.Extent)
	{}

	FScreenPassTextureViewport(FRDGTextureRef InTexture)
		: Rect(FIntPoint::ZeroValue, InTexture->Desc.Extent)
		, Extent(InTexture->Desc.Extent)
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

/** Contains a transform that maps UV coordinates from one screen pass texture viewport to another.
 *  Assumes normalized UV coordinates [0, 0]x[1, 1] where [0, 0] maps to the source view min
 *  coordinate and [1, 1] maps to the source view rect max coordinate.
 *
 *  Example Usage:
 *     float2 DestinationUV = SourceUV * UVScaleBias.xy + UVScaleBias.zw;
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

// View information cached off for use by screen passes.
class FScreenPassViewInfo
{
public:
	FScreenPassViewInfo(const FViewInfo& InView);
	FScreenPassViewInfo(const FScreenPassViewInfo&) = default;

	// Returns the load action we should use when we expect to overwrite all relevant pixels.
	// Takes into account the HMD mesh.
	ERenderTargetLoadAction GetOverwriteLoadAction() const;

	const FViewInfo& View;

	// The vertex shader used by draw screen pass. Cached here to avoid many lookups.
	const TShaderMapRef<FScreenPassVS> ScreenPassVS;

	// VR - Which stereo pass is being rendered.
	const EStereoscopicPass StereoPass;

	// VR - Whether a HMD hidden area mask is being used for VR.
	const bool bHasHMDMask;

	// Whether screen passes should use compute.
	const bool bUseComputePasses;
};

/** Draw information for the more advanced DrawScreenPass variant. Allows customizing the blend / depth stencil state,
 *  providing a custom vertex shader, and more fine-grained control of the underlying draw call.
 */
struct FScreenPassDrawInfo
{
	enum class EFlags : uint8
	{
		None,

		// Flips the Y axis of the rendered quad. Used by mobile rendering.
		FlipYAxis = 0x1
	};

	using FDefaultBlendState = TStaticBlendState<>;
	using FDefaultDepthStencilState = TStaticDepthStencilState<false, CF_Always>;

	FScreenPassDrawInfo() = default;

	FScreenPassDrawInfo(
		FShader* InVertexShader,
		FShader* InPixelShader,
		FRHIBlendState* InBlendState = FDefaultBlendState::GetRHI(),
		FRHIDepthStencilState* InDepthStencilState = FDefaultDepthStencilState::GetRHI(),
		EFlags InFlags = EFlags::None)
		: VertexShader(InVertexShader)
		, PixelShader(InPixelShader)
		, BlendState(InBlendState)
		, DepthStencilState(InDepthStencilState)
		, Flags(InFlags)
	{}

	void Validate() const
	{
		check(VertexShader);
		check(PixelShader);
		check(BlendState);
		check(DepthStencilState);
	}

	FShader* VertexShader = nullptr;
	FShader* PixelShader = nullptr;
	FRHIBlendState* BlendState = nullptr;
	FRHIDepthStencilState* DepthStencilState = nullptr;
	EFlags Flags = EFlags::None;
};

ENUM_CLASS_FLAGS(FScreenPassDrawInfo::EFlags);

// Helper function which sets the pipeline state object on the command list prior to invoking a screen pass.
void SetScreenPassPipelineState(FRHICommandListImmediate& RHICmdList, const FScreenPassDrawInfo& ScreenPassDraw);

/** Draws a full-viewport triangle with the provided pixel shader type. The destination full-viewport triangle
 *  and interpolated source UV coordinates are derived from the viewport and texture rectangles, respectively.
 *  @param OutputViewport The output viewport defining the render target extent and viewport rect.
 *  @param InputViewport The input viewport defining the rect region to map UV coordinates into.
 *  @param PixelShader The pixel shader instance assigned to the pipeline state.
 *  @param PixelShaderParameters The parameter block assigned to the pixel shader prior to draw.
 */
template <typename TPixelShaderType>
void DrawScreenPass(
	FRHICommandListImmediate& RHICmdList,
	const FScreenPassViewInfo& ScreenPassView,
	const FScreenPassTextureViewport& OutputViewport,
	const FScreenPassTextureViewport& InputViewport,
	TPixelShaderType* PixelShader,
	const typename TPixelShaderType::FParameters& PixelShaderParameters)
{
	const FIntRect InputRect = InputViewport.Rect;
	const FIntPoint InputSize = InputViewport.Extent;
	const FIntRect OutputRect = OutputViewport.Rect;
	const FIntPoint OutputSize = OutputRect.Size();

	RHICmdList.SetViewport(OutputRect.Min.X, OutputRect.Min.Y, 0.0f, OutputRect.Max.X, OutputRect.Max.Y, 1.0f);

	SetScreenPassPipelineState(RHICmdList, FScreenPassDrawInfo(*ScreenPassView.ScreenPassVS, PixelShader));

	SetShaderParameters(RHICmdList, PixelShader, PixelShader->GetPixelShader(), PixelShaderParameters);

	DrawPostProcessPass(
		RHICmdList,
		0, 0, OutputSize.X, OutputSize.Y,
		InputRect.Min.X, InputRect.Min.Y, InputRect.Width(), InputRect.Height(),
		OutputSize,
		InputSize,
		*ScreenPassView.ScreenPassVS,
		ScreenPassView.StereoPass,
		ScreenPassView.bHasHMDMask,
		EDRF_UseTriangleOptimization);
}

/** More advanced variant of screen pass drawing. Supports overriding blend / depth stencil
 *  pipeline state, and providing a custom vertex shader. Shader parameters are not bound by
 *  this method, instead the user provides a setup function that is called prior to draw, but
 *  after setting the PSO. This setup function should assign shader parameters.
 */
template<typename TSetupFunction>
void DrawScreenPass(
	FRHICommandListImmediate& RHICmdList,
	const FScreenPassViewInfo& ScreenPassView,
	const FScreenPassTextureViewport& OutputViewport,
	const FScreenPassTextureViewport& InputViewport,
	const FScreenPassDrawInfo& ScreenPassDraw,
	TSetupFunction SetupFunction)
{
	ScreenPassDraw.Validate();

	const FIntRect InputRect = InputViewport.Rect;
	const FIntPoint InputSize = InputViewport.Extent;
	const FIntRect OutputRect = OutputViewport.Rect;
	const FIntPoint OutputSize = OutputRect.Size();

	RHICmdList.SetViewport(OutputRect.Min.X, OutputRect.Min.Y, 0.0f, OutputRect.Max.X, OutputRect.Max.Y, 1.0f);

	EDrawRectangleFlags DrawRectangleFlags = EDRF_UseTriangleOptimization;

	FIntPoint LocalOutputPos(FIntPoint::ZeroValue);
	FIntPoint LocalOutputSize(OutputSize);

	if ((ScreenPassDraw.Flags & FScreenPassDrawInfo::EFlags::FlipYAxis) == FScreenPassDrawInfo::EFlags::FlipYAxis)
	{
		// Draw the quad flipped. Requires that the cull mode be disabled.
		LocalOutputPos.Y = OutputSize.Y;
		LocalOutputSize.Y = -OutputSize.Y;

		// Triangle optimization currently doesn't work when flipped.
		DrawRectangleFlags = EDRF_Default;
	}

	SetScreenPassPipelineState(RHICmdList, ScreenPassDraw);

	SetupFunction(RHICmdList);

	DrawPostProcessPass(
		RHICmdList,
		LocalOutputPos.X, LocalOutputPos.Y, LocalOutputSize.X, LocalOutputSize.Y,
		InputRect.Min.X, InputRect.Min.Y, InputRect.Width(), InputRect.Height(),
		OutputSize,
		InputSize,
		ScreenPassDraw.VertexShader,
		ScreenPassView.StereoPass,
		ScreenPassView.bHasHMDMask,
		DrawRectangleFlags);
}

/** Render graph variant of simpler DrawScreenPass function. Clears graph resources unused by the
 *  pixel shader prior to adding the pass.
 */
template <typename TPixelShaderType>
void AddDrawScreenPass(
	FRDGBuilder& GraphBuilder,
	FRDGEventName&& PassName,
	const FScreenPassViewInfo& ScreenPassView,
	const FScreenPassTextureViewport& OutputViewport,
	const FScreenPassTextureViewport& InputViewport,
	TPixelShaderType* PixelShader,
	typename TPixelShaderType::FParameters* PixelShaderParameters)
{
	check(PixelShader);
	check(PixelShaderParameters);

	ClearUnusedGraphResources(PixelShader, PixelShaderParameters);

	GraphBuilder.AddPass(
		Forward<FRDGEventName>(PassName),
		PixelShaderParameters,
		ERDGPassFlags::Raster,
		[ScreenPassView, OutputViewport, InputViewport, PixelShader, PixelShaderParameters]
	(FRHICommandListImmediate& RHICmdList)
	{
		DrawScreenPass(
			RHICmdList,
			ScreenPassView,
			OutputViewport,
			InputViewport,
			PixelShader,
			*PixelShaderParameters);
	});
}

/** Render graph variant of more advanced DrawScreenPass function. Does *not* clear unused graph
 *  resources, since the parameters might be shared between the vertex and pixel shaders.
 */
template <typename TSetupFunction, typename TPassParameterStruct>
void AddDrawScreenPass(
	FRDGBuilder& GraphBuilder,
	FRDGEventName&& PassName,
	const FScreenPassViewInfo& ScreenPassView,
	const FScreenPassTextureViewport& OutputViewport,
	const FScreenPassTextureViewport& InputViewport,
	const FScreenPassDrawInfo& ScreenPassDraw,
	TPassParameterStruct* PassParameterStruct,
	TSetupFunction SetupFunction)
{
	ScreenPassDraw.Validate();
	check(PassParameterStruct);

	GraphBuilder.AddPass(
		Forward<FRDGEventName>(PassName),
		PassParameterStruct,
		ERDGPassFlags::Raster,
		[ScreenPassView, OutputViewport, InputViewport, ScreenPassDraw, SetupFunction]
		(FRHICommandListImmediate& RHICmdList)
	{
		DrawScreenPass(
			RHICmdList,
			ScreenPassView,
			OutputViewport,
			InputViewport,
			ScreenPassDraw,
			SetupFunction);
	});
}

/** Helper function which copies a region of an input texture to a region of the output texture,
 *  with support for format conversion. If formats match, the method falls back to a simple DMA
 *  (CopyTexture); otherwise, it rasterizes using a pixel shader. Use this method if the two
 *  textures may have different formats.
 */
void AddDrawTexturePass(
	FRDGBuilder& GraphBuilder,
	const FScreenPassViewInfo& ScreenPassView,
	FRDGTextureRef InputTexture,
	FRDGTextureRef OutputTexture,
	FIntPoint InputPosition = FIntPoint::ZeroValue,
	FIntPoint OutputPosition = FIntPoint::ZeroValue,
	FIntPoint Size = FIntPoint::ZeroValue);

/** Helper variant which takes a shared viewport instead of unique input / output positions. */
inline void AddDrawTexturePass(
	FRDGBuilder& GraphBuilder,
	const FScreenPassViewInfo& ScreenPassView,
	FRDGTextureRef InputTexture,
	FRDGTextureRef OutputTexture,
	FIntRect ViewportRect)
{
	AddDrawTexturePass(
		GraphBuilder,
		ScreenPassView,
		InputTexture,
		OutputTexture,
		ViewportRect.Min,
		ViewportRect.Min,
		ViewportRect.Size());
}