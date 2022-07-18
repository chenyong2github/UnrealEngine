// Copyright Epic Games, Inc. All Rights Reserved.

#include "SingleLayerWaterRendering.h"
#include "BasePassRendering.h"
#include "DeferredShadingRenderer.h"
#include "MeshPassProcessor.inl"
#include "PixelShaderUtils.h"
#include "PostProcess/PostProcessSubsurface.h"
#include "PostProcess/SceneRenderTargets.h"
#include "TemporalAA.h"
#include "RayTracing/RaytracingOptions.h"
#include "RayTracing/RayTracingReflections.h"
#include "VolumetricRenderTarget.h"
#include "RenderGraph.h"
#include "ScenePrivate.h"
#include "SceneRendering.h"
#include "ScreenSpaceRayTracing.h"
#include "SceneTextureParameters.h"
#include "Strata/Strata.h"
#include "VirtualShadowMaps/VirtualShadowMapArray.h"
#include "Lumen/LumenSceneData.h"
#include "Lumen/LumenTracingUtils.h"

DECLARE_GPU_STAT_NAMED(RayTracingWaterReflections, TEXT("Ray Tracing Water Reflections"));

DECLARE_GPU_STAT(SingleLayerWater);
DECLARE_CYCLE_STAT(TEXT("WaterSingleLayer"), STAT_CLP_WaterSingleLayerPass, STATGROUP_ParallelCommandListMarkers);

