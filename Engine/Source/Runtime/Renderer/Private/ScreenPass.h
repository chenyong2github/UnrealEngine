// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommonRenderResources.h"
#include "GlobalShader.h"
#include "PipelineStateCache.h"
#include "RenderGraph.h"
#include "PostProcess/SceneFilterRendering.h"
#include "RenderTargetTemp.h"
#include "CanvasTypes.h"

// Returns whether a HMD hidden area mask is being used for VR.
bool IsHMDHiddenAreaMaskActive();

// Returns the global engine mini font texture.
const FTextureRHIRef& GetMiniFontTexture();

// Creates and returns an RDG texture for the view family output. Returns null if no RHI texture exists.
FRDGTextureRef TryCreateViewFamilyTexture(FRDGBuilder& GraphBuilder, const FSceneViewFamily& ViewFamily);

// The vertex shader used by DrawScreenPass to draw a rectangle.
class RENDERER_API FScreenPassVS : public FGlobalShader
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

// Describes a texture with a paired viewport rect.
struct FScreenPassTexture
{
	FScreenPassTexture() = default;

	explicit FScreenPassTexture(FRDGTextureRef InTexture);

	FScreenPassTexture(FRDGTextureRef InTexture, FIntRect InViewRect)
		: Texture(InTexture)
		, ViewRect(InViewRect)
	{}

	bool IsValid() const;

	bool operator==(FScreenPassTexture Other) const;
	bool operator!=(FScreenPassTexture Other) const;

	FRDGTextureRef Texture = nullptr;
	FIntRect ViewRect;
};

// Describes a texture with a load action for usage as a render target.
struct FScreenPassRenderTarget : public FScreenPassTexture
{
	static FScreenPassRenderTarget CreateFromInput(
		FRDGBuilder& GraphBuilder,
		FScreenPassTexture Input,
		ERenderTargetLoadAction OutputLoadAction,
		const TCHAR* OutputName);

	static FScreenPassRenderTarget CreateViewFamilyOutput(FRDGTextureRef ViewFamilyTexture, const FViewInfo& View);

	FScreenPassRenderTarget() = default;

	FScreenPassRenderTarget(FScreenPassTexture InTexture, ERenderTargetLoadAction InLoadAction)
		: FScreenPassTexture(InTexture)
		, LoadAction(InLoadAction)
	{}

	FScreenPassRenderTarget(FRDGTextureRef InTexture, ERenderTargetLoadAction InLoadAction)
		: FScreenPassTexture(InTexture)
		, LoadAction(InLoadAction)
	{}

	FScreenPassRenderTarget(FRDGTextureRef InTexture, FIntRect InViewRect, ERenderTargetLoadAction InLoadAction)
		: FScreenPassTexture(InTexture, InViewRect)
		, LoadAction(InLoadAction)
	{}

	bool operator==(FScreenPassRenderTarget Other) const;
	bool operator!=(FScreenPassRenderTarget Other) const;

	FRenderTargetBinding GetRenderTargetBinding() const;

	ERenderTargetLoadAction LoadAction = ERenderTargetLoadAction::ENoAction;
};

// Describes a view rect contained within the extent of a texture. Used to derive texture coordinate transformations.
class FScreenPassTextureViewport
{
public:
	// Creates a viewport that is downscaled by an integer multiple.
	static FScreenPassTextureViewport CreateDownscaled(const FScreenPassTextureViewport& Other, FIntPoint ScaleFactor);

	// Creates a viewport scaled by a floating point multiplier.
	static FScreenPassTextureViewport CreateScaled(const FScreenPassTextureViewport& Other, FVector2D Scale);

	FScreenPassTextureViewport() = default;

	explicit FScreenPassTextureViewport(FIntRect InRect)
		: Extent(InRect.Max)
		, Rect(InRect)
	{}

	explicit FScreenPassTextureViewport(FScreenPassTexture InTexture);

