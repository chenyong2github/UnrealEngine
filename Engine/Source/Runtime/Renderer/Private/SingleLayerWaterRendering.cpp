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
#include "RenderGraph.h"
#include "ScenePrivate.h"
#include "SceneRendering.h"
#include "ScreenSpaceRayTracing.h"
#include "SceneTextureParameters.h"

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

int32 GSingleLayerWaterRefractionDownsampleFactor = 2;
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
	TEXT("Enable SSR for the single water renderring system."),
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
	bool bUsesSimpleShading;
	bUsesSimpleShading = IsVulkanMobileSM5Platform(ShaderPlatform);
	bUsesSimpleShading = bUsesSimpleShading || FDataDrivenShaderPlatformInfo::GetWaterUsesSimpleForwardShading(ShaderPlatform);
	bUsesSimpleShading = bUsesSimpleShading && IsForwardShadingEnabled(ShaderPlatform);

	return bUsesSimpleShading;
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

bool UseSingleLayerWaterIndirectDraw(EShaderPlatform ShaderPlatform)
{
	return IsFeatureLevelSupported(ShaderPlatform, ERHIFeatureLevel::SM5)
		// Vulkan gives error with WaterTileCatergorisationCS usage of atomic, and Metal does not play nice, either.
		&& !IsVulkanMobilePlatform(ShaderPlatform)
		&& FDataDrivenShaderPlatformInfo::GetSupportsWaterIndirectDraw(ShaderPlatform);
}

BEGIN_SHADER_PARAMETER_STRUCT(FSingleLayerWaterCommonShaderParameters, )
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ScreenSpaceReflectionsTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, ScreenSpaceReflectionsSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D, PreIntegratedGF)
	SHADER_PARAMETER_SAMPLER(SamplerState, PreIntegratedGFSampler)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneNoWaterDepthTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, SceneNoWaterDepthSampler)
	SHADER_PARAMETER(FVector4, SceneNoWaterMinMaxUV)
	SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)	// Water scene texture
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	SHADER_PARAMETER_STRUCT_REF(FReflectionCaptureShaderData, ReflectionCaptureData)
	SHADER_PARAMETER_STRUCT_REF(FReflectionUniformParameters, ReflectionsParameters)
	SHADER_PARAMETER_STRUCT_REF(FForwardLightData, ForwardLightData)
END_SHADER_PARAMETER_STRUCT()

class FSingleLayerWaterCompositePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSingleLayerWaterCompositePS);
	SHADER_USE_PARAMETER_STRUCT(FSingleLayerWaterCompositePS, FGlobalShader)

	class FScreenSpaceReflections : SHADER_PERMUTATION_BOOL("SCREEN_SPACE_REFLECTION");
	class FHasBoxCaptures : SHADER_PERMUTATION_BOOL("REFLECTION_COMPOSITE_HAS_BOX_CAPTURES");
	class FHasSphereCaptures : SHADER_PERMUTATION_BOOL("REFLECTION_COMPOSITE_HAS_SPHERE_CAPTURES");
	using FPermutationDomain = TShaderPermutationDomain<FScreenSpaceReflections, FHasBoxCaptures, FHasSphereCaptures>;

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
	}
};

IMPLEMENT_GLOBAL_SHADER(FSingleLayerWaterCompositePS, "/Engine/Private/SingleLayerWaterComposite.usf", "SingleLayerWaterCompositePS", SF_Pixel);

class FWaterTileCategorisationCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FWaterTileCategorisationCS);
	SHADER_USE_PARAMETER_STRUCT(FWaterTileCategorisationCS, FGlobalShader)

	using FPermutationDomain = TShaderPermutationDomain<>;

	static int32 GetTileSize()
	{
		return 8;
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSingleLayerWaterCommonShaderParameters, CommonParameters)
		SHADER_PARAMETER(uint32, VertexCountPerInstanceIndirect)
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

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return UseSingleLayerWaterIndirectDraw(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("TILE_VERTEX_SHADER"), 1.0f);
		OutEnvironment.SetDefine(TEXT("WORK_TILE_SIZE"), FWaterTileCategorisationCS::GetTileSize());
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

IMPLEMENT_GLOBAL_SHADER(FWaterTileVS, "/Engine/Private/SingleLayerWaterComposite.usf", "WaterTileVS", SF_Vertex);

class FWaterRefractionCopyPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FWaterRefractionCopyPS);
	SHADER_USE_PARAMETER_STRUCT(FWaterRefractionCopyPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorCopyDownsampleTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorCopyDownsampleSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthCopyDownsampleTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SceneDepthCopyDownsampleSampler)
		SHADER_PARAMETER(FVector2D, SVPositionToSourceTextureUV)
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
	check(Views.Num() > 0);
	check(SceneColorTexture);
	check(SceneDepthTexture);

	const bool bCopyColor = !SingleLayerWaterUsesSimpleShading(Views[0].GetShaderPlatform());

	const FRDGTextureDesc& SceneColorDesc = SceneColorTexture->Desc;
	const FRDGTextureDesc& SceneDepthDesc = SceneColorTexture->Desc;

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);
	const int32 RefractionDownsampleFactor = FMath::Clamp(GSingleLayerWaterRefractionDownsampleFactor, 1, 8);
	const FIntPoint RefractionResolution = FIntPoint::DivideAndRoundDown(SceneColorDesc.Extent, RefractionDownsampleFactor);
	FRDGTextureRef SceneColorWithoutSingleLayerWaterTexture = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);

	if (bCopyColor)
	{
		const FRDGTextureDesc ColorDesc = FRDGTextureDesc::Create2D(RefractionResolution, SceneColorDesc.Format, SceneColorDesc.ClearValue, TexCreate_ShaderResource | TexCreate_RenderTargetable);
		SceneColorWithoutSingleLayerWaterTexture = GraphBuilder.CreateTexture(ColorDesc, TEXT("SceneColorWithoutSingleLayerWater"));
	}

	const FRDGTextureDesc DepthDesc(FRDGTextureDesc::Create2D(RefractionResolution, ViewFamily.EngineShowFlags.SingleLayerWaterRefractionFullPrecision ? PF_R32_FLOAT : PF_R16F, SceneDepthDesc.ClearValue, TexCreate_ShaderResource | TexCreate_RenderTargetable));
	FRDGTextureRef SceneDepthWithoutSingleLayerWaterTexture = GraphBuilder.CreateTexture(DepthDesc, TEXT("SceneDepthWithoutSingleLayerWater"));

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
		PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
		PassParameters->SceneColorCopyDownsampleTexture = SceneColorTexture;
		PassParameters->SceneColorCopyDownsampleSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->SceneDepthCopyDownsampleTexture = SceneDepthTexture;
		PassParameters->SceneDepthCopyDownsampleSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->SVPositionToSourceTextureUV = FVector2D(RefractionDownsampleFactor / float(SceneColorDesc.Extent.X), RefractionDownsampleFactor / float(SceneColorDesc.Extent.Y));

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

		const FIntRect RefractionViewRect = FIntRect(FIntPoint::DivideAndRoundDown(View.ViewRect.Min, RefractionDownsampleFactor), FIntPoint::DivideAndRoundDown(View.ViewRect.Max, RefractionDownsampleFactor));

		Textures.Views[ViewIndex].ViewRect   = RefractionViewRect;
		Textures.Views[ViewIndex].MinMaxUV.X = (RefractionViewRect.Min.X + 0.5f) / RefractionResolution.X; 
		Textures.Views[ViewIndex].MinMaxUV.Y = (RefractionViewRect.Min.Y + 0.5f) / RefractionResolution.Y;
		Textures.Views[ViewIndex].MinMaxUV.Z = (RefractionViewRect.Max.X - 0.5f) / RefractionResolution.X; 
		Textures.Views[ViewIndex].MinMaxUV.W = (RefractionViewRect.Max.Y - 0.5f) / RefractionResolution.Y;

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
	return MoveTemp(Textures);
}