static TAutoConsoleVariable<int32> CVarWaterSingleLayer(
	TEXT("r.Water.SingleLayer"), 1,
	TEXT("Enable the single water rendering system."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarWaterSingleLayerReflection(
	TEXT("r.Water.SingleLayer.Reflection"), 1,
	TEXT("Enable reflection rendering on water."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarWaterSingleLayerTiledComposite(
	TEXT("r.Water.SingleLayer.TiledComposite"), 1,
	TEXT("Enable tiled optimisation of the water reflection rendering."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

int32 GSingleLayerWaterRefractionDownsampleFactor = 1;
static FAutoConsoleVariableRef CVarWaterSingleLayerRefractionDownsampleFactor(
	TEXT("r.Water.SingleLayer.RefractionDownsampleFactor"),
	GSingleLayerWaterRefractionDownsampleFactor,
	TEXT("Resolution divider for the water refraction buffer."),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarParallelSingleLayerWaterPass(
	TEXT("r.ParallelSingleLayerWaterPass"),
	1,
	TEXT("Toggles parallel single layer water pass rendering. Parallel rendering must be enabled for this to have an effect."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarWaterSingleLayerSSR(
	TEXT("r.Water.SingleLayer.SSR"), 1,
	TEXT("Enable SSR for the single water rendering system."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarWaterSingleLayerLumenReflections(
	TEXT("r.Water.SingleLayer.LumenReflections"), 1,
	TEXT("Enable Lumen reflections for the single water rendering system."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarWaterSingleLayerShadersSupportDistanceFieldShadow(
	TEXT("r.Water.SingleLayer.ShadersSupportDistanceFieldShadow"),
	1,
	TEXT("Whether or not the single layer water material shaders are compiled with support for distance field shadow, i.e. output main directional light luminance in a separate render target. This is preconditioned on using deferred shading and having distance field support enabled in the project."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

// The project setting for the cloud shadow to affect SingleLayerWater (enable/disable runtime and shader code).  This is not implemented on mobile as VolumetricClouds are not available on these platforms.
static TAutoConsoleVariable<int32> CVarSupportCloudShadowOnSingleLayerWater(
	TEXT("r.Water.SingleLayerWater.SupportCloudShadow"),
	0,
	TEXT("Enables cloud shadows on SingleLayerWater materials."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarWaterSingleLayerDistanceFieldShadow(
	TEXT("r.Water.SingleLayer.DistanceFieldShadow"), 1,
	TEXT("When using deferred, distance field shadow tracing is supported on single layer water. This cvar can be used to toggle it on/off at runtime."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarWaterSingleLayerRTR(
	TEXT("r.Water.SingleLayer.RTR"), 1,
	TEXT("Enable RTR for the single water renderring system."),
	ECVF_RenderThreadSafe | ECVF_Scalability);
static TAutoConsoleVariable<int32> CVarWaterSingleLayerSSRTAA(
	TEXT("r.Water.SingleLayer.SSRTAA"), 1,
	TEXT("Enable SSR denoising using TAA for the single water renderring system."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarRHICmdFlushRenderThreadTasksSingleLayerWater(
	TEXT("r.RHICmdFlushRenderThreadTasksSingleLayerWater"),
	0,
	TEXT("Wait for completion of parallel render thread tasks at the end of Single layer water. A more granular version of r.RHICmdFlushRenderThreadTasks. If either r.RHICmdFlushRenderThreadTasks or r.RHICmdFlushRenderThreadTasksSingleLayerWater is > 0 we will flush."));

// This is to have platforms use the simple single layer water shading similar to mobile: no dynamic lights, only sun and sky, no distortion, no colored transmittance on background, no custom depth read.
bool SingleLayerWaterUsesSimpleShading(EShaderPlatform ShaderPlatform)
{
	return FDataDrivenShaderPlatformInfo::GetWaterUsesSimpleForwardShading(ShaderPlatform) && IsForwardShadingEnabled(ShaderPlatform);
}

bool ShouldRenderSingleLayerWater(TArrayView<const FViewInfo> Views)
{
	if (CVarWaterSingleLayer.GetValueOnRenderThread() > 0)
	{
		for (const FViewInfo& View : Views)
		{
			if (View.bHasSingleLayerWaterMaterial)
			{
				return true;
			}
		}
	}
	return false;
}

bool ShouldRenderSingleLayerWaterSkippedRenderEditorNotification(TArrayView<const FViewInfo> Views)
{
	if (CVarWaterSingleLayer.GetValueOnRenderThread() <= 0)
	{
		for (const FViewInfo& View : Views)
		{
			if (View.bHasSingleLayerWaterMaterial)
			{
				return true;
			}
		}
	}
	return false;
}

bool ShouldUseBilinearSamplerForDepthWithoutSingleLayerWater(EPixelFormat DepthTextureFormat)
{
	const bool bHasDownsampling = GSingleLayerWaterRefractionDownsampleFactor > 1;
	const bool bSupportsLinearSampling = !!(GPixelFormats[DepthTextureFormat].Capabilities & EPixelFormatCapabilities::TextureSample);
	
	// Linear sampling is only required if the depth texture has been downsampled.
	return bHasDownsampling && bSupportsLinearSampling;
}

bool UseSingleLayerWaterIndirectDraw(EShaderPlatform ShaderPlatform)
{
	return IsFeatureLevelSupported(ShaderPlatform, ERHIFeatureLevel::SM5)
		// Vulkan gives error with WaterTileCatergorisationCS usage of atomic, and Metal does not play nice, either.
		&& !IsVulkanMobilePlatform(ShaderPlatform)
		&& FDataDrivenShaderPlatformInfo::GetSupportsWaterIndirectDraw(ShaderPlatform);
}

bool IsWaterDistanceFieldShadowEnabled_Runtime(const FStaticShaderPlatform Platform)
{
	return IsWaterDistanceFieldShadowEnabled(Platform) && CVarWaterSingleLayerDistanceFieldShadow.GetValueOnRenderThread() > 0;
}

BEGIN_SHADER_PARAMETER_STRUCT(FSingleLayerWaterCommonShaderParameters, )
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ScreenSpaceReflectionsTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, ScreenSpaceReflectionsSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D, PreIntegratedGF)
	SHADER_PARAMETER_SAMPLER(SamplerState, PreIntegratedGFSampler)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneNoWaterDepthTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, SceneNoWaterDepthSampler)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SeparatedMainDirLightTexture)
	SHADER_PARAMETER(FVector4f, SceneNoWaterMinMaxUV)
	SHADER_PARAMETER(FVector2f, SceneNoWaterTextureSize)
	SHADER_PARAMETER(FVector2f, SceneNoWaterInvTextureSize)
	SHADER_PARAMETER(float, UseSeparatedMainDirLightTexture)
	SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)	// Water scene texture
	SHADER_PARAMETER_STRUCT_INCLUDE(FViewShaderParameters, View)
	SHADER_PARAMETER_STRUCT_REF(FReflectionCaptureShaderData, ReflectionCaptureData)
	SHADER_PARAMETER_STRUCT_REF(FReflectionUniformParameters, ReflectionsParameters)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FForwardLightData, ForwardLightData)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FStrataGlobalUniformParameters, Strata)
END_SHADER_PARAMETER_STRUCT()

class FSingleLayerWaterCompositePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSingleLayerWaterCompositePS);
	SHADER_USE_PARAMETER_STRUCT(FSingleLayerWaterCompositePS, FGlobalShader)

	class FHasBoxCaptures : SHADER_PERMUTATION_BOOL("REFLECTION_COMPOSITE_HAS_BOX_CAPTURES");
	class FHasSphereCaptures : SHADER_PERMUTATION_BOOL("REFLECTION_COMPOSITE_HAS_SPHERE_CAPTURES");
	using FPermutationDomain = TShaderPermutationDomain<FHasBoxCaptures, FHasSphereCaptures>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSingleLayerWaterCommonShaderParameters, CommonParameters)
	END_SHADER_PARAMETER_STRUCT()

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FForwardLightingParameters::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);//Support reflection captures
	}
};

IMPLEMENT_GLOBAL_SHADER(FSingleLayerWaterCompositePS, "/Engine/Private/SingleLayerWaterComposite.usf", "SingleLayerWaterCompositePS", SF_Pixel);

class FWaterTileCategorisationCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FWaterTileCategorisationCS);
	SHADER_USE_PARAMETER_STRUCT(FWaterTileCategorisationCS, FGlobalShader)

	static int32 GetTileSize()
	{
		return 8;
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSingleLayerWaterCommonShaderParameters, CommonParameters)
		SHADER_PARAMETER(uint32, VertexCountPerInstanceIndirect)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, DrawIndirectDataUAV)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, DispatchIndirectDataUAV)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, WaterTileListDataUAV)
	END_SHADER_PARAMETER_STRUCT()

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return UseSingleLayerWaterIndirectDraw(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("TILE_CATERGORISATION_SHADER"), 1.0f);
		OutEnvironment.SetDefine(TEXT("WORK_TILE_SIZE"), GetTileSize());
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

IMPLEMENT_GLOBAL_SHADER(FWaterTileCategorisationCS, "/Engine/Private/SingleLayerWaterComposite.usf", "WaterTileCatergorisationCS", SF_Compute);

bool FWaterTileVS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return UseSingleLayerWaterIndirectDraw(Parameters.Platform);
	}

void FWaterTileVS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("TILE_VERTEX_SHADER"), 1.0f);
		OutEnvironment.SetDefine(TEXT("WORK_TILE_SIZE"), FWaterTileCategorisationCS::GetTileSize());
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

IMPLEMENT_GLOBAL_SHADER(FWaterTileVS, "/Engine/Private/SingleLayerWaterComposite.usf", "WaterTileVS", SF_Vertex);

class FWaterRefractionCopyPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FWaterRefractionCopyPS);
	SHADER_USE_PARAMETER_STRUCT(FWaterRefractionCopyPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FViewShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorCopyDownsampleTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorCopyDownsampleSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthCopyDownsampleTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SceneDepthCopyDownsampleSampler)
		SHADER_PARAMETER(FVector2f, SVPositionToSourceTextureUV)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	class FDownsampleRefraction : SHADER_PERMUTATION_BOOL("DOWNSAMPLE_REFRACTION");
	class FDownsampleColor : SHADER_PERMUTATION_BOOL("DOWNSAMPLE_COLOR");

	using FPermutationDomain = TShaderPermutationDomain<FDownsampleRefraction, FDownsampleColor>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

IMPLEMENT_GLOBAL_SHADER(FWaterRefractionCopyPS, "/Engine/Private/SingleLayerWaterComposite.usf", "WaterRefractionCopyPS", SF_Pixel);

static FSceneWithoutWaterTextures AddCopySceneWithoutWaterPass(
	FRDGBuilder& GraphBuilder,
	const FSceneViewFamily& ViewFamily, 
	TArrayView<const FViewInfo> Views,
	FRDGTextureRef SceneColorTexture,
	FRDGTextureRef SceneDepthTexture)
{
	RDG_EVENT_SCOPE(GraphBuilder, "SLW::CopySceneWithoutWater");

	check(Views.Num() > 0);
	check(SceneColorTexture);
	check(SceneDepthTexture);

	EShaderPlatform ShaderPlatform = Views[0].GetShaderPlatform();
	const bool bCopyColor = !SingleLayerWaterUsesSimpleShading(ShaderPlatform);

	const FRDGTextureDesc& SceneColorDesc = SceneColorTexture->Desc;
	const FRDGTextureDesc& SceneDepthDesc = SceneColorTexture->Desc;

	const int32 RefractionDownsampleFactor = FMath::Clamp(GSingleLayerWaterRefractionDownsampleFactor, 1, 8);
	const FIntPoint RefractionResolution = FIntPoint::DivideAndRoundDown(SceneColorDesc.Extent, RefractionDownsampleFactor);
	FRDGTextureRef SceneColorWithoutSingleLayerWaterTexture = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);

	if (bCopyColor)
	{
		const FRDGTextureDesc ColorDesc = FRDGTextureDesc::Create2D(RefractionResolution, SceneColorDesc.Format, SceneColorDesc.ClearValue, TexCreate_ShaderResource | TexCreate_RenderTargetable);
		SceneColorWithoutSingleLayerWaterTexture = GraphBuilder.CreateTexture(ColorDesc, TEXT("SLW.SceneColorWithout"));
	}

	const FRDGTextureDesc DepthDesc(FRDGTextureDesc::Create2D(RefractionResolution, PF_R32_FLOAT, SceneDepthDesc.ClearValue, TexCreate_ShaderResource | TexCreate_RenderTargetable));
	FRDGTextureRef SceneDepthWithoutSingleLayerWaterTexture = GraphBuilder.CreateTexture(DepthDesc, TEXT("SLW.SceneDepthWithout"));

	const FRDGTextureDesc SeparatedMainDirLightDesc(FRDGTextureDesc::Create2D(SceneColorDesc.Extent, PF_FloatR11G11B10, FClearValueBinding(FLinearColor::White), TexCreate_ShaderResource | TexCreate_RenderTargetable));
	FRDGTextureRef SeparatedMainDirLightTexture = GraphBuilder.CreateTexture(SeparatedMainDirLightDesc, TEXT("SLW.SeparatedMainDirLight"));
	if (IsWaterDistanceFieldShadowEnabled_Runtime(ShaderPlatform) && Strata::IsStrataEnabled())
	{
		// This clear is needed with strata because that texture will be modulated by DFShadows.
		// STRATA_TODO: when strata is enabled, we can change RenderRayTracedDistanceFieldProjection to have a bForceNoBlending instead if bForceRGBModulation and remove that clear.
		AddClearRenderTargetPass(GraphBuilder, SeparatedMainDirLightTexture);
	}

	FSceneWithoutWaterTextures Textures;
	Textures.RefractionDownsampleFactor = float(RefractionDownsampleFactor);
	Textures.Views.SetNum(Views.Num());

	ERenderTargetLoadAction LoadAction = ERenderTargetLoadAction::ENoAction;

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		const FViewInfo& View = Views[ViewIndex];

		if (!View.ShouldRenderView())
		{
			continue;
		}

		RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
		RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);

		FWaterRefractionCopyPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FWaterRefractionCopyPS::FParameters>();
		PassParameters->View = View.GetShaderParameters();
		PassParameters->SceneColorCopyDownsampleTexture = SceneColorTexture;
		PassParameters->SceneColorCopyDownsampleSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->SceneDepthCopyDownsampleTexture = SceneDepthTexture;
		PassParameters->SceneDepthCopyDownsampleSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->SVPositionToSourceTextureUV = FVector2f(RefractionDownsampleFactor / float(SceneColorDesc.Extent.X), RefractionDownsampleFactor / float(SceneColorDesc.Extent.Y));

		PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneDepthWithoutSingleLayerWaterTexture, LoadAction);

		if (bCopyColor)
		{
			PassParameters->RenderTargets[1] = FRenderTargetBinding(SceneColorWithoutSingleLayerWaterTexture, LoadAction);
		}

		if (!View.Family->bMultiGPUForkAndJoin)
		{
			LoadAction = ERenderTargetLoadAction::ELoad;
		}

		FWaterRefractionCopyPS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FWaterRefractionCopyPS::FDownsampleRefraction>(RefractionDownsampleFactor > 1);
		PermutationVector.Set<FWaterRefractionCopyPS::FDownsampleColor>(bCopyColor);
		auto PixelShader = View.ShaderMap->GetShader<FWaterRefractionCopyPS>(PermutationVector);

		// if we have a particular case of ISR where two views are laid out in side by side, we should copy both views at once
		const bool bIsInstancedStereoSideBySide = View.bIsInstancedStereoEnabled && !View.bIsMobileMultiViewEnabled && IStereoRendering::IsStereoEyeView(View);
		FIntRect RectToCopy = View.ViewRect;
		if (bIsInstancedStereoSideBySide)
		{
			const FViewInfo* NeighboringStereoView = View.GetInstancedView();
			if (ensure(NeighboringStereoView))
			{
				RectToCopy.Union(NeighboringStereoView->ViewRect);
			}
		}

		const FIntRect RefractionViewRect = FIntRect(FIntPoint::DivideAndRoundDown(RectToCopy.Min, RefractionDownsampleFactor), FIntPoint::DivideAndRoundDown(RectToCopy.Max, RefractionDownsampleFactor));

		Textures.Views[ViewIndex].ViewRect   = RefractionViewRect;

		// This is usually half a pixel. But it seems that when using Gather4, 0.5 is not conservative enough and can return pixel outside the guard band. 
		// That is why it is a tiny bit higher than 0.5: for Gathre4 to always return pixels within the valid side of UVs (see EvaluateWaterVolumeLighting).
		const float PixelSafeGuardBand = 0.55;
		Textures.Views[ViewIndex].MinMaxUV.X = (RefractionViewRect.Min.X + PixelSafeGuardBand) / RefractionResolution.X;
		Textures.Views[ViewIndex].MinMaxUV.Y = (RefractionViewRect.Min.Y + PixelSafeGuardBand) / RefractionResolution.Y;
		Textures.Views[ViewIndex].MinMaxUV.Z = (RefractionViewRect.Max.X - PixelSafeGuardBand) / RefractionResolution.X;
		Textures.Views[ViewIndex].MinMaxUV.W = (RefractionViewRect.Max.Y - PixelSafeGuardBand) / RefractionResolution.Y;

		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder,
			View.ShaderMap,
			{},
			PixelShader,
			PassParameters,
			RefractionViewRect);
	}

	check(SceneColorWithoutSingleLayerWaterTexture);
	check(SceneDepthWithoutSingleLayerWaterTexture);
	Textures.ColorTexture = SceneColorWithoutSingleLayerWaterTexture;
	Textures.DepthTexture = SceneDepthWithoutSingleLayerWaterTexture;
	Textures.SeparatedMainDirLightTexture = SeparatedMainDirLightTexture;
	return MoveTemp(Textures);
}

