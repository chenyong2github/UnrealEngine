// Copyright Epic Games, Inc. All Rights Reserved.

#include "RendererPrivate.h"
#include "GlobalShader.h"
#include "DeferredShadingRenderer.h"
#include "SceneTextureParameters.h"

#if RHI_RAYTRACING

#include "ClearQuad.h"
#include "SceneRendering.h"
#include "SceneRenderTargets.h"
#include "RHIResources.h"
#include "SystemTextures.h"
#include "ScreenSpaceDenoise.h"
#include "PostProcess/PostProcessing.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PipelineStateCache.h"
#include "RayTracing/RaytracingOptions.h"
#include "Raytracing/RaytracingLighting.h"


static TAutoConsoleVariable<int32> CVarRayTracingTranslucency(
	TEXT("r.RayTracing.Translucency"),
	-1,
	TEXT("-1: Value driven by postprocess volume (default) \n")
	TEXT(" 0: ray tracing translucency off (use raster) \n")
	TEXT(" 1: ray tracing translucency enabled"),
	ECVF_RenderThreadSafe);

static float GRayTracingTranslucencyMaxRoughness = -1;
static FAutoConsoleVariableRef CVarRayTracingTranslucencyMaxRoughness(
	TEXT("r.RayTracing.Translucency.MaxRoughness"),
	GRayTracingTranslucencyMaxRoughness,
	TEXT("Sets the maximum roughness until which ray tracing reflections will be visible (default = -1 (max roughness driven by postprocessing volume))")
);

static int32 GRayTracingTranslucencyMaxRefractionRays = -1;
static FAutoConsoleVariableRef CVarRayTracingTranslucencyMaxRefractionRays(
	TEXT("r.RayTracing.Translucency.MaxRefractionRays"),
	GRayTracingTranslucencyMaxRefractionRays,
	TEXT("Sets the maximum number of refraction rays for ray traced translucency (default = -1 (max bounces driven by postprocessing volume)"));

static int32 GRayTracingTranslucencyEmissiveAndIndirectLighting = 1;
static FAutoConsoleVariableRef CVarRayTracingTranslucencyEmissiveAndIndirectLighting(
	TEXT("r.RayTracing.Translucency.EmissiveAndIndirectLighting"),
	GRayTracingTranslucencyEmissiveAndIndirectLighting,
	TEXT("Enables ray tracing translucency emissive and indirect lighting (default = 1)")
);

static int32 GRayTracingTranslucencyDirectLighting = 1;
static FAutoConsoleVariableRef CVarRayTracingTranslucencyDirectLighting(
	TEXT("r.RayTracing.Translucency.DirectLighting"),
	GRayTracingTranslucencyDirectLighting,
	TEXT("Enables ray tracing translucency direct lighting (default = 1)")
);

static int32 GRayTracingTranslucencyShadows = -1;
static FAutoConsoleVariableRef CVarRayTracingTranslucencyShadows(
	TEXT("r.RayTracing.Translucency.Shadows"),
	GRayTracingTranslucencyShadows,
	TEXT("Enables shadows in ray tracing translucency)")
	TEXT(" -1: Shadows driven by postprocessing volume (default)")
	TEXT(" 0: Shadows disabled ")
	TEXT(" 1: Hard shadows")
	TEXT(" 2: Soft area shadows")
);

static float GRayTracingTranslucencyMinRayDistance = -1;
static FAutoConsoleVariableRef CVarRayTracingTranslucencyMinRayDistance(
	TEXT("r.RayTracing.Translucency.MinRayDistance"),
	GRayTracingTranslucencyMinRayDistance,
	TEXT("Sets the minimum ray distance for ray traced translucency rays. Actual translucency ray length is computed as Lerp(MaxRayDistance, MinRayDistance, Roughness), i.e. translucency rays become shorter when traced from rougher surfaces. (default = -1 (infinite rays))")
);

static float GRayTracingTranslucencyMaxRayDistance = -1;
static FAutoConsoleVariableRef CVarRayTracingTranslucencyMaxRayDistance(
	TEXT("r.RayTracing.Translucency.MaxRayDistance"),
	GRayTracingTranslucencyMaxRayDistance,
	TEXT("Sets the maximum ray distance for ray traced translucency rays. When ray shortening is used, skybox will not be sampled in RT translucency pass and will be composited later, together with local reflection captures. Negative values turn off this optimization. (default = -1 (infinite rays))")
);

static int32 GRayTracingTranslucencySamplesPerPixel = 1;
static FAutoConsoleVariableRef CVarRayTracingTranslucencySamplesPerPixel(
	TEXT("r.RayTracing.Translucency.SamplesPerPixel"),
	GRayTracingTranslucencySamplesPerPixel,
	TEXT("Sets the samples-per-pixel for Translucency (default = 1)"));

static int32 GRayTracingTranslucencyHeightFog = 1;
static FAutoConsoleVariableRef CVarRayTracingTranslucencyHeightFog(
	TEXT("r.RayTracing.Translucency.HeightFog"),
	GRayTracingTranslucencyHeightFog,
	TEXT("Enables height fog in ray traced Translucency (default = 1)"));

