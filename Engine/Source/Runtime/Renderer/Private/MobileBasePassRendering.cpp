// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MobileBasePassRendering.cpp: Base pass rendering implementation.
=============================================================================*/

#include "MobileBasePassRendering.h"
#include "DynamicPrimitiveDrawing.h"
#include "ScenePrivate.h"
#include "SceneTextureParameters.h"
#include "ShaderPlatformQualitySettings.h"
#include "MaterialShaderQualitySettings.h"
#include "PrimitiveSceneInfo.h"
#include "MeshPassProcessor.h"
#include "MeshPassProcessor.inl"
#include "EditorPrimitivesRendering.h"

#include "FramePro/FrameProProfiler.h"
#include "PostProcess/PostProcessPixelProjectedReflectionMobile.h"

// Changing this causes a full shader recompile
static TAutoConsoleVariable<int32> CVarMobileDisableVertexFog(
	TEXT("r.Mobile.DisableVertexFog"),
	1,
	TEXT("Set to 1 to disable vertex fogging in all mobile shaders."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarMobileEnableMovableSpotLights(
	TEXT("r.Mobile.EnableMovableSpotlights"),
	0,
	TEXT("If 1 then enable movable spotlight support"),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarMobileEnableMovableSpotLightShadows(
	TEXT("r.Mobile.EnableMovableSpotlightsShadow"),
	0,
	TEXT("If 1 then enable movable spotlight shadow support"),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarMobileMaxVisibleMovableSpotLightsShadow(
	TEXT("r.Mobile.MaxVisibleMovableSpotLightsShadow"),
	8,
	TEXT("The max number of visible spotlighs can cast shadow sorted by screen size, should be as less as possible for performance reason"),
	ECVF_RenderThreadSafe);

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FMobileBasePassUniformParameters, "MobileBasePass");

static TAutoConsoleVariable<int32> CVarMobileUseHWsRGBEncoding(
	TEXT("r.Mobile.UseHWsRGBEncoding"),
	0,
	TEXT("0: Write sRGB encoding in the shader\n")
	TEXT("1: Use GPU HW to convert linear to sRGB automatically (device must support sRGB write control)\n"),
	ECVF_RenderThreadSafe);

namespace MobileBasePass
{
	FShaderPlatformCachedIniValue<bool> MobileDynamicPointLightsUseStaticBranchIniValue(TEXT("/Script/Engine.RendererSettings"), TEXT("r.MobileDynamicPointLightsUseStaticBranch"));
	FShaderPlatformCachedIniValue<int32> MobileNumDynamicPointLightsIniValue(TEXT("/Script/Engine.RendererSettings"), TEXT("r.MobileNumDynamicPointLights"));
};

bool ShouldCacheShaderByPlatformAndOutputFormat(EShaderPlatform Platform, EOutputFormat OutputFormat)
{
	bool bSupportsMobileHDR = IsMobileHDR();
	bool bShaderUsesLDR = (OutputFormat == LDR_GAMMA_32);

	// only cache this shader if the LDR/HDR output matches what we currently support.  IsMobileHDR can't change, so we don't need
	// the LDR shaders if we are doing HDR, and vice-versa.
	return (bShaderUsesLDR && !bSupportsMobileHDR) || (!bShaderUsesLDR && bSupportsMobileHDR);
}

#define IMPLEMENT_MOBILE_SHADING_BASEPASS_LIGHTMAPPED_VERTEX_SHADER_TYPE(LightMapPolicyType,LightMapPolicyName) \
	typedef TMobileBasePassVS< LightMapPolicyType, LDR_GAMMA_32 > TMobileBasePassVS##LightMapPolicyName##LDRGamma32; \
	typedef TMobileBasePassVS< LightMapPolicyType, HDR_LINEAR_64 > TMobileBasePassVS##LightMapPolicyName##HDRLinear64; \
	IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, TMobileBasePassVS##LightMapPolicyName##LDRGamma32, TEXT("/Engine/Private/MobileBasePassVertexShader.usf"), TEXT("Main"), SF_Vertex); \
	IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, TMobileBasePassVS##LightMapPolicyName##HDRLinear64, TEXT("/Engine/Private/MobileBasePassVertexShader.usf"), TEXT("Main"), SF_Vertex);

#define IMPLEMENT_MOBILE_SHADING_BASEPASS_LIGHTMAPPED_PIXEL_SHADER_TYPE(LightMapPolicyType,LightMapPolicyName,NumMovablePointLights) \
	typedef TMobileBasePassPS< LightMapPolicyType, LDR_GAMMA_32, false, NumMovablePointLights > TMobileBasePassPS##LightMapPolicyName##NumMovablePointLights##LDRGamma32; \
	typedef TMobileBasePassPS< LightMapPolicyType, HDR_LINEAR_64, false, NumMovablePointLights > TMobileBasePassPS##LightMapPolicyName##NumMovablePointLights##HDRLinear64; \
	typedef TMobileBasePassPS< LightMapPolicyType, LDR_GAMMA_32, true, NumMovablePointLights > TMobileBasePassPS##LightMapPolicyName##NumMovablePointLights##LDRGamma32##Skylight; \
	typedef TMobileBasePassPS< LightMapPolicyType, HDR_LINEAR_64, true, NumMovablePointLights > TMobileBasePassPS##LightMapPolicyName##NumMovablePointLights##HDRLinear64##Skylight; \
	IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, TMobileBasePassPS##LightMapPolicyName##NumMovablePointLights##LDRGamma32, TEXT("/Engine/Private/MobileBasePassPixelShader.usf"), TEXT("Main"), SF_Pixel); \
	IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, TMobileBasePassPS##LightMapPolicyName##NumMovablePointLights##HDRLinear64, TEXT("/Engine/Private/MobileBasePassPixelShader.usf"), TEXT("Main"), SF_Pixel); \
	IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, TMobileBasePassPS##LightMapPolicyName##NumMovablePointLights##LDRGamma32##Skylight, TEXT("/Engine/Private/MobileBasePassPixelShader.usf"), TEXT("Main"), SF_Pixel); \
	IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, TMobileBasePassPS##LightMapPolicyName##NumMovablePointLights##HDRLinear64##Skylight, TEXT("/Engine/Private/MobileBasePassPixelShader.usf"), TEXT("Main"), SF_Pixel);

static_assert(MAX_BASEPASS_DYNAMIC_POINT_LIGHTS == 4, "If you change MAX_BASEPASS_DYNAMIC_POINT_LIGHTS, you need to add shader types below");

// Permutations for the number of point lights to support. INT32_MAX indicates the shader should use branching to support a variable number of point lights.
#define IMPLEMENT_MOBILE_SHADING_BASEPASS_LIGHTMAPPED_SHADER_TYPE(LightMapPolicyType,LightMapPolicyName) \
	IMPLEMENT_MOBILE_SHADING_BASEPASS_LIGHTMAPPED_VERTEX_SHADER_TYPE(LightMapPolicyType, LightMapPolicyName) \
	IMPLEMENT_MOBILE_SHADING_BASEPASS_LIGHTMAPPED_PIXEL_SHADER_TYPE(LightMapPolicyType, LightMapPolicyName, 0) \
	IMPLEMENT_MOBILE_SHADING_BASEPASS_LIGHTMAPPED_PIXEL_SHADER_TYPE(LightMapPolicyType, LightMapPolicyName, 1) \
	IMPLEMENT_MOBILE_SHADING_BASEPASS_LIGHTMAPPED_PIXEL_SHADER_TYPE(LightMapPolicyType, LightMapPolicyName, 2) \
	IMPLEMENT_MOBILE_SHADING_BASEPASS_LIGHTMAPPED_PIXEL_SHADER_TYPE(LightMapPolicyType, LightMapPolicyName, 3) \
	IMPLEMENT_MOBILE_SHADING_BASEPASS_LIGHTMAPPED_PIXEL_SHADER_TYPE(LightMapPolicyType, LightMapPolicyName, 4) \
	IMPLEMENT_MOBILE_SHADING_BASEPASS_LIGHTMAPPED_PIXEL_SHADER_TYPE(LightMapPolicyType, LightMapPolicyName, INT32_MAX)

// Implement shader types per lightmap policy 
IMPLEMENT_MOBILE_SHADING_BASEPASS_LIGHTMAPPED_SHADER_TYPE(TUniformLightMapPolicy<LMP_NO_LIGHTMAP>, FNoLightMapPolicy);
IMPLEMENT_MOBILE_SHADING_BASEPASS_LIGHTMAPPED_SHADER_TYPE(TUniformLightMapPolicy<LMP_LQ_LIGHTMAP>, TLightMapPolicyLQ);
IMPLEMENT_MOBILE_SHADING_BASEPASS_LIGHTMAPPED_SHADER_TYPE(TUniformLightMapPolicy<LMP_MOBILE_DISTANCE_FIELD_SHADOWS_AND_LQ_LIGHTMAP>, FMobileDistanceFieldShadowsAndLQLightMapPolicy);
IMPLEMENT_MOBILE_SHADING_BASEPASS_LIGHTMAPPED_SHADER_TYPE(TUniformLightMapPolicy<LMP_MOBILE_DISTANCE_FIELD_SHADOWS_LIGHTMAP_AND_CSM>, FMobileDistanceFieldShadowsLightMapAndCSMLightingPolicy);
IMPLEMENT_MOBILE_SHADING_BASEPASS_LIGHTMAPPED_SHADER_TYPE(TUniformLightMapPolicy<LMP_MOBILE_DIRECTIONAL_LIGHT_CSM_AND_LIGHTMAP>, FMobileDirectionalLightCSMAndLightMapPolicy);
IMPLEMENT_MOBILE_SHADING_BASEPASS_LIGHTMAPPED_SHADER_TYPE(TUniformLightMapPolicy<LMP_MOBILE_DIRECTIONAL_LIGHT_AND_SH_INDIRECT>, FMobileDirectionalLightAndSHIndirectPolicy);
IMPLEMENT_MOBILE_SHADING_BASEPASS_LIGHTMAPPED_SHADER_TYPE(TUniformLightMapPolicy<LMP_MOBILE_DIRECTIONAL_LIGHT_CSM_AND_SH_INDIRECT>, FMobileDirectionalLightCSMAndSHIndirectPolicy);
IMPLEMENT_MOBILE_SHADING_BASEPASS_LIGHTMAPPED_SHADER_TYPE(TUniformLightMapPolicy<LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_WITH_LIGHTMAP>, FMobileMovableDirectionalLightWithLightmapPolicy);
IMPLEMENT_MOBILE_SHADING_BASEPASS_LIGHTMAPPED_SHADER_TYPE(TUniformLightMapPolicy<LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_CSM_WITH_LIGHTMAP>, FMobileMovableDirectionalLightCSMWithLightmapPolicy);
IMPLEMENT_MOBILE_SHADING_BASEPASS_LIGHTMAPPED_SHADER_TYPE(TUniformLightMapPolicy<LMP_MOBILE_DIRECTIONAL_LIGHT_CSM>, FMobileDirectionalLightAndCSMPolicy);

template<typename LightMapPolicyType>
bool TMobileBasePassPSPolicyParamType<LightMapPolicyType>::ModifyCompilationEnvironmentForQualityLevel(EShaderPlatform Platform, EMaterialQualityLevel::Type QualityLevel, FShaderCompilerEnvironment& OutEnvironment)
{
	// Get quality settings for shader platform
	const UShaderPlatformQualitySettings* MaterialShadingQuality = UMaterialShaderQualitySettings::Get()->GetShaderPlatformQualitySettings(Platform);
	const FMaterialQualityOverrides& QualityOverrides = MaterialShadingQuality->GetQualityOverrides(QualityLevel);

	// the point of this check is to keep the logic between enabling overrides here and in UMaterial::GetQualityLevelUsage() in sync
	checkf(QualityOverrides.CanOverride(Platform), TEXT("ShaderPlatform %d was not marked as being able to use quality overrides! Include it in CanOverride() and recook."), static_cast<int32>(Platform));
	OutEnvironment.SetDefine(TEXT("MOBILE_QL_FORCE_FULLY_ROUGH"), QualityOverrides.bEnableOverride && QualityOverrides.bForceFullyRough != 0 ? 1u : 0u);
	OutEnvironment.SetDefine(TEXT("MOBILE_QL_FORCE_NONMETAL"), QualityOverrides.bEnableOverride && QualityOverrides.bForceNonMetal != 0 ? 1u : 0u);
	OutEnvironment.SetDefine(TEXT("QL_FORCEDISABLE_LM_DIRECTIONALITY"), QualityOverrides.bEnableOverride && QualityOverrides.bForceDisableLMDirectionality != 0 ? 1u : 0u);
	OutEnvironment.SetDefine(TEXT("MOBILE_QL_FORCE_LQ_REFLECTIONS"), QualityOverrides.bEnableOverride && QualityOverrides.bForceLQReflections != 0 ? 1u : 0u);
	OutEnvironment.SetDefine(TEXT("MOBILE_QL_FORCE_DISABLE_PREINTEGRATEDGF"), QualityOverrides.bEnableOverride && QualityOverrides.bForceDisablePreintegratedGF != 0 ? 1u : 0u);
	OutEnvironment.SetDefine(TEXT("MOBILE_SHADOW_QUALITY"), (uint32)QualityOverrides.MobileShadowQuality);
	OutEnvironment.SetDefine(TEXT("MOBILE_QL_DISABLE_MATERIAL_NORMAL"), QualityOverrides.bEnableOverride && QualityOverrides.bDisableMaterialNormalCalculation);
	return true;
}

FMobileBasePassMovableLightInfo::FMobileBasePassMovableLightInfo(const FPrimitiveSceneProxy* InSceneProxy)
: NumMovablePointLights(0)
{
	static auto* MobileNumDynamicPointLightsCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileNumDynamicPointLights"));
	const int32 MobileNumDynamicPointLights = MobileNumDynamicPointLightsCVar->GetValueOnRenderThread();

	if (InSceneProxy != nullptr)
	{
		for (FLightPrimitiveInteraction* LPI = InSceneProxy->GetPrimitiveSceneInfo()->LightList; LPI && NumMovablePointLights < MobileNumDynamicPointLights; LPI = LPI->GetNextLight())
		{
			FLightSceneInfo* LightSceneInfo = LPI->GetLight();
			FLightSceneProxy* LightProxy = LightSceneInfo->Proxy;
			const uint8 LightType = LightProxy->GetLightType(); 
			const bool bIsValidLightType =
				  LightType == LightType_Point
				|| LightType == LightType_Rect
				|| (LightType == LightType_Spot && CVarMobileEnableMovableSpotLights.GetValueOnRenderThread());
				
			if (bIsValidLightType && LightProxy->IsMovable() && (LightProxy->GetLightingChannelMask() & InSceneProxy->GetLightingChannelMask()) != 0)
			{
				MovablePointLightUniformBuffer[NumMovablePointLights] = LightProxy->GetMobileMovablePointLightUniformBufferRHI();

				NumMovablePointLights++;
			}
		}
	}
}

extern void SetupFogUniformParameters(FRDGBuilder* GraphBuilder, const FViewInfo& View, FFogUniformParameters& OutParameters);

void SetupMobileBasePassUniformParameters(
	FRHICommandListImmediate& RHICmdList, 
	const FViewInfo& View, 
	bool bTranslucentPass, 
	bool bCanUseCSM,
	FMobileBasePassUniformParameters& BasePassParameters)
{
	SetupFogUniformParameters(nullptr, View, BasePassParameters.Fog);

	const FScene* Scene = View.Family->Scene ? View.Family->Scene->GetRenderScene() : nullptr;
	const FPlanarReflectionSceneProxy* ReflectionSceneProxy = Scene ? Scene->GetForwardPassGlobalPlanarReflection() : nullptr;
	SetupPlanarReflectionUniformParameters(View, ReflectionSceneProxy, BasePassParameters.PlanarReflection);
	BasePassParameters.UseCSM = bCanUseCSM ? 1 : 0;

	EMobileSceneTextureSetupMode SetupMode = EMobileSceneTextureSetupMode::None;
	if (bTranslucentPass)
	{
		SetupMode |= EMobileSceneTextureSetupMode::SceneColor;
	}
	if (View.bCustomDepthStencilValid)
	{
		SetupMode |= EMobileSceneTextureSetupMode::CustomDepth;
	}

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	SetupMobileSceneTextureUniformParameters(SceneContext, SetupMode, BasePassParameters.SceneTextures);

	BasePassParameters.PreIntegratedGFTexture = GSystemTextures.PreintegratedGF->GetRenderTargetItem().ShaderResourceTexture;
	BasePassParameters.PreIntegratedGFSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	if (GPixelProjectedReflectionMobileOutputs.IsValid())
	{
		if (bTranslucentPass)
		{
			BasePassParameters.PlanarReflection.PlanarReflectionTexture = GPixelProjectedReflectionMobileOutputs.PixelProjectedReflectionTexture->GetRenderTargetItem().ShaderResourceTexture;
			if (GetMobilePixelProjectedReflectionQuality() <= EMobilePixelProjectedReflectionQuality::BestPerformance)
			{
				// We only render the meshes used for pixel projected reflection once and it could cause color bleeding artifact if we use bilinear filter.
				BasePassParameters.PlanarReflection.PlanarReflectionSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			}
			else
			{
				// We render the meshes used for pixel projected reflection twice, so we could use bilinear filter.
				BasePassParameters.PlanarReflection.PlanarReflectionSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			}
		}
		else
		{
			// Clear the ReflectionPlane to skip planar reflection on opaque mesh when the PPR is enabled because we render the reflection meshes used for pixel projected reflection in the translucent pass.
			BasePassParameters.PlanarReflection.ReflectionPlane.Set(0.0f, 0.0f, 0.0f, 0.0f);
		}
	}

	BasePassParameters.EyeAdaptationBuffer = GetEyeAdaptationBuffer(View);

	if (!bTranslucentPass && GAmbientOcclusionMobileOutputs.IsValid() && IsUsingMobileAmbientOcclusion(View.GetShaderPlatform()))
	{
		BasePassParameters.AmbientOcclusionTexture = GAmbientOcclusionMobileOutputs.AmbientOcclusionTexture->GetRenderTargetItem().ShaderResourceTexture;
	}
	else
	{
		BasePassParameters.AmbientOcclusionTexture = GSystemTextures.WhiteDummy->GetRenderTargetItem().ShaderResourceTexture;
	}
	BasePassParameters.AmbientOcclusionSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	BasePassParameters.AmbientOcclusionStaticFraction = FMath::Clamp(View.FinalPostProcessSettings.AmbientOcclusionStaticFraction, 0.0f, 1.0f);

	bool bRequiresDistanceFieldShadowingPass = IsMobileDistanceFieldShadowingEnabled(View.GetShaderPlatform());
	if (bRequiresDistanceFieldShadowingPass && GScreenSpaceShadowMaskTextureMobileOutputs.ScreenSpaceShadowMaskTextureMobile.IsValid())
	{
		BasePassParameters.ScreenSpaceShadowMaskTexture = GScreenSpaceShadowMaskTextureMobileOutputs.ScreenSpaceShadowMaskTextureMobile->GetRenderTargetItem().ShaderResourceTexture;
		BasePassParameters.ScreenSpaceShadowMaskSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	}
	else
	{
		BasePassParameters.ScreenSpaceShadowMaskTexture = GSystemTextures.WhiteDummy->GetRenderTargetItem().ShaderResourceTexture;
		BasePassParameters.ScreenSpaceShadowMaskSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	}
}

void CreateMobileBasePassUniformBuffer(
	FRHICommandListImmediate& RHICmdList, 
	const FViewInfo& View,
	bool bTranslucentPass,
	bool bCanUseCSM,
	TUniformBufferRef<FMobileBasePassUniformParameters>& BasePassUniformBuffer)
{
	FMobileBasePassUniformParameters BasePassParameters;
	SetupMobileBasePassUniformParameters(RHICmdList, View, bTranslucentPass, bCanUseCSM, BasePassParameters);
	BasePassUniformBuffer = TUniformBufferRef<FMobileBasePassUniformParameters>::CreateUniformBufferImmediate(BasePassParameters, UniformBuffer_SingleFrame);
}

void SetupMobileDirectionalLightUniformParameters(
	const FScene& Scene,
	const FViewInfo& SceneView,
	const TArray<FVisibleLightInfo,SceneRenderingAllocator> VisibleLightInfos,
	int32 ChannelIdx,
	bool bDynamicShadows,
	FMobileDirectionalLightShaderParameters& Params)
{
	ERHIFeatureLevel::Type FeatureLevel = Scene.GetFeatureLevel();
	FLightSceneInfo* Light = Scene.MobileDirectionalLights[ChannelIdx];
	if (Light)
	{
		Params.DirectionalLightColor = Light->Proxy->GetColor() / PI;
		if (Light->Proxy->IsUsedAsAtmosphereSunLight())
		{
			Params.DirectionalLightColor *= Light->Proxy->GetTransmittanceFactor();
		}
		Params.DirectionalLightDirectionAndShadowTransition = FVector4(-Light->Proxy->GetDirection(), 0.f);

		const FVector2D FadeParams = Light->Proxy->GetDirectionalLightDistanceFadeParameters(FeatureLevel, Light->IsPrecomputedLightingValid(), SceneView.MaxShadowCascades);
		Params.DirectionalLightDistanceFadeMADAndSpecularScale.X = FadeParams.Y;
		Params.DirectionalLightDistanceFadeMADAndSpecularScale.Y = -FadeParams.X * FadeParams.Y;
		Params.DirectionalLightDistanceFadeMADAndSpecularScale.Z = Light->Proxy->GetSpecularScale();

		if (bDynamicShadows && VisibleLightInfos.IsValidIndex(Light->Id) && VisibleLightInfos[Light->Id].AllProjectedShadows.Num() > 0)
		{
			const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& DirectionalLightShadowInfos = VisibleLightInfos[Light->Id].AllProjectedShadows;
			static_assert(MAX_MOBILE_SHADOWCASCADES <= 4, "more than 4 cascades not supported by the shader and uniform buffer");

			const int32 NumShadowsToCopy = FMath::Min(DirectionalLightShadowInfos.Num(), MAX_MOBILE_SHADOWCASCADES);
			int32_t OutShadowIndex = 0;
			for (int32 i = 0; i < NumShadowsToCopy; ++i)
			{
				const FProjectedShadowInfo* ShadowInfo = DirectionalLightShadowInfos[i];

				if (ShadowInfo->ShadowDepthView && !ShadowInfo->bRayTracedDistanceField)
				{
					if (OutShadowIndex == 0)
					{
						const FIntPoint ShadowBufferResolution = ShadowInfo->GetShadowBufferResolution();
						const FVector4 ShadowBufferSizeValue((float)ShadowBufferResolution.X, (float)ShadowBufferResolution.Y, 1.0f / (float)ShadowBufferResolution.X, 1.0f / (float)ShadowBufferResolution.Y);

						Params.DirectionalLightShadowTexture = ShadowInfo->RenderTargets.DepthTarget->GetRenderTargetItem().ShaderResourceTexture.GetReference();
						Params.DirectionalLightDirectionAndShadowTransition.W = 1.0f / ShadowInfo->ComputeTransitionSize();
						Params.DirectionalLightShadowSize = ShadowBufferSizeValue;
					}
					Params.DirectionalLightScreenToShadow[OutShadowIndex] = ShadowInfo->GetScreenToShadowMatrix(SceneView);
					Params.DirectionalLightShadowDistances[OutShadowIndex] = ShadowInfo->CascadeSettings.SplitFar;
					OutShadowIndex++;
				}
			}
		}
	}
	
	if (SceneView.MobileMovableSpotLightsShadowInfo.ShadowDepthTexture != nullptr)
	{
		checkSlow(Params.DirectionalLightShadowTexture == SceneView.MobileMovableSpotLightsShadowInfo.ShadowDepthTexture || Params.DirectionalLightShadowTexture == GWhiteTexture->TextureRHI);

		Params.DirectionalLightShadowSize = SceneView.MobileMovableSpotLightsShadowInfo.ShadowBufferSize;
		Params.DirectionalLightShadowTexture = SceneView.MobileMovableSpotLightsShadowInfo.ShadowDepthTexture;
	}
}

void SetupMobileSkyReflectionUniformParameters(FSkyLightSceneProxy* SkyLight, FMobileReflectionCaptureShaderParameters& Parameters)
{
	float Brightness = 0.f;
	float SkyMaxMipIndex = 0.f;
	FTexture* CaptureTexture = GBlackTextureCube;

	if (SkyLight && SkyLight->ProcessedTexture)
	{
		check(SkyLight->ProcessedTexture->IsInitialized());
		CaptureTexture = SkyLight->ProcessedTexture;
		SkyMaxMipIndex = FMath::Log2(CaptureTexture->GetSizeX());
		Brightness = SkyLight->AverageBrightness;
	}
	
	//To keep ImageBasedReflectionLighting coherence with PC, use AverageBrightness instead of InvAverageBrightness to calculate the IBL contribution
	Parameters.Params = FVector4(Brightness, SkyMaxMipIndex, 0.f, 0.f);
	Parameters.Texture = CaptureTexture->TextureRHI;
	Parameters.TextureSampler = CaptureTexture->SamplerStateRHI;
}

void FMobileSceneRenderer::RenderMobileBasePass(FRHICommandListImmediate& RHICmdList, const TArrayView<const FViewInfo*> PassViews)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderBasePass);
	SCOPED_DRAW_EVENT(RHICmdList, MobileBasePass);
	SCOPE_CYCLE_COUNTER(STAT_BasePassDrawTime);
	SCOPED_GPU_STAT(RHICmdList, Basepass);

	for (int32 ViewIndex = 0; ViewIndex < PassViews.Num(); ViewIndex++)
	{
		SCOPED_CONDITIONAL_DRAW_EVENTF(RHICmdList, EventView, Views.Num() > 1, TEXT("View%d"), ViewIndex);
		const FViewInfo& View = *PassViews[ViewIndex];
		if (!View.ShouldRenderView())
		{
			continue;
		}

		if (Scene->UniformBuffers.UpdateViewUniformBuffer(View))
		{
			UpdateOpaqueBasePassUniformBuffer(RHICmdList, View);
			UpdateDirectionalLightUniformBuffers(RHICmdList, View);
		}
		
		RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1);
		View.ParallelMeshDrawCommandPasses[EMeshPass::BasePass].DispatchDraw(nullptr, RHICmdList);
		
		if (View.Family->EngineShowFlags.Atmosphere)
		{
			View.ParallelMeshDrawCommandPasses[EMeshPass::SkyPass].DispatchDraw(nullptr, RHICmdList);
		}

		// editor primitives
		{
			FMeshPassProcessorRenderState DrawRenderState(View, Scene->UniformBuffers.MobileOpaqueBasePassUniformBuffer);
			DrawRenderState.SetBlendState(TStaticBlendStateWriteMask<CW_RGBA>::GetRHI());
			DrawRenderState.SetDepthStencilAccess(Scene->DefaultBasePassDepthStencilAccess);
			DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());
			RenderMobileEditorPrimitives(RHICmdList, View, DrawRenderState);
		}
	}
}