BEGIN_SHADER_PARAMETER_STRUCT(FWaterCompositeParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FWaterTileVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FSingleLayerWaterCompositePS::FParameters, PS)
	RDG_BUFFER_ACCESS(IndirectDrawParameter, ERHIAccess::IndirectArgs)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void FDeferredShadingSceneRenderer::RenderSingleLayerWaterReflections(
	FRDGBuilder& GraphBuilder,
	const FSceneTextures& SceneTextures,
	const FSceneWithoutWaterTextures& SceneWithoutWaterTextures,
	FLumenSceneFrameTemporaries& LumenFrameTemporaries)
{
	if (CVarWaterSingleLayer.GetValueOnRenderThread() <= 0 || CVarWaterSingleLayerReflection.GetValueOnRenderThread() <= 0)
	{
		return;
	}

	const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);
	FRDGTextureRef SceneColorTexture = SceneTextures.Color.Resolve;

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& View = Views[ViewIndex];

		// Unfortunately, reflections cannot handle two views at once (yet?) - because of that, allow the secondary pass here.
		// Note: not completely removing ShouldRenderView in case some other reason to not render it is valid.
		if (!View.ShouldRenderView() && !IStereoRendering::IsASecondaryPass(View.StereoPass))
		{
			continue;
		}

		RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
		RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);

		FRDGTextureRef ReflectionsColor = nullptr;
		FRDGTextureRef BlackDummyTexture = SystemTextures.Black;
		FRDGTextureRef WhiteDummyTexture = SystemTextures.White;
		const FSceneTextureParameters SceneTextureParameters = GetSceneTextureParameters(GraphBuilder, SceneTextures);

		auto SetCommonParameters = [&](FSingleLayerWaterCommonShaderParameters& Parameters)
		{
			FIntVector DepthTextureSize = SceneWithoutWaterTextures.DepthTexture ? SceneWithoutWaterTextures.DepthTexture->Desc.GetSize() : FIntVector::ZeroValue;
			const bool bShouldUseBilinearSamplerForDepth = SceneWithoutWaterTextures.DepthTexture && ShouldUseBilinearSamplerForDepthWithoutSingleLayerWater(SceneWithoutWaterTextures.DepthTexture->Desc.Format);

			const bool bIsInstancedStereoSideBySide = View.bIsInstancedStereoEnabled && !View.bIsMobileMultiViewEnabled && IStereoRendering::IsStereoEyeView(View);

			Parameters.ScreenSpaceReflectionsTexture = ReflectionsColor ? ReflectionsColor : BlackDummyTexture;
			Parameters.ScreenSpaceReflectionsSampler = TStaticSamplerState<SF_Point>::GetRHI();
			Parameters.PreIntegratedGF = GSystemTextures.PreintegratedGF->GetRHI();
			Parameters.PreIntegratedGFSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			Parameters.SceneNoWaterDepthTexture = SceneWithoutWaterTextures.DepthTexture ? SceneWithoutWaterTextures.DepthTexture : BlackDummyTexture;
			Parameters.SceneNoWaterDepthSampler = bShouldUseBilinearSamplerForDepth ? TStaticSamplerState<SF_Bilinear>::GetRHI() : TStaticSamplerState<SF_Point>::GetRHI();
			Parameters.SceneNoWaterMinMaxUV = SceneWithoutWaterTextures.Views[bIsInstancedStereoSideBySide ? View.PrimaryViewIndex : ViewIndex].MinMaxUV; // instanced view does not have rect initialized, instead the primary view covers both
			Parameters.SceneNoWaterTextureSize = SceneWithoutWaterTextures.DepthTexture ? FVector2f(DepthTextureSize.X, DepthTextureSize.Y) : FVector2f();
			Parameters.SceneNoWaterInvTextureSize = SceneWithoutWaterTextures.DepthTexture ? FVector2f(1.0f / DepthTextureSize.X, 1.0f / DepthTextureSize.Y) : FVector2f();
			Parameters.SeparatedMainDirLightTexture = BlackDummyTexture;
			Parameters.UseSeparatedMainDirLightTexture = 0.0f;
			Parameters.SceneTextures = SceneTextureParameters;
			Parameters.View = View.GetShaderParameters();
			Parameters.ReflectionCaptureData = View.ReflectionCaptureUniformBuffer;
			{
				FReflectionUniformParameters ReflectionUniformParameters;
				SetupReflectionUniformParameters(View, ReflectionUniformParameters);
				Parameters.ReflectionsParameters = CreateUniformBufferImmediate(ReflectionUniformParameters, UniformBuffer_SingleDraw);
			}
			Parameters.ForwardLightData = View.ForwardLightingResources.ForwardLightUniformBuffer;
			Parameters.Strata = Strata::BindStrataGlobalUniformParameters(View);
		};

		const bool bRunTiled = UseSingleLayerWaterIndirectDraw(View.GetShaderPlatform()) && CVarWaterSingleLayerTiledComposite.GetValueOnRenderThread();
		FTiledReflection TiledScreenSpaceReflection = {nullptr, nullptr, nullptr, 8};
		FIntVector ViewRes(View.ViewRect.Width(), View.ViewRect.Height(), 1);
		FIntVector TiledViewRes = FIntVector::DivideAndRoundUp(ViewRes, TiledScreenSpaceReflection.TileSize);

		if (bRunTiled)
		{
			TiledScreenSpaceReflection.DrawIndirectParametersBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDrawIndirectParameters>(), TEXT("SLW.WaterIndirectDrawParameters"));
			TiledScreenSpaceReflection.DispatchIndirectParametersBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("SLW.WaterIndirectDispatchParameters"));

			FRDGBufferRef TileListDataBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), TiledViewRes.X * TiledViewRes.Y), TEXT("SLW.TileListDataBuffer"));
			TiledScreenSpaceReflection.TileListDataBufferSRV = GraphBuilder.CreateSRV(TileListDataBuffer, PF_R32_UINT);

			FRDGBufferUAVRef DrawIndirectParametersBufferUAV = GraphBuilder.CreateUAV(TiledScreenSpaceReflection.DrawIndirectParametersBuffer);
			FRDGBufferUAVRef DispatchIndirectParametersBufferUAV = GraphBuilder.CreateUAV(TiledScreenSpaceReflection.DispatchIndirectParametersBuffer);
			FRDGBufferUAVRef TileListDataBufferUAV = GraphBuilder.CreateUAV(TileListDataBuffer, PF_R32_UINT);

			// Clear DrawIndirectParametersBuffer
			AddClearUAVPass(GraphBuilder, DrawIndirectParametersBufferUAV, 0);
			AddClearUAVPass(GraphBuilder, DispatchIndirectParametersBufferUAV, 0);

			// Categorization based on SHADING_MODEL_ID
			{
				TShaderMapRef<FWaterTileCategorisationCS> ComputeShader(View.ShaderMap);

				FWaterTileCategorisationCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FWaterTileCategorisationCS::FParameters>();
				SetCommonParameters(PassParameters->CommonParameters);
				PassParameters->VertexCountPerInstanceIndirect = GRHISupportsRectTopology ? 3 : 6;
				PassParameters->DrawIndirectDataUAV = DrawIndirectParametersBufferUAV;
				PassParameters->DispatchIndirectDataUAV = DispatchIndirectParametersBufferUAV;
				PassParameters->WaterTileListDataUAV = TileListDataBufferUAV;

				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("SLW::TileCategorisation"), ComputeShader, PassParameters, TiledViewRes);
			}
		}

		const FPerViewPipelineState& ViewPipelineState = GetViewPipelineState(View);

		if (IsWaterDistanceFieldShadowEnabled_Runtime(View.GetShaderPlatform()))
		{

			FProjectedShadowInfo* DistanceFieldShadowInfo = nullptr;

			// Try to find the ProjectedShadowInfo corresponding to ray trace shadow info for the main directional light.
			const FLightSceneProxy* SelectedForwardDirectionalLightProxy = View.ForwardLightingResources.SelectedForwardDirectionalLightProxy;
			if (SelectedForwardDirectionalLightProxy)
			{
				FLightSceneInfo* LightSceneInfo = SelectedForwardDirectionalLightProxy->GetLightSceneInfo();
				FVisibleLightInfo& VisibleLightViewInfo = VisibleLightInfos[LightSceneInfo->Id];

				for (int32 ShadowIndex = 0; ShadowIndex < VisibleLightViewInfo.ShadowsToProject.Num(); ShadowIndex++)
				{
					FProjectedShadowInfo* ProjectedShadowInfo = VisibleLightViewInfo.ShadowsToProject[ShadowIndex];
					if (ProjectedShadowInfo->bRayTracedDistanceField)
					{
						DistanceFieldShadowInfo = ProjectedShadowInfo;
					}
				}
			}

			// If DFShadow data has been found, then combine it with the separate main directional light luminance texture.
			FRDGTextureRef ScreenShadowMaskTexture = SystemTextures.White;
			if (DistanceFieldShadowInfo)
			{
				RDG_EVENT_SCOPE(GraphBuilder, "SLW::DistanceFieldShadow");

				FIntRect ScissorRect;
				if (!SelectedForwardDirectionalLightProxy->GetScissorRect(ScissorRect, View, View.ViewRect))
				{
					ScissorRect = View.ViewRect;
				}

				// Reset the cached texture to create a new one mapping to the water depth buffer
				DistanceFieldShadowInfo->ResetRayTracedDistanceFieldShadow(&View);

				FTiledShadowRendering TiledShadowRendering;
				if (bRunTiled)
				{
					TiledShadowRendering.DrawIndirectParametersBuffer = TiledScreenSpaceReflection.DrawIndirectParametersBuffer;
					TiledShadowRendering.TileListDataBufferSRV = TiledScreenSpaceReflection.TileListDataBufferSRV;
					TiledShadowRendering.TileSize = TiledScreenSpaceReflection.TileSize;
				}

				const bool bProjectingForForwardShading = false;
				const bool bForceRGBModulation = true;
				DistanceFieldShadowInfo->RenderRayTracedDistanceFieldProjection(
					GraphBuilder,
					SceneTextures,
					SceneWithoutWaterTextures.SeparatedMainDirLightTexture,
					View,
					ScissorRect,
					bProjectingForForwardShading,
					bForceRGBModulation,
					bRunTiled ? &TiledShadowRendering : nullptr);
			}
		}

		if (ViewPipelineState.ReflectionsMethod == EReflectionsMethod::Lumen && CVarWaterSingleLayerLumenReflections.GetValueOnRenderThread() != 0)
		{
			RDG_EVENT_SCOPE(GraphBuilder, "SLW::LumenReflections");

			FLumenMeshSDFGridParameters MeshSDFGridParameters;
			LumenRadianceCache::FRadianceCacheInterpolationParameters RadianceCacheParameters;
			FLumenReflectionCompositeParameters LumenReflectionCompositeParameters;

			ReflectionsColor = RenderLumenReflections(
				GraphBuilder,
				View,
				SceneTextures,
				LumenFrameTemporaries,
				MeshSDFGridParameters,
				RadianceCacheParameters,
				ELumenReflectionPass::SingleLayerWater,
				&TiledScreenSpaceReflection,
				nullptr,
				LumenReflectionCompositeParameters);
		}
		else if (ViewPipelineState.ReflectionsMethod == EReflectionsMethod::RTR 
			&& CVarWaterSingleLayerRTR.GetValueOnRenderThread() != 0 
			&& FDataDrivenShaderPlatformInfo::GetSupportsHighEndRayTracingReflections(View.GetShaderPlatform()))
		{
			RDG_EVENT_SCOPE(GraphBuilder, "SLW::RayTracingReflections");
			RDG_GPU_STAT_SCOPE(GraphBuilder, RayTracingWaterReflections);

			IScreenSpaceDenoiser::FReflectionsInputs DenoiserInputs;
			IScreenSpaceDenoiser::FReflectionsRayTracingConfig RayTracingConfig;

			//RayTracingConfig.ResolutionFraction = FMath::Clamp(GetRayTracingReflectionsScreenPercentage() / 100.0f, 0.25f, 1.0f);
			RayTracingConfig.ResolutionFraction = 1.0f;
			//RayTracingConfig.RayCountPerPixel = GetRayTracingReflectionsSamplesPerPixel(View) > -1 ? GetRayTracingReflectionsSamplesPerPixel(View) : View.FinalPostProcessSettings.RayTracingReflectionsSamplesPerPixel;
			RayTracingConfig.RayCountPerPixel = 1;

			// Water is assumed to have zero roughness and is not currently denoised.
			//int32 DenoiserMode = GetReflectionsDenoiserMode();
			//bool bDenoise = DenoiserMode != 0;
			int32 DenoiserMode = 0;
			bool bDenoise = false;

			if (!bDenoise)
			{
				RayTracingConfig.ResolutionFraction = 1.0f;
			}

			FRayTracingReflectionOptions Options;
			Options.Algorithm = FRayTracingReflectionOptions::BruteForce;
			Options.SamplesPerPixel = 1;
			Options.ResolutionFraction = 1.0;
			Options.bReflectOnlyWater = true;

			{
				float UpscaleFactor = 1.0;
				FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
					SceneTextures.Config.Extent / UpscaleFactor,
					PF_FloatRGBA,
					FClearValueBinding::None,
					TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV);

				DenoiserInputs.Color = GraphBuilder.CreateTexture(Desc, TEXT("SLW.RayTracingReflections"));

				Desc.Format = PF_R16F;
				DenoiserInputs.RayHitDistance = GraphBuilder.CreateTexture(Desc, TEXT("SLW.RayTracingReflectionsHitDistance"));
				DenoiserInputs.RayImaginaryDepth = GraphBuilder.CreateTexture(Desc, TEXT("SLW.RayTracingReflectionsImaginaryDepth"));
			}

			RenderRayTracingReflections(
				GraphBuilder,
				SceneTextures,
				View,
				DenoiserMode,
				Options,
				&DenoiserInputs);

			if (bDenoise)
			{
				const IScreenSpaceDenoiser* DefaultDenoiser = IScreenSpaceDenoiser::GetDefaultDenoiser();
				const IScreenSpaceDenoiser* DenoiserToUse = DenoiserMode == 1 ? DefaultDenoiser : GScreenSpaceDenoiser;

				// Standard event scope for denoiser to have all profiling information not matter what, and with explicit detection of third party.
				RDG_EVENT_SCOPE(GraphBuilder, "%s%s(WaterReflections) %dx%d",
					DenoiserToUse != DefaultDenoiser ? TEXT("ThirdParty ") : TEXT(""),
					DenoiserToUse->GetDebugName(),
					View.ViewRect.Width(), View.ViewRect.Height());

				IScreenSpaceDenoiser::FReflectionsOutputs DenoiserOutputs = DenoiserToUse->DenoiseWaterReflections(
					GraphBuilder,
					View,
					&View.PrevViewInfo,
					SceneTextureParameters,
					DenoiserInputs,
					RayTracingConfig);

				ReflectionsColor = DenoiserOutputs.Color;
			}
			else
			{
				ReflectionsColor = DenoiserInputs.Color;
			}
		}
		else if (ViewPipelineState.ReflectionsMethod == EReflectionsMethod::SSR && CVarWaterSingleLayerSSR.GetValueOnRenderThread() != 0)
		{
			// RUN SSR
			// Uses the water GBuffer (depth, ABCDEF) to know how to start tracing.
			// The water scene depth is used to know where to start tracing.
			// Then it uses the scene HZB for the ray casting process.

			IScreenSpaceDenoiser::FReflectionsInputs DenoiserInputs;
			IScreenSpaceDenoiser::FReflectionsRayTracingConfig RayTracingConfig;
			ESSRQuality SSRQuality;
			ScreenSpaceRayTracing::GetSSRQualityForView(View, &SSRQuality, &RayTracingConfig);

			RDG_EVENT_SCOPE(GraphBuilder, "SLW::ScreenSpaceReflections(Quality=%d)", int32(SSRQuality));

			const bool bDenoise = false;
			const bool bSingleLayerWater = true;
			ScreenSpaceRayTracing::RenderScreenSpaceReflections(
				GraphBuilder, SceneTextureParameters, SceneTextures.Color.Resolve, View, SSRQuality, bDenoise, &DenoiserInputs, bSingleLayerWater, bRunTiled ? &TiledScreenSpaceReflection : nullptr);

			ReflectionsColor = DenoiserInputs.Color;

			if (CVarWaterSingleLayerSSRTAA.GetValueOnRenderThread() && ScreenSpaceRayTracing::IsSSRTemporalPassRequired(View)) // TAA pass is an option
			{
				check(View.ViewState);
				FTAAPassParameters TAASettings(View);
				TAASettings.SceneDepthTexture = SceneTextureParameters.SceneDepthTexture;
				TAASettings.SceneVelocityTexture = SceneTextureParameters.GBufferVelocityTexture;
				TAASettings.Pass = ETAAPassConfig::ScreenSpaceReflections;
				TAASettings.SceneColorInput = DenoiserInputs.Color;
				TAASettings.bOutputRenderTargetable = true;

				FTAAOutputs TAAOutputs = AddTemporalAAPass(
					GraphBuilder,
					View,
					TAASettings,
					View.PrevViewInfo.WaterSSRHistory,
					&View.ViewState->PrevFrameViewInfo.WaterSSRHistory);

				ReflectionsColor = TAAOutputs.SceneColor;
			}
		}

		// Composite reflections on water
		{
			const bool bHasBoxCaptures = (View.NumBoxReflectionCaptures > 0);
			const bool bHasSphereCaptures = (View.NumSphereReflectionCaptures > 0);

			FSingleLayerWaterCompositePS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FSingleLayerWaterCompositePS::FHasBoxCaptures>(bHasBoxCaptures);
			PermutationVector.Set<FSingleLayerWaterCompositePS::FHasSphereCaptures>(bHasSphereCaptures);
			TShaderMapRef<FSingleLayerWaterCompositePS> PixelShader(View.ShaderMap, PermutationVector);

			FWaterCompositeParameters* PassParameters = GraphBuilder.AllocParameters<FWaterCompositeParameters>();

			PassParameters->VS.ViewUniformBuffer = GetShaderBinding(View.ViewUniformBuffer);
			PassParameters->VS.TileListData = TiledScreenSpaceReflection.TileListDataBufferSRV;

			SetCommonParameters(PassParameters->PS.CommonParameters);
			if (IsWaterDistanceFieldShadowEnabled_Runtime(Scene->GetShaderPlatform()))
			{
				PassParameters->PS.CommonParameters.SeparatedMainDirLightTexture = SceneWithoutWaterTextures.SeparatedMainDirLightTexture;
				PassParameters->PS.CommonParameters.UseSeparatedMainDirLightTexture = 1.0f;
			}

			PassParameters->IndirectDrawParameter = TiledScreenSpaceReflection.DrawIndirectParametersBuffer;
			PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColorTexture, ERenderTargetLoadAction::ELoad);

			ValidateShaderParameters(PixelShader, PassParameters->PS);
			ClearUnusedGraphResources(PixelShader, &PassParameters->PS);

			if (bRunTiled)
			{
				FWaterTileVS::FPermutationDomain VsPermutationVector;
				TShaderMapRef<FWaterTileVS> VertexShader(View.ShaderMap, VsPermutationVector);
				ValidateShaderParameters(VertexShader, PassParameters->VS);
				ClearUnusedGraphResources(VertexShader, &PassParameters->VS);

				GraphBuilder.AddPass(
					RDG_EVENT_NAME("SLW::Composite %dx%d", View.ViewRect.Width(), View.ViewRect.Height()),
					PassParameters,
					ERDGPassFlags::Raster,
					[PassParameters, &View, TiledScreenSpaceReflection, VertexShader, PixelShader, bRunTiled](FRHICommandList& InRHICmdList)
				{
					InRHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

					FGraphicsPipelineStateInitializer GraphicsPSOInit;
					InRHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
					GraphicsPSOInit.PrimitiveType = GRHISupportsRectTopology ? PT_RectList : PT_TriangleList;
					GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_SourceAlpha>::GetRHI();
					GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
					GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
					GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
					GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
					SetGraphicsPipelineState(InRHICmdList, GraphicsPSOInit, 0);

					SetShaderParameters(InRHICmdList, VertexShader, VertexShader.GetVertexShader(), PassParameters->VS);
					SetShaderParameters(InRHICmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters->PS);

					InRHICmdList.DrawPrimitiveIndirect(PassParameters->IndirectDrawParameter->GetIndirectRHICallBuffer(), 0);
				});
			}
			else
			{
				GraphBuilder.AddPass(
					RDG_EVENT_NAME("SLW::Composite %dx%d", View.ViewRect.Width(), View.ViewRect.Height()),
					PassParameters,
					ERDGPassFlags::Raster,
					[PassParameters, &View, TiledScreenSpaceReflection, PixelShader, bRunTiled](FRHICommandList& InRHICmdList)
				{
					InRHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

					FGraphicsPipelineStateInitializer GraphicsPSOInit;
					FPixelShaderUtils::InitFullscreenPipelineState(InRHICmdList, View.ShaderMap, PixelShader, GraphicsPSOInit);

					// Premultiplied alpha where alpha is transmittance.
					GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_SourceAlpha>::GetRHI();

					SetGraphicsPipelineState(InRHICmdList, GraphicsPSOInit, 0);
					SetShaderParameters(InRHICmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters->PS);
					FPixelShaderUtils::DrawFullscreenTriangle(InRHICmdList);
				});
			}
		}
	}
}