static int32 GRayTracingTranslucencyRefraction = -1;
static FAutoConsoleVariableRef CVarRayTracingTranslucencyRefraction(
	TEXT("r.RayTracing.Translucency.Refraction"),
	GRayTracingTranslucencyRefraction,
	TEXT("Enables refraction in ray traced Translucency (default = 1)"));

static float GRayTracingTranslucencyPrimaryRayBias = 1e-5;
static FAutoConsoleVariableRef CVarRayTracingTranslucencyPrimaryRayBias(
	TEXT("r.RayTracing.Translucency.PrimaryRayBias"),
	GRayTracingTranslucencyPrimaryRayBias,
	TEXT("Sets the bias to be subtracted from the primary ray TMax in ray traced Translucency. Larger bias reduces the chance of opaque objects being intersected in ray traversal, saving performance, but at the risk of skipping some thin translucent objects in proximity of opaque objects. (recommended range: 0.00001 - 0.1) (default = 0.00001)"));

DECLARE_GPU_STAT_NAMED(RayTracingTranslucency, TEXT("Ray Tracing Translucency"));

#if RHI_RAYTRACING

FRayTracingPrimaryRaysOptions GetRayTracingTranslucencyOptions()
{
	FRayTracingPrimaryRaysOptions Options;

	Options.IsEnabled = CVarRayTracingTranslucency.GetValueOnRenderThread();
	Options.SamplerPerPixel = GRayTracingTranslucencySamplesPerPixel;
	Options.ApplyHeightFog = GRayTracingTranslucencyHeightFog;
	Options.PrimaryRayBias = GRayTracingTranslucencyPrimaryRayBias;
	Options.MaxRoughness = GRayTracingTranslucencyMaxRoughness;
	Options.MaxRefractionRays = GRayTracingTranslucencyMaxRefractionRays;
	Options.EnableEmmissiveAndIndirectLighting = GRayTracingTranslucencyEmissiveAndIndirectLighting;
	Options.EnableDirectLighting = GRayTracingTranslucencyDirectLighting;
	Options.EnableShadows = GRayTracingTranslucencyShadows;
	Options.MinRayDistance = GRayTracingTranslucencyMinRayDistance;
	Options.MaxRayDistance = GRayTracingTranslucencyMaxRayDistance;
	Options.EnableRefraction = GRayTracingTranslucencyRefraction;

	return Options;
}

bool ShouldRenderRayTracingTranslucency(const FViewInfo& View)
{
	bool bViewWithRaytracingTranslucency = View.FinalPostProcessSettings.TranslucencyType == ETranslucencyType::RayTracing;

	const int32 GRayTracingTranslucency = CVarRayTracingTranslucency.GetValueOnRenderThread();
	const bool bTranslucencyCvarEnabled = GRayTracingTranslucency < 0 ? bViewWithRaytracingTranslucency : (GRayTracingTranslucency != 0);
	const int32 ForceAllRayTracingEffects = GetForceRayTracingEffectsCVarValue();
	const bool bRayTracingTranslucencyEnabled = (ForceAllRayTracingEffects > 0 || (bTranslucencyCvarEnabled && ForceAllRayTracingEffects < 0));

	return IsRayTracingEnabled() && bRayTracingTranslucencyEnabled;
}
#endif // RHI_RAYTRACING

//#dxr-todo: should we unify it with the composition happening in the non raytraced translucency pass? In that case it should use FCopySceneColorPS
// Probably, but the architecture depends on the denoiser -> discuss

class FCompositeTranslucencyPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FCompositeTranslucencyPS, Global);

public:
	static bool ShouldCache(EShaderPlatform Platform)
	{
		return ShouldCompileRayTracingShadersForProject(Platform);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	FCompositeTranslucencyPS() {}
	virtual ~FCompositeTranslucencyPS() {}

	FCompositeTranslucencyPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		SceneTextureParameters.Bind(Initializer);
		TranslucencyTextureParameter.Bind(Initializer.ParameterMap, TEXT("TranslucencyTexture"));
		TranslucencyTextureSamplerParameter.Bind(Initializer.ParameterMap, TEXT("TranslucencyTextureSampler"));
	}

	template<typename TRHICommandList>
	void SetParameters(
		TRHICommandList& RHICmdList,
		const FViewInfo& View,
		FRHITexture* TranslucencyTexture,
		FRHITexture* HitDistanceTexture)
	{
		FRHIPixelShader* ShaderRHI = GetPixelShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);
		SceneTextureParameters.Set(RHICmdList, ShaderRHI, View.FeatureLevel, ESceneTextureSetupMode::All);

		SetTextureParameter(RHICmdList, ShaderRHI, TranslucencyTextureParameter, TranslucencyTextureSamplerParameter, TStaticSamplerState<SF_Bilinear>::GetRHI(), TranslucencyTexture);
		// #dxr_todo: UE-72581 Use hit-distance texture for denoising
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << TranslucencyTextureParameter;
		Ar << TranslucencyTextureSamplerParameter;
		return bShaderHasOutdatedParameters;
	}

