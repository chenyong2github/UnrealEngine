// Copyright Epic Games, Inc. All Rights Reserved.

#include "PostProcess/PostProcessMotionBlur.h"
#include "StaticBoundShaderState.h"
#include "CanvasTypes.h"
#include "RenderTargetTemp.h"
#include "SpriteIndexBuffer.h"
#include "PostProcessing.h"

namespace
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	TAutoConsoleVariable<int32> CVarMotionBlurFiltering(
		TEXT("r.MotionBlurFiltering"),
		0,
		TEXT("Useful developer variable\n")
		TEXT("0: off (default, expected by the shader for better quality)\n")
		TEXT("1: on"),
		ECVF_Cheat | ECVF_RenderThreadSafe);
#endif

	TAutoConsoleVariable<float> CVarMotionBlur2ndScale(
		TEXT("r.MotionBlur2ndScale"),
		1.0f,
		TEXT(""),
		ECVF_Cheat | ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarMotionBlurScatter(
		TEXT("r.MotionBlurScatter"),
		0,
		TEXT("Forces scatter based max velocity method (slower)."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarMotionBlurSeparable(
		TEXT("r.MotionBlurSeparable"),
		0,
		TEXT("Adds a second motion blur pass that smooths noise for a higher quality blur."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarMotionBlurPreferCompute(
		TEXT("r.MotionBlur.PreferCompute"),
		0,
		TEXT("Will use compute shaders for motion blur pass."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarAllowMotionBlurInVR(
		TEXT("vr.AllowMotionBlurInVR"),
		0,
		TEXT("For projects with motion blur enabled, this allows motion blur to be enabled even while in VR."));

	FMatrix GetPreviousWorldToClipMatrix(const FViewInfo& View)
	{
		if (View.Family->EngineShowFlags.CameraInterpolation)
		{
			// Instead of finding the world space position of the current pixel, calculate the world space position offset by the camera position, 
			// then translate by the difference between last frame's camera position and this frame's camera position,
			// then apply the rest of the transforms.  This effectively avoids precision issues near the extents of large levels whose world space position is very large.
			FVector ViewOriginDelta = View.ViewMatrices.GetViewOrigin() - View.PrevViewInfo.ViewMatrices.GetViewOrigin();
			return FTranslationMatrix(ViewOriginDelta) * View.PrevViewInfo.ViewMatrices.ComputeViewRotationProjectionMatrix();
		}
		else
		{
			return View.ViewMatrices.ComputeViewRotationProjectionMatrix();
		}
	}

	int32 GetMotionBlurQualityFromCVar()
	{
		int32 MotionBlurQuality;

		static const auto ICVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MotionBlurQuality"));
		MotionBlurQuality = FMath::Clamp(ICVar->GetValueOnRenderThread(), 0, 4);

		return MotionBlurQuality;
	}
}

const int32 kMotionBlurTileSize = 16;
const int32 kMotionBlurComputeTileSizeX = 8;
const int32 kMotionBlurComputeTileSizeY = 8;

bool IsMotionBlurEnabled(const FViewInfo& View)
{
	if (View.GetFeatureLevel() < ERHIFeatureLevel::SM5)
	{
		return false;
	}

	const int32 MotionBlurQuality = GetMotionBlurQualityFromCVar();

	const FSceneViewFamily& ViewFamily = *View.Family;

	return ViewFamily.EngineShowFlags.PostProcessing
		&& ViewFamily.EngineShowFlags.MotionBlur
		&& View.FinalPostProcessSettings.MotionBlurAmount > 0.001f
		&& View.FinalPostProcessSettings.MotionBlurMax > 0.001f
		&& ViewFamily.bRealtimeUpdate
		&& MotionBlurQuality > 0
		&& !IsSimpleForwardShadingEnabled(GShaderPlatformForFeatureLevel[View.GetFeatureLevel()])
		&& (CVarAllowMotionBlurInVR->GetInt() != 0 || !(ViewFamily.Views.Num() > 1));
}

bool IsVisualizeMotionBlurEnabled(const FViewInfo& View)
{
	return View.Family->EngineShowFlags.VisualizeMotionBlur && View.GetFeatureLevel() >= ERHIFeatureLevel::SM5;
}

bool IsMotionBlurScatterRequired(const FViewInfo& View, const FScreenPassTextureViewport& SceneViewport)
{
	const FSceneViewState* ViewState = View.ViewState;
	const float ViewportWidth = SceneViewport.Rect.Width();

	// Normalize percentage value.
	const float VelocityMax = View.FinalPostProcessSettings.MotionBlurMax / 100.0f;

	// Scale by 0.5 due to blur samples going both ways and convert to tiles.
	const float VelocityMaxInTiles = VelocityMax * ViewportWidth * (0.5f / 16.0f);

	// Compute path only supports the immediate neighborhood of tiles.
	const float TileDistanceMaxGathered = 3.0f;

	// Scatter is used when maximum velocity exceeds the distance supported by the gather approach.
	const bool bIsScatterRequiredByVelocityLength = VelocityMaxInTiles > TileDistanceMaxGathered;

	// Cinematic is paused.
	const bool bInPausedCinematic = (ViewState && ViewState->SequencerState == ESS_Paused);

	// Use the scatter approach if requested by cvar or we're in a paused cinematic (higher quality).
	const bool bIsScatterRequiredByUser = CVarMotionBlurScatter.GetValueOnRenderThread() == 1 || bInPausedCinematic;

	return bIsScatterRequiredByUser || bIsScatterRequiredByVelocityLength;
}

FIntPoint GetMotionBlurTileCount(FIntPoint SizeInPixels)
{
	const uint32 TilesX = FMath::DivideAndRoundUp(SizeInPixels.X, kMotionBlurTileSize);
	const uint32 TilesY = FMath::DivideAndRoundUp(SizeInPixels.Y, kMotionBlurTileSize);
	return FIntPoint(TilesX, TilesY);
}

EMotionBlurQuality GetMotionBlurQuality()
{
	// Quality levels begin at 1. 0 is reserved for 'off'.
	const int32 Quality = FMath::Clamp(GetMotionBlurQualityFromCVar(), 1, static_cast<int32>(EMotionBlurQuality::MAX));

	return static_cast<EMotionBlurQuality>(Quality - 1);
}

FRHISamplerState* GetMotionBlurColorSampler()
{
	bool bFiltered = false;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	bFiltered = CVarMotionBlurFiltering.GetValueOnRenderThread() != 0;
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

	if (bFiltered)
	{
		return TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	}
	else
	{
		return TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	}
}

FRHISamplerState* GetMotionBlurVelocitySampler()
{
	return TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
}

// Set of common shader parameters shared by all motion blur shaders.
BEGIN_SHADER_PARAMETER_STRUCT(FMotionBlurParameters, )
	SHADER_PARAMETER(float, AspectRatio)
	SHADER_PARAMETER(float, VelocityScale)
	SHADER_PARAMETER(float, VelocityScaleForTiles)
	SHADER_PARAMETER(float, VelocityMax)
END_SHADER_PARAMETER_STRUCT()

FMotionBlurParameters GetMotionBlurParameters(const FViewInfo& View, FIntPoint SceneViewportSize, float BlurScale)
{
	const FSceneViewState* ViewState = View.ViewState;

	const float TileSize = kMotionBlurTileSize;
	const float SceneViewportSizeX = SceneViewportSize.X;
	const float SceneViewportSizeY = SceneViewportSize.Y;
	const float MotionBlurTimeScale = ViewState ? ViewState->MotionBlurTimeScale : 1.0f;

	// Scale by 0.5 due to blur samples going both ways.
	const float VelocityScale = MotionBlurTimeScale * View.FinalPostProcessSettings.MotionBlurAmount * 0.5f;
	const float VelocityUVToPixel = BlurScale * SceneViewportSizeX * 0.5f;

	// 0:no 1:full screen width, percent conversion
	const float UVVelocityMax = View.FinalPostProcessSettings.MotionBlurMax / 100.0f;

	FMotionBlurParameters MotionBlurParameters;
	MotionBlurParameters.AspectRatio = SceneViewportSizeY / SceneViewportSizeX;
	MotionBlurParameters.VelocityScale = VelocityUVToPixel * VelocityScale;
	MotionBlurParameters.VelocityScaleForTiles = MotionBlurParameters.VelocityScale / TileSize;
	MotionBlurParameters.VelocityMax = FMath::Abs(VelocityUVToPixel) * UVVelocityMax;
	return MotionBlurParameters;
}

// Base class for a motion blur / velocity shader.
class FMotionBlurShader : public FGlobalShader
{
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	FMotionBlurShader() = default;
	FMotionBlurShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{}
};

class FMotionBlurVelocityFlattenCS : public FMotionBlurShader
{
public:
	static const uint32 ThreadGroupSize = 16;

	DECLARE_GLOBAL_SHADER(FMotionBlurVelocityFlattenCS);
	SHADER_USE_PARAMETER_STRUCT(FMotionBlurVelocityFlattenCS, FMotionBlurShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT(FMotionBlurParameters, MotionBlur)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Velocity)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, VelocityTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutVelocityFlatTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutVelocityTileTexture)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FMotionBlurVelocityFlattenCS, "/Engine/Private/PostProcessVelocityFlatten.usf", "VelocityFlattenMain", SF_Compute);

BEGIN_SHADER_PARAMETER_STRUCT(FMotionBlurVelocityDilateParameters, )
	SHADER_PARAMETER_STRUCT(FMotionBlurParameters, MotionBlur)
	SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, VelocityTile)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, VelocityTileTexture)
END_SHADER_PARAMETER_STRUCT()

class FMotionBlurVelocityDilateGatherCS : public FMotionBlurShader
{
public:
	static const uint32 ThreadGroupSize = 16;

	DECLARE_GLOBAL_SHADER(FMotionBlurVelocityDilateGatherCS);
	SHADER_USE_PARAMETER_STRUCT(FMotionBlurVelocityDilateGatherCS, FMotionBlurShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FMotionBlurVelocityDilateParameters, Dilate)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutVelocityTileTexture)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FMotionBlurVelocityDilateGatherCS, "/Engine/Private/PostProcessVelocityFlatten.usf", "VelocityGatherCS", SF_Compute);

enum class EMotionBlurVelocityScatterPass : uint32
{
	DrawMin,
	DrawMax,
	MAX
};

BEGIN_SHADER_PARAMETER_STRUCT(FMotionBlurVelocityDilateScatterParameters, )
	SHADER_PARAMETER(uint32, ScatterPass)
	SHADER_PARAMETER_STRUCT_INCLUDE(FMotionBlurVelocityDilateParameters, Dilate)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FMotionBlurVelocityDilateScatterVS : public FMotionBlurShader
{
public:
	DECLARE_GLOBAL_SHADER(FMotionBlurVelocityDilateScatterVS);
	SHADER_USE_PARAMETER_STRUCT(FMotionBlurVelocityDilateScatterVS, FMotionBlurShader);
	using FParameters = FMotionBlurVelocityDilateScatterParameters;
};

IMPLEMENT_GLOBAL_SHADER(FMotionBlurVelocityDilateScatterVS, "/Engine/Private/PostProcessVelocityFlatten.usf", "VelocityScatterVS", SF_Vertex);

class FMotionBlurVelocityDilateScatterPS : public FMotionBlurShader
{
public:
	DECLARE_GLOBAL_SHADER(FMotionBlurVelocityDilateScatterPS);
	SHADER_USE_PARAMETER_STRUCT(FMotionBlurVelocityDilateScatterPS, FMotionBlurShader);
	using FParameters = FMotionBlurVelocityDilateScatterParameters;
};

IMPLEMENT_GLOBAL_SHADER(FMotionBlurVelocityDilateScatterPS, "/Engine/Private/PostProcessVelocityFlatten.usf", "VelocityScatterPS", SF_Pixel);

class FMotionBlurQualityDimension : SHADER_PERMUTATION_ENUM_CLASS("MOTION_BLUR_QUALITY", EMotionBlurQuality);

using FMotionBlurFilterPermutationDomain = TShaderPermutationDomain<FMotionBlurQualityDimension>;

BEGIN_SHADER_PARAMETER_STRUCT(FMotionBlurFilterParameters, )
	SHADER_PARAMETER_STRUCT(FMotionBlurParameters, MotionBlur)

	SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Color)
	SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Velocity)
	SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, VelocityTile)

	SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportTransform, ColorToVelocity)
	SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportTransform, ColorToVelocityTile)

	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ColorTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, VelocityFlatTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, VelocityTileTexture)

	SHADER_PARAMETER_SAMPLER(SamplerState, ColorSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, VelocitySampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, VelocityTileSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, VelocityFlatSampler)