void FMobileSceneRenderer::RenderMobileEditorPrimitives(FRHICommandList& RHICmdList, const FViewInfo& View, const FMeshPassProcessorRenderState& DrawRenderState)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_EditorDynamicPrimitiveDrawTime);
	SCOPED_DRAW_EVENT(RHICmdList, DynamicEd);

	View.SimpleElementCollector.DrawBatchedElements(RHICmdList, DrawRenderState, View, EBlendModeFilter::OpaqueAndMasked, SDPG_World);
	View.SimpleElementCollector.DrawBatchedElements(RHICmdList, DrawRenderState, View, EBlendModeFilter::OpaqueAndMasked, SDPG_Foreground);

	if (!View.Family->EngineShowFlags.CompositeEditorPrimitives)
	{
		const bool bNeedToSwitchVerticalAxis = RHINeedsToSwitchVerticalAxis(GShaderPlatformForFeatureLevel[FeatureLevel]) && !IsMobileHDR();

		DrawDynamicMeshPass(View, RHICmdList,
			[&View, &DrawRenderState](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
			{
				FEditorPrimitivesBasePassMeshProcessor PassMeshProcessor(
					View.Family->Scene->GetRenderScene(),
					View.GetFeatureLevel(),
					&View,
					DrawRenderState,
					false,
					DynamicMeshPassContext);

				const uint64 DefaultBatchElementMask = ~0ull;
					
				for (int32 MeshIndex = 0; MeshIndex < View.ViewMeshElements.Num(); MeshIndex++)
				{
					const FMeshBatch& MeshBatch = View.ViewMeshElements[MeshIndex];
					PassMeshProcessor.AddMeshBatch(MeshBatch, DefaultBatchElementMask, nullptr);
				}
			});

		// Draw the view's batched simple elements(lines, sprites, etc).
		View.BatchedViewElements.Draw(RHICmdList, DrawRenderState, FeatureLevel, bNeedToSwitchVerticalAxis, View, false);

		DrawDynamicMeshPass(View, RHICmdList,
			[&View, &DrawRenderState](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
		{
			FEditorPrimitivesBasePassMeshProcessor PassMeshProcessor(
				View.Family->Scene->GetRenderScene(),
				View.GetFeatureLevel(),
				&View,
				DrawRenderState,
				false,
				DynamicMeshPassContext);

			const uint64 DefaultBatchElementMask = ~0ull;

			for (int32 MeshIndex = 0; MeshIndex < View.TopViewMeshElements.Num(); MeshIndex++)
			{
				const FMeshBatch& MeshBatch = View.TopViewMeshElements[MeshIndex];
				PassMeshProcessor.AddMeshBatch(MeshBatch, DefaultBatchElementMask, nullptr);
			}
		});

		// Draw the view's batched simple elements(lines, sprites, etc).
		View.TopBatchedViewElements.Draw(RHICmdList, DrawRenderState, FeatureLevel, bNeedToSwitchVerticalAxis, View, false);
	}
}