	explicit FScreenPassTextureViewport(FRDGTextureRef InTexture)
		: FScreenPassTextureViewport(FScreenPassTexture(InTexture))
	{}

	explicit FScreenPassTextureViewport(FIntPoint InExtent)
		: Extent(InExtent)
		, Rect(FIntPoint::ZeroValue, InExtent)
	{}

	FScreenPassTextureViewport(FIntPoint InExtent, FIntRect InRect)
		: Extent(InExtent)
		, Rect(InRect)
	{}

	FScreenPassTextureViewport(FRDGTextureRef InTexture, FIntRect InRect)
		: FScreenPassTextureViewport(FScreenPassTexture(InTexture, InRect))
	{}

	FScreenPassTextureViewport(const FScreenPassTextureViewport&) = default;

	bool operator==(const FScreenPassTextureViewport& Other) const;
	bool operator!=(const FScreenPassTextureViewport& Other) const;

	// Returns whether the viewport contains an empty viewport or extent.
	bool IsEmpty() const;

	// Returns whether the viewport covers the full extent of the texture.
	bool IsFullscreen() const;

	// Returns the ratio of rect size to extent along each axis.
	FVector2D GetRectToExtentRatio() const;

	// The texture extent, in pixels; defines a super-set [0, 0]x(Extent, Extent).
	FIntPoint Extent = FIntPoint::ZeroValue;

	// The viewport rect, in pixels; defines a sub-set within [0, 0]x(Extent, Extent).
	FIntRect Rect;
};

// Returns an extent downscaled by a multiple of the integer divisor (and clamped to 1).
FIntPoint GetDownscaledExtent(FIntPoint Extent, FIntPoint Divisor);

// Returns an extent scaled by the floating point scale factor.
FIntPoint GetScaledExtent(FIntPoint Extent, FVector2D Multiplier);
FIntPoint GetScaledExtent(FIntPoint Extent, float Multiplier);

// Returns a rect downscaled by a multiple of the integer divisor.
FIntRect GetDownscaledRect(FIntRect Rect, FIntPoint Divisor);

// Returns a rect scaled by the floating point scale factor.
FIntRect GetScaledRect(FIntRect Rect, FVector2D Multiplier);
FIntRect GetScaledRect(FIntRect Rect, float Multiplier);

// Returns the texture viewport downscaled by an integer divisor.
FScreenPassTextureViewport GetDownscaledViewport(FScreenPassTextureViewport Viewport, FIntPoint Divisor);

// Returns the texture viewport scaled by a float multiplier.
FScreenPassTextureViewport GetScaledViewport(FScreenPassTextureViewport Viewport, FVector2D Multiplier);

// Returns a rect with the min point at the origin and the max point at Extent.
FIntRect GetRectFromExtent(FIntPoint Extent);

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

FScreenPassTextureViewportParameters RENDERER_API GetScreenPassTextureViewportParameters(const FScreenPassTextureViewport& InViewport);

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

// A utility shader parameter struct containing the viewport, texture, and sampler for a unique texture input to a shader.
BEGIN_SHADER_PARAMETER_STRUCT(FScreenPassTextureInput, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FScreenPassTextureViewportParameters, Viewport)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Texture)
	SHADER_PARAMETER_SAMPLER(SamplerState, Sampler)
END_SHADER_PARAMETER_STRUCT()

FScreenPassTextureInput GetScreenPassTextureInput(FScreenPassTexture Input, FRHISamplerState* Sampler);

/** Draw information for the more advanced DrawScreenPass variant. Allows customizing the blend / depth stencil state,
 *  providing a custom vertex shader, and more fine-grained control of the underlying draw call.
 */
struct FScreenPassPipelineState
{
	using FDefaultBlendState = TStaticBlendState<>;
	using FDefaultDepthStencilState = TStaticDepthStencilState<false, CF_Always>;

	FScreenPassPipelineState() = default;