END_SHADER_PARAMETER_STRUCT()

class FMotionBlurFilterPS : public FMotionBlurShader
{
public:
	DECLARE_GLOBAL_SHADER(FMotionBlurFilterPS);
	SHADER_USE_PARAMETER_STRUCT(FMotionBlurFilterPS, FMotionBlurShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FMotionBlurFilterParameters, Filter)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = FMotionBlurFilterPermutationDomain;
};

IMPLEMENT_GLOBAL_SHADER(FMotionBlurFilterPS, "/Engine/Private/PostProcessMotionBlur.usf", "MainPS", SF_Pixel);

class FMotionBlurFilterCS : public FMotionBlurShader
{
public:
	DECLARE_GLOBAL_SHADER(FMotionBlurFilterCS);
	SHADER_USE_PARAMETER_STRUCT(FMotionBlurFilterCS, FMotionBlurShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FMotionBlurFilterParameters, Filter)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutColorTexture)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), kMotionBlurComputeTileSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), kMotionBlurComputeTileSizeY);
	}

	using FPermutationDomain = FMotionBlurFilterPermutationDomain;
};

IMPLEMENT_GLOBAL_SHADER(FMotionBlurFilterCS, "/Engine/Private/PostProcessMotionBlur.usf", "MainCS", SF_Compute);

