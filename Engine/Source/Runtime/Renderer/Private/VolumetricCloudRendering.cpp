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


//PRAGMA_DISABLE_OPTIMIZATION

////////////////////////////////////////////////////////////////////////// Cloud rendering and tracing

// The runtime ON/OFF toggle
static TAutoConsoleVariable<int32> CVarVolumetricCloud(
	TEXT("r.VolumetricCloud"), 1,
	TEXT("VolumetricCloud components are rendered when this is not 0, otherwise ignored."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarVolumetricCloudPerSampleSkyAtmosphereTransmittance(
	TEXT("r.VolumetricCloud.PerSampleSkyAtmosphereTransmittance"), 0,
	TEXT("This is necessary to get correct colorisation on clouds when viewed from space."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarVolumetricCloudTracingStartMaxDistance(
	TEXT("r.VolumetricCloud.TracingStartMaxDistance"), 350.0f,
	TEXT("The maximum distance (kilometers) of the volumetric surface before which we will accept to start tracing."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarVolumetricCloudTracingMaxDistance(
	TEXT("r.VolumetricCloud.TracingMaxDistance"), 50.0f,
	TEXT("The maximum distance (kilometers) that will be traced inside the cloud layer."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarVolumetricCloudSampleCountMax(
	TEXT("r.VolumetricCloud.SampleCountMax"), 32,
	TEXT("The maximum number of samples taken while ray marching primary rays."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarVolumetricCloudDistanceToSampleCountMax(
	TEXT("r.VolumetricCloud.DistanceToSampleCountMax"), 15.0f,
	TEXT("The number of ray marching samples will span 0 to SampleCountMax from 0 to DistanceToSampleCountMax (kilometers). After that it is capped at SampleCountMax."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarVolumetricCloudReflectionSampleCountMax(
	TEXT("r.VolumetricCloud.ReflectionSampleCountMax"), 10,
	TEXT("The maximum number of samples taken while ray marching primary rays in reflections."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarVolumetricCloudIntersectWithOpaque(
	TEXT("r.VolumetricCloud.IntersectWithOpaque"), 1,
	TEXT("True if cloud will intersects with opaque and not be rendered behind opaques."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

////////////////////////////////////////////////////////////////////////// Shadow tracing

static TAutoConsoleVariable<int32> CVarVolumetricCloudShadowSampleCountMax(
	TEXT("r.VolumetricCloud.Shadow.SampleCountMax"), 10,
	TEXT("The maximum number of samples taken while ray marching shadow rays."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarVolumetricCloudReflectionShadowSampleCountMax(
	TEXT("r.VolumetricCloud.Shadow.ReflectionSampleCountMax"), 3,
	TEXT("The maximum number of samples taken while ray marching shadow rays in reflections."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarVolumetricCloudShadowTracingMaxDistance(
	TEXT("r.VolumetricCloud.Shadow.TracingMaxDistance"), 10.0f,
	TEXT("The maximum distance (kilometers) that will be traced inside the cloud layer for shadow rays."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarVolumetricCloudShadowSampleAtmosphericLightShadowmap(
	TEXT("r.VolumetricCloud.Shadow.SampleAtmosphericLightShadowmap"), 0,
	TEXT("Enable the sampling of atmospheric lights shadow map in order to produce volumetric shadows."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

////////////////////////////////////////////////////////////////////////// Cloud SKY AO

static TAutoConsoleVariable<int32> CVarVolumetricCloudSkyAO(
	TEXT("r.VolumetricCloud.SkyAO"), 1,
	TEXT("The resolution of the texture storting occlusion information for the lighting coming from the ground."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarVolumetricCloudSkyAODebug(
	TEXT("r.VolumetricCloud.SkyAO.Debug"), 0,
	TEXT("Print information to debug the cloud sky ao map."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarVolumetricCloudSkyAOSnapLength(
	TEXT("r.VolumetricCloud.SkyAO.SnapLength"), 20.0f,
	TEXT("Snapping size in kilometers of the cloud SkyAO texture position to avoid flickering."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarVolumetricCloudSkyAOTextureResolution(
	TEXT("r.VolumetricCloud.SkyAO.TextureResolution"), 256,
	TEXT("The resolution of the texture storting occlusion information for the lighting coming from the ground."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarVolumetricCloudSkyAOSampleCount(
	TEXT("r.VolumetricCloud.SkyAO.SampleCount"), 10,
	TEXT("The number of sample taken to evaluate ground lighting occlusion."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarVolumetricCloudSkyAOStrength(
	TEXT("r.VolumetricCloud.SkyAO.Strength"), 1.0f,
	TEXT("The strenght of the cloud AO."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarVolumetricCloudSkyAOOverrideExtend(
	TEXT("r.VolumetricCloud.SkyAO.OverrideExtent"), 150.0f,
	TEXT("The world space extent of the ground lighting occlusion texture can be overriden when this is greater than 0 (Kilometers)."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarVolumetricCloudSkyAOFiltering(
	TEXT("r.VolumetricCloud.SkyAO.Filtering"), 1,
	TEXT("Enable / disable the sky AO dilation/smoothing filter."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarVolumetricCloudSkyAOFilteringMinTransmittanceClamp(
	TEXT("r.VolumetricCloud.SkyAO.Filtering.MinTransmittanceClamp"), 0.0f,
	TEXT("The minimum transmittance clamp value allowed from cloud. This is needed because we do not re-inject cloud scattered light (TODO)"),
	ECVF_RenderThreadSafe | ECVF_Scalability);

////////////////////////////////////////////////////////////////////////// Cloud shadow map

static TAutoConsoleVariable<int32> CVarVolumetricCloudShadowMap(
	TEXT("r.VolumetricCloud.ShadowMap"), 0,
	TEXT("Enable / disable the shadow map."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarVolumetricCloudShadowMapDebug(
	TEXT("r.VolumetricCloud.ShadowMap.Debug"), 0,
	TEXT("Print information to debug the cloud shadow map."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarVolumetricCloudShadowMapSnapLength(
	TEXT("r.VolumetricCloud.ShadowMap.SnapLength"), 20.0f,
	TEXT("Snapping size in kilometers of the cloud shadowmap position to avoid flickering."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarVolumetricCloudShadowMapResolution(
	TEXT("r.VolumetricCloud.ShadowMap.Resolution"), 512,
	TEXT("The resolution of the cloud shadow map."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarVolumetricCloudShadowMapStrength(
	TEXT("r.VolumetricCloud.ShadowMap.Strength"), 0.2f,
	TEXT("The strenght of the cloud shadow."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarVolumetricCloudShadowMapOverrideExtend(
	TEXT("r.VolumetricCloud.ShadowMap.OverrideExtent"), 150.0f,
	TEXT("The world space extent of the cloud shadow map around the camera in kilometers, -1 to use the maximum trace distance."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarVolumetricCloudShadowFiltering(
	TEXT("r.VolumetricCloud.ShadowMap.Filtering"), 1,
	TEXT("Enable / disable the shadow map dilation/smoothing filter."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

////////////////////////////////////////////////////////////////////////// Lighting component controls

static TAutoConsoleVariable<int32> CVarVolumetricCloudEnableAerialPerspectiveSampling(
	TEXT("r.VolumetricCloud.EnableAerialPerspectiveSampling"), 1,
	TEXT("Enable/Disable the aerial perspective contribution on clouds."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarVolumetricCloudEnableDistantSkyLightSampling(
	TEXT("r.VolumetricCloud.EnableDistantSkyLightSampling"), 1,
	TEXT("Enable/Disable the distant sky light contribution on clouds."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarVolumetricCloudEnableAtmosphericLightsSampling(
	TEXT("r.VolumetricCloud.EnableAtmosphericLightsSampling"), 1,
	TEXT("Enable/Disable the atmospheric lights contribution on clouds."),
	ECVF_RenderThreadSafe);

////////////////////////////////////////////////////////////////////////// 

static TAutoConsoleVariable<int32> CVarVolumetricCloudDebugSampleCountMode(
	TEXT("r.VolumetricCloud.Debug.SampleCountMode"), 0,
	TEXT("Debug mode for per trace sample count."));

////////////////////////////////////////////////////////////////////////// 


static bool ShouldPipelineCompileVolumetricCloudShader(EShaderPlatform ShaderPlatform)
{
	// Requires SM5 or ES3_1 (GL/Vulkan) for compute shaders and volume textures support.
	return RHISupportsComputeShaders(ShaderPlatform);
}

bool ShouldRenderVolumetricCloud(const FScene* Scene, const FEngineShowFlags& EngineShowFlags)
{
	if (Scene && Scene->HasVolumetricCloud() ) //&& EngineShowFlags.VolumetricCloud) TODO apply 10810454 for clouds
	{
		const FVolumetricCloudRenderSceneInfo* VolumetricCloud = Scene->GetVolumetricCloudSceneInfo();
		check(VolumetricCloud);

		const bool ShadersCompiled = ShouldPipelineCompileVolumetricCloudShader(Scene->GetShaderPlatform());

		return ShadersCompiled && CVarVolumetricCloud.GetValueOnRenderThread() > 0 && Scene->AtmosphereLights[0]!=nullptr;
	}
	return false;
}

static int32 GetVolumetricCloudShadowMapResolution()
{
	return FMath::Max(32, CVarVolumetricCloudShadowMapResolution.GetValueOnAnyThread());
}

static int32 GetVolumetricCloudSkyAOResolution()
{
	return FMath::Max(32, CVarVolumetricCloudSkyAOTextureResolution.GetValueOnAnyThread());
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
	SHADER_PARAMETER_TEXTURE(Texture2D, SceneDepthTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, CloudSkyAOTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D<float2>, CloudShadowTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, CloudBilinearTextureSampler)
	SHADER_PARAMETER_STRUCT_INCLUDE(FVolumeShadowingShaderParametersGlobal0, Light0Shadow)
//	SHADER_PARAMETER_STRUCT(FBlueNoise, BlueNoise)
	SHADER_PARAMETER(FUintVector4, SubSetCoordToFullResolutionScaleBias)
	SHADER_PARAMETER(uint32, NoiseFrameIndexModPattern)
	SHADER_PARAMETER(int32, IntersectWithOpaque)
	SHADER_PARAMETER(uint32, VolumetricRenderTargetMode)
	SHADER_PARAMETER(uint32, SampleCountDebugMode)
	SHADER_PARAMETER(uint32, IsReflectionRendering)
	SHADER_PARAMETER(uint32, HasValidHZB)
	SHADER_PARAMETER(uint32, TraceShadowmap)
	SHADER_PARAMETER(FVector, HZBUvFactor)
	SHADER_PARAMETER(FVector4, HZBSize)
	SHADER_PARAMETER_TEXTURE(Texture2D<float>, HZBTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, HZBSampler)
	SHADER_PARAMETER(FVector4, OutputSizeInvSize)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FRenderVolumetricCloudGlobalParameters, "RenderVolumetricCloudParameters");

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FVolumetricCloudCommonGlobalShaderParameters, "VolumetricCloudCommonParameters");

// When calling this, you still need to setup Light0Shadow yourself.
void SetupDefaultRenderVolumetricCloudGlobalParameters(FRenderVolumetricCloudGlobalParameters& VolumetricCloudParams, FVolumetricCloudRenderSceneInfo& CloudInfo)
{
	TRefCountPtr<IPooledRenderTarget> BlackDummy = GSystemTextures.BlackDummy;
	VolumetricCloudParams.VolumetricCloud = CloudInfo.GetVolumetricCloudCommonShaderParameters();
	VolumetricCloudParams.SceneDepthTexture = BlackDummy->GetRenderTargetItem().ShaderResourceTexture;
	VolumetricCloudParams.CloudSkyAOTexture = BlackDummy->GetRenderTargetItem().ShaderResourceTexture;
	VolumetricCloudParams.CloudShadowTexture = BlackDummy->GetRenderTargetItem().ShaderResourceTexture;
	VolumetricCloudParams.CloudBilinearTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
	// Light0Shadow
/*#if RHI_RAYTRACING
	InitializeBlueNoise(VolumetricCloudParams.BlueNoise);
#else
	// Blue noise texture is undified for some configuration so replace by other noise for now.
	VolumetricCloudParams.BlueNoise.Dimensions = FIntVector(16, 16, 4); // 16 is the size of the tile, so 4 dimension for the 64x64 HighFrequencyNoiseTexture.
	VolumetricCloudParams.BlueNoise.Texture = GEngine->HighFrequencyNoiseTexture->Resource->TextureRHI;
#endif*/
	VolumetricCloudParams.SubSetCoordToFullResolutionScaleBias = FUintVector4(1, 1, 0, 0);
	VolumetricCloudParams.NoiseFrameIndexModPattern = 0;
	VolumetricCloudParams.VolumetricRenderTargetMode = GetVolumetricRenderTargetMode();
	VolumetricCloudParams.SampleCountDebugMode = FMath::Clamp(CVarVolumetricCloudDebugSampleCountMode.GetValueOnAnyThread(), 0, 5);

	VolumetricCloudParams.HasValidHZB = false;
	VolumetricCloudParams.HZBTexture = BlackDummy->GetRenderTargetItem().ShaderResourceTexture;
	VolumetricCloudParams.HZBSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
}

static void SetupRenderVolumetricCloudGlobalParametersHZB(const FViewInfo& ViewInfo, FRenderVolumetricCloudGlobalParameters& ShaderParameters)
{
	ShaderParameters.HasValidHZB = ViewInfo.HZB.IsValid() ? 1 : 0;

	ShaderParameters.HZBTexture = (ShaderParameters.HasValidHZB ? ViewInfo.HZB : GSystemTextures.BlackDummy)->GetRenderTargetItem().ShaderResourceTexture;
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
	{
		PassUniformBuffer.Bind(Initializer.ParameterMap, FRenderVolumetricCloudGlobalParameters::StaticStructMetadata.GetShaderVariableName());
	}

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
	VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance0_SampleShadow0,
	VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance1_SampleShadow0,
	VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance0_SampleShadow1,
	VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance1_SampleShadow1,
	VolumetricCloudRenderViewPsCount
};

BEGIN_SHADER_PARAMETER_STRUCT(FRenderVolumetricCloudRenderViewParametersPS, )
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CloudSkyAOTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CloudShadowTexture)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

template<EVolumetricCloudRenderViewPsPermutations Permutation>
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
		return bIsCompatible;
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_RENDERVIEW_PS"), TEXT("1"));
		OutEnvironment.SetDefine(TEXT("CLOUD_LAYER_PIXEL_SHADER"), TEXT("1"));

		const bool bUseAtmosphereTransmittance = Permutation == VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance1_SampleShadow0 || Permutation == VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance1_SampleShadow1;
		OutEnvironment.SetDefine(TEXT("CLOUD_PER_SAMPLE_ATMOSPHERE_TRANSMITTANCE"), bUseAtmosphereTransmittance ? TEXT("1") : TEXT("0"));

		const bool bSampleLightShadowmap = Permutation == VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance0_SampleShadow1 || Permutation == VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance1_SampleShadow1;
		OutEnvironment.SetDefine(TEXT("CLOUD_SAMPLE_ATMOSPHERIC_LIGHT_SHADOWMAP"), bSampleLightShadowmap ? TEXT("1") : TEXT("0"));
	}

private:
};

IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FRenderVolumetricCloudRenderViewPs<VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance0_SampleShadow0>, TEXT("/Engine/Private/VolumetricCloud.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FRenderVolumetricCloudRenderViewPs<VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance1_SampleShadow0>, TEXT("/Engine/Private/VolumetricCloud.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FRenderVolumetricCloudRenderViewPs<VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance0_SampleShadow1>, TEXT("/Engine/Private/VolumetricCloud.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FRenderVolumetricCloudRenderViewPs<VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance1_SampleShadow1>, TEXT("/Engine/Private/VolumetricCloud.usf"), TEXT("MainPS"), SF_Pixel);



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



static bool GetSkyUsesPerPixelTransmittance(const FScene* Scene, const FViewInfo* InViewIfDynamicMeshCommand)
{
	return CVarVolumetricCloudPerSampleSkyAtmosphereTransmittance.GetValueOnRenderThread() > 0 &&
		Scene->HasSkyAtmosphere() && ShouldRenderSkyAtmosphere(Scene, InViewIfDynamicMeshCommand->Family->EngineShowFlags);
}

class FVolumetricCloudRenderViewMeshProcessor : public FMeshPassProcessor
{
public:
	FVolumetricCloudRenderViewMeshProcessor(const FScene* Scene, const FViewInfo* InViewIfDynamicMeshCommand, 
		TUniformBufferRef<FViewUniformShaderParameters> ViewUniformBuffer, bool bShouldViewRenderVolumetricRenderTarget, bool bSkipAtmosphericLightShadowmap,
		FMeshPassDrawListContext* InDrawListContext, TUniformBufferRef<FRenderVolumetricCloudGlobalParameters> VolumetricCloudParmsUB)
		: FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
		, bVolumetricCloudPerSampleAtmosphereTransmittance(GetSkyUsesPerPixelTransmittance(Scene, InViewIfDynamicMeshCommand))
		, bVolumetricCloudSampleLightShadowmap(!bSkipAtmosphericLightShadowmap && CVarVolumetricCloudShadowSampleAtmosphericLightShadowmap.GetValueOnAnyThread() > 0)
	{
		PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
		PassDrawRenderState.SetPassUniformBuffer(VolumetricCloudParmsUB);

		PassDrawRenderState.SetViewUniformBuffer(ViewUniformBuffer);

		if (bShouldViewRenderVolumetricRenderTarget)
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

		if (bVolumetricCloudSampleLightShadowmap)
		{
			if (bVolumetricCloudPerSampleAtmosphereTransmittance)
			{
				TMeshProcessorShaders< FRenderVolumetricCloudVS, FMeshMaterialShader, FMeshMaterialShader,
					FRenderVolumetricCloudRenderViewPs<VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance1_SampleShadow1>> PassShaders;
				PassShaders.PixelShader = MaterialResource.GetShader<FRenderVolumetricCloudRenderViewPs<VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance1_SampleShadow1>>(VertexFactory->GetType());
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
			else
			{
				TMeshProcessorShaders< FRenderVolumetricCloudVS, FMeshMaterialShader, FMeshMaterialShader,
					FRenderVolumetricCloudRenderViewPs<VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance0_SampleShadow1>> PassShaders;
				PassShaders.PixelShader = MaterialResource.GetShader<FRenderVolumetricCloudRenderViewPs<VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance0_SampleShadow1>>(VertexFactory->GetType());
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
		}
		else
		{
			if (bVolumetricCloudPerSampleAtmosphereTransmittance)
			{
				TMeshProcessorShaders< FRenderVolumetricCloudVS, FMeshMaterialShader, FMeshMaterialShader,
					FRenderVolumetricCloudRenderViewPs<VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance1_SampleShadow0>> PassShaders;
				PassShaders.PixelShader = MaterialResource.GetShader<FRenderVolumetricCloudRenderViewPs<VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance1_SampleShadow0>>(VertexFactory->GetType());
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
			else
			{
				TMeshProcessorShaders< FRenderVolumetricCloudVS, FMeshMaterialShader, FMeshMaterialShader,
					FRenderVolumetricCloudRenderViewPs<VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance0_SampleShadow0>> PassShaders;
				PassShaders.PixelShader = MaterialResource.GetShader<FRenderVolumetricCloudRenderViewPs<VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance0_SampleShadow0>>(VertexFactory->GetType());
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
		}
	}

	FMeshPassProcessorRenderState PassDrawRenderState;
	bool bVolumetricCloudPerSampleAtmosphereTransmittance;
	bool bVolumetricCloudSampleLightShadowmap;
};



//////////////////////////////////////////////////////////////////////////

BEGIN_SHADER_PARAMETER_STRUCT(FVolumetricCloudShadowParametersPS, )
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FVolumetricCloudShadowPS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FVolumetricCloudShadowPS, MeshMaterial);

public:

	FVolumetricCloudShadowPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		PassUniformBuffer.Bind(Initializer.ParameterMap, FRenderVolumetricCloudGlobalParameters::StaticStructMetadata.GetShaderVariableName());
	}

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
	}

private:
};

IMPLEMENT_MATERIAL_SHADER_TYPE(, FVolumetricCloudShadowPS, TEXT("/Engine/Private/VolumetricCloud.usf"), TEXT("MainPS"), SF_Pixel);



class FVolumetricCloudRenderShadowMeshProcessor : public FMeshPassProcessor
{
public:
	FVolumetricCloudRenderShadowMeshProcessor(const FScene* Scene, const FViewInfo* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext, TUniformBufferRef<FRenderVolumetricCloudGlobalParameters> VolumetricCloudParmsUB)
		: FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
	{
		PassDrawRenderState.SetBlendState(TStaticBlendState<>::GetRHI());
		PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
		PassDrawRenderState.SetViewUniformBuffer(Scene->UniformBuffers.ViewUniformBuffer);
		PassDrawRenderState.SetPassUniformBuffer(VolumetricCloudParmsUB);
	}

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final
	{
		// Determine the mesh's material and blend mode.
		const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
		const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);

		check(Material.GetMaterialDomain() == MD_Volume);

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
		SHADER_PARAMETER(float, SkyAOMinTransmittanceClamp)
		SHADER_PARAMETER(float, CloudLayerStartHeightMeters)
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


static bool CloudSkyAORenderPassEnabled()
{
	return CVarVolumetricCloudSkyAO.GetValueOnRenderThread() > 0 && CVarVolumetricCloudSkyAOSampleCount.GetValueOnRenderThread() > 0 && CVarVolumetricCloudSkyAOTextureResolution.GetValueOnRenderThread() > 0;
}


void FSceneRenderer::InitVolumetricCloudsForViews(FRHICommandListImmediate& RHICmdList)
{
	if (Scene)
	{
		check(ShouldRenderVolumetricCloud(Scene, ViewFamily.EngineShowFlags)); // This should not be called if we should not render SkyAtmosphere

		check(Scene->GetVolumetricCloudSceneInfo());
		const FSkyAtmosphereRenderSceneInfo* SkyInfo = Scene->GetSkyAtmosphereSceneInfo();
		FVolumetricCloudRenderSceneInfo& CloudInfo = *Scene->GetVolumetricCloudSceneInfo();
		const FVolumetricCloudSceneProxy& CloudProxy = CloudInfo.GetVolumetricCloudSceneProxy();
		const float KilometersToCentimeters = 100000.0f;
		const float CentimetersToKilometers = 1.0f / KilometersToCentimeters;
		const float KilometersToMeters = 1000.0f;
		const float MetersToKilometers = 1.0f / KilometersToMeters;

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
			CloudGlobalShaderParams.AtmosphericLightsContributionFactor = CloudProxy.AtmosphericLightsContributionFactor;

			CloudGlobalShaderParams.TracingStartMaxDistance = KilometersToCentimeters * CVarVolumetricCloudTracingStartMaxDistance.GetValueOnAnyThread();
			CloudGlobalShaderParams.TracingMaxDistance = KilometersToCentimeters * CVarVolumetricCloudTracingMaxDistance.GetValueOnAnyThread();

			CloudGlobalShaderParams.SampleCountMax = FMath::Max(2, CVarVolumetricCloudSampleCountMax.GetValueOnAnyThread());
			CloudGlobalShaderParams.InvDistanceToSampleCountMax = 1.0f / FMath::Max(1.0f, KilometersToCentimeters * CVarVolumetricCloudDistanceToSampleCountMax.GetValueOnAnyThread());
			CloudGlobalShaderParams.ShadowSampleCountMax = FMath::Max(2, CVarVolumetricCloudShadowSampleCountMax.GetValueOnAnyThread());
			CloudGlobalShaderParams.ShadowTracingMaxDistance = KilometersToCentimeters * FMath::Max(0.1f, CVarVolumetricCloudShadowTracingMaxDistance.GetValueOnAnyThread());

			CloudGlobalShaderParams.EnableAerialPerspectiveSampling = CVarVolumetricCloudEnableAerialPerspectiveSampling.GetValueOnAnyThread() > 0;
			CloudGlobalShaderParams.EnableDistantSkyLightSampling = CVarVolumetricCloudEnableDistantSkyLightSampling.GetValueOnAnyThread() > 0;
			CloudGlobalShaderParams.EnableAtmosphericLightsSampling = CVarVolumetricCloudEnableAtmosphericLightsSampling.GetValueOnAnyThread() > 0;

			const float CloudShadowmapResolution = float(GetVolumetricCloudShadowMapResolution());
			const float CloudShadowmapResolutionInv = 1.0f / CloudShadowmapResolution;
			CloudGlobalShaderParams.CloudShadowmapSizeInvSize = FVector4(CloudShadowmapResolution, CloudShadowmapResolution, CloudShadowmapResolutionInv, CloudShadowmapResolutionInv);
			CloudGlobalShaderParams.CloudShadowmapStrength = FMath::Max(0.0f, CVarVolumetricCloudShadowMapStrength.GetValueOnAnyThread());

			FLightSceneInfo* LightSceneInfo = Scene->AtmosphereLights[0];
			if(LightSceneInfo)
			{
				const FVector AtmopshericLight0Direction = LightSceneInfo->Proxy->GetDirection();
				const FVector UpVector = FMath::Abs(FVector::DotProduct(AtmopshericLight0Direction, FVector::UpVector)) > 0.99f ? FVector::ForwardVector : FVector::UpVector;
				const float OverrideExtent = CVarVolumetricCloudShadowMapOverrideExtend.GetValueOnAnyThread() * KilometersToCentimeters;

				const float SphereRadius = OverrideExtent > 0.0f ? OverrideExtent : CloudGlobalShaderParams.TracingStartMaxDistance + CloudGlobalShaderParams.TracingMaxDistance;
				const float NearPlane = 0.0f;
				const float FarPlane = 2.0f * SphereRadius;
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
					const FVector LightPosition = LookAtPosition - AtmopshericLight0Direction * SphereRadius;

					float WorldSizeSnap = CVarVolumetricCloudShadowMapSnapLength.GetValueOnAnyThread() * KilometersToCentimeters;
					LookAtPosition.X = (FMath::FloorToFloat((LookAtPosition.X - 0.5f * WorldSizeSnap) / WorldSizeSnap)) * WorldSizeSnap; // offset by 0.5 to not snap around origin
					LookAtPosition.Y = (FMath::FloorToFloat((LookAtPosition.Y - 0.5f * WorldSizeSnap) / WorldSizeSnap)) * WorldSizeSnap;
					LookAtPosition.Z = (FMath::FloorToFloat((LookAtPosition.Z - 0.5f * WorldSizeSnap) / WorldSizeSnap)) * WorldSizeSnap;
				}

				const FVector LightPosition = LookAtPosition - AtmopshericLight0Direction * SphereRadius;
				FReversedZOrthoMatrix ShadowProjectionMatrix(SphereRadius, SphereRadius, ZScale, ZOffset);
				FLookAtMatrix ShadowViewMatrix(LightPosition, LookAtPosition, UpVector);
				CloudGlobalShaderParams.CloudShadowmapWorldToLightClipMatrix = ShadowViewMatrix * ShadowProjectionMatrix;
				CloudGlobalShaderParams.CloudShadowmapWorldToLightClipMatrixInv = CloudGlobalShaderParams.CloudShadowmapWorldToLightClipMatrix.InverseFast();
				CloudGlobalShaderParams.CloudShadowmapLight0Dir = AtmopshericLight0Direction;
				CloudGlobalShaderParams.CloudShadowmapFarDepthKm = FarPlane * CentimetersToKilometers;

				// More samples when the sun is at the horizon: a lot more distance to travel and less pixel covered so trying to keep the same cost and quality.
				CloudGlobalShaderParams.CloudShadowmapSampleClount = 16.0f + 32.0f * FMath::Clamp(0.2f / FMath::Abs(FVector::DotProduct(PlanetToCameraNormUp, AtmopshericLight0Direction)) - 1.0f, 0.0f, 1.0f);
			}
			else
			{
				CloudGlobalShaderParams.CloudShadowmapWorldToLightClipMatrix = FMatrix::Identity;
				CloudGlobalShaderParams.CloudShadowmapWorldToLightClipMatrixInv = FMatrix::Identity;
				CloudGlobalShaderParams.CloudShadowmapFarDepthKm = 1.0f;
				CloudGlobalShaderParams.CloudShadowmapSampleClount = 0.0f;
			}

			// Setup cloud SkyAO constants
			{
				const float CloudSkyAOResolution = float(GetVolumetricCloudSkyAOResolution());
				const float CloudSkyAOResolutionInv = 1.0f / CloudSkyAOResolution;
				CloudGlobalShaderParams.CloudSkyAOSizeInvSize = FVector4(CloudSkyAOResolution, CloudSkyAOResolution, CloudSkyAOResolutionInv, CloudSkyAOResolutionInv);
				CloudGlobalShaderParams.CloudSkyAOStrength = FMath::Max(0.0f, CVarVolumetricCloudSkyAOStrength.GetValueOnAnyThread());

				const float OverrideExtent = CVarVolumetricCloudSkyAOOverrideExtend.GetValueOnAnyThread() * KilometersToCentimeters;
				const float SphereRadius = OverrideExtent > 0.0f ? OverrideExtent : CloudGlobalShaderParams.TracingStartMaxDistance + CloudGlobalShaderParams.TracingMaxDistance;
				const float NearPlane = 0.0f;
				const float FarPlane = 2.0f * SphereRadius;
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
					float WorldSizeSnap = CVarVolumetricCloudSkyAOSnapLength.GetValueOnAnyThread() * KilometersToCentimeters;
					LookAtPosition.X = (FMath::FloorToFloat((LookAtPosition.X - 0.5f * WorldSizeSnap) / WorldSizeSnap)) * WorldSizeSnap; // offset by 0.5 to not snap around origin
					LookAtPosition.Y = (FMath::FloorToFloat((LookAtPosition.Y - 0.5f * WorldSizeSnap) / WorldSizeSnap)) * WorldSizeSnap;
					LookAtPosition.Z = (FMath::FloorToFloat((LookAtPosition.Z - 0.5f * WorldSizeSnap) / WorldSizeSnap)) * WorldSizeSnap;
				}

				// Trace direction is towards the ground
				FVector TraceDirection =  CloudGlobalShaderParams.CloudLayerCenterKm * KilometersToCentimeters - LookAtPosition;
				TraceDirection.Normalize();

				const FVector UpVector = FVector::ForwardVector; //FMath::Abs(FVector::DotProduct(-TraceDirection, FVector::RightVector)) > 0.99f ? FVector::ForwardVector : FVector::RightVector;
				const FVector LightPosition = LookAtPosition - TraceDirection * SphereRadius;
				FReversedZOrthoMatrix ShadowProjectionMatrix(SphereRadius, SphereRadius, ZScale, ZOffset);
				FLookAtMatrix ShadowViewMatrix(LightPosition, LookAtPosition, UpVector);
				CloudGlobalShaderParams.CloudSkyAOWorldToLightClipMatrix = ShadowViewMatrix * ShadowProjectionMatrix;
				CloudGlobalShaderParams.CloudSkyAOWorldToLightClipMatrixInv = CloudGlobalShaderParams.CloudSkyAOWorldToLightClipMatrix.InverseFast();
				CloudGlobalShaderParams.CloudSkyAOTrace0Dir = TraceDirection;
				CloudGlobalShaderParams.CloudSkyAOFarDepthKm = FarPlane * CentimetersToKilometers;

				// More samples when the sun is at the horizon: a lot more distance to travel and less pixel covered so trying to keep the same cost and quality.
				CloudGlobalShaderParams.CloudSkyAOSampleClount = CVarVolumetricCloudSkyAOSampleCount.GetValueOnAnyThread();
			}

			FVolumetricCloudCommonGlobalShaderParameters CloudGlobalShaderParamsUB;
			CloudGlobalShaderParamsUB.VolumetricCloudCommonParams = CloudGlobalShaderParams;
			CloudInfo.GetVolumetricCloudCommonShaderParametersUB() = TUniformBufferRef<FVolumetricCloudCommonGlobalShaderParameters>::CreateUniformBufferImmediate(CloudGlobalShaderParamsUB, UniformBuffer_SingleFrame);
		}



		if (CloudProxy.GetCloudVolumeMaterial())
		{
			FMaterialRenderProxy* CloudVolumeMaterialProxy = CloudProxy.GetCloudVolumeMaterial()->GetRenderProxy();
			if (CloudVolumeMaterialProxy->GetMaterial(ViewFamily.GetFeatureLevel())->GetMaterialDomain() == MD_Volume)
			{
				SCOPED_DRAW_EVENT(RHICmdList, VolumetricCloudShadow);
				SCOPED_GPU_STAT(RHICmdList, VolumetricCloudShadow);

				FRDGBuilder GraphBuilder(RHICmdList);

				FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
				TRefCountPtr<IPooledRenderTarget> BlackDummy = GSystemTextures.BlackDummy;
				FRDGTextureRef BlackDummyRDG = GraphBuilder.RegisterExternalTexture(BlackDummy);

				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
				{
					FViewInfo& ViewInfo = Views[ViewIndex];
					FVector ViewOrigin = ViewInfo.ViewMatrices.GetViewOrigin();

					FVolumeShadowingShaderParametersGlobal0 LightShadowShaderParams0;
					SetVolumeShadowingDefaultShaderParameters(LightShadowShaderParams0);

					FRenderVolumetricCloudGlobalParameters VolumetricCloudParams;
					VolumetricCloudParams.Light0Shadow = LightShadowShaderParams0;
					SetupDefaultRenderVolumetricCloudGlobalParameters(VolumetricCloudParams, CloudInfo); 

					VolumetricCloudParams.TraceShadowmap = 1;
					TUniformBufferRef<FRenderVolumetricCloudGlobalParameters> TraceVolumetricCloudShadowParamsUB = TUniformBufferRef<FRenderVolumetricCloudGlobalParameters>::CreateUniformBufferImmediate(VolumetricCloudParams, UniformBuffer_SingleFrame);
					VolumetricCloudParams.TraceShadowmap = 0;
					TUniformBufferRef<FRenderVolumetricCloudGlobalParameters> TraceVolumetricCloudSkyAOParamsUB = TUniformBufferRef<FRenderVolumetricCloudGlobalParameters>::CreateUniformBufferImmediate(VolumetricCloudParams, UniformBuffer_SingleFrame);


					auto TraceCloudTexture = [&](FRDGTextureRef CloudTextureTracedOutput, bool bSkyAOPass,
						TUniformBufferRef<FRenderVolumetricCloudGlobalParameters> TraceVolumetricCloudParamsUB)
					{
						FVolumetricCloudShadowParametersPS* CloudShadowParameters = GraphBuilder.AllocParameters<FVolumetricCloudShadowParametersPS>();
						CloudShadowParameters->RenderTargets[0] = FRenderTargetBinding(CloudTextureTracedOutput, ERenderTargetLoadAction::ENoAction);

						GraphBuilder.AddPass(
							bSkyAOPass ? RDG_EVENT_NAME("CloudSkyAO") : RDG_EVENT_NAME("CloudShadow"),
							CloudShadowParameters,
							ERDGPassFlags::Raster,
							[CloudShadowParameters, Scene = Scene, &ViewInfo, &CloudVolumeMaterialProxy, TraceVolumetricCloudParamsUB](FRHICommandListImmediate& RHICmdList)
							{
								DrawDynamicMeshPass(ViewInfo, RHICmdList,
									[&ViewInfo, &CloudVolumeMaterialProxy, &RHICmdList, &TraceVolumetricCloudParamsUB](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
									{
										FVolumetricCloudRenderShadowMeshProcessor PassMeshProcessor(
											ViewInfo.Family->Scene->GetRenderScene(), &ViewInfo,
											DynamicMeshPassContext, TraceVolumetricCloudParamsUB);

										FMeshBatch LocalSingleTriangleMesh;
										GetSingleTriangleMeshBatch(LocalSingleTriangleMesh, CloudVolumeMaterialProxy, ViewInfo.GetFeatureLevel());

										const FPrimitiveSceneProxy* PrimitiveSceneProxy = nullptr;
										const uint64 DefaultBatchElementMask = ~0ull;
										PassMeshProcessor.AddMeshBatch(LocalSingleTriangleMesh, DefaultBatchElementMask, PrimitiveSceneProxy);
									});
							});
					};

					auto FilterTracedCloudTexture = [&](FRDGTextureRef* TracedCloudTextureOutput, FVector4 TracedTextureSizeInvSize, bool bSkyAOPass)
					{
						FRDGTextureRef CloudShadowTexture2 = GraphBuilder.CreateTexture(
							FRDGTextureDesc::Create2DDesc(FIntPoint(TracedTextureSizeInvSize.X, TracedTextureSizeInvSize.Y), PF_G16R16F,
								FClearValueBinding::None, TexCreate_None, TexCreate_ShaderResource | TexCreate_UAV, false, 1), bSkyAOPass ? TEXT("CloudSkyAOTexture2") : TEXT("CloudShadowTexture2"));

						FCloudShadowFilterCS::FPermutationDomain Permutation;
						Permutation.Set<FCloudShadowFilterCS::FFilterSkyAO>(bSkyAOPass);
						TShaderMapRef<FCloudShadowFilterCS> ComputeShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5), Permutation);

						FCloudShadowFilterCS::FParameters* Parameters = GraphBuilder.AllocParameters<FCloudShadowFilterCS::FParameters>();
						Parameters->BilinearSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
						Parameters->CloudShadowTexture = *TracedCloudTextureOutput;
						Parameters->CloudTextureSizeInvSize = TracedTextureSizeInvSize;
						Parameters->SkyAOMinTransmittanceClamp = FMath::Clamp(CVarVolumetricCloudSkyAOFilteringMinTransmittanceClamp.GetValueOnAnyThread(), 0.0f, 1.0f);
						Parameters->CloudLayerStartHeightMeters = CloudProxy.LayerBottomAltitudeKm * KilometersToMeters;
						Parameters->OutCloudShadowTexture = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(CloudShadowTexture2));

						const FIntVector CloudShadowTextureSize = FIntVector(TracedTextureSizeInvSize.X, TracedTextureSizeInvSize.Y, 1);
						const FIntVector DispatchCount = DispatchCount.DivideAndRoundUp(FIntVector(CloudShadowTextureSize.X, CloudShadowTextureSize.Y, 1), FIntVector(8, 8, 1));
						FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("CloudDataFilter"), ComputeShader, Parameters, DispatchCount);

						*TracedCloudTextureOutput = CloudShadowTexture2;
					};


					// Render Cloud SKY AO
					if (CloudSkyAORenderPassEnabled())
					{
						const uint32 VolumetricCloudSkyAOResolution = GetVolumetricCloudSkyAOResolution();
						FRDGTextureRef CloudSkyAOTexture = GraphBuilder.CreateTexture(
							FRDGTextureDesc::Create2DDesc(FIntPoint(VolumetricCloudSkyAOResolution, VolumetricCloudSkyAOResolution), PF_G16R16F,
								FClearValueBinding::None, TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable, false, 1), TEXT("CloudSkyAOTexture"));

						TraceCloudTexture(CloudSkyAOTexture, true, TraceVolumetricCloudSkyAOParamsUB);

						if (CVarVolumetricCloudSkyAOFiltering.GetValueOnAnyThread() > 0)
						{
							FilterTracedCloudTexture(&CloudSkyAOTexture, VolumetricCloudParams.VolumetricCloud.CloudSkyAOSizeInvSize, true);
						}

						GraphBuilder.QueueTextureExtraction(CloudSkyAOTexture, &ViewInfo.VolumetricCloudSkyAO);
					}



					// Render atmospheric lights shadow maps
					if (CVarVolumetricCloudShadowMap.GetValueOnAnyThread() > 0)
					{
						const uint32 VolumetricCloudShadowMapResolution = GetVolumetricCloudShadowMapResolution();
						FRDGTextureRef CloudShadowTexture = GraphBuilder.CreateTexture(
							FRDGTextureDesc::Create2DDesc(FIntPoint(VolumetricCloudShadowMapResolution, VolumetricCloudShadowMapResolution), PF_G16R16F,
								FClearValueBinding::None, TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable, false, 1), TEXT("CloudShadowTexture"));
						
						TraceCloudTexture(CloudShadowTexture, false, TraceVolumetricCloudShadowParamsUB);

						if(CVarVolumetricCloudShadowFiltering.GetValueOnAnyThread() > 0)
						{
							FilterTracedCloudTexture(&CloudShadowTexture, VolumetricCloudParams.VolumetricCloud.CloudShadowmapSizeInvSize, false);
						}

						GraphBuilder.QueueTextureExtraction(CloudShadowTexture, &ViewInfo.VolumetricCloudShadowMap);
					}
				}

				GraphBuilder.Execute();
			}
		}
	}
}

CloudRenderContext::CloudRenderContext()
{
	SubSetCoordToFullResolutionScaleBias = FUintVector4(1, 1, 0, 0);
	NoiseFrameIndexModPattern = 0;

	bIsReflectionRendering = false;
	bSkipAtmosphericLightShadowmap = false;
}

void FSceneRenderer::RenderVolumetricCloudsInternal(FRDGBuilder& GraphBuilder, CloudRenderContext& CloudRC)
{
	FRenderVolumetricCloudRenderViewParametersPS* RenderViewPassParameters = GraphBuilder.AllocParameters<FRenderVolumetricCloudRenderViewParametersPS>();
	RenderViewPassParameters->RenderTargets = CloudRC.RenderTargets;
	RenderViewPassParameters->CloudShadowTexture = CloudRC.VolumetricCloudShadowTexture;
	RenderViewPassParameters->CloudSkyAOTexture = RenderViewPassParameters->CloudShadowTexture;

	FRDGTexture* RT0 = CloudRC.RenderTargets.Output[0].GetTexture();
	FVector4 OutputSizeInvSize = FVector4(float(RT0->Desc.Extent.X), float(RT0->Desc.Extent.Y), 1.0f/float(RT0->Desc.Extent.X), 1.0f/float(RT0->Desc.Extent.Y));

	// Copy parameters to lambda
	check(CloudRC.MainView);
	check(CloudRC.CloudInfo);
	check(CloudRC.CloudVolumeMaterialProxy);
	FViewInfo& MainView = *CloudRC.MainView;
	FVolumetricCloudRenderSceneInfo& CloudInfo = *CloudRC.CloudInfo;
	FMaterialRenderProxy* CloudVolumeMaterialProxy = CloudRC.CloudVolumeMaterialProxy;
	TUniformBufferRef<FViewUniformShaderParameters> ViewUniformBuffer = CloudRC.ViewUniformBuffer;
	const bool bShouldViewRenderVolumetricRenderTarget = CloudRC.bShouldViewRenderVolumetricRenderTarget;
	const bool bIsReflectionRendering = CloudRC.bIsReflectionRendering;
	const bool bSkipAtmosphericLightShadowmap = CloudRC.bSkipAtmosphericLightShadowmap;

	FUintVector4 SubSetCoordToFullResolutionScaleBias = CloudRC.SubSetCoordToFullResolutionScaleBias;
	uint32 NoiseFrameIndexModPattern = CloudRC.NoiseFrameIndexModPattern;
	TRefCountPtr<IPooledRenderTarget> SceneDepthZ = CloudRC.SceneDepthZ;
	FVolumeShadowingShaderParametersGlobal0 LightShadowShaderParams0 = CloudRC.LightShadowShaderParams0;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("CloudView"),
		RenderViewPassParameters,
		ERDGPassFlags::Raster,
		[RenderViewPassParameters, Scene = Scene, &MainView, ViewUniformBuffer, 
		bShouldViewRenderVolumetricRenderTarget, CloudVolumeMaterialProxy, bIsReflectionRendering, bSkipAtmosphericLightShadowmap,
		&CloudInfo, SceneDepthZ, LightShadowShaderParams0, SubSetCoordToFullResolutionScaleBias, NoiseFrameIndexModPattern, OutputSizeInvSize](FRHICommandListImmediate& RHICmdList)
		{
			FRenderVolumetricCloudGlobalParameters VolumetricCloudParams;
			SetupDefaultRenderVolumetricCloudGlobalParameters(VolumetricCloudParams, CloudInfo);
			VolumetricCloudParams.SceneDepthTexture = SceneDepthZ->GetRenderTargetItem().ShaderResourceTexture;
			VolumetricCloudParams.Light0Shadow = LightShadowShaderParams0;
			VolumetricCloudParams.CloudSkyAOTexture = RenderViewPassParameters->CloudSkyAOTexture->GetPooledRenderTarget()->GetRenderTargetItem().ShaderResourceTexture;
			VolumetricCloudParams.CloudShadowTexture = RenderViewPassParameters->CloudShadowTexture->GetPooledRenderTarget()->GetRenderTargetItem().ShaderResourceTexture;
			VolumetricCloudParams.SubSetCoordToFullResolutionScaleBias = SubSetCoordToFullResolutionScaleBias;
			VolumetricCloudParams.NoiseFrameIndexModPattern = NoiseFrameIndexModPattern;
			VolumetricCloudParams.IntersectWithOpaque = CVarVolumetricCloudIntersectWithOpaque.GetValueOnAnyThread();
			VolumetricCloudParams.IsReflectionRendering = bIsReflectionRendering ? 1 : 0;
			if (bIsReflectionRendering)
			{
				VolumetricCloudParams.VolumetricCloud.SampleCountMax = FMath::Max(2, CVarVolumetricCloudReflectionSampleCountMax.GetValueOnAnyThread());
				VolumetricCloudParams.VolumetricCloud.ShadowSampleCountMax = FMath::Max(2, CVarVolumetricCloudReflectionShadowSampleCountMax.GetValueOnAnyThread());
			}
			VolumetricCloudParams.OutputSizeInvSize = OutputSizeInvSize;
			SetupRenderVolumetricCloudGlobalParametersHZB(MainView, VolumetricCloudParams);
			TUniformBufferRef<FRenderVolumetricCloudGlobalParameters> VolumetricCloudRenderViewParamsUB = TUniformBufferRef<FRenderVolumetricCloudGlobalParameters>::CreateUniformBufferImmediate(VolumetricCloudParams, UniformBuffer_SingleFrame);

			DrawDynamicMeshPass(MainView, RHICmdList,
				[&MainView, ViewUniformBuffer, bShouldViewRenderVolumetricRenderTarget, bSkipAtmosphericLightShadowmap,
				CloudVolumeMaterialProxy, &RHICmdList, &VolumetricCloudRenderViewParamsUB](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
				{
					FVolumetricCloudRenderViewMeshProcessor PassMeshProcessor(
						MainView.Family->Scene->GetRenderScene(), &MainView, ViewUniformBuffer, bShouldViewRenderVolumetricRenderTarget,
						bSkipAtmosphericLightShadowmap, DynamicMeshPassContext, VolumetricCloudRenderViewParamsUB);

					FMeshBatch LocalSingleTriangleMesh;
					GetSingleTriangleMeshBatch(LocalSingleTriangleMesh, CloudVolumeMaterialProxy, MainView.GetFeatureLevel());

					const FPrimitiveSceneProxy* PrimitiveSceneProxy = nullptr;
					const uint64 DefaultBatchElementMask = ~0ull;
					PassMeshProcessor.AddMeshBatch(LocalSingleTriangleMesh, DefaultBatchElementMask, PrimitiveSceneProxy);
				});
		});
}

void FSceneRenderer::RenderVolumetricCloud(FRHICommandListImmediate& RHICmdList)
{
	check(ShouldRenderVolumetricCloud(Scene, ViewFamily.EngineShowFlags)); // This should not be called if we should not render SkyAtmosphere

	FVolumetricCloudRenderSceneInfo& CloudInfo = *Scene->GetVolumetricCloudSceneInfo();
	FVolumetricCloudSceneProxy& CloudSceneProxy = CloudInfo.GetVolumetricCloudSceneProxy();

	if (CloudSceneProxy.GetCloudVolumeMaterial())
	{
		FMaterialRenderProxy* CloudVolumeMaterialProxy = CloudSceneProxy.GetCloudVolumeMaterial()->GetRenderProxy();
		if (CloudVolumeMaterialProxy->GetMaterial(ViewFamily.GetFeatureLevel())->GetMaterialDomain() == MD_Volume)
		{
			SCOPED_DRAW_EVENT(RHICmdList, VolumetricCloud);
			SCOPED_GPU_STAT(RHICmdList, VolumetricCloud);

			FRDGBuilder GraphBuilder(RHICmdList);

			FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

			TRefCountPtr<IPooledRenderTarget> SceneDepthZ = SceneContext.SceneDepthZ;
			TRefCountPtr<IPooledRenderTarget> BlackDummy = GSystemTextures.BlackDummy;
			FRDGTextureRef BlackDummyRDG = GraphBuilder.RegisterExternalTexture(BlackDummy);

			CloudRenderContext CloudRC;
			CloudRC.CloudInfo = &CloudInfo;
			CloudRC.CloudVolumeMaterialProxy= CloudVolumeMaterialProxy;
			CloudRC.SceneDepthZ = SceneDepthZ;

			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
			{
				FViewInfo& ViewInfo = Views[ViewIndex];

				CloudRC.MainView = &ViewInfo;

				bool bShouldViewRenderVolumetricRenderTarget = ShouldViewRenderVolumetricRenderTarget(ViewInfo);
				CloudRC.bShouldViewRenderVolumetricRenderTarget = bShouldViewRenderVolumetricRenderTarget;
				CloudRC.ViewUniformBuffer = bShouldViewRenderVolumetricRenderTarget ? ViewInfo.VolumetricRenderTargetViewUniformBuffer : ViewInfo.ViewUniformBuffer;

				FRDGTextureRef DestinationRT;
				FRDGTextureRef DestinationRTDepth;
				const bool bUseVolumetricRenderTarget = ShouldViewRenderVolumetricRenderTarget(ViewInfo); // not used by reflection captures for instance
				CloudRC.SubSetCoordToFullResolutionScaleBias = FUintVector4(1, 1, 0, 0);
				CloudRC.NoiseFrameIndexModPattern = ViewInfo.CachedViewUniformShaderParameters->StateFrameIndexMod8;
				if (bUseVolumetricRenderTarget)
				{
					FVolumetricRenderTargetViewStateData& VRT = ViewInfo.ViewState->VolumetricRenderTarget;
					DestinationRT = VRT.GetOrCreateVolumetricTracingRT(GraphBuilder);
					DestinationRTDepth = VRT.GetOrCreateVolumetricTracingRTDepth(GraphBuilder);

					// No action because we only need to render volumetric clouds so we do not blend in that render target.
					// When we have more elements rendered in that target later, we can clear it to default and blend.
					CloudRC.RenderTargets[0] = FRenderTargetBinding(DestinationRT, ERenderTargetLoadAction::ENoAction);
					CloudRC.RenderTargets[1] = FRenderTargetBinding(DestinationRTDepth, ERenderTargetLoadAction::ENoAction);
					CloudRC.SubSetCoordToFullResolutionScaleBias = VRT.GetTracingToFullResResolutionScaleBias();
					CloudRC.NoiseFrameIndexModPattern = VRT.GetNoiseFrameIndexModPattern();
				}
				else
				{
					DestinationRT = GraphBuilder.RegisterExternalTexture(SceneContext.GetSceneColor(), TEXT("SceneColor"));
					FIntVector RtSize = SceneContext.GetSceneColor()->GetDesc().GetSize();
					DestinationRTDepth = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2DDesc(FIntPoint(RtSize.X, RtSize.Y), PF_R16F, FClearValueBinding::Black,
						TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable, false, 1), TEXT("DummyDepth"));
					CloudRC.RenderTargets[0] = FRenderTargetBinding(DestinationRT, ERenderTargetLoadAction::ELoad);
					CloudRC.RenderTargets[1] = FRenderTargetBinding(DestinationRTDepth, ERenderTargetLoadAction::ENoAction);
				}



				const bool bShouldSampleOpaqueShadow = true;
				const FLightSceneInfo* LightVolumetricShadowSceneinfo0 = Scene->AtmosphereLights[0];
				const FProjectedShadowInfo* ProjectedShadowInfo0 = nullptr;
				if (LightVolumetricShadowSceneinfo0)
				{
					ProjectedShadowInfo0 = GetLastCascadeShadowInfo(LightVolumetricShadowSceneinfo0->Proxy, VisibleLightInfos[LightVolumetricShadowSceneinfo0->Id]);
				}
				if (bShouldSampleOpaqueShadow && LightVolumetricShadowSceneinfo0 && ProjectedShadowInfo0)
				{
					SetVolumeShadowingShaderParameters(CloudRC.LightShadowShaderParams0, ViewInfo, LightVolumetricShadowSceneinfo0, ProjectedShadowInfo0, INDEX_NONE);
				}
				else
				{
					SetVolumeShadowingDefaultShaderParameters(CloudRC.LightShadowShaderParams0);
				}
				// Cannot nest a global buffer into another one and we are limited to only one PassUniformBuffer on PassDrawRenderState.
				//TUniformBufferRef<FVolumeShadowingShaderParametersGlobal0> LightShadowShaderParams0UniformBuffer = TUniformBufferRef<FVolumeShadowingShaderParametersGlobal0>::CreateUniformBufferImmediate(LightShadowShaderParams0, UniformBuffer_SingleFrame);

				CloudRC.VolumetricCloudShadowTexture = ViewInfo.VolumetricCloudShadowMap.IsValid() ? GraphBuilder.RegisterExternalTexture(ViewInfo.VolumetricCloudShadowMap) : BlackDummyRDG;

				RenderVolumetricCloudsInternal(GraphBuilder, CloudRC);

				if (bUseVolumetricRenderTarget)
				{
					ViewInfo.ViewState->VolumetricRenderTarget.ExtractToVolumetricTracingRT(GraphBuilder, DestinationRT);
					ViewInfo.ViewState->VolumetricRenderTarget.ExtractToVolumetricTracingRTDepth(GraphBuilder, DestinationRTDepth);
				}



				const bool DebugCloudShadowMap = CVarVolumetricCloudShadowMapDebug.GetValueOnRenderThread() && CVarVolumetricCloudShadowMap.GetValueOnRenderThread() > 0;
				const bool DebugCloudSkyAO = CVarVolumetricCloudSkyAODebug.GetValueOnRenderThread() && CloudSkyAORenderPassEnabled();
				if (DebugCloudShadowMap || DebugCloudSkyAO)
				{
					FViewElementPDI ShadowFrustumPDI(&ViewInfo, nullptr, nullptr);

					FRenderVolumetricCloudGlobalParameters VolumetricCloudParams;
					SetupDefaultRenderVolumetricCloudGlobalParameters(VolumetricCloudParams, CloudInfo);

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
						{
							const float ViewPortWidth = float(ViewInfo.ViewRect.Width());
							const float ViewPortHeight = float(ViewInfo.ViewRect.Height());
							FRenderTargetTemp TempRenderTarget(ViewInfo, (const FTexture2DRHIRef&)SceneContext.GetSceneColor()->GetRenderTargetItem().TargetableTexture);
							FCanvas Canvas(&TempRenderTarget, NULL, ViewInfo.Family->CurrentRealTime, ViewFamily.CurrentWorldTime, ViewFamily.DeltaWorldTime, ViewInfo.GetFeatureLevel());
							FLinearColor TextColor(1.0f, 0.5f, 0.0f);
							FString Text = FString::Printf(TEXT("Shadow Sample Count = %.1f"), VolumetricCloudParams.VolumetricCloud.CloudShadowmapSampleClount);
							Canvas.DrawShadowedString(0.05f, ViewPortHeight * 0.4f, *Text, GetStatsFont(), TextColor);
							Canvas.Flush_RenderThread(RHICmdList);
						}

						DrawFrustumWireframe(&ShadowFrustumPDI, VolumetricCloudParams.VolumetricCloud.CloudShadowmapWorldToLightClipMatrixInv, FColor::Orange, 0);
						FDrawDebugCloudShadowCS::FParameters* Parameters = GraphBuilder.AllocParameters<FDrawDebugCloudShadowCS::FParameters>();
						Parameters->CloudTracedTexture = CloudRC.VolumetricCloudShadowTexture;
						Parameters->CloudTextureSizeInvSize = VolumetricCloudParams.VolumetricCloud.CloudShadowmapSizeInvSize;
						Parameters->CloudTraceDirection = VolumetricCloudParams.VolumetricCloud.CloudShadowmapLight0Dir;
						Parameters->CloudWorldToLightClipMatrixInv = VolumetricCloudParams.VolumetricCloud.CloudShadowmapWorldToLightClipMatrixInv;
						DebugCloudTexture(Parameters);
					}

					if (DebugCloudSkyAO)
					{
						DrawFrustumWireframe(&ShadowFrustumPDI, VolumetricCloudParams.VolumetricCloud.CloudSkyAOWorldToLightClipMatrixInv, FColor::Blue, 0);
						FDrawDebugCloudShadowCS::FParameters* Parameters = GraphBuilder.AllocParameters<FDrawDebugCloudShadowCS::FParameters>();
						Parameters->CloudTracedTexture = GraphBuilder.RegisterExternalTexture(ViewInfo.VolumetricCloudSkyAO);
						Parameters->CloudTextureSizeInvSize = VolumetricCloudParams.VolumetricCloud.CloudSkyAOSizeInvSize;
						Parameters->CloudTraceDirection = VolumetricCloudParams.VolumetricCloud.CloudSkyAOTrace0Dir;
						Parameters->CloudWorldToLightClipMatrixInv = VolumetricCloudParams.VolumetricCloud.CloudSkyAOWorldToLightClipMatrixInv;
						DebugCloudTexture(Parameters);
					}
				}
			}

			GraphBuilder.Execute();
		}
	}

}