BEGIN_SHADER_PARAMETER_STRUCT(FWaterCompositeParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FWaterTileVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FSingleLayerWaterCompositePS::FParameters, PS)
	SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, IndirectDrawParameter)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void FDeferredShadingSceneRenderer::RenderSingleLayerWaterReflections(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef SceneColorTexture,
	const FSceneWithoutWaterTextures& SceneWithoutWaterTextures)
{
	if (CVarWaterSingleLayer.GetValueOnRenderThread() <= 0 || CVarWaterSingleLayerReflection.GetValueOnRenderThread() <= 0)
	{
		return;
	}

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& View = Views[ViewIndex];

		if (!View.ShouldRenderView())
		{
			continue;
		}

		RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
		RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);

		FRDGTextureRef ReflectionsColor = nullptr;
		FRDGTextureRef BlackDummy = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
		FSceneTextureParameters SceneTextures = GetSceneTextureParameters(GraphBuilder);

		auto SetCommonParameters = [&](FSingleLayerWaterCommonShaderParameters& Parameters)
		{
			Parameters.ScreenSpaceReflectionsTexture = ReflectionsColor ? ReflectionsColor : BlackDummy;
			Parameters.ScreenSpaceReflectionsSampler = TStaticSamplerState<SF_Point>::GetRHI();
			Parameters.PreIntegratedGF = GSystemTextures.PreintegratedGF->GetRenderTargetItem().ShaderResourceTexture;
			Parameters.PreIntegratedGFSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			Parameters.SceneNoWaterDepthTexture = SceneWithoutWaterTextures.DepthTexture ? SceneWithoutWaterTextures.DepthTexture : BlackDummy;
			Parameters.SceneNoWaterDepthSampler = TStaticSamplerState<SF_Point>::GetRHI();
			Parameters.SceneNoWaterMinMaxUV = SceneWithoutWaterTextures.Views[ViewIndex].MinMaxUV;
			Parameters.SceneTextures = SceneTextures;
			Parameters.ViewUniformBuffer = View.ViewUniformBuffer;
			Parameters.ReflectionCaptureData = View.ReflectionCaptureUniformBuffer;
			{
				FReflectionUniformParameters ReflectionUniformParameters;
				SetupReflectionUniformParameters(View, ReflectionUniformParameters);
				Parameters.ReflectionsParameters = CreateUniformBufferImmediate(ReflectionUniformParameters, UniformBuffer_SingleDraw);
			}
			Parameters.ForwardLightData = View.ForwardLightingResources->ForwardLightDataUniformBuffer;
		};

		const bool bRunTiled = UseSingleLayerWaterIndirectDraw(View.GetShaderPlatform()) && CVarWaterSingleLayerTiledComposite.GetValueOnRenderThread();
		FTiledScreenSpaceReflection TiledScreenSpaceReflection = {nullptr, nullptr, nullptr, nullptr, nullptr, 8};
		FIntVector ViewRes(View.ViewRect.Width(), View.ViewRect.Height(), 1);
		FIntVector TiledViewRes = FIntVector::DivideAndRoundUp(ViewRes, TiledScreenSpaceReflection.TileSize);
		if (bRunTiled)
		{
			TiledScreenSpaceReflection.DispatchIndirectParametersBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDrawIndirectParameters>(), TEXT("WaterIndirectDrawParameters"));
			TiledScreenSpaceReflection.DispatchIndirectParametersBufferUAV = GraphBuilder.CreateUAV(TiledScreenSpaceReflection.DispatchIndirectParametersBuffer);
			TiledScreenSpaceReflection.TileListDataBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), TiledViewRes.X * TiledViewRes.Y), TEXT("TileListDataBuffer"));
			TiledScreenSpaceReflection.TileListStructureBufferUAV = GraphBuilder.CreateUAV(TiledScreenSpaceReflection.TileListDataBuffer, PF_R32_UINT);
			TiledScreenSpaceReflection.TileListStructureBufferSRV = GraphBuilder.CreateSRV(TiledScreenSpaceReflection.TileListDataBuffer, PF_R32_UINT);

			// Clear DispatchIndirectParametersBuffer
			AddClearUAVPass(GraphBuilder, TiledScreenSpaceReflection.DispatchIndirectParametersBufferUAV, 0);

			// Categorization based on SHADING_MODEL_ID
			{
				FWaterTileCategorisationCS::FPermutationDomain PermutationVector;
				TShaderMapRef<FWaterTileCategorisationCS> ComputeShader(View.ShaderMap, PermutationVector);

				FWaterTileCategorisationCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FWaterTileCategorisationCS::FParameters>();
				SetCommonParameters(PassParameters->CommonParameters);
				PassParameters->VertexCountPerInstanceIndirect = GRHISupportsRectTopology ? 3 : 6;
				PassParameters->DispatchIndirectDataUAV = TiledScreenSpaceReflection.DispatchIndirectParametersBufferUAV;
				PassParameters->WaterTileListDataUAV = TiledScreenSpaceReflection.TileListStructureBufferUAV;

				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("WaterTileCategorisation"), ComputeShader, PassParameters, TiledViewRes);
			}
		}

 		const bool bEnableSSR = CVarWaterSingleLayerSSR.GetValueOnRenderThread() != 0 && ShouldRenderScreenSpaceReflections(View);
		const bool bEnableRTR = CVarWaterSingleLayerRTR.GetValueOnRenderThread() != 0 && ShouldRenderRayTracingReflections(View);
		if (bEnableRTR)
		{
			RDG_EVENT_SCOPE(GraphBuilder, "RayTracingWaterReflections");
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
					SceneTextures.SceneDepthTexture->Desc.Extent / UpscaleFactor,
					PF_FloatRGBA,
					FClearValueBinding::None,
					TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV);

				DenoiserInputs.Color = GraphBuilder.CreateTexture(Desc, TEXT("RayTracingReflections"));

				Desc.Format = PF_R16F;
				DenoiserInputs.RayHitDistance = GraphBuilder.CreateTexture(Desc, TEXT("RayTracingReflectionsHitDistance"));
				DenoiserInputs.RayImaginaryDepth = GraphBuilder.CreateTexture(Desc, TEXT("RayTracingReflectionsImaginaryDepth"));
			}

			bool bReflectOnlyWater = true;
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
					SceneTextures,
					DenoiserInputs,
					RayTracingConfig);

				ReflectionsColor = DenoiserOutputs.Color;
			}
			else
			{
				ReflectionsColor = DenoiserInputs.Color;
			}
		}
		else if (bEnableSSR)
		{
			// RUN SSR
			// Uses the water GBuffer (depth, ABCDEF) to know how to start tracing.
			// The water scene depth is used to know where to start tracing.
			// Then it uses the scene HZB for the ray casting process.

			IScreenSpaceDenoiser::FReflectionsInputs DenoiserInputs;
			IScreenSpaceDenoiser::FReflectionsRayTracingConfig RayTracingConfig;
			ESSRQuality SSRQuality;
			GetSSRQualityForView(View, &SSRQuality, &RayTracingConfig);

			RDG_EVENT_SCOPE(GraphBuilder, "Water ScreenSpaceReflections(Quality=%d)", int32(SSRQuality));

			const bool bDenoise = false;
			RenderScreenSpaceReflections(
				GraphBuilder, SceneTextures, SceneColorTexture, View, SSRQuality, bDenoise, &DenoiserInputs, bRunTiled ? &TiledScreenSpaceReflection : nullptr);

			ReflectionsColor = DenoiserInputs.Color;

			if (CVarWaterSingleLayerSSRTAA.GetValueOnRenderThread() && IsSSRTemporalPassRequired(View)) // TAA pass is an option
			{
				check(View.ViewState);
				FTAAPassParameters TAASettings(View);
				TAASettings.SceneDepthTexture = SceneTextures.SceneDepthTexture;
				TAASettings.SceneVelocityTexture = SceneTextures.GBufferVelocityTexture;
				TAASettings.Pass = ETAAPassConfig::ScreenSpaceReflections;
				TAASettings.SceneColorInput = DenoiserInputs.Color;

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
			PermutationVector.Set<FSingleLayerWaterCompositePS::FScreenSpaceReflections>(bEnableSSR);
			PermutationVector.Set<FSingleLayerWaterCompositePS::FHasBoxCaptures>(bHasBoxCaptures);
			PermutationVector.Set<FSingleLayerWaterCompositePS::FHasSphereCaptures>(bHasSphereCaptures);
			TShaderMapRef<FSingleLayerWaterCompositePS> PixelShader(View.ShaderMap, PermutationVector);

			FWaterCompositeParameters* PassParameters = GraphBuilder.AllocParameters<FWaterCompositeParameters>();

			PassParameters->VS.ViewUniformBuffer = View.ViewUniformBuffer;
			PassParameters->VS.TileListData = TiledScreenSpaceReflection.TileListStructureBufferSRV;

			SetCommonParameters(PassParameters->PS.CommonParameters);

			PassParameters->IndirectDrawParameter = TiledScreenSpaceReflection.DispatchIndirectParametersBuffer;
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
					RDG_EVENT_NAME("Water Composite %dx%d", View.ViewRect.Width(), View.ViewRect.Height()),
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
					SetGraphicsPipelineState(InRHICmdList, GraphicsPSOInit);

					SetShaderParameters(InRHICmdList, VertexShader, VertexShader.GetVertexShader(), PassParameters->VS);
					SetShaderParameters(InRHICmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters->PS);

					InRHICmdList.DrawPrimitiveIndirect(PassParameters->IndirectDrawParameter->GetIndirectRHICallBuffer(), 0);
				});
			}
			else
			{
				GraphBuilder.AddPass(
					RDG_EVENT_NAME("Water Composite %dx%d", View.ViewRect.Width(), View.ViewRect.Height()),
					PassParameters,
					ERDGPassFlags::Raster,
					[PassParameters, &View, TiledScreenSpaceReflection, PixelShader, bRunTiled](FRHICommandList& InRHICmdList)
				{
					InRHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

					FGraphicsPipelineStateInitializer GraphicsPSOInit;
					FPixelShaderUtils::InitFullscreenPipelineState(InRHICmdList, View.ShaderMap, PixelShader, GraphicsPSOInit);

					// Premultiplied alpha where alpha is transmittance.
					GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_SourceAlpha>::GetRHI();

					SetGraphicsPipelineState(InRHICmdList, GraphicsPSOInit);
					SetShaderParameters(InRHICmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters->PS);
					FPixelShaderUtils::DrawFullscreenTriangle(InRHICmdList);
				});
			}
		}
	}
}