class FMotionBlurVisualizePS : public FMotionBlurShader
{
public:
	DECLARE_GLOBAL_SHADER(FMotionBlurVisualizePS);
	SHADER_USE_PARAMETER_STRUCT(FMotionBlurVisualizePS, FMotionBlurShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FMatrix, WorldToClipPrev)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ColorTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, VelocityTexture)

		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Color)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Velocity)

		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportTransform, ColorToVelocity)

		SHADER_PARAMETER_SAMPLER(SamplerState, ColorSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, VelocitySampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, DepthSampler)

		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMotionBlurVisualizePS, "/Engine/Private/PostProcessMotionBlur.usf", "VisualizeMotionBlurPS", SF_Pixel);

TGlobalResource<FSpriteIndexBuffer<8>> GScatterQuadIndexBuffer;

enum class EMotionBlurFilterPass : uint32
{
	Separable0,
	Separable1,
	Unified,
	MAX
};

struct FMotionBlurViewports
{
	FMotionBlurViewports(
		const FScreenPassTextureViewport& InColorViewport,
		const FScreenPassTextureViewport& InVelocityViewport)
	{
		Color = InColorViewport;
		Velocity = InVelocityViewport;
		VelocityTile = FScreenPassTextureViewport(
			FIntRect(
				FIntPoint::ZeroValue,
				GetMotionBlurTileCount(Velocity.Rect.Size())));

		ColorParameters = GetScreenPassTextureViewportParameters(Color);
		VelocityParameters = GetScreenPassTextureViewportParameters(Velocity);
		VelocityTileParameters = GetScreenPassTextureViewportParameters(VelocityTile);

		ColorToVelocityTransform = GetScreenPassTextureViewportTransform(ColorParameters, VelocityParameters);
		ColorToVelocityTileTransform = GetScreenPassTextureViewportTransform(ColorParameters, VelocityTileParameters);
	}

