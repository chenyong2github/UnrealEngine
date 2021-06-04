// Copyright Epic Games, Inc. All Rights Reserved.

#include "IndirectLightRendering.h"
#include "RenderGraph.h"
#include "PixelShaderUtils.h"
#include "AmbientCubemapParameters.h"
#include "SceneTextureParameters.h"
#include "ScreenSpaceDenoise.h"
#include "ScreenSpaceRayTracing.h"
#include "DeferredShadingRenderer.h"
#include "PostProcess/PostProcessSubsurface.h"
#include "PostProcess/TemporalAA.h"
#include "PostProcessing.h" // for FPostProcessVS
#include "RendererModule.h" 
#include "RayTracing/RaytracingOptions.h"
#include "RayTracing/RayTracingReflections.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "VolumetricCloudRendering.h"

static TAutoConsoleVariable<bool> CVarGlobalIlluminationExperimentalPluginEnable(
	TEXT("r.GlobalIllumination.ExperimentalPlugin"),
	false,
	TEXT("Whether to use a plugin for global illumination (experimental) (default = false)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarDiffuseIndirectDenoiser(
	TEXT("r.DiffuseIndirect.Denoiser"), 1,
	TEXT("Denoising options (default = 1)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarDenoiseSSR(
	TEXT("r.SSR.ExperimentalDenoiser"), 0,
	TEXT("Replace SSR's TAA pass with denoiser."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarSkySpecularOcclusionStrength(
	TEXT("r.SkySpecularOcclusionStrength"),
	1,
	TEXT("Strength of skylight specular occlusion from DFAO (default is 1.0)"),
	ECVF_RenderThreadSafe);


DECLARE_GPU_STAT_NAMED(ReflectionEnvironment, TEXT("Reflection Environment"));
DECLARE_GPU_STAT_NAMED(RayTracingReflections, TEXT("Ray Tracing Reflections"));
DECLARE_GPU_STAT_NAMED(HairSkyLighting, TEXT("Hair Sky lighting"));
DECLARE_GPU_STAT(SkyLightDiffuse);

int GetReflectionEnvironmentCVar();
bool IsAmbientCubemapPassRequired(const FSceneView& View);


class FDiffuseIndirectCompositePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDiffuseIndirectCompositePS)
	SHADER_USE_PARAMETER_STRUCT(FDiffuseIndirectCompositePS, FGlobalShader)

	class FApplyDiffuseIndirectDim : SHADER_PERMUTATION_BOOL("DIM_APPLY_DIFFUSE_INDIRECT");
	class FApplyAmbientOcclusionDim : SHADER_PERMUTATION_BOOL("DIM_APPLY_AMBIENT_OCCLUSION");

	using FPermutationDomain = TShaderPermutationDomain<
		FApplyDiffuseIndirectDim,
		FApplyAmbientOcclusionDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		// Do not compile a shader that does not apply anything.
		if (!PermutationVector.Get<FApplyDiffuseIndirectDim>() &&
			!PermutationVector.Get<FApplyAmbientOcclusionDim>())
		{
			return false;
		}

		// Diffuse indirect generation is SM5 only.
		if (PermutationVector.Get<FApplyDiffuseIndirectDim>())
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}

		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(float, AmbientOcclusionStaticFraction)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DiffuseIndirectTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, DiffuseIndirectSampler)
		
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, AmbientOcclusionTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState,  AmbientOcclusionSampler)
		
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

class FAmbientCubemapCompositePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FAmbientCubemapCompositePS)
	SHADER_USE_PARAMETER_STRUCT(FAmbientCubemapCompositePS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_TEXTURE(Texture2D, PreIntegratedGF)
		SHADER_PARAMETER_SAMPLER(SamplerState, PreIntegratedGFSampler)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, AmbientOcclusionTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState,  AmbientOcclusionSampler)
		
		SHADER_PARAMETER_STRUCT_INCLUDE(FAmbientCubemapParameters, AmbientCubemap)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