	FScreenPassPipelineState(
		const TShaderRef<FShader>& InVertexShader,
		const TShaderRef<FShader>& InPixelShader,
		FRHIBlendState* InBlendState = FDefaultBlendState::GetRHI(),
		FRHIDepthStencilState* InDepthStencilState = FDefaultDepthStencilState::GetRHI(),
		FRHIVertexDeclaration* InVertexDeclaration = GFilterVertexDeclaration.VertexDeclarationRHI)
		: VertexShader(InVertexShader)
		, PixelShader(InPixelShader)
		, BlendState(InBlendState)
		, DepthStencilState(InDepthStencilState)
		, VertexDeclaration(InVertexDeclaration)
	{}

	void Validate() const
	{
		check(VertexShader.IsValid());
		check(PixelShader.IsValid());
		check(BlendState);
		check(DepthStencilState);
		check(VertexDeclaration);
	}

	TShaderRef<FShader> VertexShader;
	TShaderRef<FShader> PixelShader;
	FRHIBlendState* BlendState = nullptr;
	FRHIDepthStencilState* DepthStencilState = nullptr;
	FRHIVertexDeclaration* VertexDeclaration = nullptr;
};

// Helper function which sets the pipeline state object on the command list prior to invoking a screen pass.
void RENDERER_API SetScreenPassPipelineState(FRHICommandList& RHICmdList, const FScreenPassPipelineState& ScreenPassDraw);

enum class EScreenPassDrawFlags : uint8
{
	None,

	// Flips the Y axis of the rendered quad. Used by mobile rendering.
	FlipYAxis = 0x1,

	// Allows the screen pass to use a HMD hidden area mask if one is available. Used for VR.
	AllowHMDHiddenAreaMask = 0x2
};
ENUM_CLASS_FLAGS(EScreenPassDrawFlags);

/** More advanced variant of screen pass drawing. Supports overriding blend / depth stencil
 *  pipeline state, and providing a custom vertex shader. Shader parameters are not bound by
 *  this method, instead the user provides a setup function that is called prior to draw, but
 *  after setting the PSO. This setup function should assign shader parameters.
 */
template<typename TSetupFunction>
void DrawScreenPass(
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	const FScreenPassTextureViewport& OutputViewport,
	const FScreenPassTextureViewport& InputViewport,
	const FScreenPassPipelineState& PipelineState,
	EScreenPassDrawFlags Flags,
	TSetupFunction SetupFunction)
{
	PipelineState.Validate();

	const FIntRect InputRect = InputViewport.Rect;
	const FIntPoint InputSize = InputViewport.Extent;
	const FIntRect OutputRect = OutputViewport.Rect;
	const FIntPoint OutputSize = OutputRect.Size();

	RHICmdList.SetViewport(OutputRect.Min.X, OutputRect.Min.Y, 0.0f, OutputRect.Max.X, OutputRect.Max.Y, 1.0f);

	SetScreenPassPipelineState(RHICmdList, PipelineState);

	SetupFunction(RHICmdList);

	FIntPoint LocalOutputPos(FIntPoint::ZeroValue);
	FIntPoint LocalOutputSize(OutputSize);
	EDrawRectangleFlags DrawRectangleFlags = EDRF_UseTriangleOptimization;

	const bool bFlipYAxis = (Flags & EScreenPassDrawFlags::FlipYAxis) == EScreenPassDrawFlags::FlipYAxis;

	if (bFlipYAxis)
	{
		// Draw the quad flipped. Requires that the cull mode be disabled.
		LocalOutputPos.Y = OutputSize.Y;
		LocalOutputSize.Y = -OutputSize.Y;

		// Triangle optimization currently doesn't work when flipped.
		DrawRectangleFlags = EDRF_Default;
	}

	const bool bUseHMDHiddenAreaMask = (Flags & EScreenPassDrawFlags::AllowHMDHiddenAreaMask) == EScreenPassDrawFlags::AllowHMDHiddenAreaMask
		? View.bHMDHiddenAreaMaskActive
		: false;

	DrawPostProcessPass(
		RHICmdList,
		LocalOutputPos.X, LocalOutputPos.Y, LocalOutputSize.X, LocalOutputSize.Y,
		InputRect.Min.X, InputRect.Min.Y, InputRect.Width(), InputRect.Height(),
		OutputSize,
		InputSize,
		PipelineState.VertexShader,
		View.StereoPass,
		bUseHMDHiddenAreaMask,
		DrawRectangleFlags);
}