	FScreenPassTextureViewport Color;
	FScreenPassTextureViewport Velocity;
	FScreenPassTextureViewport VelocityTile;

	FScreenPassTextureViewportParameters ColorParameters;
	FScreenPassTextureViewportParameters VelocityParameters;
	FScreenPassTextureViewportParameters VelocityTileParameters;

	FScreenPassTextureViewportTransform ColorToVelocityTransform;
	FScreenPassTextureViewportTransform ColorToVelocityTileTransform;
};

void AddMotionBlurVelocityPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FMotionBlurViewports& Viewports,
	FRDGTextureRef ColorTexture,
	FRDGTextureRef DepthTexture,
	FRDGTextureRef VelocityTexture,
	FRDGTextureRef* VelocityFlatTextureOutput,
	FRDGTextureRef* VelocityTileTextureOutput)
{
	check(ColorTexture);
	check(DepthTexture);
	check(VelocityTexture);
	check(VelocityFlatTextureOutput);
	check(VelocityTileTextureOutput);

	const FIntPoint VelocityTileCount = Viewports.VelocityTile.Extent;

	FRDGTextureRef VelocityFlatTexture =
		GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2DDesc(
				Viewports.Velocity.Extent,
				PF_FloatR11G11B10,
				FClearValueBinding::None,
				GFastVRamConfig.VelocityFlat,
				TexCreate_ShaderResource | TexCreate_UAV,
				false),
			TEXT("VelocityFlat"));

	FRDGTextureRef VelocityTileTextureSetup =
		GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2DDesc(
				VelocityTileCount,
				PF_FloatRGBA,
				FClearValueBinding::None,
				GFastVRamConfig.VelocityMax,
				TexCreate_ShaderResource | TexCreate_UAV,
				false),
			TEXT("VelocityTile"));

	const FMotionBlurParameters MotionBlurParametersNoScale = GetMotionBlurParameters(View, Viewports.Color.Rect.Size(), 1.0f);

	// Velocity flatten pass: combines depth / velocity into a single target for sampling efficiency.
	{
		FMotionBlurVelocityFlattenCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FMotionBlurVelocityFlattenCS::FParameters>();
		PassParameters->MotionBlur = MotionBlurParametersNoScale;
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->Velocity = Viewports.VelocityParameters;
		PassParameters->DepthTexture = DepthTexture;
		PassParameters->VelocityTexture = VelocityTexture;
		PassParameters->OutVelocityFlatTexture = GraphBuilder.CreateUAV(VelocityFlatTexture);
		PassParameters->OutVelocityTileTexture = GraphBuilder.CreateUAV(VelocityTileTextureSetup);

		TShaderMapRef<FMotionBlurVelocityFlattenCS> ComputeShader(View.ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Velocity Flatten"),
			*ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(Viewports.Velocity.Rect.Size(), FMotionBlurVelocityFlattenCS::ThreadGroupSize));
	}

	bool ScatterDilatation = IsMotionBlurScatterRequired(View, Viewports.Color);

	FRDGTextureRef VelocityTileTexture =
		GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2DDesc(
				VelocityTileCount,
				PF_FloatRGBA,
				FClearValueBinding::None,
				GFastVRamConfig.MotionBlur,
				TexCreate_ShaderResource | (ScatterDilatation ? TexCreate_RenderTargetable : TexCreate_UAV),
				false),
			TEXT("DilatedVelocityTile"));

	FMotionBlurVelocityDilateParameters VelocityDilateParameters;
	VelocityDilateParameters.MotionBlur = MotionBlurParametersNoScale;
	VelocityDilateParameters.VelocityTile = Viewports.VelocityTileParameters;
	VelocityDilateParameters.VelocityTileTexture = VelocityTileTextureSetup;

	if (ScatterDilatation)
	{
		FRDGTextureRef VelocityTileDepthTexture =
			GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create2DDesc(
					VelocityTileCount,
					PF_ShadowDepth,
					FClearValueBinding::DepthOne,
					TexCreate_None,
					TexCreate_DepthStencilTargetable,
					false),
				TEXT("DilatedVelocityDepth"));

		FMotionBlurVelocityDilateScatterParameters* PassParameters = GraphBuilder.AllocParameters<FMotionBlurVelocityDilateScatterParameters>();
		PassParameters->Dilate = VelocityDilateParameters;

		PassParameters->RenderTargets.DepthStencil =
			FDepthStencilBinding(
				VelocityTileDepthTexture,
				ERenderTargetLoadAction::EClear,
				ERenderTargetLoadAction::ENoAction,
				FExclusiveDepthStencil::DepthWrite_StencilNop);

		PassParameters->RenderTargets[0] =
			FRenderTargetBinding(
				VelocityTileTexture,
				ERenderTargetLoadAction::ENoAction);

		TShaderMapRef<FMotionBlurVelocityDilateScatterVS> VertexShader(View.ShaderMap);
		TShaderMapRef<FMotionBlurVelocityDilateScatterPS> PixelShader(View.ShaderMap);
		
		ValidateShaderParameters(*VertexShader, *PassParameters);
		ValidateShaderParameters(*PixelShader, *PassParameters);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("VelocityTileScatter %dx%d", VelocityTileCount.X, VelocityTileCount.Y),
			PassParameters,
			ERDGPassFlags::Raster,
			[VertexShader, PixelShader, VelocityTileCount, PassParameters](FRHICommandListImmediate& RHICmdList)
		{
			FRHIVertexShader* RHIVertexShader = GETSAFERHISHADER_VERTEX(*VertexShader);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = RHIVertexShader;
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

			// Max >= Min so no need to clear on second pass
			RHICmdList.SetViewport(0, 0, 0.0f, VelocityTileCount.X, VelocityTileCount.Y, 1.0f);

			// Min, Max
			for (uint32 ScatterPassIndex = 0; ScatterPassIndex < static_cast<uint32>(EMotionBlurVelocityScatterPass::MAX); ScatterPassIndex++)
			{
				const EMotionBlurVelocityScatterPass ScatterPass = static_cast<EMotionBlurVelocityScatterPass>(ScatterPassIndex);

				if (ScatterPass == EMotionBlurVelocityScatterPass::DrawMin)
				{
					GraphicsPSOInit.BlendState = TStaticBlendStateWriteMask<CW_RGBA>::GetRHI();
					GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_Less>::GetRHI();
				}
				else
				{
					GraphicsPSOInit.BlendState = TStaticBlendStateWriteMask<CW_BA>::GetRHI();
					GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_Greater>::GetRHI();
				}

				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

				PassParameters->ScatterPass = ScatterPassIndex;

				SetShaderParameters(RHICmdList, *VertexShader, RHIVertexShader, *PassParameters);

				// Needs to be the same on shader side (faster on NVIDIA and AMD)
				const int32 QuadsPerInstance = 8;

				RHICmdList.SetStreamSource(0, nullptr, 0);
				RHICmdList.DrawIndexedPrimitive(GScatterQuadIndexBuffer.IndexBufferRHI, 0, 0, 32, 0, 2 * QuadsPerInstance, FMath::DivideAndRoundUp(VelocityTileCount.X * VelocityTileCount.Y, QuadsPerInstance));
			}
		});
	}
	else
	{
		FMotionBlurVelocityDilateGatherCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FMotionBlurVelocityDilateGatherCS::FParameters>();
		PassParameters->Dilate = VelocityDilateParameters;
		PassParameters->OutVelocityTileTexture = GraphBuilder.CreateUAV(VelocityTileTexture);

		TShaderMapRef<FMotionBlurVelocityDilateGatherCS> ComputeShader(View.ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("VelocityTileGatherCS %dx%d", VelocityTileCount.X, VelocityTileCount.Y),
			*ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(VelocityTileCount, FMotionBlurVelocityDilateGatherCS::ThreadGroupSize));
	}

	*VelocityFlatTextureOutput = VelocityFlatTexture;
	*VelocityTileTextureOutput = VelocityTileTexture;
}