/** Pixel shader that does tiled deferred culling of reflection captures, then sorts and composites them. */
class FReflectionEnvironmentSkyLightingPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FReflectionEnvironmentSkyLightingPS);
	SHADER_USE_PARAMETER_STRUCT(FReflectionEnvironmentSkyLightingPS, FGlobalShader)

	class FHasBoxCaptures : SHADER_PERMUTATION_BOOL("REFLECTION_COMPOSITE_HAS_BOX_CAPTURES");
	class FHasSphereCaptures : SHADER_PERMUTATION_BOOL("REFLECTION_COMPOSITE_HAS_SPHERE_CAPTURES");
	class FDFAOIndirectOcclusion : SHADER_PERMUTATION_BOOL("SUPPORT_DFAO_INDIRECT_OCCLUSION");
	class FSkyLight : SHADER_PERMUTATION_BOOL("ENABLE_SKY_LIGHT");
	class FDynamicSkyLight : SHADER_PERMUTATION_BOOL("ENABLE_DYNAMIC_SKY_LIGHT");
	class FSkyShadowing : SHADER_PERMUTATION_BOOL("APPLY_SKY_SHADOWING");
	class FRayTracedReflections : SHADER_PERMUTATION_BOOL("RAY_TRACED_REFLECTIONS");

	using FPermutationDomain = TShaderPermutationDomain<
		FHasBoxCaptures,
		FHasSphereCaptures,
		FDFAOIndirectOcclusion,
		FSkyLight,
		FDynamicSkyLight,
		FSkyShadowing,
		FRayTracedReflections>;

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		// FSkyLightingDynamicSkyLight requires FSkyLightingSkyLight.
		if (!PermutationVector.Get<FSkyLight>())
		{
			PermutationVector.Set<FDynamicSkyLight>(false);
		}

		// FSkyLightingSkyShadowing requires FSkyLightingDynamicSkyLight.
		if (!PermutationVector.Get<FDynamicSkyLight>())
		{
			PermutationVector.Set<FSkyShadowing>(false);
		}

		return PermutationVector;
	}

	static FPermutationDomain BuildPermutationVector(const FViewInfo& View, bool bBoxCapturesOnly, bool bSphereCapturesOnly, bool bSupportDFAOIndirectOcclusion, bool bEnableSkyLight, bool bEnableDynamicSkyLight, bool bApplySkyShadowing, bool bRayTracedReflections)
	{
		FPermutationDomain PermutationVector;

		PermutationVector.Set<FHasBoxCaptures>(bBoxCapturesOnly);
		PermutationVector.Set<FHasSphereCaptures>(bSphereCapturesOnly);
		PermutationVector.Set<FDFAOIndirectOcclusion>(bSupportDFAOIndirectOcclusion);
		PermutationVector.Set<FSkyLight>(bEnableSkyLight);
		PermutationVector.Set<FDynamicSkyLight>(bEnableDynamicSkyLight);
		PermutationVector.Set<FSkyShadowing>(bApplySkyShadowing);
		PermutationVector.Set<FRayTracedReflections>(bRayTracedReflections);

		return RemapPermutation(PermutationVector);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		if (!IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5))
		{
			return false;
		}

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		return PermutationVector == RemapPermutation(PermutationVector);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("MAX_CAPTURES"), GMaxNumReflectionCaptures);
		OutEnvironment.SetDefine(TEXT("SUPPORTS_ANISOTROPIC_MATERIALS"), FDataDrivenShaderPlatformInfo::GetSupportsAnisotropicMaterials(Parameters.Platform));
		OutEnvironment.CompilerFlags.Add(CFLAG_StandardOptimization);
		FForwardLightingParameters::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )

		// Sky light parameters.
		SHADER_PARAMETER(FVector4, OcclusionTintAndMinOcclusion)
		SHADER_PARAMETER(FVector, ContrastAndNormalizeMulAdd)
		SHADER_PARAMETER(float, ApplyBentNormalAO)
		SHADER_PARAMETER(float, InvSkySpecularOcclusionStrength)
		SHADER_PARAMETER(float, OcclusionExponent)
		SHADER_PARAMETER(float, OcclusionCombineMode)

		// Distance field AO parameters.
		// TODO. FDFAOUpsampleParameters
		SHADER_PARAMETER(FVector2D, AOBufferBilinearUVMax)
		SHADER_PARAMETER(float, DistanceFadeScale)
		SHADER_PARAMETER(float, AOMaxViewDistance)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, BentNormalAOTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, BentNormalAOSampler)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, AmbientOcclusionTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, AmbientOcclusionSampler)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ScreenSpaceReflectionsTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, ScreenSpaceReflectionsSampler)

		SHADER_PARAMETER_TEXTURE(Texture2D, PreIntegratedGF)
		SHADER_PARAMETER_SAMPLER(SamplerState, PreIntegratedGFSampler)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, CloudSkyAOTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, CloudSkyAOSampler)
		SHADER_PARAMETER(FMatrix, CloudSkyAOWorldToLightClipMatrix)
		SHADER_PARAMETER(float, CloudSkyAOFarDepthKm)
		SHADER_PARAMETER(int32, CloudSkyAOEnabled)

		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_STRUCT_REF(FReflectionUniformParameters, ReflectionsParameters)
		SHADER_PARAMETER_STRUCT_REF(FReflectionCaptureShaderData, ReflectionCaptureData)
		SHADER_PARAMETER_STRUCT_REF(FForwardLightData, ForwardLightData)

		RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT()
}; // FReflectionEnvironmentSkyLightingPS

IMPLEMENT_GLOBAL_SHADER(FDiffuseIndirectCompositePS, "/Engine/Private/DiffuseIndirectComposite.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FAmbientCubemapCompositePS, "/Engine/Private/AmbientCubemapComposite.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FReflectionEnvironmentSkyLightingPS, "/Engine/Private/ReflectionEnvironmentPixelShader.usf", "ReflectionEnvironmentSkyLighting", SF_Pixel);


IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FReflectionUniformParameters, "ReflectionStruct");

void SetupReflectionUniformParameters(const FViewInfo& View, FReflectionUniformParameters& OutParameters)
{
	FTextureRHIRef SkyLightTextureResource = GBlackTextureCube->TextureRHI;
	FSamplerStateRHIRef SkyLightCubemapSampler = TStaticSamplerState<SF_Trilinear>::GetRHI();
	FTexture* SkyLightBlendDestinationTextureResource = GBlackTextureCube;
	float ApplySkyLightMask = 0;
	float BlendFraction = 0;
	bool bSkyLightIsDynamic = false;
	float SkyAverageBrightness = 1.0f;

	const bool bApplySkyLight = View.Family->EngineShowFlags.SkyLighting;
	const FScene* Scene = (const FScene*)View.Family->Scene;

	if (Scene
		&& Scene->SkyLight
		&& (Scene->SkyLight->ProcessedTexture || (Scene->SkyLight->bRealTimeCaptureEnabled && Scene->ConvolvedSkyRenderTargetReadyIndex >= 0))
		&& bApplySkyLight)
	{
		const FSkyLightSceneProxy& SkyLight = *Scene->SkyLight;

		if (Scene->SkyLight->bRealTimeCaptureEnabled && Scene->ConvolvedSkyRenderTargetReadyIndex >= 0)
		{
			// Cannot blend with this capture mode as of today.
			SkyLightTextureResource = Scene->ConvolvedSkyRenderTarget[Scene->ConvolvedSkyRenderTargetReadyIndex]->GetRenderTargetItem().ShaderResourceTexture;
		}
		else if (Scene->SkyLight->ProcessedTexture)
		{
			SkyLightTextureResource = SkyLight.ProcessedTexture->TextureRHI;
			SkyLightCubemapSampler = SkyLight.ProcessedTexture->SamplerStateRHI;
			BlendFraction = SkyLight.BlendFraction;

			if (SkyLight.BlendFraction > 0.0f && SkyLight.BlendDestinationProcessedTexture)
			{
				if (SkyLight.BlendFraction < 1.0f)
				{
					SkyLightBlendDestinationTextureResource = SkyLight.BlendDestinationProcessedTexture;
				}
				else
				{
					SkyLightTextureResource = SkyLight.BlendDestinationProcessedTexture->TextureRHI;
					SkyLightCubemapSampler = SkyLight.ProcessedTexture->SamplerStateRHI;
					BlendFraction = 0;
				}
			}
		}

		ApplySkyLightMask = 1;
		bSkyLightIsDynamic = !SkyLight.bHasStaticLighting && !SkyLight.bWantsStaticShadowing;
		SkyAverageBrightness = SkyLight.AverageBrightness;
	}

	const int32 CubemapWidth = SkyLightTextureResource->GetSizeXYZ().X;
	const float SkyMipCount = FMath::Log2(CubemapWidth) + 1.0f;

	OutParameters.SkyLightCubemap = SkyLightTextureResource;
	OutParameters.SkyLightCubemapSampler = SkyLightCubemapSampler;
	OutParameters.SkyLightBlendDestinationCubemap = SkyLightBlendDestinationTextureResource->TextureRHI;
	OutParameters.SkyLightBlendDestinationCubemapSampler = SkyLightBlendDestinationTextureResource->SamplerStateRHI;
	OutParameters.SkyLightParameters = FVector4(SkyMipCount - 1.0f, ApplySkyLightMask, bSkyLightIsDynamic ? 1.0f : 0.0f, BlendFraction);
	OutParameters.SkyLightCubemapBrightness = SkyAverageBrightness;

	// Note: GBlackCubeArrayTexture has an alpha of 0, which is needed to represent invalid data so the sky cubemap can still be applied
	FRHITexture* CubeArrayTexture = (SupportsTextureCubeArray(View.FeatureLevel))? GBlackCubeArrayTexture->TextureRHI : GBlackTextureCube->TextureRHI;

	if (View.Family->EngineShowFlags.ReflectionEnvironment
		&& SupportsTextureCubeArray(View.FeatureLevel)
		&& Scene
		&& Scene->ReflectionSceneData.CubemapArray.IsValid()
		&& Scene->ReflectionSceneData.RegisteredReflectionCaptures.Num())
	{
		CubeArrayTexture = Scene->ReflectionSceneData.CubemapArray.GetRenderTarget().ShaderResourceTexture;
	}

	OutParameters.ReflectionCubemap = CubeArrayTexture;
	OutParameters.ReflectionCubemapSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	OutParameters.PreIntegratedGF = GSystemTextures.PreintegratedGF->GetRenderTargetItem().ShaderResourceTexture;
	OutParameters.PreIntegratedGFSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
}