private:
	FSceneTextureShaderParameters SceneTextureParameters;
	FShaderResourceParameter TranslucencyTextureParameter;
	FShaderResourceParameter TranslucencyTextureSamplerParameter;
};

IMPLEMENT_SHADER_TYPE(, FCompositeTranslucencyPS, TEXT("/Engine/Private/RayTracing/CompositeTranslucencyPS.usf"), TEXT("CompositeTranslucencyPS"), SF_Pixel)


void FDeferredShadingSceneRenderer::RenderRayTracingTranslucency(FRHICommandListImmediate& RHICmdList)
{
	if (!ShouldRenderTranslucency(ETranslucencyPass::TPT_StandardTranslucency)
		&& !ShouldRenderTranslucency(ETranslucencyPass::TPT_TranslucencyAfterDOF)
		&& !ShouldRenderTranslucency(ETranslucencyPass::TPT_AllTranslucency)
		)
	{
		return; // Early exit if nothing needs to be done.
	}

	for (int32 ViewIndex = 0, Num = Views.Num(); ViewIndex < Num; ViewIndex++)
	{
		FViewInfo& View = Views[ViewIndex];

		SCOPED_DRAW_EVENT(RHICmdList, RayTracingTranslucency);
		SCOPED_GPU_STAT(RHICmdList, RayTracingTranslucency);

		FRDGBuilder GraphBuilder(RHICmdList); 
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);

		FSceneTextureParameters SceneTextures;
		SetupSceneTextureParameters(GraphBuilder, &SceneTextures);

		//#dxr_todo: UE-72581 do not use reflections denoiser structs but separated ones
		IScreenSpaceDenoiser::FReflectionsInputs DenoiserInputs;
		float ResolutionFraction = 1.0f;
		int32 TranslucencySPP = GRayTracingTranslucencySamplesPerPixel > -1 ? GRayTracingTranslucencySamplesPerPixel : View.FinalPostProcessSettings.RayTracingTranslucencySamplesPerPixel;
		
		RenderRayTracingPrimaryRaysView(
			GraphBuilder,
			View, &DenoiserInputs.Color, &DenoiserInputs.RayHitDistance,
			TranslucencySPP, GRayTracingTranslucencyHeightFog, ResolutionFraction, 
			ERayTracingPrimaryRaysFlag::AllowSkipSkySample | ERayTracingPrimaryRaysFlag::UseGBufferForMaxDistance);

		//#dxr_todo: UE-72581 : replace DenoiserInputs with DenoiserOutputs in the following lines!
		TRefCountPtr<IPooledRenderTarget> TranslucencyColor = GSystemTextures.BlackDummy;
		TRefCountPtr<IPooledRenderTarget> TranslucencyHitDistanceColor = GSystemTextures.BlackDummy;

		GraphBuilder.QueueTextureExtraction(DenoiserInputs.Color, &TranslucencyColor);
		GraphBuilder.QueueTextureExtraction(DenoiserInputs.RayHitDistance, &TranslucencyHitDistanceColor);

		GraphBuilder.Execute();

		// Compositing result with the scene color
		//#dxr-todo: should we unify it with the composition happening in the non raytraced translucency pass? In that case it should use FCopySceneColorPS
		// Probably, but the architecture depends on the denoiser -> discuss
		{
			const auto ShaderMap = GetGlobalShaderMap(FeatureLevel);
			TShaderMapRef<FPostProcessVS> VertexShader(ShaderMap);
			TShaderMapRef<FCompositeTranslucencyPS> PixelShader(ShaderMap);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			SceneContext.BeginRenderingSceneColor(RHICmdList, ESimpleRenderTargetMode::EExistingColorAndDepth, FExclusiveDepthStencil::DepthRead_StencilWrite, true);
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			RHICmdList.SetViewport((float)View.ViewRect.Min.X, (float)View.ViewRect.Min.Y, 0.0f, (float)View.ViewRect.Max.X, (float)View.ViewRect.Max.Y, 1.0f);
			PixelShader->SetParameters(RHICmdList, View, TranslucencyColor->GetRenderTargetItem().ShaderResourceTexture, TranslucencyHitDistanceColor->GetRenderTargetItem().ShaderResourceTexture);

			DrawRectangle(
				RHICmdList,
				0, 0,
				View.ViewRect.Width(), View.ViewRect.Height(),
				View.ViewRect.Min.X, View.ViewRect.Min.Y,
				View.ViewRect.Width(), View.ViewRect.Height(),
				FIntPoint(View.ViewRect.Width(), View.ViewRect.Height()),
				SceneContext.GetBufferSizeXY(),
				*VertexShader);
		}

		ResolveSceneColor(RHICmdList);
		SceneContext.FinishRenderingSceneColor(RHICmdList);
	}
}

#endif // RHI_RAYTRACING