FRDGTextureRef AddMotionBlurFilterPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FMotionBlurViewports& Viewports,
	FRDGTextureRef ColorTexture,
	FRDGTextureRef VelocityFlatTexture,
	FRDGTextureRef VelocityTileTexture,
	EMotionBlurFilterPass MotionBlurFilterPass,
	EMotionBlurQuality MotionBlurQuality)
{
	check(ColorTexture);
	check(VelocityFlatTexture);
	check(VelocityTileTexture);
	check(MotionBlurFilterPass != EMotionBlurFilterPass::MAX);
	check(MotionBlurQuality != EMotionBlurQuality::MAX);

	const float MotionBlur2ndScale = CVarMotionBlur2ndScale.GetValueOnRenderThread();

	const bool bUseCompute = View.bUseComputePasses;

	const float BlurScaleLUT[static_cast<uint32>(EMotionBlurFilterPass::MAX)][static_cast<uint32>(EMotionBlurQuality::MAX)] =
	{
		// Separable0
		{
			1.0f - 0.5f / 4.0f,
			1.0f - 0.5f / 6.0f,
			1.0f - 0.5f / 8.0f,
			1.0f - 0.5f / 16.0f
		},

		// Separable1
		{
			1.0f / 4.0f  * MotionBlur2ndScale,
			1.0f / 6.0f  * MotionBlur2ndScale,
			1.0f / 8.0f  * MotionBlur2ndScale,
			1.0f / 16.0f * MotionBlur2ndScale
		},

		// Unified
		{
			1.0f,
			1.0f,
			1.0f,
			1.0f
		}
	};

	const float BlurScale = BlurScaleLUT[static_cast<uint32>(MotionBlurFilterPass)][static_cast<uint32>(MotionBlurQuality)];

	FMotionBlurFilterParameters MotionBlurFilterParameters;
	MotionBlurFilterParameters.MotionBlur = GetMotionBlurParameters(View, Viewports.Color.Rect.Size(), BlurScale);
	MotionBlurFilterParameters.Color = Viewports.ColorParameters;
	MotionBlurFilterParameters.Velocity = Viewports.VelocityParameters;
	MotionBlurFilterParameters.VelocityTile = Viewports.VelocityTileParameters;
	MotionBlurFilterParameters.ColorToVelocity = Viewports.ColorToVelocityTransform;
	MotionBlurFilterParameters.ColorToVelocityTile = Viewports.ColorToVelocityTileTransform;
	MotionBlurFilterParameters.ColorTexture = ColorTexture;
	MotionBlurFilterParameters.VelocityFlatTexture = VelocityFlatTexture;
	MotionBlurFilterParameters.VelocityTileTexture = VelocityTileTexture;
	MotionBlurFilterParameters.ColorSampler = GetMotionBlurColorSampler();
	MotionBlurFilterParameters.VelocitySampler = GetMotionBlurVelocitySampler();
	MotionBlurFilterParameters.VelocityTileSampler = GetMotionBlurVelocitySampler();
	MotionBlurFilterParameters.VelocityFlatSampler = GetMotionBlurVelocitySampler();

	FRDGTextureDesc OutColorDesc = ColorTexture->Desc;
	OutColorDesc.Reset();
	OutColorDesc.TargetableFlags &= ~(TexCreate_RenderTargetable | TexCreate_UAV);
	OutColorDesc.TargetableFlags |= bUseCompute ? TexCreate_UAV : TexCreate_RenderTargetable;
	OutColorDesc.Flags |= GFastVRamConfig.MotionBlur;
	OutColorDesc.AutoWritable = false;
	OutColorDesc.Format =  IsPostProcessingWithAlphaChannelSupported() ? PF_FloatRGBA : PF_FloatRGB;

	FRDGTextureRef ColorTextureOutput = GraphBuilder.CreateTexture(OutColorDesc, TEXT("MotionBlur"));

	FMotionBlurFilterPermutationDomain PermutationVector;
	PermutationVector.Set<FMotionBlurQualityDimension>(MotionBlurQuality);

	if (bUseCompute)
	{
		FMotionBlurFilterCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FMotionBlurFilterCS::FParameters>();
		PassParameters->Filter = MotionBlurFilterParameters;
		PassParameters->OutColorTexture = GraphBuilder.CreateUAV(ColorTextureOutput);

		TShaderMapRef<FMotionBlurFilterCS> ComputeShader(View.ShaderMap, PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Motion Blur %dx%d (CS)", Viewports.Color.Rect.Width(), Viewports.Color.Rect.Height()),
			*ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(Viewports.Color.Rect.Size(), FComputeShaderUtils::kGolden2DGroupSize));
	}
	else
	{
		FMotionBlurFilterPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FMotionBlurFilterPS::FParameters>();
		PassParameters->Filter = MotionBlurFilterParameters;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(ColorTextureOutput, View.GetOverwriteLoadAction());

		TShaderMapRef<FMotionBlurFilterPS> PixelShader(View.ShaderMap, PermutationVector);

		AddDrawScreenPass(
			GraphBuilder,
			RDG_EVENT_NAME("Motion Blur %dx%d (PS)", Viewports.Color.Rect.Width(), Viewports.Color.Rect.Height()),
			View,
			Viewports.Color,
			Viewports.Color,
			*PixelShader,
			PassParameters,
			EScreenPassDrawFlags::AllowHMDHiddenAreaMask);
	}

	return ColorTextureOutput;
}