/** Render graph variant of simpler DrawScreenPass function. Clears graph resources unused by the
 *  pixel shader prior to adding the pass.
 */
template <typename PixelShaderType>
FORCEINLINE void AddDrawScreenPass(
	FRDGBuilder& GraphBuilder,
	FRDGEventName&& PassName,
	const FViewInfo& View,
	const FScreenPassTextureViewport& OutputViewport,
	const FScreenPassTextureViewport& InputViewport,
	const TShaderRef<FShader>& VertexShader,
	const TShaderRef<PixelShaderType>& PixelShader,
	FRHIBlendState* BlendState,
	FRHIDepthStencilState* DepthStencilState,
	typename PixelShaderType::FParameters* PixelShaderParameters,
	EScreenPassDrawFlags Flags = EScreenPassDrawFlags::None)
{
	check(VertexShader.IsValid());
	check(PixelShader.IsValid());
	check(PixelShaderParameters);

	ClearUnusedGraphResources(PixelShader, PixelShaderParameters);

	const FScreenPassPipelineState PipelineState(VertexShader, PixelShader, BlendState, DepthStencilState);

	GraphBuilder.AddPass(
		Forward<FRDGEventName&&>(PassName),
		PixelShaderParameters,
		ERDGPassFlags::Raster,
		[&View, OutputViewport, InputViewport, PipelineState, PixelShader, PixelShaderParameters, Flags](FRHICommandListImmediate& RHICmdList)
	{
		DrawScreenPass(RHICmdList, View, OutputViewport, InputViewport, PipelineState, Flags, [&](FRHICommandListImmediate&)
		{
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PixelShaderParameters);
		});
	});
}

template <typename PixelShaderType>
FORCEINLINE void AddDrawScreenPass(
	FRDGBuilder& GraphBuilder,
	FRDGEventName&& PassName,
	const FViewInfo& View,
	const FScreenPassTextureViewport& OutputViewport,
	const FScreenPassTextureViewport& InputViewport,
	const TShaderRef<FShader>& VertexShader,
	const TShaderRef<PixelShaderType>& PixelShader,
	typename PixelShaderType::FParameters* PixelShaderParameters,
	EScreenPassDrawFlags Flags = EScreenPassDrawFlags::None)
{
	FRHIBlendState* BlendState = FScreenPassPipelineState::FDefaultBlendState::GetRHI();
	FRHIDepthStencilState* DepthStencilState = FScreenPassPipelineState::FDefaultDepthStencilState::GetRHI();
	AddDrawScreenPass(GraphBuilder, Forward<FRDGEventName&&>(PassName), View, OutputViewport, InputViewport, VertexShader, PixelShader, BlendState, DepthStencilState, PixelShaderParameters, Flags);
}

template <typename PixelShaderType>
FORCEINLINE void AddDrawScreenPass(
	FRDGBuilder& GraphBuilder,
	FRDGEventName&& PassName,
	const FViewInfo& View,
	const FScreenPassTextureViewport& OutputViewport,
	const FScreenPassTextureViewport& InputViewport,
	const TShaderRef<FShader>& VertexShader,
	const TShaderRef<PixelShaderType>& PixelShader,
	FRHIBlendState* BlendState,
	typename PixelShaderType::FParameters* PixelShaderParameters,
	EScreenPassDrawFlags Flags = EScreenPassDrawFlags::None)
{
	FRHIDepthStencilState* DepthStencilState = FScreenPassPipelineState::FDefaultDepthStencilState::GetRHI();
	AddDrawScreenPass(GraphBuilder, Forward<FRDGEventName&&>(PassName), View, OutputViewport, InputViewport, VertexShader, PixelShader, BlendState, DepthStencilState, PixelShaderParameters, Flags);
}