TUniformBufferRef<FReflectionUniformParameters> CreateReflectionUniformBuffer(const class FViewInfo& View, EUniformBufferUsage Usage)
{
	FReflectionUniformParameters ReflectionStruct;
	SetupReflectionUniformParameters(View, ReflectionStruct);
	return CreateUniformBufferImmediate(ReflectionStruct, Usage);
}

bool FDeferredShadingSceneRenderer::ShouldDoReflectionEnvironment() const
{
	const ERHIFeatureLevel::Type SceneFeatureLevel = Scene->GetFeatureLevel();

	return IsReflectionEnvironmentAvailable(SceneFeatureLevel)
		&& Scene->ReflectionSceneData.RegisteredReflectionCaptures.Num()
		&& ViewFamily.EngineShowFlags.ReflectionEnvironment;
}

#if RHI_RAYTRACING

bool ShouldRenderExperimentalPluginRayTracingGlobalIllumination()
{
	if(!CVarGlobalIlluminationExperimentalPluginEnable.GetValueOnRenderThread())
	{
		return false;
	}

	bool bAnyRayTracingPassEnabled = false;
	FGlobalIlluminationExperimentalPluginDelegates::FAnyRayTracingPassEnabled& Delegate = FGlobalIlluminationExperimentalPluginDelegates::AnyRayTracingPassEnabled();
	Delegate.Broadcast(bAnyRayTracingPassEnabled);

	return ShouldRenderRayTracingEffect(bAnyRayTracingPassEnabled);
}

void FDeferredShadingSceneRenderer::PrepareRayTracingGlobalIlluminationPlugin(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	// Call the GI plugin delegate function to prepare ray tracing
	FGlobalIlluminationExperimentalPluginDelegates::FPrepareRayTracing& Delegate = FGlobalIlluminationExperimentalPluginDelegates::PrepareRayTracing();
	Delegate.Broadcast(View, OutRayGenShaders);
}

#endif