FRDGTextureRef AddVisualizeMotionBlurPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FIntRect ColorViewportRect,
	FIntRect VelocityViewportRect,
	FRDGTextureRef ColorTexture,
	FRDGTextureRef DepthTexture,
	FRDGTextureRef VelocityTexture)
{
	const FScreenPassTextureViewport ColorViewport(ColorTexture, ColorViewportRect);
	const FScreenPassTextureViewport VelocityViewport(VelocityTexture, VelocityViewportRect);
	const FMotionBlurViewports Viewports(ColorViewport, VelocityViewport);

	FRDGTextureDesc OutputDesc = ColorTexture->Desc;
	OutputDesc.TargetableFlags &= ~(TexCreate_RenderTargetable | TexCreate_UAV);
	OutputDesc.TargetableFlags |= TexCreate_RenderTargetable;

	FScreenPassRenderTarget Output;
	Output.Texture = GraphBuilder.CreateTexture(OutputDesc, TEXT("MotionBlurVisualize"));
	Output.ViewRect = ColorViewport.Rect;
	Output.LoadAction = View.GetOverwriteLoadAction();

	FMotionBlurVisualizePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FMotionBlurVisualizePS::FParameters>();
	PassParameters->WorldToClipPrev = GetPreviousWorldToClipMatrix(View);
	PassParameters->View = View.ViewUniformBuffer;
	PassParameters->ColorTexture = ColorTexture;
	PassParameters->DepthTexture = DepthTexture;
	PassParameters->VelocityTexture = VelocityTexture;
	PassParameters->Color = Viewports.ColorParameters;
	PassParameters->Velocity = Viewports.VelocityParameters;
	PassParameters->ColorToVelocity = Viewports.ColorToVelocityTransform;
	PassParameters->ColorSampler = GetMotionBlurColorSampler();
	PassParameters->VelocitySampler = GetMotionBlurVelocitySampler();
	PassParameters->DepthSampler = GetMotionBlurVelocitySampler();
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	TShaderMapRef<FMotionBlurVisualizePS> PixelShader(View.ShaderMap);

	AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("Visualizer"), View, ColorViewport, ColorViewport, *PixelShader, PassParameters);

	Output.LoadAction = ERenderTargetLoadAction::ELoad;

	AddDrawCanvasPass(GraphBuilder, RDG_EVENT_NAME("Overlay"), View, Output,
		[&View](FCanvas& Canvas)
	{
		float X = 20;
		float Y = 38;
		const float YStep = 14;
		const float ColumnWidth = 200;

		FString Line;

		Line = FString::Printf(TEXT("Visualize MotionBlur"));
		Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), FLinearColor(1, 1, 0));

		static const auto MotionBlurDebugVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MotionBlurDebug"));
		const int32 MotionBlurDebug = MotionBlurDebugVar ? MotionBlurDebugVar->GetValueOnRenderThread() : 0;

		Line = FString::Printf(TEXT("%d, %d"), View.Family->FrameNumber, MotionBlurDebug);
		Canvas.DrawShadowedString(X, Y += YStep, TEXT("FrameNo, r.MotionBlurDebug:"), GetStatsFont(), FLinearColor(1, 1, 0));
		Canvas.DrawShadowedString(X + ColumnWidth, Y, *Line, GetStatsFont(), FLinearColor(1, 1, 0));

		static const auto VelocityTestVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.VelocityTest"));
		const int32 VelocityTest = VelocityTestVar ? VelocityTestVar->GetValueOnRenderThread() : 0;

		extern bool IsParallelVelocity();

		Line = FString::Printf(TEXT("%d, %d, %d"), View.Family->bWorldIsPaused, VelocityTest, IsParallelVelocity());
		Canvas.DrawShadowedString(X, Y += YStep, TEXT("Paused, r.VelocityTest, Parallel:"), GetStatsFont(), FLinearColor(1, 1, 0));
		Canvas.DrawShadowedString(X + ColumnWidth, Y, *Line, GetStatsFont(), FLinearColor(1, 1, 0));

		const FScene* Scene = (const FScene*)View.Family->Scene;
		const FSceneViewState *SceneViewState = (const FSceneViewState*)View.State;

		Line = FString::Printf(TEXT("View=%.4x PrevView=%.4x"),
			View.ViewMatrices.GetViewMatrix().ComputeHash() & 0xffff,
			View.PrevViewInfo.ViewMatrices.GetViewMatrix().ComputeHash() & 0xffff);
		Canvas.DrawShadowedString(X, Y += YStep, TEXT("ViewMatrix:"), GetStatsFont(), FLinearColor(1, 1, 0));
		Canvas.DrawShadowedString(X + ColumnWidth, Y, *Line, GetStatsFont(), FLinearColor(1, 1, 0));
	});

	return Output.Texture;
}