void FDeferredShadingSceneRenderer::RenderSingleLayerWater(
	FRDGBuilder& GraphBuilder,
	FRDGTextureMSAA SceneColorTexture,
	FRDGTextureMSAA SceneDepthTexture,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> DepthOnlySceneTextures,
	bool bShouldRenderVolumetricCloud,
	FSceneWithoutWaterTextures& SceneWithoutWaterTextures)
{
	RDG_EVENT_SCOPE(GraphBuilder, "SingleLayerWater");
	RDG_GPU_STAT_SCOPE(GraphBuilder, SingleLayerWater);
	
	// Copy the texture to be available for the water surface to refract
	SceneWithoutWaterTextures = AddCopySceneWithoutWaterPass(GraphBuilder, ViewFamily, Views, SceneColorTexture.Resolve, SceneDepthTexture.Resolve);

	// Render height fog over the color buffer if it is allocated, e.g. SingleLayerWaterUsesSimpleShading is true.
	if (SceneWithoutWaterTextures.ColorTexture && ShouldRenderFog(ViewFamily))
	{
		RenderUnderWaterFog(GraphBuilder, SceneWithoutWaterTextures, DepthOnlySceneTextures);
	}
	if (SceneWithoutWaterTextures.ColorTexture && bShouldRenderVolumetricCloud)
	{
		// This path is only taken when rendering the clouds in a render target that can be composited
		ComposeVolumetricRenderTargetOverSceneUnderWater(GraphBuilder, SceneWithoutWaterTextures, DepthOnlySceneTextures);
	}

	RenderSingleLayerWaterInner(GraphBuilder, SceneColorTexture, SceneDepthTexture, SceneWithoutWaterTextures);

	// No SSR or composite needed in Forward. Reflections are applied in the WaterGBuffer pass.
	if (!IsAnyForwardShadingEnabled(ShaderPlatform))
	{
		// If supported render SSR, the composite pass in non deferred and/or under water effect.
		RenderSingleLayerWaterReflections(GraphBuilder, SceneColorTexture.Resolve, SceneWithoutWaterTextures);
	}
}