template <typename PixelShaderType>
FORCEINLINE void AddDrawScreenPass(
	FRDGBuilder& GraphBuilder,
	FRDGEventName&& PassName,
	const FViewInfo& View,
	const FScreenPassTextureViewport& OutputViewport,
	const FScreenPassTextureViewport& InputViewport,
	const TShaderRef<FShader>& VertexShader,
	const TShaderRef<PixelShaderType>& PixelShader,
	FRHIDepthStencilState* DepthStencilState,
	typename PixelShaderType::FParameters* PixelShaderParameters,
	EScreenPassDrawFlags Flags = EScreenPassDrawFlags::None)
{
	FRHIBlendState* BlendState = FScreenPassPipelineState::FDefaultBlendState::GetRHI();
	AddDrawScreenPass(GraphBuilder, Forward<FRDGEventName&&>(PassName), View, OutputViewport, InputViewport, VertexShader, PixelShader, BlendState, DepthStencilState, PixelShaderParameters, Flags);
}

template <typename PixelShaderType>
FORCEINLINE void AddDrawScreenPass(
	FRDGBuilder& GraphBuilder,
	FRDGEventName&& PassName,
	const FViewInfo& View,
	const FScreenPassTextureViewport& OutputViewport,
	const FScreenPassTextureViewport& InputViewport,
	const TShaderRef<PixelShaderType>& PixelShader,
	typename PixelShaderType::FParameters* PixelShaderParameters,
	EScreenPassDrawFlags Flags = EScreenPassDrawFlags::None)
{
	TShaderMapRef<FScreenPassVS> VertexShader(View.ShaderMap);
	FRHIBlendState* BlendState = FScreenPassPipelineState::FDefaultBlendState::GetRHI();
	FRHIDepthStencilState* DepthStencilState = FScreenPassPipelineState::FDefaultDepthStencilState::GetRHI();
	AddDrawScreenPass(GraphBuilder, Forward<FRDGEventName&&>(PassName), View, OutputViewport, InputViewport, VertexShader, PixelShader, BlendState, DepthStencilState, PixelShaderParameters, Flags);
}

/** Render graph variant of more advanced DrawScreenPass function. Does *not* clear unused graph
 *  resources, since the parameters might be shared between the vertex and pixel shaders.
 */
template <typename TSetupFunction, typename TPassParameterStruct>
FORCEINLINE void AddDrawScreenPass(
	FRDGBuilder& GraphBuilder,
	FRDGEventName&& PassName,
	const FViewInfo& View,
	const FScreenPassTextureViewport& OutputViewport,
	const FScreenPassTextureViewport& InputViewport,
	const FScreenPassPipelineState& PipelineState,
	TPassParameterStruct* PassParameterStruct,
	EScreenPassDrawFlags Flags,
	TSetupFunction SetupFunction)
{
	PipelineState.Validate();
	check(PassParameterStruct);

	GraphBuilder.AddPass(
		Forward<FRDGEventName&&>(PassName),
		PassParameterStruct,
		ERDGPassFlags::Raster,
		[&View, OutputViewport, InputViewport, PipelineState, SetupFunction, Flags] (FRHICommandListImmediate& RHICmdList)
	{
		DrawScreenPass(RHICmdList, View, OutputViewport, InputViewport, PipelineState, Flags, SetupFunction);
	});
}