FRDGTextureRef AddMotionBlurPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FIntRect ColorViewportRect,
	FIntRect VelocityViewportRect,
	FRDGTextureRef ColorTexture,
	FRDGTextureRef DepthTexture,
	FRDGTextureRef VelocityTexture)
{
	check(ColorTexture);
	check(DepthTexture);
	check(VelocityTexture);

	const FMotionBlurViewports Viewports(
		FScreenPassTextureViewport(ColorTexture, ColorViewportRect),
		FScreenPassTextureViewport(VelocityTexture, VelocityViewportRect));

	RDG_EVENT_SCOPE(GraphBuilder, "MotionBlur");

	FRDGTextureRef VelocityFlatTexture = nullptr;
	FRDGTextureRef VelocityTileTexture = nullptr;
	AddMotionBlurVelocityPass(
		GraphBuilder,
		View,
		Viewports,
		ColorTexture,
		DepthTexture,
		VelocityTexture,
		&VelocityFlatTexture,
		&VelocityTileTexture);

	const EMotionBlurQuality MotionBlurQuality = GetMotionBlurQuality();

	const bool bUseSeparableFilter = CVarMotionBlurSeparable.GetValueOnRenderThread() != 0;

	if (bUseSeparableFilter)
	{
		FRDGTextureRef MotionBlurFilterTexture = AddMotionBlurFilterPass(
			GraphBuilder,
			View,
			Viewports,
			ColorTexture,
			VelocityFlatTexture,
			VelocityTileTexture,
			EMotionBlurFilterPass::Separable0,
			MotionBlurQuality);

		return AddMotionBlurFilterPass(
			GraphBuilder,
			View,
			Viewports,
			MotionBlurFilterTexture,
			VelocityFlatTexture,
			VelocityTileTexture,
			EMotionBlurFilterPass::Separable1,
			MotionBlurQuality);
	}
	else
	{
		return AddMotionBlurFilterPass(
			GraphBuilder,
			View,
			Viewports,
			ColorTexture,
			VelocityFlatTexture,
			VelocityTileTexture,
			EMotionBlurFilterPass::Unified,
			MotionBlurQuality);
	}
}