BEGIN_SHADER_PARAMETER_STRUCT(FSingleLayerWaterPassParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FOpaqueBasePassUniformParameters, BasePass)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void FDeferredShadingSceneRenderer::RenderSingleLayerWaterInner(
	FRDGBuilder& GraphBuilder,
	FRDGTextureMSAA SceneColorTexture,
	FRDGTextureMSAA SceneDepthTexture,
	const FSceneWithoutWaterTextures& SceneWithoutWaterTextures)
{
	RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, Water);
	SCOPED_NAMED_EVENT(FDeferredShadingSceneRenderer_RenderSingleLayerWaterPass, FColor::Emerald);
	SCOPE_CYCLE_COUNTER(STAT_WaterPassDrawTime);
	RDG_EVENT_SCOPE(GraphBuilder, "SingleLayerWater");
	RDG_GPU_STAT_SCOPE(GraphBuilder, SingleLayerWater);

	const bool bRenderInParallel = GRHICommandList.UseParallelAlgorithms() && CVarParallelSingleLayerWaterPass.GetValueOnRenderThread() == 1;

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);

	FRenderTargetBindingSlots RenderTargets;
	SceneContext.GetGBufferRenderTargets(GraphBuilder, ERenderTargetLoadAction::ELoad, RenderTargets);
	RenderTargets.DepthStencil = FDepthStencilBinding(SceneDepthTexture.Target, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilWrite);

	FRDGTextureRef WhiteForwardScreenSpaceShadowMask = GraphBuilder.RegisterExternalTexture(GSystemTextures.WhiteDummy);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		const FViewInfo& View = Views[ViewIndex];

		if (!View.ShouldRenderView())
		{
			continue;
		}

		RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
		RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);

		FSingleLayerWaterPassParameters* PassParameters = GraphBuilder.AllocParameters<FSingleLayerWaterPassParameters>();
		PassParameters->BasePass = CreateOpaqueBasePassUniformBuffer(GraphBuilder, View, WhiteForwardScreenSpaceShadowMask, &SceneWithoutWaterTextures, ViewIndex);
		PassParameters->RenderTargets = RenderTargets;

		if (bRenderInParallel)
		{
			GraphBuilder.AddPass(
				RDG_EVENT_NAME("SingleLayerWaterParallel"),
				PassParameters,
				ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass,
				[this, &View, PassParameters](FRHICommandListImmediate& RHICmdList)
			{
				Scene->UniformBuffers.UpdateViewUniformBuffer(View);
				FRDGParallelCommandListSet ParallelCommandListSet(RHICmdList, GET_STATID(STAT_CLP_WaterSingleLayerPass), *this, View, FParallelCommandListBindings(PassParameters));
				View.ParallelMeshDrawCommandPasses[EMeshPass::SingleLayerWaterPass].DispatchDraw(&ParallelCommandListSet, RHICmdList);
			});
		}
		else
		{
			GraphBuilder.AddPass(
				RDG_EVENT_NAME("SingleLayerWater"),
				PassParameters,
				ERDGPassFlags::Raster,
				[this, &View](FRHICommandListImmediate& RHICmdList)
			{
				Scene->UniformBuffers.UpdateViewUniformBuffer(View);
				SetStereoViewport(RHICmdList, View, 1.0f);
				View.ParallelMeshDrawCommandPasses[EMeshPass::SingleLayerWaterPass].DispatchDraw(nullptr, RHICmdList);
			});
		}
	}

	AddResolveSceneDepthPass(GraphBuilder, Views, SceneDepthTexture);
}