void FDeferredShadingSceneRenderer::RenderDiffuseIndirectAndAmbientOcclusion(
	FRDGBuilder& GraphBuilder,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
	FRDGTextureRef SceneColorTexture,
	FRDGTextureRef LightingChannelsTexture,
	FHairStrandsRenderingData* InHairDatas)
{
	RDG_EVENT_SCOPE(GraphBuilder, "DiffuseIndirectAndAO");

	// Forwared shading SSAO is applied before the basepass using only the depth buffer.
	if (IsForwardShadingEnabled(ViewFamily.GetShaderPlatform()))
	{
		return;
	}

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);

	FSceneTextureParameters SceneTextures = GetSceneTextureParameters(GraphBuilder, SceneTexturesUniformBuffer);
	
	const bool bSingleView = (Views.Num() == 1);
	for (FViewInfo& View : Views)
	{
		RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);

		// TODO: enum cvar. 
		const bool bApplyRTGI = ShouldRenderRayTracingGlobalIllumination(View);
		const bool bApplyPluginGI = CVarGlobalIlluminationExperimentalPluginEnable.GetValueOnRenderThread();
		const bool bApplySSGI = ShouldRenderScreenSpaceDiffuseIndirect(View) && bSingleView; // TODO: support multiple view SSGI
		const bool bApplySSAO = SceneContext.bScreenSpaceAOIsValid;
		const bool bApplyRTAO = ShouldRenderRayTracingAmbientOcclusion(View) && bSingleView; //#dxr_todo: enable RTAO in multiview mode

		int32 DenoiseMode = CVarDiffuseIndirectDenoiser.GetValueOnRenderThread();

		IScreenSpaceDenoiser::FAmbientOcclusionRayTracingConfig RayTracingConfig;

		// TODO: hybrid SSGI / RTGI
		IScreenSpaceDenoiser::FDiffuseIndirectInputs DenoiserInputs;
		if (bApplyRTGI)
		{
			bool bIsValid = RenderRayTracingGlobalIllumination(GraphBuilder, SceneTextures, View, /* out */ &RayTracingConfig, /* out */ &DenoiserInputs);
			if (!bIsValid)
			{
				DenoiseMode = 0;
			}
		}
		else if (bApplySSGI)
		{
			RenderScreenSpaceDiffuseIndirect(GraphBuilder, SceneTextures, SceneColorTexture, View, /* out */ &RayTracingConfig, /* out */ &DenoiserInputs);

			const IScreenSpaceDenoiser* DefaultDenoiser = IScreenSpaceDenoiser::GetDefaultDenoiser();
			const IScreenSpaceDenoiser* DenoiserToUse = DenoiseMode == 1 ? DefaultDenoiser : GScreenSpaceDenoiser;

			if (!DenoiserToUse->SupportsScreenSpaceDiffuseIndirectDenoiser(View.GetShaderPlatform()) && DenoiseMode > 0)
			{
				DenoiseMode = 0;
			}
		}
		else
		{
			// No need for denoising.
			DenoiseMode = 0;
		}
		
		IScreenSpaceDenoiser::FDiffuseIndirectOutputs DenoiserOutputs;
		if (DenoiseMode != 0)
		{
			const IScreenSpaceDenoiser* DefaultDenoiser = IScreenSpaceDenoiser::GetDefaultDenoiser();
			const IScreenSpaceDenoiser* DenoiserToUse = DenoiseMode == 1 ? DefaultDenoiser : GScreenSpaceDenoiser;

			RDG_EVENT_SCOPE(GraphBuilder, "%s%s(DiffuseIndirect) %dx%d",
				DenoiserToUse != DefaultDenoiser ? TEXT("ThirdParty ") : TEXT(""),
				DenoiserToUse->GetDebugName(),
				View.ViewRect.Width(), View.ViewRect.Height());

			if (bApplyRTGI)
			{
				DenoiserOutputs = DenoiserToUse->DenoiseDiffuseIndirect(
					GraphBuilder,
					View,
					&View.PrevViewInfo,
					SceneTextures,
					DenoiserInputs,
					RayTracingConfig);
			}
			else
			{
				DenoiserOutputs = DenoiserToUse->DenoiseScreenSpaceDiffuseIndirect(
					GraphBuilder,
					View,
					&View.PrevViewInfo,
					SceneTextures,
					DenoiserInputs,
					RayTracingConfig);
			}
		}
		else
		{
			DenoiserOutputs.Color = DenoiserInputs.Color;
			DenoiserOutputs.AmbientOcclusionMask = DenoiserInputs.AmbientOcclusionMask;
		}

		// Render GI from a plugin
		if (bApplyPluginGI && !bApplyRTGI)
		{
			// Get the resources and call the GI plugin's rendering function delegate
			FGlobalIlluminationExperimentalPluginResources GIPluginResources;
			GIPluginResources.GBufferA = SceneContext.GBufferA;
			GIPluginResources.GBufferB = SceneContext.GBufferB;
			GIPluginResources.GBufferC = SceneContext.GBufferC;
			GIPluginResources.LightingChannelsTexture = LightingChannelsTexture;
			GIPluginResources.SceneDepthZ = SceneContext.SceneDepthZ;
			GIPluginResources.SceneColor = SceneContext.GetSceneColor();

			FGlobalIlluminationExperimentalPluginDelegates::FRenderDiffuseIndirectLight& Delegate = FGlobalIlluminationExperimentalPluginDelegates::RenderDiffuseIndirectLight();
			Delegate.Broadcast(*Scene, View, GraphBuilder, GIPluginResources);
		}

		// Render RTAO that override any technic.
		if (bApplyRTAO)
		{
			FRDGTextureRef AmbientOcclusionMask = nullptr;

			RenderRayTracingAmbientOcclusion(
				GraphBuilder,
				View,
				SceneTextures,
				&AmbientOcclusionMask);

			DenoiserOutputs.AmbientOcclusionMask = AmbientOcclusionMask;
		}

		// Extract the dynamic AO for application of AO beyond RenderDiffuseIndirectAndAmbientOcclusion()
		if (DenoiserOutputs.AmbientOcclusionMask)
		{
			//ensureMsgf(!bApplySSAO, TEXT("Looks like SSAO has been computed for this view but is being overridden."));
			ensureMsgf(bSingleView, TEXT("Need to add support for one AO texture per view in FSceneRenderTargets")); // TODO.

			ConvertToExternalTexture(GraphBuilder, DenoiserOutputs.AmbientOcclusionMask, SceneContext.ScreenSpaceAO);
			SceneContext.bScreenSpaceAOIsValid = true;
		}
		else if (bApplySSAO)
		{
			// Fetch result of SSAO that was done earlier.
			DenoiserOutputs.AmbientOcclusionMask = GraphBuilder.RegisterExternalTexture(SceneContext.ScreenSpaceAO);
		}

		if (InHairDatas && (bApplySSGI || bApplySSAO))
		{
			RenderHairStrandsAmbientOcclusion(
				GraphBuilder,
				Views,
				InHairDatas,
				DenoiserOutputs.AmbientOcclusionMask);
		}

		// Applies diffuse indirect and ambient occlusion to the scene color.
		if (DenoiserOutputs.Color || DenoiserOutputs.AmbientOcclusionMask)
		{
			FDiffuseIndirectCompositePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDiffuseIndirectCompositePS::FParameters>();
			
			PassParameters->AmbientOcclusionStaticFraction = FMath::Clamp(View.FinalPostProcessSettings.AmbientOcclusionStaticFraction, 0.0f, 1.0f);

			PassParameters->DiffuseIndirectTexture = DenoiserOutputs.Color;
			PassParameters->DiffuseIndirectSampler = TStaticSamplerState<SF_Point>::GetRHI();

			PassParameters->AmbientOcclusionTexture = DenoiserOutputs.AmbientOcclusionMask;
			PassParameters->AmbientOcclusionSampler = TStaticSamplerState<SF_Point>::GetRHI();
			
			PassParameters->SceneTextures = SceneTextures;
			PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;

			PassParameters->RenderTargets[0] = FRenderTargetBinding(
				SceneColorTexture, ERenderTargetLoadAction::ELoad);
		
			FDiffuseIndirectCompositePS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FDiffuseIndirectCompositePS::FApplyDiffuseIndirectDim>(PassParameters->DiffuseIndirectTexture != nullptr);
			PermutationVector.Set<FDiffuseIndirectCompositePS::FApplyAmbientOcclusionDim>(PassParameters->AmbientOcclusionTexture != nullptr);

			TShaderMapRef<FDiffuseIndirectCompositePS> PixelShader(View.ShaderMap, PermutationVector);
			ClearUnusedGraphResources(PixelShader, PassParameters);

			GraphBuilder.AddPass(
				RDG_EVENT_NAME(
					"DiffuseIndirectComposite(ApplyAO=%s ApplyDiffuseIndirect=%s) %dx%d",
					PermutationVector.Get<FDiffuseIndirectCompositePS::FApplyAmbientOcclusionDim>() ? TEXT("Yes") : TEXT("No"),
					PermutationVector.Get<FDiffuseIndirectCompositePS::FApplyDiffuseIndirectDim>() ? TEXT("Yes") : TEXT("No"),
					View.ViewRect.Width(), View.ViewRect.Height()),
				PassParameters,
				ERDGPassFlags::Raster,
				[PassParameters, &View, PixelShader, PermutationVector](FRHICommandList& RHICmdList)
			{
				RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 0.0);
				
				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				FPixelShaderUtils::InitFullscreenPipelineState(RHICmdList, View.ShaderMap, PixelShader, /* out */ GraphicsPSOInit);
				
				if (PermutationVector.Get<FDiffuseIndirectCompositePS::FApplyAmbientOcclusionDim>())
				{
					GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_SourceAlpha, BO_Add, BF_Zero, BF_SourceAlpha>::GetRHI();
				}
				else
				{
					GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_One>::GetRHI();
				}
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);

				FPixelShaderUtils::DrawFullscreenTriangle(RHICmdList);
			});
		} // if (DenoiserOutputs.Color || bApplySSAO)

		// Apply the ambient cubemaps
		if (IsAmbientCubemapPassRequired(View))
		{
			FAmbientCubemapCompositePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FAmbientCubemapCompositePS::FParameters>();
			
			PassParameters->PreIntegratedGF = GSystemTextures.PreintegratedGF->GetRenderTargetItem().ShaderResourceTexture;
			PassParameters->PreIntegratedGFSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			
			PassParameters->AmbientOcclusionTexture = DenoiserOutputs.AmbientOcclusionMask;
			PassParameters->AmbientOcclusionSampler = TStaticSamplerState<SF_Point>::GetRHI();
			
			if (!PassParameters->AmbientOcclusionTexture)
			{
				PassParameters->AmbientOcclusionTexture = GraphBuilder.RegisterExternalTexture(GSystemTextures.WhiteDummy);
			}

			PassParameters->SceneTextures = SceneTextures;
			PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;

			PassParameters->RenderTargets[0] = FRenderTargetBinding(
				SceneColorTexture, ERenderTargetLoadAction::ELoad);
		
			TShaderMapRef<FAmbientCubemapCompositePS> PixelShader(View.ShaderMap);
			GraphBuilder.AddPass(
				RDG_EVENT_NAME("AmbientCubemapComposite %dx%d", View.ViewRect.Width(), View.ViewRect.Height()),
				PassParameters,
				ERDGPassFlags::Raster,
				[PassParameters, &View, PixelShader](FRHICommandList& RHICmdList)
			{
				TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
				
				RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 0.0);

				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

				// set the state
				GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI();
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				GraphicsPSOInit.PrimitiveType = PT_TriangleList;

				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

				uint32 Count = View.FinalPostProcessSettings.ContributingCubemaps.Num();
				for (const FFinalPostProcessSettings::FCubemapEntry& CubemapEntry : View.FinalPostProcessSettings.ContributingCubemaps)
				{
					FAmbientCubemapCompositePS::FParameters ShaderParameters = *PassParameters;
					SetupAmbientCubemapParameters(CubemapEntry, &ShaderParameters.AmbientCubemap);
					SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), ShaderParameters);
					
					DrawPostProcessPass(
						RHICmdList,
						0, 0,
						View.ViewRect.Width(), View.ViewRect.Height(),
						View.ViewRect.Min.X, View.ViewRect.Min.Y,
						View.ViewRect.Width(), View.ViewRect.Height(),
						View.ViewRect.Size(),
						FSceneRenderTargets::Get(RHICmdList).GetBufferSizeXY(),
						VertexShader,
						View.StereoPass, 
						false, // TODO.
						EDRF_UseTriangleOptimization);
				}
			});
		}
	}
}