void FDeferredShadingSceneRenderer::RenderSingleLayerWater(
	FRDGBuilder& GraphBuilder,
	const FSceneTextures& SceneTextures,
	bool bShouldRenderVolumetricCloud,
	FSceneWithoutWaterTextures& SceneWithoutWaterTextures,
	FLumenSceneFrameTemporaries& LumenFrameTemporaries)
{
	RDG_EVENT_SCOPE(GraphBuilder, "SingleLayerWater");
	RDG_GPU_STAT_SCOPE(GraphBuilder, SingleLayerWater);

	// Copy the texture to be available for the water surface to refract
	SceneWithoutWaterTextures = AddCopySceneWithoutWaterPass(GraphBuilder, ViewFamily, Views, SceneTextures.Color.Resolve, SceneTextures.Depth.Resolve);

	// Render height fog over the color buffer if it is allocated, e.g. SingleLayerWaterUsesSimpleShading is true.
	if (SceneWithoutWaterTextures.ColorTexture && ShouldRenderFog(ViewFamily))
	{
		RenderUnderWaterFog(GraphBuilder, SceneWithoutWaterTextures, SceneTextures.UniformBuffer);
	}
	if (SceneWithoutWaterTextures.ColorTexture && bShouldRenderVolumetricCloud)
	{
		// This path is only taken when rendering the clouds in a render target that can be composited
		ComposeVolumetricRenderTargetOverSceneUnderWater(GraphBuilder, Views, SceneWithoutWaterTextures, SceneTextures);
	}

	RenderSingleLayerWaterInner(GraphBuilder, SceneTextures, SceneWithoutWaterTextures);

	// No SSR or composite needed in Forward. Reflections are applied in the WaterGBuffer pass.
	if (!IsForwardShadingEnabled(ShaderPlatform))
	{
		// If supported render SSR, the composite pass in non deferred and/or under water effect.
		RenderSingleLayerWaterReflections(GraphBuilder, SceneTextures, SceneWithoutWaterTextures, LumenFrameTemporaries);
	}
}