class FSingleLayerWaterPassMeshProcessor : public FMeshPassProcessor
{
public:
	FSingleLayerWaterPassMeshProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, const FMeshPassProcessorRenderState& InPassDrawRenderState, FMeshPassDrawListContext* InDrawListContext);

	void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final;

private:
	void Process(
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
	// Determine the mesh's material and blend mode.
	const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
	const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);

	if (Material.GetShadingModels().HasShadingModel(MSM_SingleLayerWater))
	{
		const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
		const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, Material, OverrideSettings);
		const ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, Material, OverrideSettings);
		const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;
		Process(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, MeshFillMode, MeshCullMode);
	}
}

void FSingleLayerWaterPassMeshProcessor::Process(
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
		FBaseHS,
		FBaseDS,
		TBasePassPixelShaderPolicyParamType<LightMapPolicyType>> WaterPassShaders;

	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;
	const bool bRenderSkylight = true;
	const bool bRenderAtmosphericFog = false;
	GetBasePassShaders<LightMapPolicyType>(
		MaterialResource,
		VertexFactory->GetType(),
		NoLightmapPolicy,
		FeatureLevel,
		bRenderAtmosphericFog,
		bRenderSkylight,
		false,
		WaterPassShaders.HullShader,
		WaterPassShaders.DomainShader,
		WaterPassShaders.VertexShader,
		WaterPassShaders.PixelShader
		);

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
}

FMeshPassProcessor* CreateSingleLayerWaterPassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	FMeshPassProcessorRenderState DrawRenderState(Scene->UniformBuffers.ViewUniformBuffer);
	DrawRenderState.SetInstancedViewUniformBuffer(Scene->UniformBuffers.InstancedViewUniformBuffer);

	// Make sure depth write is enabled.
	FExclusiveDepthStencil::Type BasePassDepthStencilAccess_DepthWrite = FExclusiveDepthStencil::Type(Scene->DefaultBasePassDepthStencilAccess | FExclusiveDepthStencil::DepthWrite);
	SetupBasePassState(BasePassDepthStencilAccess_DepthWrite, false, DrawRenderState);

	return new(FMemStack::Get()) FSingleLayerWaterPassMeshProcessor(Scene, InViewIfDynamicMeshCommand, DrawRenderState, InDrawListContext);
}

FRegisterPassProcessorCreateFunction RegisterSingleLayerWaterPass(&CreateSingleLayerWaterPassProcessor, EShadingPath::Deferred, EMeshPass::SingleLayerWaterPass, EMeshPassFlags::MainView);