void FDeferredShadingSceneRenderer::RenderDeferredReflectionsAndSkyLighting(
	FRDGBuilder& GraphBuilder,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
	FRDGTextureMSAA SceneColorTexture,
	FRDGTextureRef DynamicBentNormalAOTexture,
	FRDGTextureRef VelocityTexture,
	FHairStrandsRenderingData* HairDatas)
{
	if (ViewFamily.EngineShowFlags.VisualizeLightCulling 
		|| ViewFamily.EngineShowFlags.RayTracingDebug
		|| ViewFamily.EngineShowFlags.PathTracing
		|| !ViewFamily.EngineShowFlags.Lighting)
	{
		return;
	}

	// If we're currently capturing a reflection capture, output SpecularColor * IndirectIrradiance for metals so they are not black in reflections,
	// Since we don't have multiple bounce specular reflections
	bool bReflectionCapture = false;
	for (int32 ViewIndex = 0, Num = Views.Num(); ViewIndex < Num; ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];
		bReflectionCapture = bReflectionCapture || View.bIsReflectionCapture;
	}

	if (bReflectionCapture)
	{
		// if we are rendering a reflection capture then we can skip this pass entirely (no reflection and no sky contribution evaluated in this pass)
		return;
	}

	// The specular sky light contribution is also needed by RT Reflections as a fallback.
	const bool bSkyLight = Scene->SkyLight
		&& (Scene->SkyLight->ProcessedTexture || Scene->SkyLight->bRealTimeCaptureEnabled)
		&& !Scene->SkyLight->bHasStaticLighting;

	bool bDynamicSkyLight = ShouldRenderDeferredDynamicSkyLight(Scene, ViewFamily);
	bool bApplySkyShadowing = false;
	if (bDynamicSkyLight)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "SkyLightDiffuse");
		RDG_GPU_STAT_SCOPE(GraphBuilder, SkyLightDiffuse);

		extern int32 GDistanceFieldAOApplyToStaticIndirect;
		if (Scene->SkyLight->bCastShadows
			&& !GDistanceFieldAOApplyToStaticIndirect
			&& ShouldRenderDistanceFieldAO()
			&& ShouldRenderDistanceFieldLighting()
			&& ViewFamily.EngineShowFlags.AmbientOcclusion)
		{
			bApplySkyShadowing = true;
			FDistanceFieldAOParameters Parameters(Scene->SkyLight->OcclusionMaxDistance, Scene->SkyLight->Contrast);
			RenderDistanceFieldLighting(GraphBuilder, SceneTexturesUniformBuffer, Parameters, SceneColorTexture.Target, VelocityTexture, DynamicBentNormalAOTexture, false, false);
		}
	}

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);

	const bool bReflectionEnv = ShouldDoReflectionEnvironment();

	FRDGTextureRef AmbientOcclusionTexture = GraphBuilder.RegisterExternalTexture(SceneContext.bScreenSpaceAOIsValid ? SceneContext.ScreenSpaceAO : GSystemTextures.WhiteDummy);
	float DynamicBentNormalAO = 1.0f;

	if (!DynamicBentNormalAOTexture)
	{
		DynamicBentNormalAOTexture = GraphBuilder.RegisterExternalTexture(GSystemTextures.WhiteDummy);
		DynamicBentNormalAO = 0.0f;
	}

	FSceneTextureParameters SceneTextures = GetSceneTextureParameters(GraphBuilder);

	uint32 ViewIndex = 0;
	for (FViewInfo& View : Views)
	{
		const uint32 CurrentViewIndex = ViewIndex++;

		const FRayTracingReflectionOptions RayTracingReflectionOptions = GetRayTracingReflectionOptions(View, *Scene);

		const bool bScreenSpaceReflections = !RayTracingReflectionOptions.bEnabled && ShouldRenderScreenSpaceReflections(View);
		const bool bComposePlanarReflections = !RayTracingReflectionOptions.bEnabled && HasDeferredPlanarReflections(View);

		FRDGTextureRef ReflectionsColor = nullptr;
		if (RayTracingReflectionOptions.bEnabled || bScreenSpaceReflections)
		{
			int32 DenoiserMode = GetReflectionsDenoiserMode();

			bool bDenoise = false;
			bool bTemporalFilter = false;

			// Traces the reflections, either using screen space reflection, or ray tracing.
			IScreenSpaceDenoiser::FReflectionsInputs DenoiserInputs;
			IScreenSpaceDenoiser::FReflectionsRayTracingConfig DenoiserConfig;
			if (RayTracingReflectionOptions.bEnabled)
			{
				RDG_EVENT_SCOPE(GraphBuilder, "RayTracingReflections %d", CurrentViewIndex);
				RDG_GPU_STAT_SCOPE(GraphBuilder, RayTracingReflections);

				bDenoise = DenoiserMode != 0;

				DenoiserConfig.ResolutionFraction = RayTracingReflectionOptions.ResolutionFraction;
				DenoiserConfig.RayCountPerPixel = RayTracingReflectionOptions.SamplesPerPixel;

				check(RayTracingReflectionOptions.bReflectOnlyWater == false);

				RenderRayTracingReflections(
					GraphBuilder,
					SceneTextures,
					View,
					DenoiserMode,
					RayTracingReflectionOptions,
					&DenoiserInputs);
			}
			else if (bScreenSpaceReflections)
			{
				bDenoise = DenoiserMode != 0 && CVarDenoiseSSR.GetValueOnRenderThread();
				bTemporalFilter = !bDenoise && View.ViewState && IsSSRTemporalPassRequired(View);

				ESSRQuality SSRQuality;
				GetSSRQualityForView(View, &SSRQuality, &DenoiserConfig);

				RDG_EVENT_SCOPE(GraphBuilder, "ScreenSpaceReflections(Quality=%d)", int32(SSRQuality));

				RenderScreenSpaceReflections(
					GraphBuilder, SceneTextures, SceneColorTexture.Resolve, View, SSRQuality, bDenoise, &DenoiserInputs);
			}
			else
			{
				check(0);
			}

			if (bDenoise)
			{
				const IScreenSpaceDenoiser* DefaultDenoiser = IScreenSpaceDenoiser::GetDefaultDenoiser();
				const IScreenSpaceDenoiser* DenoiserToUse = DenoiserMode == 1 ? DefaultDenoiser : GScreenSpaceDenoiser;

				// Standard event scope for denoiser to have all profiling information not matter what, and with explicit detection of third party.
				RDG_EVENT_SCOPE(GraphBuilder, "%s%s(Reflections) %dx%d",
					DenoiserToUse != DefaultDenoiser ? TEXT("ThirdParty ") : TEXT(""),
					DenoiserToUse->GetDebugName(),
					View.ViewRect.Width(), View.ViewRect.Height());

				IScreenSpaceDenoiser::FReflectionsOutputs DenoiserOutputs = DenoiserToUse->DenoiseReflections(
					GraphBuilder,
					View,
					&View.PrevViewInfo,
					SceneTextures,
					DenoiserInputs,
					DenoiserConfig);

				ReflectionsColor = DenoiserOutputs.Color;
			}
			else if (bTemporalFilter)
			{
				check(View.ViewState);
				FTAAPassParameters TAASettings(View);
				TAASettings.Pass = ETAAPassConfig::ScreenSpaceReflections;
				TAASettings.SceneDepthTexture = SceneTextures.SceneDepthTexture;
				TAASettings.SceneVelocityTexture = SceneTextures.GBufferVelocityTexture;
				TAASettings.SceneColorInput = DenoiserInputs.Color;
				TAASettings.bOutputRenderTargetable = bComposePlanarReflections;

				FTAAOutputs TAAOutputs = AddTemporalAAPass(
					GraphBuilder,
					View,
					TAASettings,
					View.PrevViewInfo.SSRHistory,
					&View.ViewState->PrevFrameViewInfo.SSRHistory);

				ReflectionsColor = TAAOutputs.SceneColor;
			}
			else
			{
				if (RayTracingReflectionOptions.bEnabled && DenoiserInputs.RayHitDistance)
				{
					// The performance of ray tracing does not allow to run without a denoiser in real time.
					// Multiple rays per pixel is unsupported by the denoiser that will most likely more bound by to
					// many rays than exporting the hit distance buffer. Therefore no permutation of the ray generation
					// shader has been judged required to be supported.
					GraphBuilder.RemoveUnusedTextureWarning(DenoiserInputs.RayHitDistance);
				}

				ReflectionsColor = DenoiserInputs.Color;
			}
		} // if (RayTracingReflectionOptions.bEnabled || bScreenSpaceReflections)

		if (bComposePlanarReflections)
		{
			check(!RayTracingReflectionOptions.bEnabled);
			RenderDeferredPlanarReflections(GraphBuilder, SceneTextures, View, /* inout */ ReflectionsColor);
		}

		bool bRequiresApply = ReflectionsColor != nullptr || bSkyLight || bDynamicSkyLight || bReflectionEnv;

		if (bRequiresApply)
		{
			RDG_GPU_STAT_SCOPE(GraphBuilder, ReflectionEnvironment);

			// Render the reflection environment with tiled deferred culling
			bool bHasBoxCaptures = (View.NumBoxReflectionCaptures > 0);
			bool bHasSphereCaptures = (View.NumSphereReflectionCaptures > 0);

			FReflectionEnvironmentSkyLightingPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FReflectionEnvironmentSkyLightingPS::FParameters>();

			// Setup the parameters of the shader.
			{
				// Setups all shader parameters related to skylight.
				{
					FSkyLightSceneProxy* SkyLight = Scene->SkyLight;

					float SkyLightContrast = 0.01f;
					float SkyLightOcclusionExponent = 1.0f;
					FVector4 SkyLightOcclusionTintAndMinOcclusion(0.0f, 0.0f, 0.0f, 0.0f);
					EOcclusionCombineMode SkyLightOcclusionCombineMode = EOcclusionCombineMode::OCM_MAX;
					if (SkyLight)
					{
						FDistanceFieldAOParameters Parameters(SkyLight->OcclusionMaxDistance, SkyLight->Contrast);
						SkyLightContrast = Parameters.Contrast;
						SkyLightOcclusionExponent = SkyLight->OcclusionExponent;
						SkyLightOcclusionTintAndMinOcclusion = FVector4(SkyLight->OcclusionTint);
						SkyLightOcclusionTintAndMinOcclusion.W = SkyLight->MinOcclusion;
						SkyLightOcclusionCombineMode = SkyLight->OcclusionCombineMode;
					}

					// Scale and bias to remap the contrast curve to [0,1]
					const float Min = 1 / (1 + FMath::Exp(-SkyLightContrast * (0 * 10 - 5)));
					const float Max = 1 / (1 + FMath::Exp(-SkyLightContrast * (1 * 10 - 5)));
					const float Mul = 1.0f / (Max - Min);
					const float Add = -Min / (Max - Min);

					PassParameters->OcclusionTintAndMinOcclusion = SkyLightOcclusionTintAndMinOcclusion;
					PassParameters->ContrastAndNormalizeMulAdd = FVector(SkyLightContrast, Mul, Add);
					PassParameters->OcclusionExponent = SkyLightOcclusionExponent;
					PassParameters->OcclusionCombineMode = SkyLightOcclusionCombineMode == OCM_Minimum ? 0.0f : 1.0f;
					PassParameters->ApplyBentNormalAO = DynamicBentNormalAO;
					PassParameters->InvSkySpecularOcclusionStrength = 1.0f / FMath::Max(CVarSkySpecularOcclusionStrength.GetValueOnRenderThread(), 0.1f);
				}

				// Setups all shader parameters related to distance field AO
				{
					FIntPoint AOBufferSize = GetBufferSizeForAO();
					PassParameters->AOBufferBilinearUVMax = FVector2D(
						(View.ViewRect.Width() / GAODownsampleFactor - 0.51f) / AOBufferSize.X, // 0.51 - so bilateral gather4 won't sample invalid texels
						(View.ViewRect.Height() / GAODownsampleFactor - 0.51f) / AOBufferSize.Y);

					extern float GAOViewFadeDistanceScale;
					PassParameters->AOMaxViewDistance = GetMaxAOViewDistance();
					PassParameters->DistanceFadeScale = 1.0f / ((1.0f - GAOViewFadeDistanceScale) * GetMaxAOViewDistance());

					PassParameters->BentNormalAOTexture = DynamicBentNormalAOTexture;
					PassParameters->BentNormalAOSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
				}

				PassParameters->AmbientOcclusionTexture = AmbientOcclusionTexture;
				PassParameters->AmbientOcclusionSampler = TStaticSamplerState<SF_Point>::GetRHI();

				PassParameters->ScreenSpaceReflectionsTexture = ReflectionsColor ? ReflectionsColor : GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
				PassParameters->ScreenSpaceReflectionsSampler = TStaticSamplerState<SF_Point>::GetRHI();

				if (Scene->HasVolumetricCloud())
				{
					FVolumetricCloudRenderSceneInfo* CloudInfo = Scene->GetVolumetricCloudSceneInfo();

					PassParameters->CloudSkyAOTexture = GraphBuilder.RegisterExternalTexture(View.VolumetricCloudSkyAO.IsValid() ? View.VolumetricCloudSkyAO : GSystemTextures.BlackDummy);
					PassParameters->CloudSkyAOWorldToLightClipMatrix = CloudInfo->GetVolumetricCloudCommonShaderParameters().CloudSkyAOWorldToLightClipMatrix;
					PassParameters->CloudSkyAOFarDepthKm = CloudInfo->GetVolumetricCloudCommonShaderParameters().CloudSkyAOFarDepthKm;
					PassParameters->CloudSkyAOEnabled = 1;
				}
				else
				{
					PassParameters->CloudSkyAOTexture = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
					PassParameters->CloudSkyAOEnabled = 0;
				}
				PassParameters->CloudSkyAOSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();

				PassParameters->PreIntegratedGF = GSystemTextures.PreintegratedGF->GetRenderTargetItem().ShaderResourceTexture;
				PassParameters->PreIntegratedGFSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

				PassParameters->SceneTextures = SceneTextures;

				PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
				PassParameters->ReflectionCaptureData = View.ReflectionCaptureUniformBuffer;
				{
					FReflectionUniformParameters ReflectionUniformParameters;
					SetupReflectionUniformParameters(View, ReflectionUniformParameters);
					PassParameters->ReflectionsParameters = CreateUniformBufferImmediate(ReflectionUniformParameters, UniformBuffer_SingleDraw);
				}
				PassParameters->ForwardLightData = View.ForwardLightingResources->ForwardLightDataUniformBuffer;
			}

			PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColorTexture.Target, ERenderTargetLoadAction::ELoad);

			// Bind hair data
			const bool bCheckerboardSubsurfaceRendering = IsSubsurfaceCheckerboardFormat(SceneColorTexture.Target->Desc.Format);

			// ScreenSpace and SortedDeferred ray traced reflections use the same reflection environment shader,
			// but main RT reflection shader requires a custom path as it evaluates the clear coat BRDF differently.
			const bool bRequiresSpecializedReflectionEnvironmentShader = RayTracingReflectionOptions.bEnabled
				&& RayTracingReflectionOptions.Algorithm != FRayTracingReflectionOptions::EAlgorithm::SortedDeferred;

			auto PermutationVector = FReflectionEnvironmentSkyLightingPS::BuildPermutationVector(
				View, bHasBoxCaptures, bHasSphereCaptures, DynamicBentNormalAO != 0.0f,
				bSkyLight, bDynamicSkyLight, bApplySkyShadowing,
				bRequiresSpecializedReflectionEnvironmentShader);

			TShaderMapRef<FReflectionEnvironmentSkyLightingPS> PixelShader(View.ShaderMap, PermutationVector);
			ClearUnusedGraphResources(PixelShader, PassParameters);

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("ReflectionEnvironmentAndSky %dx%d", View.ViewRect.Width(), View.ViewRect.Height()),
				PassParameters,
				ERDGPassFlags::Raster,
				[PassParameters, &View, PixelShader, bCheckerboardSubsurfaceRendering](FRHICommandList& InRHICmdList)
			{
				InRHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				FPixelShaderUtils::InitFullscreenPipelineState(InRHICmdList, View.ShaderMap, PixelShader, GraphicsPSOInit);

				extern int32 GAOOverwriteSceneColor;
				if (GetReflectionEnvironmentCVar() == 2 || GAOOverwriteSceneColor)
				{
					// override scene color for debugging
					GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
				}
				else
				{
					if (bCheckerboardSubsurfaceRendering)
					{
						GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_One>::GetRHI();
					}
					else
					{
						GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI();
					}
				}

				SetGraphicsPipelineState(InRHICmdList, GraphicsPSOInit);
				SetShaderParameters(InRHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);
				FPixelShaderUtils::DrawFullscreenTriangle(InRHICmdList);
			});
		}

		const bool bIsHairSkyLightingEnabled = HairDatas && (bSkyLight || bDynamicSkyLight || bReflectionEnv);
		if (bIsHairSkyLightingEnabled)
		{
			RDG_GPU_STAT_SCOPE(GraphBuilder, HairSkyLighting);
			RenderHairStrandsEnvironmentLighting(GraphBuilder, Scene, CurrentViewIndex, Views, HairDatas);
		}
	}

	AddResolveSceneColorPass(GraphBuilder, Views, SceneColorTexture);
}