BEGIN_UNIFORM_BUFFER_STRUCT(FSingleLayerWaterPassUniformParameters,)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorWithoutSingleLayerWaterTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorWithoutSingleLayerWaterSampler)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthWithoutSingleLayerWaterTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, SceneDepthWithoutSingleLayerWaterSampler)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CustomDepthTexture)
	SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<uint2>, CustomStencilTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, CustomDepthSampler)
	SHADER_PARAMETER(FVector4f, SceneWithoutSingleLayerWaterMinMaxUV)
	SHADER_PARAMETER(FVector4f, DistortionParams)
	SHADER_PARAMETER(FVector2f, SceneWithoutSingleLayerWaterTextureSize)
	SHADER_PARAMETER(FVector2f, SceneWithoutSingleLayerWaterInvTextureSize)
	SHADER_PARAMETER_STRUCT(FLightCloudTransmittanceParameters, ForwardDirLightCloudShadow)
END_UNIFORM_BUFFER_STRUCT()

// At the moment we reuse the DeferredDecals static uniform buffer slot because it is currently unused in this pass.
// When we add support for decals on SLW in the future, we might need to find another solution.
IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FSingleLayerWaterPassUniformParameters, "SingleLayerWater", DeferredDecals);

BEGIN_SHADER_PARAMETER_STRUCT(FSingleLayerWaterPassParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FViewShaderParameters, View)
	SHADER_PARAMETER_STRUCT_REF(FReflectionCaptureShaderData, ReflectionCapture)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FOpaqueBasePassUniformParameters, BasePass)
	SHADER_PARAMETER_STRUCT_INCLUDE(FInstanceCullingDrawParams, InstanceCullingDrawParams)
	SHADER_PARAMETER_STRUCT_INCLUDE(FVirtualShadowMapSamplingParameters, VirtualShadowMapSamplingParameters)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSingleLayerWaterPassUniformParameters, SingleLayerWater)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void FDeferredShadingSceneRenderer::RenderSingleLayerWaterInner(
	FRDGBuilder& GraphBuilder,
	const FSceneTextures& SceneTextures,
	const FSceneWithoutWaterTextures& SceneWithoutWaterTextures)
{
	RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, Water);
	SCOPED_NAMED_EVENT(FDeferredShadingSceneRenderer_RenderSingleLayerWaterPass, FColor::Emerald);
	SCOPE_CYCLE_COUNTER(STAT_WaterPassDrawTime);
	RDG_EVENT_SCOPE(GraphBuilder, "SLW::Draw");

	const bool bRenderInParallel = GRHICommandList.UseParallelAlgorithms() && CVarParallelSingleLayerWaterPass.GetValueOnRenderThread() == 1;

	const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);

	TStaticArray<FTextureRenderTargetBinding, MaxSimultaneousRenderTargets> BasePassTextures;
	uint32 BasePassTextureCount = SceneTextures.GetGBufferRenderTargets(BasePassTextures);
	if(IsWaterDistanceFieldShadowEnabled_Runtime(Scene->GetShaderPlatform()) 
		&& !Strata::IsStrataEnabled())	// We do not bind that texture if Strata is enabled as the data will go through the Strata material buffer.
	{
		const bool bNeverClear = true;
		BasePassTextures[BasePassTextureCount++] = FTextureRenderTargetBinding(SceneWithoutWaterTextures.SeparatedMainDirLightTexture, bNeverClear);
	}
	Strata::AppendStrataMRTs(*this, BasePassTextureCount, BasePassTextures);
	TArrayView<FTextureRenderTargetBinding> BasePassTexturesView = MakeArrayView(BasePassTextures.GetData(), BasePassTextureCount);

	FRDGTextureRef WhiteForwardScreenSpaceShadowMask = SystemTextures.White;

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		FViewInfo& View = Views[ViewIndex];

		if (!View.ShouldRenderView())
		{
			continue;
		}

		RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
		RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);
		View.BeginRenderView();

		FSingleLayerWaterPassUniformParameters &SLWUniformParameters = *GraphBuilder.AllocParameters<FSingleLayerWaterPassUniformParameters>();
		{
			const bool bShouldUseBilinearSamplerForDepth = ShouldUseBilinearSamplerForDepthWithoutSingleLayerWater(SceneWithoutWaterTextures.DepthTexture->Desc.Format);
			const bool bCustomDepthTextureProduced = HasBeenProduced(SceneTextures.CustomDepth.Depth);
			const FIntVector DepthTextureSize = SceneWithoutWaterTextures.DepthTexture->Desc.GetSize();

			SLWUniformParameters.SceneColorWithoutSingleLayerWaterTexture = SceneWithoutWaterTextures.ColorTexture;
			SLWUniformParameters.SceneColorWithoutSingleLayerWaterSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
			SLWUniformParameters.SceneDepthWithoutSingleLayerWaterTexture = SceneWithoutWaterTextures.DepthTexture;
			SLWUniformParameters.SceneDepthWithoutSingleLayerWaterSampler = bShouldUseBilinearSamplerForDepth ? TStaticSamplerState<SF_Bilinear>::GetRHI() : TStaticSamplerState<SF_Point>::GetRHI();
			SLWUniformParameters.CustomDepthTexture = bCustomDepthTextureProduced ? SceneTextures.CustomDepth.Depth : SystemTextures.DepthDummy;
			SLWUniformParameters.CustomStencilTexture = bCustomDepthTextureProduced ? SceneTextures.CustomDepth.Stencil : SystemTextures.StencilDummySRV;
			SLWUniformParameters.CustomDepthSampler = TStaticSamplerState<SF_Point>::GetRHI();
			SLWUniformParameters.SceneWithoutSingleLayerWaterMinMaxUV = SceneWithoutWaterTextures.Views[ViewIndex].MinMaxUV;
			SetupDistortionParams(SLWUniformParameters.DistortionParams, View);
			SLWUniformParameters.SceneWithoutSingleLayerWaterTextureSize = FVector2f(DepthTextureSize.X, DepthTextureSize.Y);
			SLWUniformParameters.SceneWithoutSingleLayerWaterInvTextureSize = FVector2f(1.0f / DepthTextureSize.X, 1.0f / DepthTextureSize.Y);

			const FLightSceneProxy *SelectedForwardDirectionalLightProxy = View.ForwardLightingResources.SelectedForwardDirectionalLightProxy;
			SetupLightCloudTransmittanceParameters(GraphBuilder, Scene, View, SelectedForwardDirectionalLightProxy ? SelectedForwardDirectionalLightProxy->GetLightSceneInfo() : nullptr, SLWUniformParameters.ForwardDirLightCloudShadow);
		}

		FSingleLayerWaterPassParameters* PassParameters = GraphBuilder.AllocParameters<FSingleLayerWaterPassParameters>();
		PassParameters->View = View.GetShaderParameters();
		PassParameters->ReflectionCapture = View.ReflectionCaptureUniformBuffer;
		PassParameters->BasePass = CreateOpaqueBasePassUniformBuffer(GraphBuilder, View, ViewIndex);
		PassParameters->VirtualShadowMapSamplingParameters = VirtualShadowMapArray.GetSamplingParameters(GraphBuilder);
		PassParameters->SingleLayerWater = GraphBuilder.CreateUniformBuffer(&SLWUniformParameters);
		PassParameters->RenderTargets = GetRenderTargetBindings(ERenderTargetLoadAction::ELoad, BasePassTexturesView);
		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(SceneTextures.Depth.Target, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilWrite);

		View.ParallelMeshDrawCommandPasses[EMeshPass::SingleLayerWaterPass].BuildRenderingCommands(GraphBuilder, Scene->GPUScene, PassParameters->InstanceCullingDrawParams);

		if (bRenderInParallel)
		{
			GraphBuilder.AddPass(
				RDG_EVENT_NAME("SingleLayerWaterParallel"),
				PassParameters,
				ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass,
				[this, &View, PassParameters](const FRDGPass* InPass, FRHICommandListImmediate& RHICmdList)
			{
				FRDGParallelCommandListSet ParallelCommandListSet(InPass, RHICmdList, GET_STATID(STAT_CLP_WaterSingleLayerPass), *this, View, FParallelCommandListBindings(PassParameters));
				View.ParallelMeshDrawCommandPasses[EMeshPass::SingleLayerWaterPass].DispatchDraw(&ParallelCommandListSet, RHICmdList, &PassParameters->InstanceCullingDrawParams);
			});
		}
		else
		{
			GraphBuilder.AddPass(
				RDG_EVENT_NAME("SingleLayerWater"),
				PassParameters,
				ERDGPassFlags::Raster,
				[this, &View, PassParameters](FRHICommandList& RHICmdList)
			{
				SetStereoViewport(RHICmdList, View, 1.0f);
				View.ParallelMeshDrawCommandPasses[EMeshPass::SingleLayerWaterPass].DispatchDraw(nullptr, RHICmdList, &PassParameters->InstanceCullingDrawParams);
			});
		}
	}

	AddResolveSceneDepthPass(GraphBuilder, Views, SceneTextures.Depth);
}