template <typename TSetupFunction, typename TPassParameterStruct>
FORCEINLINE void AddDrawScreenPass(
	FRDGBuilder& GraphBuilder,
	FRDGEventName&& PassName,
	const FViewInfo& View,
	const FScreenPassTextureViewport& OutputViewport,
	const FScreenPassTextureViewport& InputViewport,
	const FScreenPassPipelineState& PipelineState,
	TPassParameterStruct* PassParameterStruct,
	TSetupFunction SetupFunction)
{
	AddDrawScreenPass(GraphBuilder, Forward<FRDGEventName&&>(PassName), View, OutputViewport, InputViewport, PipelineState, PassParameterStruct, EScreenPassDrawFlags::None, SetupFunction);
}

/** Helper function which copies a region of an input texture to a region of the output texture,
 *  with support for format conversion. If formats match, the method falls back to a simple DMA
 *  (CopyTexture); otherwise, it rasterizes using a pixel shader. Use this method if the two
 *  textures may have different formats.
 */
void RENDERER_API AddDrawTexturePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FRDGTextureRef InputTexture,
	FRDGTextureRef OutputTexture,
	FIntPoint InputPosition = FIntPoint::ZeroValue,
	FIntPoint OutputPosition = FIntPoint::ZeroValue,
	FIntPoint Size = FIntPoint::ZeroValue);

/** Helper variant which takes a shared viewport instead of unique input / output positions. */
FORCEINLINE void AddDrawTexturePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FRDGTextureRef InputTexture,
	FRDGTextureRef OutputTexture,
	FIntRect ViewportRect)
{
	AddDrawTexturePass(GraphBuilder, View, InputTexture, OutputTexture, ViewportRect.Min, ViewportRect.Min, ViewportRect.Size());
}

void RENDERER_API AddDrawTexturePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FScreenPassTexture Input,
	FScreenPassRenderTarget Output);

/** Helper function render a canvas to an output texture. Must be called within a render pass with Output as the render target. */
template <typename TFunction>
void DrawCanvasPass(
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	FScreenPassTexture Output,
	TFunction&& Function)
{
	check(Output.IsValid());

	const FSceneViewFamily& ViewFamily = *View.Family;
	FRenderTargetTemp TempRenderTarget(static_cast<FRHITexture2D*>(Output.Texture->GetRHI()), Output.ViewRect.Size());
	FCanvas Canvas(&TempRenderTarget, nullptr, ViewFamily.CurrentRealTime, ViewFamily.CurrentWorldTime, ViewFamily.DeltaWorldTime, View.GetFeatureLevel());
	Canvas.SetRenderTargetRect(Output.ViewRect);

	Function(Canvas);

	const bool bFlush = false;
	const bool bInsideRenderPass = true;
	Canvas.Flush_RenderThread(RHICmdList, bFlush, bInsideRenderPass);
}

template <typename TFunction>
FORCEINLINE void AddRenderTargetPass(
	FRDGBuilder& GraphBuilder,
	FRDGEventName&& PassName,
	FScreenPassRenderTarget Output,
	TFunction&& Function)
{
	FRenderTargetParameters* PassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();
	GraphBuilder.AddPass(MoveTemp(PassName), PassParameters, ERDGPassFlags::Raster, MoveTemp(Function));
}

template <typename TFunction>
FORCEINLINE void AddDrawCanvasPass(
	FRDGBuilder& GraphBuilder,
	FRDGEventName&& PassName,
	const FViewInfo& View,
	FScreenPassRenderTarget Output,
	TFunction Function)
{
	AddRenderTargetPass(GraphBuilder, MoveTemp(PassName), Output, [Output, &View, Function](FRHICommandListImmediate& RHICmdList)
	{
		DrawCanvasPass(RHICmdList, View, Output, Function);
	});
}

enum class EDownsampleDepthFilter
{
	// Produces a depth value that is not conservative but has consistent error (i.e. picks the sample).
	Point,

	// Produces a conservative max depth value.
	Max,

	// Produces a checkerboarded selection of min and max depth values
	Checkerboard
};

void AddDownsampleDepthPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FScreenPassTexture Input,
	FScreenPassRenderTarget Output,
	EDownsampleDepthFilter DownsampleDepthFilter);

#include "ScreenPass.inl"