void FDeferredShadingSceneRenderer::RenderDeferredReflectionsAndSkyLightingHair(
	FRDGBuilder& GraphBuilder,
	FHairStrandsRenderingData* HairDatas)
{
	if (ViewFamily.EngineShowFlags.VisualizeLightCulling || !ViewFamily.EngineShowFlags.Lighting)
	{
		return;
	}

	// If we're currently capturing a reflection capture, output SpecularColor * IndirectIrradiance for metals so they are not black in reflections,
	// Since we don't have multiple bounce specular reflections
	bool bReflectionCapture = false;
	for (int32 ViewIndex = 0, Num = Views.Num(); ViewIndex < Num; ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];
		bReflectionCapture = bReflectionCapture || View.bIsReflectionCapture;
	}

	if (bReflectionCapture)
	{
		// if we are rendering a reflection capture then we can skip this pass entirely (no reflection and no sky contribution evaluated in this pass)
		return;
	}

	// The specular sky light contribution is also needed by RT Reflections as a fallback.
	const bool bSkyLight = Scene->SkyLight
		&& Scene->SkyLight->ProcessedTexture
		&& !Scene->SkyLight->bHasStaticLighting;

	bool bDynamicSkyLight = ShouldRenderDeferredDynamicSkyLight(Scene, ViewFamily);
	bool bApplySkyShadowing = false;
	const bool bReflectionEnv = ShouldDoReflectionEnvironment();

	uint32 ViewIndex = 0;
	for (FViewInfo& View : Views)
	{
		const uint32 CurrentViewIndex = ViewIndex++;
		const bool bIsHairSkyLightingEnabled = HairDatas && (bSkyLight || bDynamicSkyLight || bReflectionEnv);
		if (bIsHairSkyLightingEnabled)
		{
			RenderHairStrandsEnvironmentLighting(GraphBuilder, Scene, CurrentViewIndex, Views, HairDatas);
		}
	}
}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
void FDeferredShadingSceneRenderer::RenderGlobalIlluminationExperimentalPluginVisualizations(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef LightingChannelsTexture)
{
	// Early out if GI plugins aren't enabled
	if (!CVarGlobalIlluminationExperimentalPluginEnable.GetValueOnRenderThread()) return;
	
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);

	// Get the resources passed to GI plugins
	FGlobalIlluminationExperimentalPluginResources GIPluginResources;
	GIPluginResources.GBufferA = SceneContext.GBufferA;
	GIPluginResources.GBufferB = SceneContext.GBufferB;
	GIPluginResources.GBufferC = SceneContext.GBufferC;
	GIPluginResources.LightingChannelsTexture = LightingChannelsTexture;
	GIPluginResources.SceneDepthZ = SceneContext.SceneDepthZ;
	GIPluginResources.SceneColor = SceneContext.GetSceneColor();

	// Render visualizations to all views by calling the GI plugin's delegate
	FGlobalIlluminationExperimentalPluginDelegates::FRenderDiffuseIndirectVisualizations& PRVDelegate = FGlobalIlluminationExperimentalPluginDelegates::RenderDiffuseIndirectVisualizations();
	for (int32 ViewIndexZ = 0; ViewIndexZ < Views.Num(); ViewIndexZ++)
	{
		PRVDelegate.Broadcast(*Scene, Views[ViewIndexZ], GraphBuilder, GIPluginResources);
	}
}
#endif //!(UE_BUILD_SHIPPING || UE_BUILD_TEST)