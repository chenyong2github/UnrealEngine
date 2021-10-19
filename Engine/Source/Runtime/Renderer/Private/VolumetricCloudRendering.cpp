// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VolumetricCloudRendering.cpp
=============================================================================*/

#include "VolumetricCloudRendering.h"
#include "Components/VolumetricCloudComponent.h"
#include "VolumetricCloudProxy.h"
#include "DeferredShadingRenderer.h"
#include "PixelShaderUtils.h"
#include "RenderCore/Public/RenderGraphUtils.h"
#include "ScenePrivate.h"
#include "MeshPassProcessor.inl"
#include "StaticMeshResources.h"
#include "SkyAtmosphereRendering.h"
#include "VolumeLighting.h"
#include "DynamicPrimitiveDrawing.h"
#include "GpuDebugRendering.h"
#include "CanvasTypes.h"
#include "RenderTargetTemp.h"
#include "VolumetricRenderTarget.h"
#include "BlueNoise.h"
#include "FogRendering.h"
#include "SkyAtmosphereRendering.h"


//PRAGMA_DISABLE_OPTIMIZATION


////////////////////////////////////////////////////////////////////////// Cloud rendering and tracing

// The runtime ON/OFF toggle
static TAutoConsoleVariable<int32> CVarVolumetricCloud(
	TEXT("r.VolumetricCloud"), 1,
	TEXT("VolumetricCloud components are rendered when this is not 0, otherwise ignored."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarVolumetricCloudDistanceToSampleMaxCount(
	TEXT("r.VolumetricCloud.DistanceToSampleMaxCount"), 15.0f,
	TEXT("Distance in kilometers over which the total number of ray samples will be evenly distributed. Before that, the number of ray samples will span 1 to SampleCountMax, for for tracing distance ranging from 0 to DistanceToSampleCountMax (kilometers)."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarVolumetricCloudSampleMinCount(
	TEXT("r.VolumetricCloud.SampleMinCount"), 2,
	TEXT("The minimum number of samples to take along a ray. This can help with quality for volume close to the camera, e.g. if cloud layer is also used as low altitude fog. SampleMinCount should remain relatively small because it is applied to all tracing process."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarVolumetricCloudViewRaySampleMaxCount(
	TEXT("r.VolumetricCloud.ViewRaySampleMaxCount"), 768,
	TEXT("The maximum number of samples taken while ray marching view primary rays."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarVolumetricCloudReflectionRaySampleMaxCount(
	TEXT("r.VolumetricCloud.ReflectionRaySampleMaxCount"), 80,
	TEXT("The maximum number of samples taken while ray marching primary rays in reflections."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarVolumetricCloudStepSizeOnZeroConservativeDensity(
	TEXT("r.VolumetricCloud.StepSizeOnZeroConservativeDensity"), 1,
	TEXT("Raymarch step size when a sample giving zero conservative density is encountered. If > 1, performance will likely improve but banding artifacts can show up if too large."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarVolumetricCloudHighQualityAerialPerspective(
	TEXT("r.VolumetricCloud.HighQualityAerialPerspective"), 0,
	TEXT("Enable/disable a second pass to trace the aerial perspective per pixel on clouds instead of using the aerial persepctive texture. Only usable when r.VolumetricCloud.EnableAerialPerspectiveSampling=1 and only needed for extra quality when r.VolumetricRenderTarget=1."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarVolumetricCloudHzbCulling(
	TEXT("r.VolumetricCloud.HzbCulling"), 1,
	TEXT("Enable/disable the use of the HZB in order to not trace behind opaque surfaces. Should be disabled when r.VolumetricRenderTarget.Mode is 2."),
	ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarVolumetricCloudDisableCompute(
	TEXT("r.VolumetricCloud.DisableCompute"), 0,
	TEXT("Do not use compute shader for cloud tracing."),
	ECVF_Scalability);

////////////////////////////////////////////////////////////////////////// Shadow tracing

static TAutoConsoleVariable<float> CVarVolumetricCloudShadowViewRaySampleMaxCount(
	TEXT("r.VolumetricCloud.Shadow.ViewRaySampleMaxCount"), 80,
	TEXT("The maximum number of samples taken while ray marching shadow rays."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarVolumetricCloudShadowReflectionRaySampleMaxCount(
	TEXT("r.VolumetricCloud.Shadow.ReflectionRaySampleMaxCount"), 24,
	TEXT("The maximum number of samples taken while ray marching shadow rays in reflections."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarVolumetricCloudShadowSampleAtmosphericLightShadowmap(
	TEXT("r.VolumetricCloud.Shadow.SampleAtmosphericLightShadowmap"), 1,
	TEXT("Enable the sampling of atmospheric lights shadow map in order to produce volumetric shadows."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

////////////////////////////////////////////////////////////////////////// Cloud SKY AO

static TAutoConsoleVariable<int32> CVarVolumetricCloudSkyAO(
	TEXT("r.VolumetricCloud.SkyAO"), 1,
	TEXT("Enable/disable cloud sky ambient occlusion, the scene must contain a Skylight component with Cloud Ambient Occlusion enabled on it."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarVolumetricCloudSkyAODebug(
	TEXT("r.VolumetricCloud.SkyAO.Debug"), 0,
	TEXT("Print information to debug the cloud sky AO map."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarVolumetricCloudSkyAOSnapLength(
	TEXT("r.VolumetricCloud.SkyAO.SnapLength"), 20.0f,
	TEXT("Snapping size in kilometers of the cloud sky AO texture position to avoid flickering."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarVolumetricCloudSkyAOMaxResolution(
	TEXT("r.VolumetricCloud.SkyAO.MaxResolution"), 2048,
	TEXT("The maximum resolution of the texture storing ambient occlusion information for the environment lighting coming from sky light. The active resolution is controlled by the CloudAmbientOcclusionMapResolutionScale property on the Skylight component."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarVolumetricCloudSkyAOTraceSampleCount(
	TEXT("r.VolumetricCloud.SkyAO.TraceSampleCount"), 10,
	TEXT("The number of samples taken to evaluate ground lighting occlusion."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarVolumetricCloudSkyAOFiltering(
	TEXT("r.VolumetricCloud.SkyAO.Filtering"), 1,
	TEXT("Enable/disable the sky AO dilation/smoothing filter."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

////////////////////////////////////////////////////////////////////////// Cloud shadow map

static TAutoConsoleVariable<int32> CVarVolumetricCloudShadowMap(
	TEXT("r.VolumetricCloud.ShadowMap"), 1,
	TEXT("Enable/disable the shadow map, only if the scene contains a DirectionalLight component with Cast Cloud Shadows enabled on it."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarVolumetricCloudShadowMapDebug(
	TEXT("r.VolumetricCloud.ShadowMap.Debug"), 0,
	TEXT("Print information to debug the cloud shadow map."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarVolumetricCloudShadowMapSnapLength(
	TEXT("r.VolumetricCloud.ShadowMap.SnapLength"), 20.0f,
	TEXT("Snapping size in kilometers of the cloud shadowmap position to avoid flickering."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarVolumetricCloudShadowMapRaySampleMaxCount(
	TEXT("r.VolumetricCloud.ShadowMap.RaySampleMaxCount"), 128.0f,
	TEXT("The maximum number of samples taken while ray marching shadow rays to evaluate the cloud shadow map."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarVolumetricCloudShadowMapRaySampleHorizonMultiplier(
	TEXT("r.VolumetricCloud.ShadowMap.RaySampleHorizonMultiplier"), 2.0f,
	TEXT("The multipler on the sample count applied when the atmospheric light reach the horizon. Less pixels in the shadow map need to be traced, but rays need to travel a lot longer."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarVolumetricCloudShadowMapMaxResolution(
	TEXT("r.VolumetricCloud.ShadowMap.MaxResolution"), 2048,
	TEXT("The maximum resolution of the cloud shadow map. The active resolution is controlled by the CloudShadowMapResolutionScale property on the Directional Light component."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarVolumetricCloudShadowSpatialFiltering(
	TEXT("r.VolumetricCloud.ShadowMap.SpatialFiltering"), 1,
	TEXT("Enable/disable the shadow map dilation/smoothing spatial filter. Enabled when greater than 0 and it represents the number of blur iterations (constrained to a maximum of 4)."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarVolumetricCloudShadowTemporalFilteringNewFrameWeight(
	TEXT("r.VolumetricCloud.ShadowMap.TemporalFiltering.NewFrameWeight"), 1.0f,
	TEXT("Experimental and needs more work so disabled by default. Value between [0.0, 1.0] representing the weight of current frame's contribution. Low values can cause precision issues resulting in depth not converging over time. Disabled when set to 1."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarVolumetricCloudShadowTemporalFilteringLightRotationCutHistory(
	TEXT("r.VolumetricCloud.ShadowMap.TemporalFiltering.LightRotationCutHistory"), 10.0f,
	TEXT("When the atmospheric light rotation in degree is larger than that, the temporal accumulation is restarted."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

////////////////////////////////////////////////////////////////////////// Lighting component controls

static TAutoConsoleVariable<int32> CVarVolumetricCloudEnableAerialPerspectiveSampling(
	TEXT("r.VolumetricCloud.EnableAerialPerspectiveSampling"), 1,
	TEXT("Enable/disable the aerial perspective contribution on clouds."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarVolumetricCloudEnableDistantSkyLightSampling(
	TEXT("r.VolumetricCloud.EnableDistantSkyLightSampling"), 1,
	TEXT("Enable/disable the distant sky light contribution on clouds."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarVolumetricCloudEnableAtmosphericLightsSampling(
	TEXT("r.VolumetricCloud.EnableAtmosphericLightsSampling"), 1,
	TEXT("Enable/disable the atmospheric lights contribution on clouds."),
	ECVF_RenderThreadSafe);

////////////////////////////////////////////////////////////////////////// 

static TAutoConsoleVariable<int32> CVarVolumetricCloudDebugSampleCountMode(
	TEXT("r.VolumetricCloud.Debug.SampleCountMode"), 0,
	TEXT("Only for developers. [0] Disabled [1] Primary material sample count [2] Advanced:raymarched shadow sample count [3] Shadow material sample count [4] Advanced:ground shadow sample count [5] Advanced:ground shadow material sample count"));
////////////////////////////////////////////////////////////////////////// 


static bool ShouldPipelineCompileVolumetricCloudShader(EShaderPlatform ShaderPlatform)
{
	// Requires SM5 or ES3_1 (GL/Vulkan) for compute shaders and volume textures support.
	return RHISupportsComputeShaders(ShaderPlatform);
}

static bool ShouldUseComputeForCloudTracing(const FStaticFeatureLevel InFeatureLevel)
{
	// When capturing a non-real-time sky light, the reflection cube face is first rendered in the scene texture 
	// before it is copied to the corresponding cubemap face. 
	// We cannot create UAV on a MSAA texture for the compute shader.
	// So in this case, when capturing such a sky light, we must disabled the compute path.
	const bool bMSAAEnabled = FSceneRenderTargets::GetNumSceneColorMSAASamples(InFeatureLevel) > 1;

	return !CVarVolumetricCloudDisableCompute.GetValueOnRenderThread() && GDynamicRHI->RHIIsTypedUAVLoadSupported(PF_FloatRGBA) && GDynamicRHI->RHIIsTypedUAVLoadSupported(PF_G16R16F) && !bMSAAEnabled;
}

bool ShouldRenderVolumetricCloud(const FScene* Scene, const FEngineShowFlags& EngineShowFlags)
{
	if (Scene && Scene->HasVolumetricCloud() && EngineShowFlags.Atmosphere)
	{
		const FVolumetricCloudRenderSceneInfo* VolumetricCloud = Scene->GetVolumetricCloudSceneInfo();
		check(VolumetricCloud);

		const bool bShadersCompiled = ShouldPipelineCompileVolumetricCloudShader(Scene->GetShaderPlatform());
		return bShadersCompiled && CVarVolumetricCloud.GetValueOnRenderThread() > 0 && VolumetricCloud->GetVolumetricCloudSceneProxy().GetCloudVolumeMaterial()!=nullptr;
	}
	return false;
}

bool ShouldViewVisualizeVolumetricCloudConservativeDensity(const FViewInfo& ViewInfo, const FEngineShowFlags& EngineShowFlags)
{
	return !!EngineShowFlags.VisualizeVolumetricCloudConservativeDensity
		&& !ViewInfo.bIsReflectionCapture
		&& !ViewInfo.bIsSceneCapture
		&& GIsEditor;
}

static bool ShouldRenderCloudShadowmap(const FLightSceneProxy* AtmosphericLight)
{
	return CVarVolumetricCloudShadowMap.GetValueOnRenderThread() > 0 && AtmosphericLight && AtmosphericLight->GetCastCloudShadows();
}

//////////////////////////////////////////////////////////////////////////

static float GetVolumetricCloudShadowmapStrength(const FLightSceneProxy* AtmosphericLight)
{
	if (AtmosphericLight)
	{
		return AtmosphericLight->GetCloudShadowStrength();
	}
	return 1.0f;
}

static float GetVolumetricCloudShadowmapDepthBias(const FLightSceneProxy* AtmosphericLight)
{
	if (AtmosphericLight)
	{
		return AtmosphericLight->GetCloudShadowDepthBias();
	}
	return 0.0f;
}

static int32 GetVolumetricCloudShadowMapResolution(const FLightSceneProxy* AtmosphericLight)
{
	if (AtmosphericLight)
	{
		return FMath::Min( int32(512.0f * float(AtmosphericLight->GetCloudShadowMapResolutionScale())), CVarVolumetricCloudShadowMapMaxResolution.GetValueOnAnyThread());
	}
	return 32;
}

static int32 GetVolumetricCloudShadowRaySampleCountScale(const FLightSceneProxy* AtmosphericLight)
{
	if (AtmosphericLight)
	{
		return AtmosphericLight->GetCloudShadowRaySampleCountScale();
	}
	return 4;
}

static float GetVolumetricCloudShadowMapExtentKm(const FLightSceneProxy* AtmosphericLight)
{
	if (AtmosphericLight)
	{
		return AtmosphericLight->GetCloudShadowExtent();
	}
	return 1.0f;
}

static bool GetVolumetricCloudReceiveAtmosphericLightShadowmap(const FLightSceneProxy* AtmosphericLight)
{
	if (AtmosphericLight)
	{
		return AtmosphericLight->GetCastShadowsOnClouds();
	}
	return false;
}

static FLinearColor GetVolumetricCloudScatteredLuminanceScale(const FLightSceneProxy* AtmosphericLight)
{
	if (AtmosphericLight)
	{
		return AtmosphericLight->GetCloudScatteredLuminanceScale();
	}
	return FLinearColor::White;
}

static bool ShouldRenderCloudSkyAO(const FSkyLightSceneProxy* SkyLight)
{
	return CVarVolumetricCloudSkyAO.GetValueOnRenderThread() > 0 && SkyLight && SkyLight->bCloudAmbientOcclusion;
}

static float GetVolumetricCloudSkyAOStrength(const FSkyLightSceneProxy* SkyLight)
{
	if (SkyLight)
	{
		return SkyLight->CloudAmbientOcclusionStrength;
	}
	return 1.0f;
}

static int32 GetVolumetricCloudSkyAOResolution(const FSkyLightSceneProxy* SkyLight)
{
	if (SkyLight)
	{
		return FMath::Min(int32(512.0f * float(SkyLight->CloudAmbientOcclusionMapResolutionScale)), CVarVolumetricCloudSkyAOMaxResolution.GetValueOnAnyThread());
	}
	return 32;
}

static float GetVolumetricCloudSkyAOExtentKm(const FSkyLightSceneProxy* SkyLight)
{
	if (SkyLight)
	{
		return SkyLight->CloudAmbientOcclusionExtent;
	}
	return 1.0f;
}

static float GetVolumetricCloudSkyAOApertureScale(const FSkyLightSceneProxy* SkyLight)
{
	if (SkyLight)
	{
		return SkyLight->CloudAmbientOcclusionApertureScale;
	}
	return 1.0f;
}

static bool ShouldUsePerSampleAtmosphereTransmittance(const FScene* Scene, const FViewInfo* InViewIfDynamicMeshCommand)
{
	return Scene->VolumetricCloud && Scene->VolumetricCloud->GetVolumetricCloudSceneProxy().bUsePerSampleAtmosphericLightTransmittance &&
		Scene->HasSkyAtmosphere() && ShouldRenderSkyAtmosphere(Scene, InViewIfDynamicMeshCommand->Family->EngineShowFlags);
}


//////////////////////////////////////////////////////////////////////////


void GetCloudShadowAOData(FVolumetricCloudRenderSceneInfo* CloudInfo, FViewInfo& View, FRDGBuilder& GraphBuilder, FCloudShadowAOData& OutData)
{
	// We pick up the texture if they exists, the decision has been mande to render them before already.
	const bool VolumetricCloudShadowMap0Valid = View.VolumetricCloudShadowRenderTarget[0].IsValid();
	const bool VolumetricCloudShadowMap1Valid = View.VolumetricCloudShadowRenderTarget[1].IsValid();
	OutData.bShouldSampleCloudShadow = CloudInfo && (VolumetricCloudShadowMap0Valid || VolumetricCloudShadowMap1Valid);
	OutData.VolumetricCloudShadowMap[0] = OutData.bShouldSampleCloudShadow && VolumetricCloudShadowMap0Valid ? GraphBuilder.RegisterExternalTexture(View.VolumetricCloudShadowRenderTarget[0]) : GSystemTextures.GetBlackDummy(GraphBuilder);
	OutData.VolumetricCloudShadowMap[1] = OutData.bShouldSampleCloudShadow && VolumetricCloudShadowMap1Valid ? GraphBuilder.RegisterExternalTexture(View.VolumetricCloudShadowRenderTarget[1]) : GSystemTextures.GetBlackDummy(GraphBuilder);

	OutData.bShouldSampleCloudSkyAO = CloudInfo && View.VolumetricCloudSkyAO.IsValid();
	OutData.VolumetricCloudSkyAO = OutData.bShouldSampleCloudSkyAO ? GraphBuilder.RegisterExternalTexture(View.VolumetricCloudSkyAO) : GSystemTextures.GetBlackDummy(GraphBuilder);

	// We also force disable shadows if the VolumetricCloud component itself has not initialised due to extra reasons in ShouldRenderVolumetricCloud().
	// The below test is a simple way to check the fact that if ShouldRenderVolumetricCloud is false, then InitVolumetricCloudsForViews is not run
	// and in this case the GetVolumetricCloudCommonShaderParametersUB has not been created, preventing the rendering of cloud shadow on SkyAtmosphere.
	const bool bCloudComponentValid = CloudInfo && CloudInfo->GetVolumetricCloudCommonShaderParametersUB().IsValid();
	OutData.bShouldSampleCloudShadow = OutData.bShouldSampleCloudShadow && bCloudComponentValid;
	OutData.bShouldSampleCloudSkyAO  = OutData.bShouldSampleCloudSkyAO  && bCloudComponentValid;
}


/*=============================================================================
	FVolumetricCloudRenderSceneInfo implementation.
=============================================================================*/


FVolumetricCloudRenderSceneInfo::FVolumetricCloudRenderSceneInfo(FVolumetricCloudSceneProxy& VolumetricCloudSceneProxyIn)
	:VolumetricCloudSceneProxy(VolumetricCloudSceneProxyIn)
{
}

FVolumetricCloudRenderSceneInfo::~FVolumetricCloudRenderSceneInfo()
{
}



/*=============================================================================
	FScene functions
=============================================================================*/



void FScene::AddVolumetricCloud(FVolumetricCloudSceneProxy* VolumetricCloudSceneProxy)
{
	check(VolumetricCloudSceneProxy);
	FScene* Scene = this;

	ENQUEUE_RENDER_COMMAND(FAddVolumetricCloudCommand)(
		[Scene, VolumetricCloudSceneProxy](FRHICommandListImmediate& RHICmdList)
		{
			check(!Scene->VolumetricCloudStack.Contains(VolumetricCloudSceneProxy));
			Scene->VolumetricCloudStack.Push(VolumetricCloudSceneProxy);

			VolumetricCloudSceneProxy->RenderSceneInfo = new FVolumetricCloudRenderSceneInfo(*VolumetricCloudSceneProxy);

			// Use the most recently enabled VolumetricCloud
			Scene->VolumetricCloud = VolumetricCloudSceneProxy->RenderSceneInfo;
		} );
}

void FScene::RemoveVolumetricCloud(FVolumetricCloudSceneProxy* VolumetricCloudSceneProxy)
{
	check(VolumetricCloudSceneProxy);
	FScene* Scene = this;

	ENQUEUE_RENDER_COMMAND(FRemoveVolumetricCloudCommand)(
		[Scene, VolumetricCloudSceneProxy](FRHICommandListImmediate& RHICmdList)
		{
			delete VolumetricCloudSceneProxy->RenderSceneInfo;
			Scene->VolumetricCloudStack.RemoveSingle(VolumetricCloudSceneProxy);

			if (Scene->VolumetricCloudStack.Num() > 0)
			{
				// Use the most recently enabled VolumetricCloud
				Scene->VolumetricCloud = Scene->VolumetricCloudStack.Last()->RenderSceneInfo;
			}
			else
			{
				Scene->VolumetricCloud = nullptr;
			}
		} );
}



/*=============================================================================
	VolumetricCloud rendering functions
=============================================================================*/



DECLARE_GPU_STAT(VolumetricCloud);
DECLARE_GPU_STAT(VolumetricCloudShadow);



FORCEINLINE bool IsVolumetricCloudMaterialSupported(const EShaderPlatform Platform)
{
	return GetMaxSupportedFeatureLevel(Platform) >= ERHIFeatureLevel::SM5;
}


FORCEINLINE bool IsMaterialCompatibleWithVolumetricCloud(const FMaterialShaderParameters& Material, const EShaderPlatform Platform)
{
	return IsVolumetricCloudMaterialSupported(Platform) && Material.MaterialDomain == MD_Volume;
}



//////////////////////////////////////////////////////////////////////////

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FRenderVolumetricCloudGlobalParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FVolumetricCloudCommonShaderParameters, VolumetricCloud)
	SHADER_PARAMETER_STRUCT(FSceneTextureUniformParameters, SceneTextures)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float3>, CloudShadowTexture0)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float3>, CloudShadowTexture1)
	SHADER_PARAMETER_SAMPLER(SamplerState, CloudBilinearTextureSampler)
	SHADER_PARAMETER_STRUCT_INCLUDE(FVolumeShadowingShaderParametersGlobal0, Light0Shadow)
//	SHADER_PARAMETER_STRUCT(FBlueNoise, BlueNoise)
	SHADER_PARAMETER(FUintVector4, TracingCoordToZbufferCoordScaleBias)
	SHADER_PARAMETER(uint32, NoiseFrameIndexModPattern)
	SHADER_PARAMETER(int32, OpaqueIntersectionMode)
	SHADER_PARAMETER(uint32, VolumetricRenderTargetMode)
	SHADER_PARAMETER(uint32, SampleCountDebugMode)
	SHADER_PARAMETER(uint32, IsReflectionRendering)
	SHADER_PARAMETER(uint32, HasValidHZB)
	SHADER_PARAMETER(uint32, ClampRayTToDepthBufferPostHZB)
	SHADER_PARAMETER(uint32, TraceShadowmap)
	SHADER_PARAMETER(FVector, HZBUvFactor)
	SHADER_PARAMETER(FVector4, HZBSize)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, HZBTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, HZBSampler)
	SHADER_PARAMETER(FVector4, OutputSizeInvSize)
	SHADER_PARAMETER(int32, StepSizeOnZeroConservativeDensity)
	SHADER_PARAMETER(int32, EnableAerialPerspectiveSampling)
	SHADER_PARAMETER(int32, EnableDistantSkyLightSampling)
	SHADER_PARAMETER(int32, EnableAtmosphericLightsSampling)
	SHADER_PARAMETER(int32, EnableHeightFog)
	SHADER_PARAMETER_STRUCT_INCLUDE(FFogUniformParameters, FogStruct)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FRenderVolumetricCloudGlobalParameters, "RenderVolumetricCloudParameters", SceneTextures);

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FVolumetricCloudCommonGlobalShaderParameters, "VolumetricCloudCommonParameters");

uint32 GetVolumetricCloudDebugSampleCountMode(const FEngineShowFlags& ShowFlags)
{
	if (ShowFlags.VisualizeVolumetricCloudConservativeDensity)
	{
		return 6;
	}
	// Add view modes for visualize other kinds of sample count
	return FMath::Clamp(CVarVolumetricCloudDebugSampleCountMode.GetValueOnAnyThread(), 0, 5);
}

// When calling this, you still need to setup Light0Shadow yourself.
void SetupDefaultRenderVolumetricCloudGlobalParameters(FRDGBuilder& GraphBuilder, FRenderVolumetricCloudGlobalParameters& VolumetricCloudParams, FVolumetricCloudRenderSceneInfo& CloudInfo, FViewInfo& ViewInfo)
{
	FRDGTextureRef BlackDummy = GSystemTextures.GetBlackDummy(GraphBuilder);
	VolumetricCloudParams.VolumetricCloud = CloudInfo.GetVolumetricCloudCommonShaderParameters();
	VolumetricCloudParams.SceneDepthTexture = BlackDummy;
	VolumetricCloudParams.CloudShadowTexture0 = BlackDummy;
	VolumetricCloudParams.CloudShadowTexture1 = BlackDummy;
	VolumetricCloudParams.CloudBilinearTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
	// Light0Shadow
/*#if RHI_RAYTRACING
	InitializeBlueNoise(VolumetricCloudParams.BlueNoise);
#else
	// Blue noise texture is undified for some configuration so replace by other noise for now.
	VolumetricCloudParams.BlueNoise.Dimensions = FIntVector(16, 16, 4); // 16 is the size of the tile, so 4 dimension for the 64x64 HighFrequencyNoiseTexture.
	VolumetricCloudParams.BlueNoise.Texture = GEngine->HighFrequencyNoiseTexture->Resource->TextureRHI;
#endif*/
	VolumetricCloudParams.TracingCoordToZbufferCoordScaleBias = FUintVector4(1, 1, 0, 0);
	VolumetricCloudParams.NoiseFrameIndexModPattern = 0;
	VolumetricCloudParams.VolumetricRenderTargetMode = ViewInfo.ViewState ? ViewInfo.ViewState->VolumetricCloudRenderTarget.GetMode() : 0;
	VolumetricCloudParams.SampleCountDebugMode = GetVolumetricCloudDebugSampleCountMode(ViewInfo.Family->EngineShowFlags);

	VolumetricCloudParams.HasValidHZB = 0;
	VolumetricCloudParams.ClampRayTToDepthBufferPostHZB = 0;
	VolumetricCloudParams.HZBTexture = BlackDummy;
	VolumetricCloudParams.HZBSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	VolumetricCloudParams.StepSizeOnZeroConservativeDensity = FMath::Max(CVarVolumetricCloudStepSizeOnZeroConservativeDensity.GetValueOnRenderThread(), 1);

	VolumetricCloudParams.EnableHeightFog = ViewInfo.Family->Scene->HasAnyExponentialHeightFog() && ShouldRenderFog(*ViewInfo.Family);
	SetupFogUniformParameters(GraphBuilder, ViewInfo, VolumetricCloudParams.FogStruct);

	ESceneTextureSetupMode SceneTextureSetupMode = ESceneTextureSetupMode::All;
	EnumRemoveFlags(SceneTextureSetupMode, ESceneTextureSetupMode::SceneColor);
	EnumRemoveFlags(SceneTextureSetupMode, ESceneTextureSetupMode::SceneVelocity);
	EnumRemoveFlags(SceneTextureSetupMode, ESceneTextureSetupMode::GBuffers);
	EnumRemoveFlags(SceneTextureSetupMode, ESceneTextureSetupMode::SSAO);
	SetupSceneTextureUniformParameters(GraphBuilder, ViewInfo.FeatureLevel, ESceneTextureSetupMode::CustomDepth, VolumetricCloudParams.SceneTextures);
}

static void SetupRenderVolumetricCloudGlobalParametersHZB(FRDGBuilder& GraphBuilder, const FViewInfo& ViewInfo, FRenderVolumetricCloudGlobalParameters& ShaderParameters)
{
	ShaderParameters.HasValidHZB = (ViewInfo.HZB.IsValid() && CVarVolumetricCloudHzbCulling.GetValueOnAnyThread() > 0) ? 1 : 0;

	ShaderParameters.HZBTexture = GraphBuilder.RegisterExternalTexture(ShaderParameters.HasValidHZB ? ViewInfo.HZB : GSystemTextures.BlackDummy);
	ShaderParameters.HZBSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	const float kHZBTestMaxMipmap = 9.0f;
	const float HZBMipmapCounts = FMath::Log2(FMath::Max(ViewInfo.HZBMipmap0Size.X, ViewInfo.HZBMipmap0Size.Y));
	const FVector HZBUvFactor(
		float(ViewInfo.ViewRect.Width()) / float(2 * ViewInfo.HZBMipmap0Size.X),
		float(ViewInfo.ViewRect.Height()) / float(2 * ViewInfo.HZBMipmap0Size.Y),
		FMath::Max(HZBMipmapCounts - kHZBTestMaxMipmap, 0.0f)
	);
	const FVector4 HZBSize(
		ViewInfo.HZBMipmap0Size.X,
		ViewInfo.HZBMipmap0Size.Y,
		1.0f / float(ViewInfo.HZBMipmap0Size.X),
		1.0f / float(ViewInfo.HZBMipmap0Size.Y)
	);
	ShaderParameters.HZBUvFactor = HZBUvFactor;
	ShaderParameters.HZBSize = HZBSize;
}

//////////////////////////////////////////////////////////////////////////

class FRenderVolumetricCloudVS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FRenderVolumetricCloudVS, MeshMaterial);

public:

	FRenderVolumetricCloudVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{}

	FRenderVolumetricCloudVS() {}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		const bool bIsCompatible = IsMaterialCompatibleWithVolumetricCloud(Parameters.MaterialParameters, Parameters.Platform);
		return bIsCompatible;
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMeshMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_MAINVS"), TEXT("1"));
	}

private:
};

IMPLEMENT_MATERIAL_SHADER_TYPE(, FRenderVolumetricCloudVS, TEXT("/Engine/Private/VolumetricCloud.usf"), TEXT("MainVS"), SF_Vertex);

//////////////////////////////////////////////////////////////////////////

enum EVolumetricCloudRenderViewPsPermutations
{
	VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance0_SampleShadow0_SecondLight0,
	VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance1_SampleShadow0_SecondLight0,
	VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance0_SampleShadow1_SecondLight0,
	VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance1_SampleShadow1_SecondLight0,
	VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance0_SampleShadow0_SecondLight1,
	VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance1_SampleShadow0_SecondLight1,
	VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance0_SampleShadow1_SecondLight1,
	VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance1_SampleShadow1_SecondLight1,
	VolumetricCloudRenderViewPsCount
};

BEGIN_SHADER_PARAMETER_STRUCT(FRenderVolumetricCloudRenderViewParametersPS, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FRenderVolumetricCloudGlobalParameters, VolumetricCloudRenderViewParamsUB)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CloudShadowTexture0)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CloudShadowTexture1)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

template<EVolumetricCloudRenderViewPsPermutations Permutation, bool bSampleCountDebugMode>
class FRenderVolumetricCloudRenderViewPs : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FRenderVolumetricCloudRenderViewPs, MeshMaterial);

public:

	FRenderVolumetricCloudRenderViewPs(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		PassUniformBuffer.Bind(Initializer.ParameterMap, FRenderVolumetricCloudGlobalParameters::StaticStructMetadata.GetShaderVariableName());
	}

	FRenderVolumetricCloudRenderViewPs() {}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		const bool bIsCompatible = IsMaterialCompatibleWithVolumetricCloud(Parameters.MaterialParameters, Parameters.Platform);
		
		if (bSampleCountDebugMode)
		{
			return bIsCompatible
				&& EnumHasAllFlags(Parameters.Flags, EShaderPermutationFlags::HasEditorOnlyData)
				&& Permutation == VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance0_SampleShadow0_SecondLight0;
		}

		return bIsCompatible;
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_RENDERVIEW_PS"), TEXT("1"));
		OutEnvironment.SetDefine(TEXT("CLOUD_LAYER_PIXEL_SHADER"), TEXT("1"));

		// Force texture fetches to not use automatic mip generation because the pixel shader is using a dynamic loop to evaluate the material multiple times.
		OutEnvironment.SetDefine(TEXT("USE_FORCE_TEXTURE_MIP"), TEXT("1"));

		const bool bUseAtmosphereTransmittance = Permutation == VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance1_SampleShadow0_SecondLight0 || Permutation == VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance1_SampleShadow1_SecondLight0 ||
			Permutation == VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance1_SampleShadow0_SecondLight1 || Permutation == VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance1_SampleShadow1_SecondLight1;
		OutEnvironment.SetDefine(TEXT("CLOUD_PER_SAMPLE_ATMOSPHERE_TRANSMITTANCE"), bUseAtmosphereTransmittance ? TEXT("1") : TEXT("0"));

		const bool bSampleLightShadowmap = Permutation == VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance0_SampleShadow1_SecondLight0 || Permutation == VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance1_SampleShadow1_SecondLight0 ||
			Permutation == VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance0_SampleShadow1_SecondLight1 || Permutation == VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance1_SampleShadow1_SecondLight1;
		OutEnvironment.SetDefine(TEXT("CLOUD_SAMPLE_ATMOSPHERIC_LIGHT_SHADOWMAP"), bSampleLightShadowmap ? TEXT("1") : TEXT("0"));

		const bool bSampleSecondLight = Permutation == VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance0_SampleShadow0_SecondLight1 || Permutation == VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance1_SampleShadow0_SecondLight1 ||
			Permutation == VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance0_SampleShadow1_SecondLight1 || Permutation == VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance1_SampleShadow1_SecondLight1;
		OutEnvironment.SetDefine(TEXT("CLOUD_SAMPLE_SECOND_LIGHT"), bSampleSecondLight ? TEXT("1") : TEXT("0"));

		OutEnvironment.SetDefine(TEXT("CLOUD_SAMPLE_COUNT_DEBUG_MODE"), bSampleCountDebugMode);
	}

private:
};

#define IMPLEMENT_CLOUD_RENDERVIEW_PS(A, B, C, D) \
	typedef FRenderVolumetricCloudRenderViewPs<VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance##A##_SampleShadow##B##_SecondLight##C, D> FCloudRenderViewPS##A##B##C##D; \
	IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FCloudRenderViewPS##A##B##C##D, TEXT("/Engine/Private/VolumetricCloud.usf"), TEXT("MainPS"), SF_Pixel)

IMPLEMENT_CLOUD_RENDERVIEW_PS(0, 0, 0, false);
IMPLEMENT_CLOUD_RENDERVIEW_PS(1, 0, 0, false);
IMPLEMENT_CLOUD_RENDERVIEW_PS(0, 1, 0, false);
IMPLEMENT_CLOUD_RENDERVIEW_PS(1, 1, 0, false);
IMPLEMENT_CLOUD_RENDERVIEW_PS(0, 0, 1, false);
IMPLEMENT_CLOUD_RENDERVIEW_PS(1, 0, 1, false);
IMPLEMENT_CLOUD_RENDERVIEW_PS(0, 1, 1, false);
IMPLEMENT_CLOUD_RENDERVIEW_PS(1, 1, 1, false);
IMPLEMENT_CLOUD_RENDERVIEW_PS(0, 0, 0, true);

#undef IMPLEMENT_CLOUD_RENDERVIEW_PS

//////////////////////////////////////////////////////////////////////////

class FRenderVolumetricCloudRenderViewCS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FRenderVolumetricCloudRenderViewCS, MeshMaterial)
	SHADER_USE_PARAMETER_STRUCT_WITH_LEGACY_BASE(FRenderVolumetricCloudRenderViewCS, FMeshMaterialShader)

	class FCloudPerSampleAtmosphereTransmittance : SHADER_PERMUTATION_BOOL("CLOUD_PER_SAMPLE_ATMOSPHERE_TRANSMITTANCE");
	class FCloudSampleAtmosphericLightShadowmap : SHADER_PERMUTATION_BOOL("CLOUD_SAMPLE_ATMOSPHERIC_LIGHT_SHADOWMAP");
	class FCloudSampleSecondLight : SHADER_PERMUTATION_BOOL("CLOUD_SAMPLE_SECOND_LIGHT");
	class FCloudSampleCountDebugMode : SHADER_PERMUTATION_BOOL("CLOUD_SAMPLE_COUNT_DEBUG_MODE");

	using FPermutationDomain = TShaderPermutationDomain<
		FCloudPerSampleAtmosphereTransmittance,
		FCloudSampleAtmosphericLightShadowmap,
		FCloudSampleSecondLight,
		FCloudSampleCountDebugMode>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector4, OutputViewRect)
		SHADER_PARAMETER(int32, bBlendCloudColor)
		SHADER_PARAMETER(int32, TargetCubeFace)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FRenderVolumetricCloudGlobalParameters, VolumetricCloudRenderViewParamsUB)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutCloudColor)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutCloudDepth)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutCloudColorCube)
	END_SHADER_PARAMETER_STRUCT()

	static const int32 ThreadGroupSizeX = 8;
	static const int32 ThreadGroupSizeY = 8;

	static bool ShouldCompilePermutation(const FMaterialShaderPermutationParameters& Parameters)
	{
		const FPermutationDomain PermutationVector(Parameters.PermutationId);
		const bool bIsCompatible = IsMaterialCompatibleWithVolumetricCloud(Parameters.MaterialParameters, Parameters.Platform);

		if (PermutationVector.Get<FCloudSampleCountDebugMode>())
		{
			// Only compile visualization shaders for editor builds
			return bIsCompatible
				&& EnumHasAllFlags(Parameters.Flags, EShaderPermutationFlags::HasEditorOnlyData)
				&& !PermutationVector.Get<FCloudPerSampleAtmosphereTransmittance>()
				&& !PermutationVector.Get<FCloudSampleAtmosphericLightShadowmap>()
				&& !PermutationVector.Get<FCloudSampleSecondLight>();
		}

		return bIsCompatible;
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("SHADER_RENDERVIEW_CS"), TEXT("1"));
		OutEnvironment.SetDefine(TEXT("CLOUD_LAYER_PIXEL_SHADER"), TEXT("1"));

		// Force texture fetches to not use automatic mip generation because the shader is compute and using a dynamic loop to evaluate the material multiple times.
		OutEnvironment.SetDefine(TEXT("USE_FORCE_TEXTURE_MIP"), TEXT("1"));

		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), ThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), ThreadGroupSizeY);

		// This shader must support typed UAV load and we are testing if it is supported at runtime using RHIIsTypedUAVLoadSupported
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
	}
};

IMPLEMENT_MATERIAL_SHADER_TYPE(, FRenderVolumetricCloudRenderViewCS, TEXT("/Engine/Private/VolumetricCloud.usf"), TEXT("MainCS"), SF_Compute);

//////////////////////////////////////////////////////////////////////////

class FSingleTriangleMeshVertexBuffer : public FRenderResource
{
public:
	FStaticMeshVertexBuffers Buffers;

	FSingleTriangleMeshVertexBuffer()
	{
		TArray<FDynamicMeshVertex> Vertices;

		// Vertex position constructed in the shader
		Vertices.Add(FDynamicMeshVertex(FVector(0.0f, 0.0f, 0.0f)));
		Vertices.Add(FDynamicMeshVertex(FVector(0.0f, 0.0f, 0.0f)));
		Vertices.Add(FDynamicMeshVertex(FVector(0.0f, 0.0f, 0.0f)));

		Buffers.PositionVertexBuffer.Init(Vertices.Num());
		Buffers.StaticMeshVertexBuffer.Init(Vertices.Num(), 1);

		for (int32 i = 0; i < Vertices.Num(); i++)
		{
			const FDynamicMeshVertex& Vertex = Vertices[i];

			Buffers.PositionVertexBuffer.VertexPosition(i) = Vertex.Position;
			Buffers.StaticMeshVertexBuffer.SetVertexTangents(i, Vertex.TangentX.ToFVector(), Vertex.GetTangentY(), Vertex.TangentZ.ToFVector());
			Buffers.StaticMeshVertexBuffer.SetVertexUV(i, 0, Vertex.TextureCoordinate[0]);
		}
	}

	virtual void InitRHI() override
	{
		Buffers.PositionVertexBuffer.InitResource();
		Buffers.StaticMeshVertexBuffer.InitResource();
	}

	virtual void ReleaseRHI() override
	{
		Buffers.PositionVertexBuffer.ReleaseRHI();
		Buffers.PositionVertexBuffer.ReleaseResource();
		Buffers.StaticMeshVertexBuffer.ReleaseRHI();
		Buffers.StaticMeshVertexBuffer.ReleaseResource();
	}
};

static TGlobalResource<FSingleTriangleMeshVertexBuffer> GSingleTriangleMeshVertexBuffer;

class FSingleTriangleMeshVertexFactory : public FLocalVertexFactory
{
public:
	FSingleTriangleMeshVertexFactory(ERHIFeatureLevel::Type InFeatureLevel)
		: FLocalVertexFactory(InFeatureLevel, "FSingleTriangleMeshVertexFactory")
	{}

	~FSingleTriangleMeshVertexFactory()
	{
		ReleaseResource();
	}

	virtual void InitRHI() override
	{
		FSingleTriangleMeshVertexBuffer* VertexBuffer = &GSingleTriangleMeshVertexBuffer;
		FLocalVertexFactory::FDataType NewData;
		VertexBuffer->Buffers.PositionVertexBuffer.BindPositionVertexBuffer(this, NewData);
		VertexBuffer->Buffers.StaticMeshVertexBuffer.BindTangentVertexBuffer(this, NewData);
		VertexBuffer->Buffers.StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(this, NewData);
		VertexBuffer->Buffers.StaticMeshVertexBuffer.BindLightMapVertexBuffer(this, NewData, 0);
		FColorVertexBuffer::BindDefaultColorVertexBuffer(this, NewData, FColorVertexBuffer::NullBindStride::ZeroForDefaultBufferBind);
		// Don't call SetData(), because that ends up calling UpdateRHI(), and if the resource has already been initialized
		// (e.g. when switching the feature level in the editor), that calls InitRHI(), resulting in an infinite loop.
		Data = NewData;
		FLocalVertexFactory::InitRHI();
	}

	bool HasIncompatibleFeatureLevel(ERHIFeatureLevel::Type InFeatureLevel)
	{
		return InFeatureLevel != GetFeatureLevel();
	}
};

static FSingleTriangleMeshVertexFactory* GSingleTriangleMeshVertexFactory = NULL;

static void GetSingleTriangleMeshBatch(FMeshBatch& LocalSingleTriangleMesh, const FMaterialRenderProxy* CloudVolumeMaterialProxy, const ERHIFeatureLevel::Type FeatureLevel)
{
	if (!GSingleTriangleMeshVertexFactory || GSingleTriangleMeshVertexFactory->HasIncompatibleFeatureLevel(FeatureLevel))
	{
		if (GSingleTriangleMeshVertexFactory)
		{
			GSingleTriangleMeshVertexFactory->ReleaseResource();
			delete GSingleTriangleMeshVertexFactory;
		}
		GSingleTriangleMeshVertexFactory = new FSingleTriangleMeshVertexFactory(FeatureLevel);
		GSingleTriangleMeshVertexBuffer.UpdateRHI();
		GSingleTriangleMeshVertexFactory->InitResource();
	}
	LocalSingleTriangleMesh.VertexFactory = GSingleTriangleMeshVertexFactory;
	LocalSingleTriangleMesh.MaterialRenderProxy = CloudVolumeMaterialProxy;
	LocalSingleTriangleMesh.Elements[0].IndexBuffer = nullptr;
	LocalSingleTriangleMesh.Elements[0].FirstIndex = 0;
	LocalSingleTriangleMesh.Elements[0].NumPrimitives = 1;
	LocalSingleTriangleMesh.Elements[0].MinVertexIndex = 0;
	LocalSingleTriangleMesh.Elements[0].MaxVertexIndex = 2;

	LocalSingleTriangleMesh.Elements[0].PrimitiveUniformBuffer = nullptr;
	LocalSingleTriangleMesh.Elements[0].PrimitiveIdMode = PrimID_ForceZero;
}

//////////////////////////////////////////////////////////////////////////

class FVolumetricCloudRenderViewMeshProcessor : public FMeshPassProcessor
{
public:
	FVolumetricCloudRenderViewMeshProcessor(const FScene* Scene, const FViewInfo* InViewIfDynamicMeshCommand,
		TUniformBufferRef<FViewUniformShaderParameters> ViewUniformBuffer, bool bShouldViewRenderVolumetricRenderTarget, bool bSkipAtmosphericLightShadowmap, bool bSecondAtmosphereLightEnabled,
		bool bInVisualizeConservativeDensity, FMeshPassDrawListContext* InDrawListContext)
		: FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
		, bVolumetricCloudPerSampleAtmosphereTransmittance(ShouldUsePerSampleAtmosphereTransmittance(Scene, InViewIfDynamicMeshCommand))
		, bVolumetricCloudSampleLightShadowmap(!bSkipAtmosphericLightShadowmap && CVarVolumetricCloudShadowSampleAtmosphericLightShadowmap.GetValueOnAnyThread() > 0)
		, bVolumetricCloudSecondLight(bSecondAtmosphereLightEnabled)
		, bVisualizeConservativeDensityOrDebugSampleCount(bInVisualizeConservativeDensity)
	{
		PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
		PassDrawRenderState.SetViewUniformBuffer(ViewUniformBuffer);

		if (bShouldViewRenderVolumetricRenderTarget || bVisualizeConservativeDensityOrDebugSampleCount)
		{
			// No blending as we only render clouds in that render target today. Avoids clearing for now.
			PassDrawRenderState.SetBlendState(TStaticBlendState<>::GetRHI());
		}
		else
		{
			// When volumetric render target is not enabled globally or for some views, e.g. reflection captures.
			PassDrawRenderState.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_SourceAlpha, BO_Add, BF_Zero, BF_SourceAlpha>::GetRHI());
		}
	}

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final
	{
		// Determine the mesh's material and blend mode.
		const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
		const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);

		if (Material.GetMaterialDomain() != MD_Volume)
		{
			// Skip in this case. This can happens when the material is compiled and a fallback is provided.
			return;
		}

		const ERasterizerFillMode MeshFillMode = FM_Solid;
		const ERasterizerCullMode MeshCullMode = CM_None;
		const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;

		if (bVisualizeConservativeDensityOrDebugSampleCount)
		{
			TemplatedProcess<FRenderVolumetricCloudRenderViewPs<VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance0_SampleShadow0_SecondLight0, true>>(
				MeshBatch, BatchElementMask, PrimitiveSceneProxy, MaterialRenderProxy, Material, StaticMeshId, MeshFillMode, MeshCullMode);
		}
		else if (bVolumetricCloudSecondLight)
		{
			if (bVolumetricCloudSampleLightShadowmap)
			{
				if (bVolumetricCloudPerSampleAtmosphereTransmittance)
				{
					TemplatedProcess<FRenderVolumetricCloudRenderViewPs<VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance1_SampleShadow1_SecondLight1, false>>(
						MeshBatch, BatchElementMask, PrimitiveSceneProxy, MaterialRenderProxy, Material, StaticMeshId, MeshFillMode, MeshCullMode);
				}
				else
				{
					TemplatedProcess<FRenderVolumetricCloudRenderViewPs<VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance0_SampleShadow1_SecondLight1, false>>(
						MeshBatch, BatchElementMask, PrimitiveSceneProxy, MaterialRenderProxy, Material, StaticMeshId, MeshFillMode, MeshCullMode);
				}
			}
			else
			{
				if (bVolumetricCloudPerSampleAtmosphereTransmittance)
				{
					TemplatedProcess<FRenderVolumetricCloudRenderViewPs<VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance1_SampleShadow0_SecondLight1, false>>(
						MeshBatch, BatchElementMask, PrimitiveSceneProxy, MaterialRenderProxy, Material, StaticMeshId, MeshFillMode, MeshCullMode);
				}
				else
				{
					TemplatedProcess<FRenderVolumetricCloudRenderViewPs<VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance0_SampleShadow0_SecondLight1, false>>(
						MeshBatch, BatchElementMask, PrimitiveSceneProxy, MaterialRenderProxy, Material, StaticMeshId, MeshFillMode, MeshCullMode);
				}
			}
		}
		else
		{
			if (bVolumetricCloudSampleLightShadowmap)
			{
				if (bVolumetricCloudPerSampleAtmosphereTransmittance)
				{
					TemplatedProcess<FRenderVolumetricCloudRenderViewPs<VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance1_SampleShadow1_SecondLight0, false>>(
						MeshBatch, BatchElementMask, PrimitiveSceneProxy, MaterialRenderProxy, Material, StaticMeshId, MeshFillMode, MeshCullMode);
				}
				else
				{
					TemplatedProcess<FRenderVolumetricCloudRenderViewPs<VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance0_SampleShadow1_SecondLight0, false>>(
						MeshBatch, BatchElementMask, PrimitiveSceneProxy, MaterialRenderProxy, Material, StaticMeshId, MeshFillMode, MeshCullMode);
				}
			}
			else
			{
				if (bVolumetricCloudPerSampleAtmosphereTransmittance)
				{
					TemplatedProcess<FRenderVolumetricCloudRenderViewPs<VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance1_SampleShadow0_SecondLight0, false>>(
						MeshBatch, BatchElementMask, PrimitiveSceneProxy, MaterialRenderProxy, Material, StaticMeshId, MeshFillMode, MeshCullMode);
				}
				else
				{
					TemplatedProcess<FRenderVolumetricCloudRenderViewPs<VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance0_SampleShadow0_SecondLight0, false>>(
						MeshBatch, BatchElementMask, PrimitiveSceneProxy, MaterialRenderProxy, Material, StaticMeshId, MeshFillMode, MeshCullMode);
				}
			}
		}
	}

private:

	template<class RenderVolumetricCloudRenderViewPsType>
	void TemplatedProcess(
		const FMeshBatch& MeshBatch,
		uint64 BatchElementMask,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource,
		int32 StaticMeshId,
		ERasterizerFillMode MeshFillMode,
		ERasterizerCullMode MeshCullMode)
	{
		FMeshMaterialShaderElementData EmptyShaderElementData;
		EmptyShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

		const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

		TMeshProcessorShaders< FRenderVolumetricCloudVS, FMeshMaterialShader, FMeshMaterialShader, RenderVolumetricCloudRenderViewPsType> PassShaders;
		PassShaders.PixelShader = MaterialResource.GetShader<RenderVolumetricCloudRenderViewPsType>(VertexFactory->GetType());
		PassShaders.VertexShader = MaterialResource.GetShader<FRenderVolumetricCloudVS>(VertexFactory->GetType());
		const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(PassShaders.VertexShader, PassShaders.PixelShader);
		BuildMeshDrawCommands(
			MeshBatch,
			BatchElementMask,
			PrimitiveSceneProxy,
			MaterialRenderProxy,
			MaterialResource,
			PassDrawRenderState,
			PassShaders,
			MeshFillMode,
			MeshCullMode,
			SortKey,
			EMeshPassFeatures::Default,
			EmptyShaderElementData);
	}

	FMeshPassProcessorRenderState PassDrawRenderState;
	bool bVolumetricCloudPerSampleAtmosphereTransmittance;
	bool bVolumetricCloudSampleLightShadowmap;
	bool bVolumetricCloudSecondLight;
	bool bVisualizeConservativeDensityOrDebugSampleCount;
};



//////////////////////////////////////////////////////////////////////////

BEGIN_SHADER_PARAMETER_STRUCT(FVolumetricCloudShadowParametersPS, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FRenderVolumetricCloudGlobalParameters, TraceVolumetricCloudParamsUB)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FVolumetricCloudShadowPS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FVolumetricCloudShadowPS, MeshMaterial);

public:

	FVolumetricCloudShadowPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{}

	FVolumetricCloudShadowPS() {}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		const bool bIsCompatible = IsMaterialCompatibleWithVolumetricCloud(Parameters.MaterialParameters, Parameters.Platform);
		return bIsCompatible;
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_SHADOW_PS"), TEXT("1"));
		OutEnvironment.SetDefine(TEXT("CLOUD_LAYER_PIXEL_SHADER"), TEXT("1"));

		// Force texture fetches to not use automatic mip generation because the pixel shader is using a dynamic loop to evaluate the material multiple times.
		OutEnvironment.SetDefine(TEXT("USE_FORCE_TEXTURE_MIP"), TEXT("1"));
	}

private:
};

IMPLEMENT_MATERIAL_SHADER_TYPE(, FVolumetricCloudShadowPS, TEXT("/Engine/Private/VolumetricCloud.usf"), TEXT("MainPS"), SF_Pixel);



class FVolumetricCloudRenderShadowMeshProcessor : public FMeshPassProcessor
{
public:
	FVolumetricCloudRenderShadowMeshProcessor(const FScene* Scene, const FViewInfo* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
		: FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
	{
		PassDrawRenderState.SetBlendState(TStaticBlendState<>::GetRHI());
		PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
		PassDrawRenderState.SetViewUniformBuffer(Scene->UniformBuffers.ViewUniformBuffer);
	}

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final
	{
		// Determine the mesh's material and blend mode.
		const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
		const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);

		if(Material.GetMaterialDomain() != MD_Volume)
		{
			// Skip in this case. This can happens when the material is compiled and a fallback is provided.
			return;
		}

		const ERasterizerFillMode MeshFillMode = FM_Solid;
		const ERasterizerCullMode MeshCullMode = CM_None;
		const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;
		Process(MeshBatch, BatchElementMask, PrimitiveSceneProxy, MaterialRenderProxy, Material, StaticMeshId, MeshFillMode, MeshCullMode);
	}

private:

	void Process(
		const FMeshBatch& MeshBatch,
		uint64 BatchElementMask,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource,
		int32 StaticMeshId,
		ERasterizerFillMode MeshFillMode,
		ERasterizerCullMode MeshCullMode)
	{
		FMeshMaterialShaderElementData EmptyShaderElementData;
		EmptyShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

		const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

		TMeshProcessorShaders< FRenderVolumetricCloudVS, FMeshMaterialShader, FMeshMaterialShader,
			FVolumetricCloudShadowPS> PassShaders;
		PassShaders.PixelShader = MaterialResource.GetShader<FVolumetricCloudShadowPS>(VertexFactory->GetType());
		PassShaders.VertexShader = MaterialResource.GetShader<FRenderVolumetricCloudVS>(VertexFactory->GetType());
		const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(PassShaders.VertexShader, PassShaders.PixelShader);
		BuildMeshDrawCommands(
			MeshBatch,
			BatchElementMask,
			PrimitiveSceneProxy,
			MaterialRenderProxy,
			MaterialResource,
			PassDrawRenderState,
			PassShaders,
			MeshFillMode,
			MeshCullMode,
			SortKey,
			EMeshPassFeatures::Default,
			EmptyShaderElementData);
	}

	FMeshPassProcessorRenderState PassDrawRenderState;
};



//////////////////////////////////////////////////////////////////////////

class FDrawDebugCloudShadowCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDrawDebugCloudShadowCS);
	SHADER_USE_PARAMETER_STRUCT(FDrawDebugCloudShadowCS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderDrawDebugParameters, ShaderDrawParameters)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CloudTracedTexture)
		SHADER_PARAMETER(FVector4, CloudTextureSizeInvSize)
		SHADER_PARAMETER(FVector, CloudTraceDirection)
		SHADER_PARAMETER(FMatrix, CloudWorldToLightClipMatrixInv)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsVolumetricCloudMaterialSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_DEBUG_SHADOW_CS"), TEXT("1"));
	}
};

IMPLEMENT_GLOBAL_SHADER(FDrawDebugCloudShadowCS, "/Engine/Private/VolumetricCloud.usf", "MainDrawDebugShadowCS", SF_Compute);



//////////////////////////////////////////////////////////////////////////

class FCloudShadowFilterCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCloudShadowFilterCS);
	SHADER_USE_PARAMETER_STRUCT(FCloudShadowFilterCS, FGlobalShader);

	class FFilterSkyAO : SHADER_PERMUTATION_BOOL("PERMUTATION_SKYAO");
	using FPermutationDomain = TShaderPermutationDomain<FFilterSkyAO>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_SAMPLER(SamplerState, BilinearSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CloudShadowTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutCloudShadowTexture)
		SHADER_PARAMETER(FVector4, CloudTextureSizeInvSize)
		SHADER_PARAMETER(FVector4, CloudTextureTexelWorldSizeInvSize)
		SHADER_PARAMETER(float, CloudLayerStartHeight)
		SHADER_PARAMETER(float, CloudSkyAOApertureScaleAdd)
		SHADER_PARAMETER(float, CloudSkyAOApertureScaleMul)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsVolumetricCloudMaterialSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_SHADOW_FILTER_CS"), TEXT("1"));
	}
};

IMPLEMENT_GLOBAL_SHADER(FCloudShadowFilterCS, "/Engine/Private/VolumetricCloud.usf", "MainShadowFilterCS", SF_Compute);



//////////////////////////////////////////////////////////////////////////

class FCloudShadowTemporalProcessCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCloudShadowTemporalProcessCS);
	SHADER_USE_PARAMETER_STRUCT(FCloudShadowTemporalProcessCS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_SAMPLER(SamplerState, BilinearSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CurrCloudShadowTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PrevCloudShadowTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutCloudShadowTexture)
		SHADER_PARAMETER(FMatrix, CurrFrameCloudShadowmapWorldToLightClipMatrixInv)
		SHADER_PARAMETER(FMatrix, PrevFrameCloudShadowmapWorldToLightClipMatrix)
		SHADER_PARAMETER(FVector, CurrFrameLightPos)
		SHADER_PARAMETER(FVector, PrevFrameLightPos)
		SHADER_PARAMETER(FVector, CurrFrameLightDir)
		SHADER_PARAMETER(FVector, PrevFrameLightDir)
		SHADER_PARAMETER(FVector4, CloudTextureSizeInvSize)
		SHADER_PARAMETER(FVector4, CloudTextureTracingSizeInvSize)
		SHADER_PARAMETER(FVector4, CloudTextureTracingPixelScaleOffset)
		SHADER_PARAMETER(float, TemporalFactor)
		SHADER_PARAMETER(uint32, PreviousDataIsValid)
		SHADER_PARAMETER(uint32, CloudShadowMapAnchorPointMoved)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsVolumetricCloudMaterialSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_SHADOW_TEMPORAL_PROCESS_CS"), TEXT("1"));
	}
};

IMPLEMENT_GLOBAL_SHADER(FCloudShadowTemporalProcessCS, "/Engine/Private/VolumetricCloud.usf", "MainShadowTemporalProcessCS", SF_Compute);



//////////////////////////////////////////////////////////////////////////



void CleanUpCloudDataFunction(TArray<FViewInfo>& Views)
{
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& ViewInfo = Views[ViewIndex];
		ViewInfo.VolumetricCloudSkyAO = nullptr;
		for (int LightIndex = 0; LightIndex < NUM_ATMOSPHERE_LIGHTS; ++LightIndex)
		{
			ViewInfo.VolumetricCloudShadowRenderTarget[LightIndex] = nullptr;
			if (ViewInfo.ViewState != nullptr)
			{
				ViewInfo.ViewState->VolumetricCloudShadowRenderTarget[LightIndex].Reset();
			}
		}
	}
};

void FSceneRenderer::InitVolumetricCloudsForViews(FRDGBuilder& GraphBuilder, bool bShouldRenderVolumetricCloud)
{
	auto CleanUpCloudDataPass = [&Views = Views](FRDGBuilder& GraphBuilder)
	{
		AddPass(GraphBuilder, [&Views](FRHICommandList&)
			{
				CleanUpCloudDataFunction(Views);
			});
	};

	// First make sure we always clear the texture on views to make sure no dangling texture pointers are ever used
	CleanUpCloudDataFunction(Views);

	if (!bShouldRenderVolumetricCloud)
	{
		return;
	}

	if (Scene)
	{
		check(ShouldRenderVolumetricCloud(Scene, ViewFamily.EngineShowFlags)); // This should not be called if we should not render SkyAtmosphere

		check(Scene->GetVolumetricCloudSceneInfo());
		const FSkyAtmosphereRenderSceneInfo* SkyInfo = Scene->GetSkyAtmosphereSceneInfo();
		FVolumetricCloudRenderSceneInfo& CloudInfo = *Scene->GetVolumetricCloudSceneInfo();
		const FVolumetricCloudSceneProxy& CloudProxy = CloudInfo.GetVolumetricCloudSceneProxy();
		FLightSceneProxy* AtmosphericLight0 = Scene->AtmosphereLights[0] ? Scene->AtmosphereLights[0]->Proxy : nullptr;
		FLightSceneProxy* AtmosphericLight1 = Scene->AtmosphereLights[1] ? Scene->AtmosphereLights[1]->Proxy : nullptr;
		FSkyLightSceneProxy* SkyLight = Scene->SkyLight;
		const float KilometersToCentimeters = 100000.0f;
		const float CentimetersToKilometers = 1.0f / KilometersToCentimeters;
		const float KilometersToMeters = 1000.0f;
		const float MetersToKilometers = 1.0f / KilometersToMeters;

		const float CloudShadowTemporalWeight = FMath::Min(FMath::Max(CVarVolumetricCloudShadowTemporalFilteringNewFrameWeight.GetValueOnRenderThread(), 0.0f), 1.0f);
		const bool CloudShadowTemporalEnabled = CloudShadowTemporalWeight < 1.0f;

		// Initialise the cloud common parameters
		{
			FVolumetricCloudCommonShaderParameters& CloudGlobalShaderParams = CloudInfo.GetVolumetricCloudCommonShaderParameters();
			float PlanetRadiusKm = CloudProxy.PlanetRadiusKm;
			if (SkyInfo)
			{
				const FAtmosphereSetup& AtmosphereSetup = SkyInfo->GetSkyAtmosphereSceneProxy().GetAtmosphereSetup();
				PlanetRadiusKm = AtmosphereSetup.BottomRadiusKm;
				CloudGlobalShaderParams.CloudLayerCenterKm = AtmosphereSetup.PlanetCenterKm;
			}
			else
			{
				CloudGlobalShaderParams.CloudLayerCenterKm = FVector(0.0f, 0.0f, -PlanetRadiusKm);
			}
			CloudGlobalShaderParams.PlanetRadiusKm = PlanetRadiusKm;
			CloudGlobalShaderParams.BottomRadiusKm = PlanetRadiusKm + CloudProxy.LayerBottomAltitudeKm;
			CloudGlobalShaderParams.TopRadiusKm = CloudGlobalShaderParams.BottomRadiusKm + CloudProxy.LayerHeightKm;
			CloudGlobalShaderParams.GroundAlbedo = FLinearColor(CloudProxy.GroundAlbedo);
			CloudGlobalShaderParams.SkyLightCloudBottomVisibility = 1.0f - CloudProxy.SkyLightCloudBottomOcclusion;

			CloudGlobalShaderParams.TracingStartMaxDistance = KilometersToCentimeters * CloudProxy.TracingStartMaxDistance;
			CloudGlobalShaderParams.TracingMaxDistance		= KilometersToCentimeters * CloudProxy.TracingMaxDistance;

			const float BaseViewRaySampleCount = 96.0f;
			const float BaseShadowRaySampleCount = 10.0f;
			CloudGlobalShaderParams.SampleCountMin		= FMath::Max(0, CVarVolumetricCloudSampleMinCount.GetValueOnAnyThread());
			CloudGlobalShaderParams.SampleCountMax		= FMath::Max(2.0f, FMath::Min(BaseViewRaySampleCount   * CloudProxy.ViewSampleCountScale,       CVarVolumetricCloudViewRaySampleMaxCount.GetValueOnAnyThread()));
			CloudGlobalShaderParams.ShadowSampleCountMax= FMath::Max(2.0f, FMath::Min(BaseShadowRaySampleCount * CloudProxy.ShadowViewSampleCountScale, CVarVolumetricCloudShadowViewRaySampleMaxCount.GetValueOnAnyThread()));
			CloudGlobalShaderParams.ShadowTracingMaxDistance = KilometersToCentimeters * FMath::Max(0.1f, CloudProxy.ShadowTracingDistance);
			CloudGlobalShaderParams.InvDistanceToSampleCountMax = 1.0f / FMath::Max(1.0f, KilometersToCentimeters * CVarVolumetricCloudDistanceToSampleMaxCount.GetValueOnAnyThread());
			CloudGlobalShaderParams.StopTracingTransmittanceThreshold = FMath::Clamp(CloudProxy.StopTracingTransmittanceThreshold, 0.0f, 1.0f);


			auto PrepareCloudShadowMapLightData = [&](FLightSceneProxy* AtmosphericLight, int LightIndex)
			{
				const float CloudShadowmapResolution = float(GetVolumetricCloudShadowMapResolution(AtmosphericLight));
				const float CloudShadowmapResolutionInv = 1.0f / CloudShadowmapResolution;
				CloudGlobalShaderParams.CloudShadowmapSizeInvSize[LightIndex] = FVector4(CloudShadowmapResolution, CloudShadowmapResolution, CloudShadowmapResolutionInv, CloudShadowmapResolutionInv);
				CloudGlobalShaderParams.CloudShadowmapStrength[LightIndex] = FVector4(GetVolumetricCloudShadowmapStrength(AtmosphericLight), 0.0f, 0.0f, 0.0f);
				CloudGlobalShaderParams.CloudShadowmapDepthBias[LightIndex] = FVector4(GetVolumetricCloudShadowmapDepthBias(AtmosphericLight), 0.0f, 0.0f, 0.0f);
				CloudGlobalShaderParams.AtmosphericLightCloudScatteredLuminanceScale[LightIndex] = GetVolumetricCloudScatteredLuminanceScale(AtmosphericLight);

				if (CloudShadowTemporalEnabled)
				{
					const float CloudShadowmapHalfResolution = CloudShadowmapResolution * 0.5f;
					const float CloudShadowmapHalfResolutionInv = 1.0f / CloudShadowmapHalfResolution;
					CloudGlobalShaderParams.CloudShadowmapTracingSizeInvSize[LightIndex] = FVector4(CloudShadowmapHalfResolution, CloudShadowmapHalfResolution, CloudShadowmapHalfResolutionInv, CloudShadowmapHalfResolutionInv);

					uint32 Status = 0;
					if (Views.Num() > 0 && Views[0].ViewState)
					{
						Status = Views[0].ViewState->GetFrameIndex() % 4;
					}
					const float PixelOffsetX = Status == 1 || Status == 2 ? 1.0f : 0.0f;
					const float PixelOffsetY = Status == 1 || Status == 3 ? 1.0f : 0.0f;

					const float HalfResolutionFactor = 2.0f;
					CloudGlobalShaderParams.CloudShadowmapTracingPixelScaleOffset[LightIndex] = FVector4(HalfResolutionFactor, HalfResolutionFactor, PixelOffsetX, PixelOffsetY);
				}
				else
				{
					CloudGlobalShaderParams.CloudShadowmapTracingSizeInvSize[LightIndex] = CloudGlobalShaderParams.CloudShadowmapSizeInvSize[LightIndex];
					CloudGlobalShaderParams.CloudShadowmapTracingPixelScaleOffset[LightIndex] = FVector4(1.0f, 1.0f, 0.0f, 0.0f);
				}

				// Setup cloud shadow constants
				if (AtmosphericLight)
				{
					const FVector AtmopshericLightDirection = AtmosphericLight->GetDirection();
					const FVector UpVector = FMath::Abs(FVector::DotProduct(AtmopshericLightDirection, FVector::UpVector)) > 0.99f ? FVector::ForwardVector : FVector::UpVector;

					const float SphereRadius = GetVolumetricCloudShadowMapExtentKm(AtmosphericLight) * KilometersToCentimeters;
					const float SphereDiameter = SphereRadius * 2.0f;
					const float NearPlane = 0.0f;
					const float FarPlane = SphereDiameter;
					const float ZScale = 1.0f / (FarPlane - NearPlane);
					const float ZOffset = -NearPlane;

					// TODO Make it work for all views
					FVector LookAtPosition = FVector::ZeroVector;
					FVector PlanetToCameraNormUp = FVector::UpVector;
					if (Views.Num() > 0)
					{
						FViewInfo& View = Views[0];

						// Look at position is positioned on the planet surface under the camera.
						LookAtPosition = (View.ViewMatrices.GetViewOrigin() - (CloudGlobalShaderParams.CloudLayerCenterKm * KilometersToCentimeters));
						LookAtPosition.Normalize();
						PlanetToCameraNormUp = LookAtPosition;
						LookAtPosition = (CloudGlobalShaderParams.CloudLayerCenterKm + LookAtPosition * PlanetRadiusKm) * KilometersToCentimeters;
						// Light position is positioned away from the look at position in the light direction according to the shadowmap radius.
						const FVector LightPosition = LookAtPosition - AtmopshericLightDirection * SphereRadius;

						float WorldSizeSnap = CVarVolumetricCloudShadowMapSnapLength.GetValueOnAnyThread() * KilometersToCentimeters;
						LookAtPosition.X = (FMath::FloorToFloat((LookAtPosition.X + 0.5f * WorldSizeSnap) / WorldSizeSnap)) * WorldSizeSnap; // offset by 0.5 to not snap around origin
						LookAtPosition.Y = (FMath::FloorToFloat((LookAtPosition.Y + 0.5f * WorldSizeSnap) / WorldSizeSnap)) * WorldSizeSnap;
						LookAtPosition.Z = (FMath::FloorToFloat((LookAtPosition.Z + 0.5f * WorldSizeSnap) / WorldSizeSnap)) * WorldSizeSnap;
					}

					const FVector LightPosition = LookAtPosition - AtmopshericLightDirection * SphereRadius;
					FReversedZOrthoMatrix ShadowProjectionMatrix(SphereDiameter, SphereDiameter, ZScale, ZOffset);
					FLookAtMatrix ShadowViewMatrix(LightPosition, LookAtPosition, UpVector);
					CloudGlobalShaderParams.CloudShadowmapWorldToLightClipMatrix[LightIndex] = ShadowViewMatrix * ShadowProjectionMatrix;
					CloudGlobalShaderParams.CloudShadowmapWorldToLightClipMatrixInv[LightIndex] = CloudGlobalShaderParams.CloudShadowmapWorldToLightClipMatrix[LightIndex].Inverse();
					CloudGlobalShaderParams.CloudShadowmapLightDir[LightIndex] = AtmopshericLightDirection;
					CloudGlobalShaderParams.CloudShadowmapLightPos[LightIndex] = LightPosition;
					CloudGlobalShaderParams.CloudShadowmapLightAnchorPos[LightIndex] = LookAtPosition;
					CloudGlobalShaderParams.CloudShadowmapFarDepthKm[LightIndex] = FVector4(FarPlane * CentimetersToKilometers, 0.0f, 0.0f, 0.0f);

					// More samples when the sun is at the horizon: a lot more distance to travel and less pixel covered so trying to keep the same cost and quality.
					const float CloudShadowRaySampleCountScale = GetVolumetricCloudShadowRaySampleCountScale(AtmosphericLight);
					const float CloudShadowRaySampleBaseCount = 16.0f;
					const float CloudShadowRayMapSampleCount = FMath::Max(4.0f, FMath::Min(CloudShadowRaySampleBaseCount * CloudShadowRaySampleCountScale, CVarVolumetricCloudShadowMapRaySampleMaxCount.GetValueOnAnyThread()));
					const float RaySampleHorizonFactor = FMath::Max(0.0f, CVarVolumetricCloudShadowMapRaySampleHorizonMultiplier.GetValueOnAnyThread()-1.0f);
					const float HorizonFactor = FMath::Clamp(0.2f / FMath::Abs(FVector::DotProduct(PlanetToCameraNormUp, -AtmopshericLightDirection)), 0.0f, 1.0f);
					CloudGlobalShaderParams.CloudShadowmapSampleCount[LightIndex] = FVector4(CloudShadowRayMapSampleCount + RaySampleHorizonFactor * CloudShadowRayMapSampleCount * HorizonFactor, 0.0f, 0.0f, 0.0f);
				}
				else
				{
					const FVector4 UpVector = FVector4(0.0f, 0.0f, 1.0f, 0.0f);
					const FVector4 ZeroVector = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
					const FVector4 OneVector = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
					CloudGlobalShaderParams.CloudShadowmapWorldToLightClipMatrix[LightIndex] = FMatrix::Identity;
					CloudGlobalShaderParams.CloudShadowmapWorldToLightClipMatrixInv[LightIndex] = FMatrix::Identity;
					CloudGlobalShaderParams.CloudShadowmapLightDir[LightIndex] = UpVector;
					CloudGlobalShaderParams.CloudShadowmapLightPos[LightIndex] = ZeroVector;
					CloudGlobalShaderParams.CloudShadowmapLightAnchorPos[LightIndex] = ZeroVector;
					CloudGlobalShaderParams.CloudShadowmapFarDepthKm[LightIndex] = OneVector;
					CloudGlobalShaderParams.CloudShadowmapSampleCount[LightIndex] = ZeroVector;
				}
			};
			PrepareCloudShadowMapLightData(AtmosphericLight0, 0);
			PrepareCloudShadowMapLightData(AtmosphericLight1, 1);

			// Setup cloud SkyAO constants
			{
				const float CloudSkyAOResolution = float(GetVolumetricCloudSkyAOResolution(SkyLight));
				const float CloudSkyAOResolutionInv = 1.0f / CloudSkyAOResolution;
				CloudGlobalShaderParams.CloudSkyAOSizeInvSize = FVector4(CloudSkyAOResolution, CloudSkyAOResolution, CloudSkyAOResolutionInv, CloudSkyAOResolutionInv);
				CloudGlobalShaderParams.CloudSkyAOStrength = GetVolumetricCloudSkyAOStrength(SkyLight);

				const float WorldSizeSnap = CVarVolumetricCloudSkyAOSnapLength.GetValueOnAnyThread() * KilometersToCentimeters;
				const float SphereDiameter = GetVolumetricCloudSkyAOExtentKm(SkyLight) * KilometersToCentimeters * 2.0f;
				const float VolumeDepthRange = (CloudProxy.LayerBottomAltitudeKm + CloudProxy.LayerHeightKm) * KilometersToCentimeters + WorldSizeSnap;
				const float NearPlane = 0.0f;
				const float FarPlane = 2.0f * VolumeDepthRange;
				const float ZScale = 1.0f / (FarPlane - NearPlane);
				const float ZOffset = -NearPlane;

				// TODO Make it work for all views
				FVector LookAtPosition = FVector::ZeroVector;
				if (Views.Num() > 0)
				{
					FViewInfo& View = Views[0];

					// Look at position is positioned on the planet surface under the camera.
					LookAtPosition = (View.ViewMatrices.GetViewOrigin() - (CloudGlobalShaderParams.CloudLayerCenterKm * KilometersToCentimeters));
					LookAtPosition.Normalize();
					LookAtPosition = (CloudGlobalShaderParams.CloudLayerCenterKm + LookAtPosition * PlanetRadiusKm) * KilometersToCentimeters;

					// Snap the texture projection
					LookAtPosition.X = (FMath::FloorToFloat((LookAtPosition.X + 0.5f * WorldSizeSnap) / WorldSizeSnap)) * WorldSizeSnap; // offset by 0.5 to not snap around origin
					LookAtPosition.Y = (FMath::FloorToFloat((LookAtPosition.Y + 0.5f * WorldSizeSnap) / WorldSizeSnap)) * WorldSizeSnap;
					LookAtPosition.Z = (FMath::FloorToFloat((LookAtPosition.Z + 0.5f * WorldSizeSnap) / WorldSizeSnap)) * WorldSizeSnap;
				}

				// Trace direction is towards the ground
				FVector TraceDirection =  CloudGlobalShaderParams.CloudLayerCenterKm * KilometersToCentimeters - LookAtPosition;
				TraceDirection.Normalize();

				const FVector UpVector = FMath::Abs(FVector::DotProduct(TraceDirection, FVector::UpVector)) > 0.99f ? FVector::ForwardVector : FVector::UpVector;
				const FVector LightPosition = LookAtPosition - TraceDirection * VolumeDepthRange;
				FReversedZOrthoMatrix ShadowProjectionMatrix(SphereDiameter, SphereDiameter, ZScale, ZOffset);
				FLookAtMatrix ShadowViewMatrix(LightPosition, LookAtPosition, UpVector);
				CloudGlobalShaderParams.CloudSkyAOWorldToLightClipMatrix = ShadowViewMatrix * ShadowProjectionMatrix;
				CloudGlobalShaderParams.CloudSkyAOWorldToLightClipMatrixInv = CloudGlobalShaderParams.CloudSkyAOWorldToLightClipMatrix.Inverse();
				CloudGlobalShaderParams.CloudSkyAOTraceDir = TraceDirection;
				CloudGlobalShaderParams.CloudSkyAOFarDepthKm = FarPlane * CentimetersToKilometers;

				// More samples when the sun is at the horizon: a lot more distance to travel and less pixel covered so trying to keep the same cost and quality.
				CloudGlobalShaderParams.CloudSkyAOSampleCount = CVarVolumetricCloudSkyAOTraceSampleCount.GetValueOnAnyThread();
			}

			FVolumetricCloudCommonGlobalShaderParameters CloudGlobalShaderParamsUB;
			CloudGlobalShaderParamsUB.VolumetricCloudCommonParams = CloudGlobalShaderParams;
			CloudInfo.GetVolumetricCloudCommonShaderParametersUB() = TUniformBufferRef<FVolumetricCloudCommonGlobalShaderParameters>::CreateUniformBufferImmediate(CloudGlobalShaderParamsUB, UniformBuffer_SingleFrame);
		}



		if (CloudProxy.GetCloudVolumeMaterial())
		{
			FMaterialRenderProxy* CloudVolumeMaterialProxy = CloudProxy.GetCloudVolumeMaterial()->GetRenderProxy();
			if (CloudVolumeMaterialProxy->GetIncompleteMaterialWithFallback(ViewFamily.GetFeatureLevel()).GetMaterialDomain() == MD_Volume)
			{
				RDG_EVENT_SCOPE(GraphBuilder, "VolumetricCloudShadow");
				RDG_GPU_STAT_SCOPE(GraphBuilder, VolumetricCloudShadow);

				TRefCountPtr<IPooledRenderTarget> BlackDummy = GSystemTextures.BlackDummy;
				FRDGTextureRef BlackDummyRDG = GraphBuilder.RegisterExternalTexture(BlackDummy);

				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
				{
					FViewInfo& ViewInfo = Views[ViewIndex];
					FVector ViewOrigin = ViewInfo.ViewMatrices.GetViewOrigin();

					FVolumeShadowingShaderParametersGlobal0 LightShadowShaderParams0;
					SetVolumeShadowingDefaultShaderParameters(LightShadowShaderParams0);

					FRenderVolumetricCloudGlobalParameters& VolumetricCloudParams = *GraphBuilder.AllocParameters<FRenderVolumetricCloudGlobalParameters>();
					VolumetricCloudParams.Light0Shadow = LightShadowShaderParams0;
					SetupDefaultRenderVolumetricCloudGlobalParameters(GraphBuilder, VolumetricCloudParams, CloudInfo, ViewInfo);

					auto TraceCloudTexture = [&](FRDGTextureRef CloudTextureTracedOutput, bool bSkyAOPass,
						TRDGUniformBufferRef<FRenderVolumetricCloudGlobalParameters> TraceVolumetricCloudParamsUB)
					{
						FVolumetricCloudShadowParametersPS* CloudShadowParameters = GraphBuilder.AllocParameters<FVolumetricCloudShadowParametersPS>();
						CloudShadowParameters->TraceVolumetricCloudParamsUB = TraceVolumetricCloudParamsUB;
						CloudShadowParameters->RenderTargets[0] = FRenderTargetBinding(CloudTextureTracedOutput, ERenderTargetLoadAction::ENoAction);

						GraphBuilder.AddPass(
							bSkyAOPass ? RDG_EVENT_NAME("CloudSkyAO") : RDG_EVENT_NAME("CloudShadow"),
							CloudShadowParameters,
							ERDGPassFlags::Raster,
							[CloudShadowParameters, &ViewInfo, CloudVolumeMaterialProxy](FRHICommandListImmediate& RHICmdList)
							{
								DrawDynamicMeshPass(ViewInfo, RHICmdList,
									[&ViewInfo, CloudVolumeMaterialProxy](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
									{
										FVolumetricCloudRenderShadowMeshProcessor PassMeshProcessor(
											ViewInfo.Family->Scene->GetRenderScene(), &ViewInfo,
											DynamicMeshPassContext);

										FMeshBatch LocalSingleTriangleMesh;
										GetSingleTriangleMeshBatch(LocalSingleTriangleMesh, CloudVolumeMaterialProxy, ViewInfo.GetFeatureLevel());

										const FPrimitiveSceneProxy* PrimitiveSceneProxy = nullptr;
										const uint64 DefaultBatchElementMask = ~0ull;
										PassMeshProcessor.AddMeshBatch(LocalSingleTriangleMesh, DefaultBatchElementMask, PrimitiveSceneProxy);
									});
							});
					};

					const float CloudLayerStartHeight = CloudProxy.LayerBottomAltitudeKm * KilometersToCentimeters;

					auto FilterTracedCloudTexture = [&](FRDGTextureRef* TracedCloudTextureOutput, FVector4 TracedTextureSizeInvSize, FVector4 CloudAOTextureTexelWorldSizeInvSize, bool bSkyAOPass)
					{
						const float DownscaleFactor = bSkyAOPass ? 1.0f : 0.5f; // No downscale for AO
						const FIntPoint DownscaledResolution = FIntPoint((*TracedCloudTextureOutput)->Desc.Extent.X * DownscaleFactor, (*TracedCloudTextureOutput)->Desc.Extent.Y * DownscaleFactor);
						FRDGTextureRef CloudShadowTexture2 = GraphBuilder.CreateTexture(
							FRDGTextureDesc::Create2D(DownscaledResolution, PF_FloatR11G11B10,
								FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV), bSkyAOPass ? TEXT("CloudSkyAOTexture2") : TEXT("CloudShadowTexture2"));

						FCloudShadowFilterCS::FPermutationDomain Permutation;
						Permutation.Set<FCloudShadowFilterCS::FFilterSkyAO>(bSkyAOPass);
						TShaderMapRef<FCloudShadowFilterCS> ComputeShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5), Permutation);

						FCloudShadowFilterCS::FParameters* Parameters = GraphBuilder.AllocParameters<FCloudShadowFilterCS::FParameters>();
						Parameters->BilinearSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
						Parameters->CloudShadowTexture = *TracedCloudTextureOutput;
						Parameters->CloudTextureSizeInvSize = TracedTextureSizeInvSize;
						Parameters->CloudTextureTexelWorldSizeInvSize = CloudAOTextureTexelWorldSizeInvSize;
						Parameters->CloudLayerStartHeight = CloudLayerStartHeight;
						Parameters->CloudSkyAOApertureScaleMul = GetVolumetricCloudSkyAOApertureScale(SkyLight);
						Parameters->CloudSkyAOApertureScaleAdd = 1.0f - Parameters->CloudSkyAOApertureScaleMul;
						Parameters->OutCloudShadowTexture = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(CloudShadowTexture2));

						const FIntVector DispatchCount = DispatchCount.DivideAndRoundUp(FIntVector(DownscaledResolution.X, DownscaledResolution.Y, 1), FIntVector(8, 8, 1));
						FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("CloudDataSpatialFilter"), ComputeShader, Parameters, DispatchCount);

						*TracedCloudTextureOutput = CloudShadowTexture2;
					};

					// Render Cloud SKY AO
					if (ShouldRenderCloudSkyAO(SkyLight))
					{
						const uint32 VolumetricCloudSkyAOResolution = GetVolumetricCloudSkyAOResolution(SkyLight);
						FRDGTextureRef CloudSkyAOTexture = GraphBuilder.CreateTexture(
							FRDGTextureDesc::Create2D(FIntPoint(VolumetricCloudSkyAOResolution, VolumetricCloudSkyAOResolution), PF_FloatR11G11B10,
								FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_RenderTargetable), TEXT("CloudSkyAOTexture"));

						// We need to make a copy of the parameters on CPU to morph them because the creation is deferred.
						FRenderVolumetricCloudGlobalParameters& VolumetricCloudParamsAO = *GraphBuilder.AllocParameters<FRenderVolumetricCloudGlobalParameters>();
						VolumetricCloudParamsAO = VolumetricCloudParams;	// Use the same parameter as for the directional light shadow
						VolumetricCloudParamsAO.TraceShadowmap = 0;			// Notify that this pass is for SkyAO (avoid to use another shader permutation)
						TRDGUniformBufferRef<FRenderVolumetricCloudGlobalParameters> TraceVolumetricCloudSkyAOParamsUB = GraphBuilder.CreateUniformBuffer(&VolumetricCloudParamsAO);
						TraceCloudTexture(CloudSkyAOTexture, true, TraceVolumetricCloudSkyAOParamsUB);

						if (CVarVolumetricCloudSkyAOFiltering.GetValueOnAnyThread() > 0)
						{
							const float CloudAOTextureTexelWorldSize = GetVolumetricCloudSkyAOExtentKm(SkyLight) * KilometersToCentimeters * VolumetricCloudParamsAO.VolumetricCloud.CloudSkyAOSizeInvSize.Z;
							const FVector4 CloudAOTextureTexelWorldSizeInvSize = FVector4(CloudAOTextureTexelWorldSize, CloudAOTextureTexelWorldSize, 1.0f / CloudAOTextureTexelWorldSize, 1.0f / CloudAOTextureTexelWorldSize);

							FilterTracedCloudTexture(&CloudSkyAOTexture, VolumetricCloudParamsAO.VolumetricCloud.CloudSkyAOSizeInvSize, CloudAOTextureTexelWorldSizeInvSize, true);
						}

						ConvertToUntrackedExternalTexture(GraphBuilder, CloudSkyAOTexture, ViewInfo.VolumetricCloudSkyAO, ERHIAccess::SRVMask);
					}


					// Render atmospheric lights shadow maps
					auto GenerateCloudTexture = [&](FLightSceneProxy* AtmosphericLight, int LightIndex)
					{
						FVolumetricCloudCommonShaderParameters& CloudGlobalShaderParams = CloudInfo.GetVolumetricCloudCommonShaderParameters();
						if (ShouldRenderCloudShadowmap(AtmosphericLight))
						{
							const int32 CloudShadowSpatialFiltering = FMath::Clamp(CVarVolumetricCloudShadowSpatialFiltering.GetValueOnAnyThread(), 0, 4);
							const uint32 Resolution1D = GetVolumetricCloudShadowMapResolution(AtmosphericLight);
							const FIntPoint Resolution2D = FIntPoint(Resolution1D, Resolution1D);
							const FIntPoint TracingResolution2D = CloudShadowTemporalEnabled ? FIntPoint(Resolution1D>>1, Resolution1D>>1) : FIntPoint(Resolution1D, Resolution1D);
							const EPixelFormat CloudShadowPixelFormat = PF_FloatR11G11B10;

							if (ViewInfo.ViewState)
							{
								FTemporalRenderTargetState& CloudShadowTemporalRT = ViewInfo.ViewState->VolumetricCloudShadowRenderTarget[LightIndex];
								CloudShadowTemporalRT.Initialise(Resolution2D, CloudShadowPixelFormat);
							}

							FRDGTextureRef NewCloudShadowTexture = GraphBuilder.CreateTexture(
								FRDGTextureDesc::Create2D(TracingResolution2D, CloudShadowPixelFormat,
									FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV | TexCreate_RenderTargetable), TEXT("CloudShadowTexture"));

							VolumetricCloudParams.TraceShadowmap = 1 + LightIndex;
							TRDGUniformBufferRef<FRenderVolumetricCloudGlobalParameters> TraceVolumetricCloudShadowParamsUB = GraphBuilder.CreateUniformBuffer(&VolumetricCloudParams);
							TraceCloudTexture(NewCloudShadowTexture, false, TraceVolumetricCloudShadowParamsUB);

							// Directional light shadow temporal filter only if the view has a ViewState (not a sky light capture view for instance)
							if(CloudShadowTemporalEnabled && ViewInfo.ViewState)
							{
								const float LightRotationCutCosAngle = FMath::Cos(FMath::DegreesToRadians(CVarVolumetricCloudShadowTemporalFilteringLightRotationCutHistory.GetValueOnAnyThread()));

								FCloudShadowTemporalProcessCS::FPermutationDomain Permutation;
								TShaderMapRef<FCloudShadowTemporalProcessCS> ComputeShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5), Permutation);

								FTemporalRenderTargetState& CloudShadowTemporalRT = ViewInfo.ViewState->VolumetricCloudShadowRenderTarget[LightIndex];
								FRDGTextureRef CurrentShadowTexture = CloudShadowTemporalRT.GetOrCreateCurrentRT(GraphBuilder);

								const bool bLightRotationCutHistory = FVector::DotProduct(ViewInfo.ViewState->VolumetricCloudShadowmapPreviousAtmosphericLightDir[LightIndex], AtmosphericLight->GetDirection()) < LightRotationCutCosAngle;
								const bool bHistoryValid = CloudShadowTemporalRT.GetHistoryValid() && !bLightRotationCutHistory;

								FCloudShadowTemporalProcessCS::FParameters* Parameters = GraphBuilder.AllocParameters<FCloudShadowTemporalProcessCS::FParameters>();
								Parameters->BilinearSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
								Parameters->CurrCloudShadowTexture = NewCloudShadowTexture;
								Parameters->PrevCloudShadowTexture = bHistoryValid ? CloudShadowTemporalRT.GetOrCreatePreviousRT(GraphBuilder) : BlackDummyRDG;
								Parameters->OutCloudShadowTexture = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(CurrentShadowTexture));

								if (bHistoryValid)
								{
									Parameters->PrevCloudShadowTexture = CloudShadowTemporalRT.GetOrCreatePreviousRT(GraphBuilder);
									Parameters->CurrFrameCloudShadowmapWorldToLightClipMatrixInv = CloudGlobalShaderParams.CloudShadowmapWorldToLightClipMatrixInv[LightIndex];
									Parameters->PrevFrameCloudShadowmapWorldToLightClipMatrix = ViewInfo.ViewState->VolumetricCloudShadowmapPreviousWorldToLightClipMatrix[LightIndex];
								}
								else
								{
									Parameters->PrevCloudShadowTexture = BlackDummyRDG;
									Parameters->CurrFrameCloudShadowmapWorldToLightClipMatrixInv = FMatrix::Identity;
									Parameters->PrevFrameCloudShadowmapWorldToLightClipMatrix = FMatrix::Identity;
								}

								Parameters->CurrFrameLightPos = CloudGlobalShaderParams.CloudShadowmapLightPos[LightIndex];
								Parameters->CurrFrameLightDir = CloudGlobalShaderParams.CloudShadowmapLightDir[LightIndex];

								Parameters->PrevFrameLightPos = ViewInfo.ViewState->VolumetricCloudShadowmapPreviousAtmosphericLightPos[LightIndex];
								Parameters->PrevFrameLightDir = ViewInfo.ViewState->VolumetricCloudShadowmapPreviousAtmosphericLightDir[LightIndex];
								Parameters->CloudShadowMapAnchorPointMoved = (ViewInfo.ViewState->VolumetricCloudShadowmapPreviousAnchorPoint[LightIndex] - CloudGlobalShaderParams.CloudShadowmapLightAnchorPos[LightIndex]).SizeSquared() < KINDA_SMALL_NUMBER ? 0 : 1;

								Parameters->CloudTextureSizeInvSize = VolumetricCloudParams.VolumetricCloud.CloudShadowmapSizeInvSize[LightIndex];
								Parameters->CloudTextureTracingSizeInvSize = VolumetricCloudParams.VolumetricCloud.CloudShadowmapTracingSizeInvSize[LightIndex];
								Parameters->CloudTextureTracingPixelScaleOffset = VolumetricCloudParams.VolumetricCloud.CloudShadowmapTracingPixelScaleOffset[LightIndex];
								Parameters->PreviousDataIsValid = bHistoryValid ? 1 : 0;
								Parameters->TemporalFactor = CloudShadowTemporalWeight;

								const FIntVector CloudShadowTextureSize = FIntVector(Parameters->CloudTextureSizeInvSize.X, Parameters->CloudTextureSizeInvSize.Y, 1);
								const FIntVector DispatchCount = DispatchCount.DivideAndRoundUp(FIntVector(CloudShadowTextureSize.X, CloudShadowTextureSize.Y, 1), FIntVector(8, 8, 1));
								FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("CloudDataTemporalFilter"), ComputeShader, Parameters, DispatchCount);
								CloudShadowTemporalRT.ExtractCurrentRT(GraphBuilder, CurrentShadowTexture);

								// Spatial filtering after temporal
								if (CloudShadowSpatialFiltering > 0)
								{
									FRDGTextureRef SpatiallyFilteredShadowTexture = CurrentShadowTexture;
									const float CloudShadowTextureTexelWorldSize = GetVolumetricCloudShadowMapExtentKm(AtmosphericLight) * KilometersToCentimeters * VolumetricCloudParams.VolumetricCloud.CloudShadowmapSizeInvSize[LightIndex].Z;
									const FVector4 CloudShadowTextureTexelWorldSizeInvSize = FVector4(CloudShadowTextureTexelWorldSize, CloudShadowTextureTexelWorldSize, 1.0f / CloudShadowTextureTexelWorldSize, 1.0f / CloudShadowTextureTexelWorldSize);
									for (int i = 0; i < CloudShadowSpatialFiltering; ++i)
									{
										FilterTracedCloudTexture(&SpatiallyFilteredShadowTexture, VolumetricCloudParams.VolumetricCloud.CloudShadowmapSizeInvSize[LightIndex], CloudShadowTextureTexelWorldSizeInvSize, false);
									}
									ConvertToUntrackedExternalTexture(GraphBuilder, SpatiallyFilteredShadowTexture, ViewInfo.VolumetricCloudShadowRenderTarget[LightIndex], ERHIAccess::SRVMask);
								}
								else
								{
									ViewInfo.VolumetricCloudShadowRenderTarget[LightIndex] = CloudShadowTemporalRT.CurrentRenderTarget();
								}

							}
							else
							{
								if (CloudShadowSpatialFiltering > 0)
								{
									FRDGTextureRef SpatiallyFilteredShadowTexture = NewCloudShadowTexture;
									const float CloudShadowTextureTexelWorldSize = GetVolumetricCloudShadowMapExtentKm(AtmosphericLight) * KilometersToCentimeters * VolumetricCloudParams.VolumetricCloud.CloudShadowmapSizeInvSize[LightIndex].Z;
									const FVector4 CloudShadowTextureTexelWorldSizeInvSize = FVector4(CloudShadowTextureTexelWorldSize, CloudShadowTextureTexelWorldSize, 1.0f / CloudShadowTextureTexelWorldSize, 1.0f / CloudShadowTextureTexelWorldSize);
									for (int i = 0; i < CloudShadowSpatialFiltering; ++i)
									{
										FilterTracedCloudTexture(&SpatiallyFilteredShadowTexture, VolumetricCloudParams.VolumetricCloud.CloudShadowmapSizeInvSize[LightIndex], CloudShadowTextureTexelWorldSizeInvSize, false);
									}
									ConvertToUntrackedExternalTexture(GraphBuilder, SpatiallyFilteredShadowTexture, ViewInfo.VolumetricCloudShadowRenderTarget[LightIndex], ERHIAccess::SRVMask);
								}
								else
								{
									ConvertToUntrackedExternalTexture(GraphBuilder, NewCloudShadowTexture, ViewInfo.VolumetricCloudShadowRenderTarget[LightIndex], ERHIAccess::SRVMask);
								}
							}
						}
						else
						{
							ViewInfo.VolumetricCloudShadowRenderTarget[LightIndex].SafeRelease();
							if (ViewInfo.ViewState)
							{
								ViewInfo.ViewState->VolumetricCloudShadowRenderTarget[LightIndex].Reset();
							}
						}

						if (ViewInfo.ViewState)
						{
							// Update the view previous cloud shadow matrix for next frame
							ViewInfo.ViewState->VolumetricCloudShadowmapPreviousWorldToLightClipMatrix[LightIndex] = CloudGlobalShaderParams.CloudShadowmapWorldToLightClipMatrix[LightIndex];
							ViewInfo.ViewState->VolumetricCloudShadowmapPreviousAtmosphericLightDir[LightIndex] = AtmosphericLight ? AtmosphericLight->GetDirection() : FVector::ZeroVector;
							ViewInfo.ViewState->VolumetricCloudShadowmapPreviousAtmosphericLightPos[LightIndex] = CloudGlobalShaderParams.CloudShadowmapLightPos[LightIndex];
							ViewInfo.ViewState->VolumetricCloudShadowmapPreviousAnchorPoint[LightIndex] = CloudGlobalShaderParams.CloudShadowmapLightAnchorPos[LightIndex];
						}
					};
					GenerateCloudTexture(AtmosphericLight0, 0);
					GenerateCloudTexture(AtmosphericLight1, 1);
				}
			}
			else
			{
				CleanUpCloudDataPass(GraphBuilder);
			}
		}
		else
		{
			CleanUpCloudDataPass(GraphBuilder);
		}
	}
	else
	{
		CleanUpCloudDataPass(GraphBuilder);
	}
}

FCloudRenderContext::FCloudRenderContext()
{
	TracingCoordToZbufferCoordScaleBias = FUintVector4(1, 1, 0, 0);
	NoiseFrameIndexModPattern = 0;

	bIsReflectionRendering = false;
	bIsSkyRealTimeReflectionRendering = false;
	bSkipAtmosphericLightShadowmap = false;

	bSkipAerialPerspective = false;

	bAsyncCompute = false;
	bVisualizeConservativeDensityOrDebugSampleCount = false;
}

static TRDGUniformBufferRef<FRenderVolumetricCloudGlobalParameters> CreateCloudPassUniformBuffer(FRDGBuilder& GraphBuilder, FCloudRenderContext& CloudRC)
{
	FViewInfo& MainView = *CloudRC.MainView;
	FVolumetricCloudRenderSceneInfo& CloudInfo = *CloudRC.CloudInfo;

	FRenderVolumetricCloudGlobalParameters& VolumetricCloudParams = *GraphBuilder.AllocParameters<FRenderVolumetricCloudGlobalParameters>();
	SetupDefaultRenderVolumetricCloudGlobalParameters(GraphBuilder, VolumetricCloudParams, CloudInfo, MainView);

	VolumetricCloudParams.SceneDepthTexture = GraphBuilder.RegisterExternalTexture(CloudRC.SceneDepthZ);
	VolumetricCloudParams.Light0Shadow = CloudRC.LightShadowShaderParams0;
	VolumetricCloudParams.CloudShadowTexture0 = CloudRC.VolumetricCloudShadowTexture[0];
	VolumetricCloudParams.CloudShadowTexture1 = CloudRC.VolumetricCloudShadowTexture[1];
	VolumetricCloudParams.TracingCoordToZbufferCoordScaleBias = CloudRC.TracingCoordToZbufferCoordScaleBias;
	VolumetricCloudParams.NoiseFrameIndexModPattern = CloudRC.NoiseFrameIndexModPattern;
	VolumetricCloudParams.IsReflectionRendering = CloudRC.bIsReflectionRendering ? 1 : 0;

	if (CloudRC.bShouldViewRenderVolumetricRenderTarget && MainView.ViewState)
	{
		FVolumetricRenderTargetViewStateData& VRT = MainView.ViewState->VolumetricCloudRenderTarget;
		VolumetricCloudParams.OpaqueIntersectionMode = VRT.GetMode()== 2 ? 0 : 2;	// intersect with opaque only if not using mode 2 (full res distant cloud updating 1 out 4x4 pixels)
	}
	else
	{
		VolumetricCloudParams.OpaqueIntersectionMode = 2;	// always intersect with opaque
	}

	if (CloudRC.bIsReflectionRendering)
	{
		const float BaseReflectionRaySampleCount = 10.0f;
		const float BaseReflectionShadowRaySampleCount = 3.0f;

		VolumetricCloudParams.VolumetricCloud.SampleCountMax = FMath::Max(2.0f, FMath::Min(
			BaseReflectionRaySampleCount * CloudInfo.GetVolumetricCloudSceneProxy().ReflectionSampleCountScale,
			CVarVolumetricCloudReflectionRaySampleMaxCount.GetValueOnAnyThread()));
		VolumetricCloudParams.VolumetricCloud.ShadowSampleCountMax = FMath::Max(2.0f, FMath::Min(
			BaseReflectionShadowRaySampleCount * CloudInfo.GetVolumetricCloudSceneProxy().ShadowReflectionSampleCountScale,
			CVarVolumetricCloudShadowReflectionRaySampleMaxCount.GetValueOnAnyThread()));
	}

	VolumetricCloudParams.EnableAerialPerspectiveSampling = CloudRC.bSkipAerialPerspective ? 0 : 1;
	VolumetricCloudParams.EnableDistantSkyLightSampling = CVarVolumetricCloudEnableDistantSkyLightSampling.GetValueOnAnyThread() > 0 ? 1 : 0;
	VolumetricCloudParams.EnableAtmosphericLightsSampling = CVarVolumetricCloudEnableAtmosphericLightsSampling.GetValueOnAnyThread() > 0 ? 1 : 0;

	const FRDGTextureDesc& Desc = CloudRC.RenderTargets.Output[0].GetTexture()->Desc;
	VolumetricCloudParams.OutputSizeInvSize = FVector4(Desc.Extent.X, Desc.Extent.Y, 1.0f / Desc.Extent.X, 1.0f / Desc.Extent.Y);

	SetupRenderVolumetricCloudGlobalParametersHZB(GraphBuilder, MainView, VolumetricCloudParams);

	if (CloudRC.bIsSkyRealTimeReflectionRendering)
	{
		VolumetricCloudParams.FogStruct.ApplyVolumetricFog = 0;		// No valid camera froxel volume available.
		VolumetricCloudParams.OpaqueIntersectionMode = 0;			// No depth buffer is available
		VolumetricCloudParams.HasValidHZB = 0;						// No valid HZB is available
	}

	VolumetricCloudParams.ClampRayTToDepthBufferPostHZB = CloudRC.bShouldViewRenderVolumetricRenderTarget ? 0 : 1;

	return GraphBuilder.CreateUniformBuffer(&VolumetricCloudParams);
}

static void GetOutputTexturesWithFallback(FRDGBuilder& GraphBuilder, FCloudRenderContext& CloudRC, FRDGTextureRef& CloudColorCubeTexture, FRDGTextureRef& CloudColorTexture, FRDGTextureRef& CloudDepthTexture)
{
	CloudColorCubeTexture = CloudRC.RenderTargets[0].GetTexture();
	if (!CloudColorCubeTexture || !CloudColorCubeTexture->Desc.IsTextureCube())
	{
		CloudColorCubeTexture = GraphBuilder.CreateTexture(
			FRDGTextureDesc::CreateCube(1, PF_FloatRGBA, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV),
			TEXT("CloudColorCubeDummy"));
	}

	CloudColorTexture = CloudRC.RenderTargets[0].GetTexture();
	if (!CloudColorTexture || CloudColorTexture->Desc.IsTextureCube())
	{
		CloudColorTexture = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(FIntPoint(1, 1), PF_FloatRGBA, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV),
			TEXT("CloudColorDummy"));
	}

	CloudDepthTexture = CloudRC.RenderTargets[1].GetTexture();
	if (!CloudDepthTexture)
	{
		CloudDepthTexture = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(FIntPoint(1, 1), PF_G16R16F, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV),
			TEXT("CloudDepthDummy"));
	}
}

void FSceneRenderer::RenderVolumetricCloudsInternal(FRDGBuilder& GraphBuilder, FCloudRenderContext& CloudRC)
{
	check(CloudRC.MainView);
	check(CloudRC.CloudInfo);
	check(CloudRC.CloudVolumeMaterialProxy);

	FMaterialRenderProxy* CloudVolumeMaterialProxy = CloudRC.CloudVolumeMaterialProxy;

	// Copy parameters to lambda
	const bool bShouldViewRenderVolumetricRenderTarget = CloudRC.bShouldViewRenderVolumetricRenderTarget;
	const bool bSkipAtmosphericLightShadowmap = CloudRC.bSkipAtmosphericLightShadowmap;
	const bool bSecondAtmosphereLightEnabled = CloudRC.bSecondAtmosphereLightEnabled;
	const bool bVisualizeConservativeDensityOrDebugSampleCount = CloudRC.bVisualizeConservativeDensityOrDebugSampleCount;
	const FRDGTextureDesc& Desc = CloudRC.RenderTargets.Output[0].GetTexture()->Desc;
	FViewInfo& MainView = *CloudRC.MainView;
	TUniformBufferRef<FViewUniformShaderParameters> ViewUniformBuffer = CloudRC.ViewUniformBuffer;
	TRDGUniformBufferRef<FRenderVolumetricCloudGlobalParameters> CloudPassUniformBuffer = CreateCloudPassUniformBuffer(GraphBuilder, CloudRC);

	if (ShouldUseComputeForCloudTracing(Scene->GetFeatureLevel()))
	{
		FRDGTextureRef CloudColorCubeTexture;
		FRDGTextureRef CloudColorTexture;
		FRDGTextureRef CloudDepthTexture;
		GetOutputTexturesWithFallback(GraphBuilder, CloudRC, CloudColorCubeTexture, CloudColorTexture, CloudDepthTexture);

		FRenderVolumetricCloudRenderViewCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRenderVolumetricCloudRenderViewCS::FParameters>();
		PassParameters->OutputViewRect = FVector4(0.f, 0.f, Desc.Extent.X, Desc.Extent.Y);
		PassParameters->bBlendCloudColor = !bShouldViewRenderVolumetricRenderTarget && !bVisualizeConservativeDensityOrDebugSampleCount;
		PassParameters->TargetCubeFace = CloudRC.RenderTargets.Output[0].GetArraySlice();
		PassParameters->VolumetricCloudRenderViewParamsUB = CloudPassUniformBuffer;
		PassParameters->OutCloudColor = GraphBuilder.CreateUAV(CloudColorTexture);
		PassParameters->OutCloudDepth = GraphBuilder.CreateUAV(CloudDepthTexture);
		PassParameters->OutCloudColorCube = GraphBuilder.CreateUAV(CloudColorCubeTexture);

		const FMaterialRenderProxy* MaterialRenderProxy = nullptr;
		const FMaterial* MaterialResource = &CloudVolumeMaterialProxy->GetMaterialWithFallback(Scene->GetFeatureLevel(), MaterialRenderProxy);
		MaterialRenderProxy = MaterialRenderProxy ? MaterialRenderProxy : CloudVolumeMaterialProxy;

		typename FRenderVolumetricCloudRenderViewCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<typename FRenderVolumetricCloudRenderViewCS::FCloudPerSampleAtmosphereTransmittance>(!bVisualizeConservativeDensityOrDebugSampleCount && ShouldUsePerSampleAtmosphereTransmittance(Scene, &MainView));
		PermutationVector.Set<typename FRenderVolumetricCloudRenderViewCS::FCloudSampleAtmosphericLightShadowmap>(!bVisualizeConservativeDensityOrDebugSampleCount && !bSkipAtmosphericLightShadowmap && CVarVolumetricCloudShadowSampleAtmosphericLightShadowmap.GetValueOnRenderThread() > 0);
		PermutationVector.Set<typename FRenderVolumetricCloudRenderViewCS::FCloudSampleSecondLight>(!bVisualizeConservativeDensityOrDebugSampleCount && bSecondAtmosphereLightEnabled);
		PermutationVector.Set<typename FRenderVolumetricCloudRenderViewCS::FCloudSampleCountDebugMode>(bVisualizeConservativeDensityOrDebugSampleCount);

		TShaderRef<FRenderVolumetricCloudRenderViewCS> ComputeShader = MaterialResource->GetShader<FRenderVolumetricCloudRenderViewCS>(&FLocalVertexFactory::StaticType, PermutationVector, false);

		const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(Desc.Extent, FIntPoint(FRenderVolumetricCloudRenderViewCS::ThreadGroupSizeX, FRenderVolumetricCloudRenderViewCS::ThreadGroupSizeY));

		if (ComputeShader.IsValid())
		{
			ClearUnusedGraphResources(ComputeShader, PassParameters);
		}

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("CloudView (CS) %dx%d", Desc.Extent.X, Desc.Extent.Y),
			PassParameters,
			CloudRC.bAsyncCompute ? ERDGPassFlags::AsyncCompute : ERDGPassFlags::Compute,
			[LocalScene = Scene, MaterialRenderProxy, MaterialResource, ViewUniformBuffer, PassParameters, ComputeShader, GroupCount](FRHIComputeCommandList& RHICmdList)
			{
				if (MaterialResource->GetMaterialDomain() != MD_Volume)
				{
					return;
				}

				FMeshPassProcessorRenderState DrawRenderState;
				DrawRenderState.SetViewUniformBuffer(ViewUniformBuffer);

				FMeshMaterialShaderElementData ShaderElementData;
				ShaderElementData.FadeUniformBuffer = GDistanceCullFadedInUniformBuffer.GetUniformBufferRHI();
				ShaderElementData.DitherUniformBuffer = GDitherFadedInUniformBuffer.GetUniformBufferRHI();

				FMeshProcessorShaders PassShaders;
				PassShaders.ComputeShader = ComputeShader;

				FMeshDrawShaderBindings ShaderBindings;
				ShaderBindings.Initialize(PassShaders);

				int32 DataOffset = 0;
				FMeshDrawSingleShaderBindings SingleShaderBindings = ShaderBindings.GetSingleShaderBindings(SF_Compute, DataOffset);
				ComputeShader->GetShaderBindings(LocalScene, LocalScene->GetFeatureLevel(), nullptr, *MaterialRenderProxy, *MaterialResource, DrawRenderState, ShaderElementData, SingleShaderBindings);

				ShaderBindings.Finalize(&PassShaders);

				FRHIComputeShader* ComputeShaderRHI = ComputeShader.GetComputeShader();
				RHICmdList.SetComputeShader(ComputeShaderRHI);
				ShaderBindings.SetOnCommandList(RHICmdList, ComputeShaderRHI);
				SetShaderParameters(RHICmdList, ComputeShader, ComputeShaderRHI, *PassParameters);
				RHICmdList.DispatchComputeShader(GroupCount.X, GroupCount.Y, GroupCount.Z);
				UnsetShaderUAVs(RHICmdList, ComputeShader, ComputeShaderRHI);
			});
	}
	else
	{
		FRenderVolumetricCloudRenderViewParametersPS* RenderViewPassParameters = GraphBuilder.AllocParameters<FRenderVolumetricCloudRenderViewParametersPS>();
		RenderViewPassParameters->VolumetricCloudRenderViewParamsUB = CloudPassUniformBuffer;
		RenderViewPassParameters->CloudShadowTexture0 = CloudRC.VolumetricCloudShadowTexture[0];
		RenderViewPassParameters->CloudShadowTexture1 = CloudRC.VolumetricCloudShadowTexture[1];
		RenderViewPassParameters->RenderTargets = CloudRC.RenderTargets;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("CloudView (PS) %dx%d", Desc.Extent.X, Desc.Extent.Y),
			RenderViewPassParameters,
			ERDGPassFlags::Raster,
			[RenderViewPassParameters, Scene = Scene, &MainView, ViewUniformBuffer, bVisualizeConservativeDensityOrDebugSampleCount,
			bShouldViewRenderVolumetricRenderTarget, CloudVolumeMaterialProxy, bSkipAtmosphericLightShadowmap, bSecondAtmosphereLightEnabled](FRHICommandListImmediate& RHICmdList)
			{
				DrawDynamicMeshPass(MainView, RHICmdList,
					[&MainView, ViewUniformBuffer, bShouldViewRenderVolumetricRenderTarget, bSkipAtmosphericLightShadowmap, bSecondAtmosphereLightEnabled,
					bVisualizeConservativeDensityOrDebugSampleCount, CloudVolumeMaterialProxy](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
					{
						FVolumetricCloudRenderViewMeshProcessor PassMeshProcessor(
							MainView.Family->Scene->GetRenderScene(), &MainView, ViewUniformBuffer, bShouldViewRenderVolumetricRenderTarget,
							bSkipAtmosphericLightShadowmap, bSecondAtmosphereLightEnabled, bVisualizeConservativeDensityOrDebugSampleCount, DynamicMeshPassContext);

						FMeshBatch LocalSingleTriangleMesh;
						GetSingleTriangleMeshBatch(LocalSingleTriangleMesh, CloudVolumeMaterialProxy, MainView.GetFeatureLevel());

						const FPrimitiveSceneProxy* PrimitiveSceneProxy = nullptr;
						const uint64 DefaultBatchElementMask = ~0ull;
						PassMeshProcessor.AddMeshBatch(LocalSingleTriangleMesh, DefaultBatchElementMask, PrimitiveSceneProxy);
					});
			});
	}
}

bool FSceneRenderer::RenderVolumetricCloud(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureShaderParameters& SceneTextures,
	bool bSkipVolumetricRenderTarget,
	bool bSkipPerPixelTracing,
	FRDGTextureMSAA SceneColorTexture,
	FRDGTextureMSAA SceneDepthTexture,
	bool bAsyncCompute)
{
	check(ShouldRenderVolumetricCloud(Scene, ViewFamily.EngineShowFlags)); // This should not be called if we should not render SkyAtmosphere

	FVolumetricCloudRenderSceneInfo& CloudInfo = *Scene->GetVolumetricCloudSceneInfo();
	FVolumetricCloudSceneProxy& CloudSceneProxy = CloudInfo.GetVolumetricCloudSceneProxy();

	FLightSceneInfo* AtmosphericLight0Info = Scene->AtmosphereLights[0];
	FLightSceneProxy* AtmosphericLight0 = AtmosphericLight0Info ? AtmosphericLight0Info->Proxy : nullptr;
	FSkyLightSceneProxy* SkyLight = Scene->SkyLight;
	bool bAsyncComputeUsed = false;

	if (CloudSceneProxy.GetCloudVolumeMaterial())
	{
		FMaterialRenderProxy* CloudVolumeMaterialProxy = CloudSceneProxy.GetCloudVolumeMaterial()->GetRenderProxy();
		if (CloudVolumeMaterialProxy->GetIncompleteMaterialWithFallback(ViewFamily.GetFeatureLevel()).GetMaterialDomain() == MD_Volume)
		{
			RDG_EVENT_SCOPE(GraphBuilder, "VolumetricCloud");
			RDG_GPU_STAT_SCOPE(GraphBuilder, VolumetricCloud);

			FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);

			TRefCountPtr<IPooledRenderTarget> SceneDepthZ = SceneContext.SceneDepthZ;
			TRefCountPtr<IPooledRenderTarget> BlackDummy = GSystemTextures.BlackDummy;
			FRDGTextureRef BlackDummyRDG = GraphBuilder.RegisterExternalTexture(BlackDummy);

			FCloudRenderContext CloudRC;
			CloudRC.CloudInfo = &CloudInfo;
			CloudRC.CloudVolumeMaterialProxy= CloudVolumeMaterialProxy;
			CloudRC.bSkipAtmosphericLightShadowmap = !GetVolumetricCloudReceiveAtmosphericLightShadowmap(AtmosphericLight0);
			CloudRC.bSecondAtmosphereLightEnabled = Scene->IsSecondAtmosphereLightEnabled();

			const bool DebugCloudShadowMap = CVarVolumetricCloudShadowMapDebug.GetValueOnRenderThread() && ShouldRenderCloudShadowmap(AtmosphericLight0);
			const bool DebugCloudSkyAO = CVarVolumetricCloudSkyAODebug.GetValueOnRenderThread() && ShouldRenderCloudSkyAO(SkyLight);

			struct FLocalCloudView
			{
				FViewInfo* ViewInfo;
				bool bShouldViewRenderVolumetricCloudRenderTarget;
				bool bEnableAerialPerspectiveSampling;
				bool bShouldUseHighQualityAerialPerspective;
				bool bVisualizeConservativeDensityOrDebugSampleCount;
			};
			TArray<FLocalCloudView, TInlineAllocator<2>> ViewsToProcess;
			
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
			{
				FViewInfo& ViewInfo = Views[ViewIndex];

				bool bShouldViewRenderVolumetricCloudRenderTarget = ShouldViewRenderVolumetricCloudRenderTarget(ViewInfo); // not used by reflection captures for instance
				if ((bShouldViewRenderVolumetricCloudRenderTarget && bSkipVolumetricRenderTarget) || (!bShouldViewRenderVolumetricCloudRenderTarget && bSkipPerPixelTracing))
				{
					continue;
				}

				const bool bVisualizeConservativeDensityOrDebugSampleCount = ShouldViewVisualizeVolumetricCloudConservativeDensity(ViewInfo, ViewFamily.EngineShowFlags) || GetVolumetricCloudDebugSampleCountMode(ViewFamily.EngineShowFlags)>0;
				const bool bEnableAerialPerspectiveSampling = CVarVolumetricCloudEnableAerialPerspectiveSampling.GetValueOnAnyThread() > 0;
				const bool bShouldUseHighQualityAerialPerspective =
					bEnableAerialPerspectiveSampling
					&& Scene->HasSkyAtmosphere()
					&& CVarVolumetricCloudHighQualityAerialPerspective.GetValueOnAnyThread() > 0
					&& !ViewInfo.bIsReflectionCapture
					&& !bVisualizeConservativeDensityOrDebugSampleCount;

				if (bAsyncCompute
					&& bShouldViewRenderVolumetricCloudRenderTarget
					&& !bShouldUseHighQualityAerialPerspective
					&& !DebugCloudShadowMap
					&& !DebugCloudSkyAO)
				{
					// TODO: see whether high quality AP rendering can use async compute
					bAsyncComputeUsed = true;
				}
				else if (bAsyncCompute)
				{
					// Skip if async compute is requested but cannot fulfill
					continue;
				}

				FLocalCloudView& ViewToProcess = ViewsToProcess[ViewsToProcess.AddUninitialized()];
				ViewToProcess.ViewInfo = &ViewInfo;
				ViewToProcess.bShouldViewRenderVolumetricCloudRenderTarget = bShouldViewRenderVolumetricCloudRenderTarget;
				ViewToProcess.bEnableAerialPerspectiveSampling = bEnableAerialPerspectiveSampling;
				ViewToProcess.bShouldUseHighQualityAerialPerspective = bShouldUseHighQualityAerialPerspective;
				ViewToProcess.bVisualizeConservativeDensityOrDebugSampleCount = bVisualizeConservativeDensityOrDebugSampleCount;
			}

			for (int32 ViewIndex = 0; ViewIndex < ViewsToProcess.Num(); ViewIndex++)
			{
				FLocalCloudView& ViewToProcess = ViewsToProcess[ViewIndex];
				FViewInfo& ViewInfo = *ViewToProcess.ViewInfo;

				CloudRC.MainView = ViewToProcess.ViewInfo;
				CloudRC.bAsyncCompute = bAsyncCompute;
				CloudRC.bVisualizeConservativeDensityOrDebugSampleCount = ViewToProcess.bVisualizeConservativeDensityOrDebugSampleCount;

				const bool bShouldViewRenderVolumetricCloudRenderTarget = ViewToProcess.bShouldViewRenderVolumetricCloudRenderTarget;
				const bool bEnableAerialPerspectiveSampling = ViewToProcess.bEnableAerialPerspectiveSampling;
				const bool bShouldUseHighQualityAerialPerspective = ViewToProcess.bShouldUseHighQualityAerialPerspective;

				CloudRC.bShouldViewRenderVolumetricRenderTarget = bShouldViewRenderVolumetricCloudRenderTarget;
				CloudRC.ViewUniformBuffer = bShouldViewRenderVolumetricCloudRenderTarget ? ViewInfo.VolumetricRenderTargetViewUniformBuffer : ViewInfo.ViewUniformBuffer;

				CloudRC.bSkipAerialPerspective = !bEnableAerialPerspectiveSampling || bShouldUseHighQualityAerialPerspective; // Skip AP on clouds if we are going to trace it separately in a second pass
				CloudRC.bIsReflectionRendering = ViewInfo.bIsReflectionCapture;

				FRDGTextureRef IntermediateRT = nullptr;
				FRDGTextureRef DestinationRT = nullptr;
				FRDGTextureRef DestinationRTDepth = nullptr;
				CloudRC.TracingCoordToZbufferCoordScaleBias = FUintVector4(1, 1, 0, 0);
				CloudRC.NoiseFrameIndexModPattern = ViewInfo.CachedViewUniformShaderParameters->StateFrameIndexMod8;

				if (bShouldViewRenderVolumetricCloudRenderTarget)
				{
					FVolumetricRenderTargetViewStateData& VRT = ViewInfo.ViewState->VolumetricCloudRenderTarget;
					DestinationRT = VRT.GetOrCreateVolumetricTracingRT(GraphBuilder);
					DestinationRTDepth = VRT.GetOrCreateVolumetricTracingRTDepth(GraphBuilder);

					if (bShouldUseHighQualityAerialPerspective)
					{
						FIntPoint IntermadiateTargetResolution = FIntPoint(DestinationRT->Desc.GetSize().X, DestinationRT->Desc.GetSize().Y);
						IntermediateRT = GraphBuilder.CreateTexture(
							FRDGTextureDesc::Create2D(IntermadiateTargetResolution, PF_FloatRGBA, FClearValueBinding(FLinearColor(63000.0f, 63000.0f, 63000.0f, 63000.0f)),
								TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV), TEXT("RGBCloudIntermediate"));
					}

					// No action because we only need to render volumetric clouds so we do not blend in that render target.
					// When we have more elements rendered in that target later, we can clear it to default and blend.
					CloudRC.RenderTargets[0] = FRenderTargetBinding(bShouldUseHighQualityAerialPerspective ? IntermediateRT : DestinationRT, ERenderTargetLoadAction::ENoAction);
					CloudRC.RenderTargets[1] = FRenderTargetBinding(DestinationRTDepth, ERenderTargetLoadAction::ENoAction);
					CloudRC.TracingCoordToZbufferCoordScaleBias = VRT.GetTracingCoordToZbufferCoordScaleBias();
					// Also take into account the view rect min to be able to read correct depth
					CloudRC.TracingCoordToZbufferCoordScaleBias.Z += ViewInfo.CachedViewUniformShaderParameters->ViewRectMin.X;
					CloudRC.TracingCoordToZbufferCoordScaleBias.W += ViewInfo.CachedViewUniformShaderParameters->ViewRectMin.Y / ((VRT.GetMode() == 0 || VRT.GetMode() == 3) ? 2 : 1);
					CloudRC.NoiseFrameIndexModPattern = VRT.GetNoiseFrameIndexModPattern();

					check(VRT.GetMode() != 0 || CloudRC.bVisualizeConservativeDensityOrDebugSampleCount || ViewInfo.HalfResDepthSurfaceCheckerboardMinMax.IsValid());
					CloudRC.SceneDepthZ = (VRT.GetMode() == 0 || VRT.GetMode() == 3) ? ViewInfo.HalfResDepthSurfaceCheckerboardMinMax : SceneDepthZ;
				}
				else
				{
					DestinationRT = GraphBuilder.RegisterExternalTexture(SceneContext.GetSceneColor(), TEXT("SceneColor"));
					const FIntVector RtSize = SceneContext.GetSceneColor()->GetDesc().GetSize();

					if (bShouldUseHighQualityAerialPerspective)
					{
						FIntPoint IntermadiateTargetResolution = FIntPoint(RtSize.X, RtSize.Y);
						IntermediateRT = GraphBuilder.CreateTexture(
							FRDGTextureDesc::Create2D(IntermadiateTargetResolution, PF_FloatRGBA, FClearValueBinding(FLinearColor(0.0f, 0.0f, 0.0f, 1.0f)),
								TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV), TEXT("RGBCloudIntermediate"));
					}

					// Create a texture for cloud depth data that will be dropped out just after.
					// This texture must also match the MSAA sample count of the color buffer in case this direct to SceneColor rendering happens 
					// while capturing a non-real-time sky light. This is because, in this case, the capture scene rendering happens in the scene color which can have MSAA NumSamples > 1.
					uint8 DepthNumMips = 1;
					uint8 DepthNumSamples = bShouldUseHighQualityAerialPerspective ? IntermediateRT->Desc.NumSamples : DestinationRT->Desc.NumSamples;
					ETextureCreateFlags DepthTexCreateFlags = ETextureCreateFlags(TexCreate_ShaderResource | TexCreate_RenderTargetable | (DepthNumSamples > 1 ? TexCreate_None : TexCreate_UAV));
					DestinationRTDepth = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(FIntPoint(RtSize.X, RtSize.Y), PF_G16R16F, FClearValueBinding::Black,
						DepthTexCreateFlags, DepthNumMips, DepthNumSamples), TEXT("Cloud.DummyDepth"));

					if (bShouldUseHighQualityAerialPerspective && ShouldUseComputeForCloudTracing(Scene->GetFeatureLevel()))
					{
						// If using the compute path, we then need to clear the intermediate render target manually as RDG won't do it for us in this case.
						AddClearRenderTargetPass(GraphBuilder, IntermediateRT);
						AddClearRenderTargetPass(GraphBuilder, DestinationRTDepth);
					}

					CloudRC.RenderTargets[0] = FRenderTargetBinding(bShouldUseHighQualityAerialPerspective ? IntermediateRT : DestinationRT, bShouldUseHighQualityAerialPerspective ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad);
					CloudRC.RenderTargets[1] = FRenderTargetBinding(DestinationRTDepth, bShouldUseHighQualityAerialPerspective ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ENoAction);

					CloudRC.SceneDepthZ = SceneDepthZ;
				}

				if (CloudRC.bVisualizeConservativeDensityOrDebugSampleCount)
				{
					CloudRC.SceneDepthZ = (bool)ERHIZBuffer::IsInverted ? GSystemTextures.BlackDummy : GSystemTextures.WhiteDummy;
				}

				const FProjectedShadowInfo* ProjectedShadowInfo0 = nullptr;
				if (AtmosphericLight0Info)
				{
					ProjectedShadowInfo0 = GetLastCascadeShadowInfo(AtmosphericLight0, VisibleLightInfos[AtmosphericLight0Info->Id]);
				}
				if (!CloudRC.bSkipAtmosphericLightShadowmap && AtmosphericLight0 && ProjectedShadowInfo0)
				{
					SetVolumeShadowingShaderParameters(CloudRC.LightShadowShaderParams0, ViewInfo, AtmosphericLight0Info, ProjectedShadowInfo0, INDEX_NONE);
				}
				else
				{
					SetVolumeShadowingDefaultShaderParameters(CloudRC.LightShadowShaderParams0);
				}
				// Cannot nest a global buffer into another one and we are limited to only one PassUniformBuffer on PassDrawRenderState.
				//TUniformBufferRef<FVolumeShadowingShaderParametersGlobal0> LightShadowShaderParams0UniformBuffer = TUniformBufferRef<FVolumeShadowingShaderParametersGlobal0>::CreateUniformBufferImmediate(LightShadowShaderParams0, UniformBuffer_SingleFrame);

				FCloudShadowAOData CloudShadowAOData;
				GetCloudShadowAOData(&CloudInfo, ViewInfo, GraphBuilder, CloudShadowAOData);
				CloudRC.VolumetricCloudShadowTexture[0] = CloudShadowAOData.VolumetricCloudShadowMap[0];
				CloudRC.VolumetricCloudShadowTexture[1] = CloudShadowAOData.VolumetricCloudShadowMap[1];

				RenderVolumetricCloudsInternal(GraphBuilder, CloudRC);

				// Render high quality sky light shaft on clouds.
				if (bShouldUseHighQualityAerialPerspective)
				{
					RDG_EVENT_SCOPE(GraphBuilder, "HighQualityAerialPerspectiveOnCloud");

					FSkyAtmosphereRenderSceneInfo& SkyInfo = *Scene->GetSkyAtmosphereSceneInfo();
					const FSkyAtmosphereSceneProxy& SkyAtmosphereSceneProxy = SkyInfo.GetSkyAtmosphereSceneProxy();
					const FAtmosphereSetup& AtmosphereSetup = SkyAtmosphereSceneProxy.GetAtmosphereSetup();

					FSkyAtmosphereRenderContext SkyRC;
					SkyRC.bFastSky = false;
					SkyRC.bFastAerialPerspective = false;
					SkyRC.bFastAerialPerspectiveDepthTest = false;
					SkyRC.bSecondAtmosphereLightEnabled = Scene->IsSecondAtmosphereLightEnabled();

					SkyAtmosphereLightShadowData LightShadowData;
					SkyRC.bShouldSampleOpaqueShadow = ShouldSkySampleAtmosphereLightsOpaqueShadow(*Scene, VisibleLightInfos, LightShadowData);
					SkyRC.bUseDepthBoundTestIfPossible = false;
					SkyRC.bForceRayMarching = true;				// We do not have any valid view LUT
					SkyRC.bDepthReadDisabled = true;
					SkyRC.bDisableBlending = bShouldViewRenderVolumetricCloudRenderTarget ? true : false;

					SkyRC.TransmittanceLut = GraphBuilder.RegisterExternalTexture(SkyInfo.GetTransmittanceLutTexture());
					SkyRC.MultiScatteredLuminanceLut = GraphBuilder.RegisterExternalTexture(SkyInfo.GetMultiScatteredLuminanceLutTexture());

					// Select the AerialPersepctiveOnCloud mode and set required parameters.
					SkyRC.bAPOnCloudMode = true;
					SkyRC.VolumetricCloudDepthTexture = DestinationRTDepth;
					SkyRC.InputCloudLuminanceTransmittanceTexture = IntermediateRT;
					SkyRC.RenderTargets[0] = FRenderTargetBinding(DestinationRT, ERenderTargetLoadAction::ENoAction);

					SkyRC.ViewMatrices = &ViewInfo.ViewMatrices;
					SkyRC.ViewUniformBuffer = bShouldViewRenderVolumetricCloudRenderTarget ? ViewInfo.VolumetricRenderTargetViewUniformBuffer : ViewInfo.ViewUniformBuffer;

					SkyRC.Viewport = ViewInfo.ViewRect;
					SkyRC.bLightDiskEnabled = !ViewInfo.bIsReflectionCapture;
					SkyRC.AerialPerspectiveStartDepthInCm = GetValidAerialPerspectiveStartDepthInCm(ViewInfo, SkyAtmosphereSceneProxy);
					SkyRC.NearClippingDistance = ViewInfo.NearClippingDistance;
					SkyRC.FeatureLevel = ViewInfo.FeatureLevel;

					SkyRC.bRenderSkyPixel = false;

					if (ViewInfo.SkyAtmosphereViewLutTexture && ViewInfo.SkyAtmosphereCameraAerialPerspectiveVolume)
					{
						SkyRC.SkyAtmosphereViewLutTexture = GraphBuilder.RegisterExternalTexture(ViewInfo.SkyAtmosphereViewLutTexture);
						SkyRC.SkyAtmosphereCameraAerialPerspectiveVolume = GraphBuilder.RegisterExternalTexture(ViewInfo.SkyAtmosphereCameraAerialPerspectiveVolume);
					}
					else
					{
						SkyRC.SkyAtmosphereViewLutTexture = GSystemTextures.GetBlackDummy(GraphBuilder);
						SkyRC.SkyAtmosphereCameraAerialPerspectiveVolume = GSystemTextures.GetVolumetricBlackDummy(GraphBuilder);
					}

					GetSkyAtmosphereLightsUniformBuffers(SkyRC.LightShadowShaderParams0UniformBuffer, SkyRC.LightShadowShaderParams1UniformBuffer,
						LightShadowData, ViewInfo, SkyRC.bShouldSampleOpaqueShadow, UniformBuffer_SingleDraw);

					SkyRC.bShouldSampleCloudShadow = CloudShadowAOData.bShouldSampleCloudShadow;
					SkyRC.VolumetricCloudShadowMap[0] = CloudShadowAOData.VolumetricCloudShadowMap[0];
					SkyRC.VolumetricCloudShadowMap[1] = CloudShadowAOData.VolumetricCloudShadowMap[1];
					SkyRC.bShouldSampleCloudSkyAO = CloudShadowAOData.bShouldSampleCloudSkyAO;
					SkyRC.VolumetricCloudSkyAO = CloudShadowAOData.VolumetricCloudSkyAO;

					RenderSkyAtmosphereInternal(GraphBuilder, SceneTextures, SkyRC);
				}

				if (DebugCloudShadowMap || DebugCloudSkyAO)
				{
					FViewElementPDI ShadowFrustumPDI(&ViewInfo, nullptr, nullptr);

					FRenderVolumetricCloudGlobalParameters VolumetricCloudParams;
					SetupDefaultRenderVolumetricCloudGlobalParameters(GraphBuilder, VolumetricCloudParams, CloudInfo, ViewInfo);

					auto DebugCloudTexture = [&](FDrawDebugCloudShadowCS::FParameters* Parameters)
					{
						if (ShaderDrawDebug::IsShaderDrawDebugEnabled(ViewInfo))
						{
							FDrawDebugCloudShadowCS::FPermutationDomain Permutation;
							TShaderMapRef<FDrawDebugCloudShadowCS> ComputeShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5), Permutation);

							ShaderDrawDebug::SetParameters(GraphBuilder, ViewInfo.ShaderDrawData, Parameters->ShaderDrawParameters);

							const FIntVector CloudShadowTextureSize = Parameters->CloudTracedTexture->Desc.GetSize();
							const FIntVector DispatchCount = DispatchCount.DivideAndRoundUp(FIntVector(CloudShadowTextureSize.X, CloudShadowTextureSize.Y, 1), FIntVector(8, 8, 1));
							FComputeShaderUtils::AddPass( GraphBuilder, RDG_EVENT_NAME("DrawDebugCloudShadow"), ComputeShader, Parameters, DispatchCount);
						}
					};

					if (DebugCloudShadowMap)
					{
						const int DebugLightIndex = 0;	// only debug atmospheric light 0 for now
						const int32 CloudShadowmapSampleCount = VolumetricCloudParams.VolumetricCloud.CloudShadowmapSampleCount[DebugLightIndex].X;

						AddDrawCanvasPass(GraphBuilder, {}, ViewInfo, FScreenPassRenderTarget(SceneColorTexture.Target, ViewInfo.ViewRect, ERenderTargetLoadAction::ELoad), [CloudShadowmapSampleCount, &ViewInfo](FCanvas& Canvas)
						{
							const float ViewPortWidth = float(ViewInfo.ViewRect.Width());
							const float ViewPortHeight = float(ViewInfo.ViewRect.Height());
							FLinearColor TextColor(1.0f, 0.5f, 0.0f);
							FString Text = FString::Printf(TEXT("Shadow Sample Count = %.1f"), CloudShadowmapSampleCount);
							Canvas.DrawShadowedString(0.05f, ViewPortHeight * 0.4f, *Text, GetStatsFont(), TextColor);
						});

						DrawFrustumWireframe(&ShadowFrustumPDI, VolumetricCloudParams.VolumetricCloud.CloudShadowmapWorldToLightClipMatrixInv[DebugLightIndex], FColor::Orange, 0);
						FDrawDebugCloudShadowCS::FParameters* Parameters = GraphBuilder.AllocParameters<FDrawDebugCloudShadowCS::FParameters>();
						Parameters->CloudTracedTexture = CloudRC.VolumetricCloudShadowTexture[DebugLightIndex];
						Parameters->CloudTraceDirection = VolumetricCloudParams.VolumetricCloud.CloudShadowmapLightDir[DebugLightIndex];

						uint32 CloudShadowmapResolution = CloudRC.VolumetricCloudShadowTexture[DebugLightIndex]->Desc.Extent.X;
						const float CloudShadowmapResolutionInv = 1.0f / CloudShadowmapResolution;
						Parameters->CloudTextureSizeInvSize = FVector4(CloudShadowmapResolution, CloudShadowmapResolution, CloudShadowmapResolutionInv, CloudShadowmapResolutionInv);

						Parameters->CloudWorldToLightClipMatrixInv = VolumetricCloudParams.VolumetricCloud.CloudShadowmapWorldToLightClipMatrixInv[DebugLightIndex];
						DebugCloudTexture(Parameters);
					}

					if (DebugCloudSkyAO && ViewInfo.VolumetricCloudSkyAO.IsValid())
					{
						DrawFrustumWireframe(&ShadowFrustumPDI, VolumetricCloudParams.VolumetricCloud.CloudSkyAOWorldToLightClipMatrixInv, FColor::Blue, 0); 
						FDrawDebugCloudShadowCS::FParameters* Parameters = GraphBuilder.AllocParameters<FDrawDebugCloudShadowCS::FParameters>();
						Parameters->CloudTracedTexture = GraphBuilder.RegisterExternalTexture(ViewInfo.VolumetricCloudSkyAO);
						Parameters->CloudTextureSizeInvSize = VolumetricCloudParams.VolumetricCloud.CloudSkyAOSizeInvSize;
						Parameters->CloudTraceDirection = VolumetricCloudParams.VolumetricCloud.CloudSkyAOTraceDir;
						Parameters->CloudWorldToLightClipMatrixInv = VolumetricCloudParams.VolumetricCloud.CloudSkyAOWorldToLightClipMatrixInv;
						DebugCloudTexture(Parameters);
					}
				}
			}
		}
	}

	return bAsyncComputeUsed;
}