class FSingleLayerWaterPassMeshProcessor : public FSceneRenderingAllocatorObject<FSingleLayerWaterPassMeshProcessor>, public FMeshPassProcessor
{
public:
	FSingleLayerWaterPassMeshProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, const FMeshPassProcessorRenderState& InPassDrawRenderState, FMeshPassDrawListContext* InDrawListContext);

	void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final;

private:
	bool TryAddMeshBatch(
		const FMeshBatch& RESTRICT MeshBatch,
		uint64 BatchElementMask,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		int32 StaticMeshId,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material);

	bool Process(
		const FMeshBatch& MeshBatch,
		uint64 BatchElementMask,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		int32 StaticMeshId,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource,
		ERasterizerFillMode MeshFillMode,
		ERasterizerCullMode MeshCullMode);

	FMeshPassProcessorRenderState PassDrawRenderState;
};

FSingleLayerWaterPassMeshProcessor::FSingleLayerWaterPassMeshProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, const FMeshPassProcessorRenderState& InPassDrawRenderState, FMeshPassDrawListContext* InDrawListContext)
	: FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
	, PassDrawRenderState(InPassDrawRenderState)
{
	if (SingleLayerWaterUsesSimpleShading(Scene->GetShaderPlatform()))
	{
		// Force non opaque, pre multiplied alpha, transparent blend mode because water is going to be blended against scene color (no distortion from texture scene color).
		FRHIBlendState* ForwardSimpleWaterBlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();
		PassDrawRenderState.SetBlendState(ForwardSimpleWaterBlendState);
	}
}

void FSingleLayerWaterPassMeshProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	const FMaterialRenderProxy* MaterialRenderProxy = MeshBatch.MaterialRenderProxy;
	while (MaterialRenderProxy)
	{
		const FMaterial* Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);
		if (Material)
		{
			if (TryAddMeshBatch(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, *MaterialRenderProxy, *Material))
			{
				break;
			}
		}

		MaterialRenderProxy = MaterialRenderProxy->GetFallback(FeatureLevel);
	}
}

bool FSingleLayerWaterPassMeshProcessor::TryAddMeshBatch(
	const FMeshBatch& RESTRICT MeshBatch,
	uint64 BatchElementMask,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	int32 StaticMeshId,
	const FMaterialRenderProxy& MaterialRenderProxy,
	const FMaterial& Material)
{
	if (Material.GetShadingModels().HasShadingModel(MSM_SingleLayerWater))
	{
		// Determine the mesh's material and blend mode.
		const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
		const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, Material, OverrideSettings);
		const ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, Material, OverrideSettings);
		return Process(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, MeshFillMode, MeshCullMode);
	}

	return true;
}

bool FSingleLayerWaterPassMeshProcessor::Process(
	const FMeshBatch& MeshBatch,
	uint64 BatchElementMask,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	int32 StaticMeshId,
	const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
	const FMaterial& RESTRICT MaterialResource,
	ERasterizerFillMode MeshFillMode,
	ERasterizerCullMode MeshCullMode)
{
	FUniformLightMapPolicy NoLightmapPolicy(LMP_NO_LIGHTMAP);
	typedef FUniformLightMapPolicy LightMapPolicyType;
	TMeshProcessorShaders<
		TBasePassVertexShaderPolicyParamType<LightMapPolicyType>,
		TBasePassPixelShaderPolicyParamType<LightMapPolicyType>> WaterPassShaders;

	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;
	const bool bRenderSkylight = true;
	if (!GetBasePassShaders<LightMapPolicyType>(
		MaterialResource,
		VertexFactory->GetType(),
		NoLightmapPolicy,
		FeatureLevel,
		bRenderSkylight,
		false,
		&WaterPassShaders.VertexShader,
		&WaterPassShaders.PixelShader
		))
	{ 
		return false;
	}

	TBasePassShaderElementData<LightMapPolicyType> ShaderElementData(nullptr);
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

	const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(WaterPassShaders.VertexShader, WaterPassShaders.PixelShader);

	BuildMeshDrawCommands(
		MeshBatch,
		BatchElementMask,
		PrimitiveSceneProxy,
		MaterialRenderProxy,
		MaterialResource,
		PassDrawRenderState,
		WaterPassShaders,
		MeshFillMode,
		MeshCullMode,
		SortKey,
		EMeshPassFeatures::Default,
		ShaderElementData);

	return true;
}

FMeshPassProcessor* CreateSingleLayerWaterPassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	FMeshPassProcessorRenderState DrawRenderState;

	// Make sure depth write is enabled.
	FExclusiveDepthStencil::Type BasePassDepthStencilAccess_DepthWrite = FExclusiveDepthStencil::Type(Scene->DefaultBasePassDepthStencilAccess | FExclusiveDepthStencil::DepthWrite);
	SetupBasePassState(BasePassDepthStencilAccess_DepthWrite, false, DrawRenderState);

	return new FSingleLayerWaterPassMeshProcessor(Scene, InViewIfDynamicMeshCommand, DrawRenderState, InDrawListContext);
}

FRegisterPassProcessorCreateFunction RegisterSingleLayerWaterPass(&CreateSingleLayerWaterPassProcessor, EShadingPath::Deferred, EMeshPass::SingleLayerWaterPass, EMeshPassFlags::MainView);
