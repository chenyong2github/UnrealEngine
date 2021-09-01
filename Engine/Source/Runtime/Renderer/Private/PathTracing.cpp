// Copyright Epic Games, Inc. All Rights Reserved.

#include "PathTracing.h"
#include "RHI.h"
#include "PathTracingDenoiser.h"

PathTracingDenoiserFunction* GPathTracingDenoiserFunc = nullptr;

#if RHI_RAYTRACING

#include "RendererPrivate.h"
#include "GlobalShader.h"
#include "DeferredShadingRenderer.h"
#include "HAL/PlatformApplicationMisc.h"
#include "RayTracingTypes.h"
#include "RayTracingDefinitions.h"
#include "PathTracingDefinitions.h"
#include "RenderCore/Public/GenerateMips.h"
#include <limits>

TAutoConsoleVariable<int32> CVarPathTracingMaxBounces(
	TEXT("r.PathTracing.MaxBounces"),
	-1,
	TEXT("Sets the maximum number of path tracing bounces (default = -1 (driven by postprocesing volume))"),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarPathTracingSamplesPerPixel(
	TEXT("r.PathTracing.SamplesPerPixel"),
	-1,
	TEXT("Sets the maximum number of samples per pixel (default = -1 (driven by postprocesing volume))"),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<float> CVarPathTracingFilterWidth(
	TEXT("r.PathTracing.FilterWidth"),
	-1,
	TEXT("Sets the anti-aliasing filter width (default = -1 (driven by postprocesing volume))"),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarPathTracingMISMode(
	TEXT("r.PathTracing.MISMode"),
	2,
	TEXT("Selects the sampling technique for light integration (default = 2 (MIS enabled))\n")
	TEXT("0: Material sampling\n")
	TEXT("1: Light sampling\n")
	TEXT("2: MIS betwen material and light sampling (default)\n"),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarPathTracingMISCompensation(
	TEXT("r.PathTracing.MISCompensation"),
	1,
	TEXT("Activates MIS compensation for skylight importance sampling. (default = 1 (enabled))\n")
	TEXT("This option only takes effect when r.PathTracing.MISMode = 2\n"),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarPathTracingSkylightCaching(
	TEXT("r.PathTracing.SkylightCaching"),
	1,
	TEXT("Attempts to re-use skylight data between frames. (default = 1 (enabled))\n")
	TEXT("When set to 0, the skylight texture and importance samping data will be regenerated every frame. This is mainly intended as a benchmarking and debugging aid\n"),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarPathTracingVisibleLights(
	TEXT("r.PathTracing.VisibleLights"),
	0,
	TEXT("Should light sources be visible to camera rays? (default = 0 (off))\n")
	TEXT("0: Hide lights from camera rays (default)\n")
	TEXT("1: Make lights visible to camera\n"),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarPathTracingMaxSSSBounces(
	TEXT("r.PathTracing.MaxSSSBounces"),
	256,
	TEXT("Sets the maximum number of bounces inside subsurface materials. Lowering this value can make subsurface scattering render too dim, while setting it too high can cause long render times.  (default = 256)"),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<float> CVarPathTracingMaxPathIntensity(
	TEXT("r.PathTracing.MaxPathIntensity"),
	-1,
	TEXT("When positive, light paths greater that this amount are clamped to prevent fireflies (default = -1 (driven by postprocesing volume))"),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarPathTracingApproximateCaustics(
	TEXT("r.PathTracing.ApproximateCaustics"),
	1,
	TEXT("When non-zero, the path tracer will approximate caustic paths to reduce noise. This reduces speckles and noise from low-roughness glass and metals. (default = 1 (enabled))"),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarPathTracingEnableEmissive(
	TEXT("r.PathTracing.EnableEmissive"),
	-1,
	TEXT("Indicates if emissive materials should contribute to scene lighting (default = -1 (driven by postprocesing volume)"),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarPathTracingEnableCameraBackfaceCulling(
	TEXT("r.PathTracing.EnableCameraBackfaceCulling"),
	1,
	TEXT("When non-zero, the path tracer will skip over backfacing triangles when tracing primary rays from the camera. (default = 1 (enabled))"),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarPathTracingFrameIndependentTemporalSeed(
	TEXT("r.PathTracing.FrameIndependentTemporalSeed"),
	1,
	TEXT("Indicates to use different temporal seed for each sample across frames rather than resetting the sequence at the start of each frame\n")
	TEXT("0: off\n")
	TEXT("1: on (default)\n"),
	ECVF_RenderThreadSafe
);

// See PATHTRACER_SAMPLER_* defines
TAutoConsoleVariable<int32> CVarPathTracingSamplerType(
	TEXT("r.PathTracing.SamplerType"),
	PATHTRACER_SAMPLER_DEFAULT,
	TEXT("Controls the way the path tracer generates its random numbers\n")
	TEXT("0: use a different high quality random sequence per pixel\n")
	TEXT("1: optimize the random sequence across pixels to reduce visible error at the target sample count\n")
	TEXT("2: share random seeds across pixels to improve coherence of execution on the GPU. This trades some correlation across the image in exchange for better performance.\n"),
	ECVF_RenderThreadSafe
);

#if 0
// TODO: re-enable this when multi-gpu is supported again
// r.PathTracing.GPUCount is read only because ComputeViewGPUMasks results cannot change after UE has been launched
TAutoConsoleVariable<int32> CVarPathTracingGPUCount(
	TEXT("r.PathTracing.GPUCount"),
	1,
	TEXT("Sets the amount of GPUs used for computing the path tracing pass (default = 1 GPU)"),
	ECVF_RenderThreadSafe | ECVF_ReadOnly
);
#endif

TAutoConsoleVariable<int32> CVarPathTracingWiperMode(
	TEXT("r.PathTracing.WiperMode"),
	0,
	TEXT("Enables wiper mode to render using the path tracer only in a region of the screen for debugging purposes (default = 0, wiper mode disabled)"),
	ECVF_RenderThreadSafe 
);

TAutoConsoleVariable<int32> CVarPathTracingProgressDisplay(
	TEXT("r.PathTracing.ProgressDisplay"),
	0,
	TEXT("Enables an in-frame display of progress towards the defined sample per pixel limit. The indicator dissapears when the maximum is reached and sample accumulation has stopped (default = 0)\n")
	TEXT("0: off (default)\n")
	TEXT("1: on\n"),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarPathTracingLightGridResolution(
	TEXT("r.PathTracing.LightGridResolution"),
	256,
	TEXT("Controls the resolution of the 2D light grid used to cull irrelevant lights from lighting calculations (default = 256)\n"),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarPathTracingLightGridMaxCount(
	TEXT("r.PathTracing.LightGridMaxCount"),
	128,
	TEXT("Controls the maximum number of lights per cell in the 2D light grid. The minimum of this value and the number of lights in the scene is used. (default = 128)\n"),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarPathTracingLightGridVisualize(
	TEXT("r.PathTracing.LightGridVisualize"),
	0,
	TEXT("Enables a visualization mode of the light grid density where red indicates the maximum light count has been reached (default = 0)\n")
	TEXT("0: off (default)\n")
	TEXT("1: light count heatmap (red - close to overflow, increase r.PathTracing.LightGridMaxCount)\n")
	TEXT("2: unique light lists (colors are a function of which lights occupy each cell)\n")
	TEXT("3: area light visualization (green: point light sources only, blue: some area light sources)\n"),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarPathTracingDenoiser(
	TEXT("r.PathTracing.Denoiser"),
	-1,
	TEXT("Enable denoising of the path traced output (if a denoiser plugin is active) (default = -1 (driven by postprocesing volume))\n")
	TEXT("-1: inherit from PostProcessVolume\n")
	TEXT("0: disable denoiser\n")
	TEXT("1: enable denoiser (if a denoiser plugin is active)\n"),
	ECVF_RenderThreadSafe
);

BEGIN_SHADER_PARAMETER_STRUCT(FPathTracingData, )
	SHADER_PARAMETER(uint32, Iteration)
	SHADER_PARAMETER(uint32, TemporalSeed)
	SHADER_PARAMETER(uint32, MaxSamples)
	SHADER_PARAMETER(uint32, MaxBounces)
	SHADER_PARAMETER(uint32, MaxSSSBounces)
	SHADER_PARAMETER(uint32, MISMode)
	SHADER_PARAMETER(uint32, ApproximateCaustics)
	SHADER_PARAMETER(uint32, EnableCameraBackfaceCulling)
	SHADER_PARAMETER(uint32, EnableDirectLighting)
	SHADER_PARAMETER(uint32, EnableEmissive)
	SHADER_PARAMETER(uint32, SamplerType)
	SHADER_PARAMETER(uint32, VisualizeLightGrid)
	SHADER_PARAMETER(float, MaxPathIntensity)
	SHADER_PARAMETER(float, MaxNormalBias)
	SHADER_PARAMETER(float, FilterWidth)
END_SHADER_PARAMETER_STRUCT()

// This function prepares the portion of shader arguments that may involve invalidating the path traced state
static bool PrepareShaderArgs(const FViewInfo& View, FPathTracingData& PathTracingData) {
	PathTracingData.EnableDirectLighting = true;
	int32 MaxBounces = CVarPathTracingMaxBounces.GetValueOnRenderThread();
	if (MaxBounces < 0)
	{
		MaxBounces = View.FinalPostProcessSettings.PathTracingMaxBounces;
	}
	if (View.Family->EngineShowFlags.DirectLighting)
	{
		if (!View.Family->EngineShowFlags.GlobalIllumination)
		{
			// direct lighting, but no GI
			MaxBounces = 1;
		}
	}
	else
	{
		PathTracingData.EnableDirectLighting = false;
		if (View.Family->EngineShowFlags.GlobalIllumination)
		{
			// skip direct lighting, but still do the full bounces
		}
		else
		{
			// neither direct, nor GI is on
			MaxBounces = 0;
		}
	}

	PathTracingData.MaxBounces = MaxBounces;
	PathTracingData.MaxSSSBounces = CVarPathTracingMaxSSSBounces.GetValueOnRenderThread();
	PathTracingData.MaxNormalBias = GetRaytracingMaxNormalBias();
	PathTracingData.MISMode = CVarPathTracingMISMode.GetValueOnRenderThread();
	uint32 VisibleLights = CVarPathTracingVisibleLights.GetValueOnRenderThread();
	PathTracingData.MaxPathIntensity = CVarPathTracingMaxPathIntensity.GetValueOnRenderThread();
	if (PathTracingData.MaxPathIntensity <= 0)
	{
		// cvar clamp disabled, use PPV exposure value instad
		PathTracingData.MaxPathIntensity = FMath::Pow(2.0f, View.FinalPostProcessSettings.PathTracingMaxPathExposure);
	}
	PathTracingData.ApproximateCaustics = CVarPathTracingApproximateCaustics.GetValueOnRenderThread();
	PathTracingData.EnableCameraBackfaceCulling = CVarPathTracingEnableCameraBackfaceCulling.GetValueOnRenderThread();
	PathTracingData.SamplerType = CVarPathTracingSamplerType.GetValueOnRenderThread();
	int32 EnableEmissive = CVarPathTracingEnableEmissive.GetValueOnRenderThread();
	PathTracingData.EnableEmissive = EnableEmissive < 0 ? View.FinalPostProcessSettings.PathTracingEnableEmissive : EnableEmissive;
	PathTracingData.VisualizeLightGrid = CVarPathTracingLightGridVisualize.GetValueOnRenderThread();
	float FilterWidth = CVarPathTracingFilterWidth.GetValueOnRenderThread();
	if (FilterWidth < 0)
	{
		FilterWidth = View.FinalPostProcessSettings.PathTracingFilterWidth;
	}
	PathTracingData.FilterWidth = FilterWidth;

	bool NeedInvalidation = false;

	// If any of the parameters above changed since last time -- reset the accumulation
	// TODO: find something cleaner than just using static variables here. Should all
	// the state used for comparison go into ViewState?
	static uint32 PrevMaxBounces = PathTracingData.MaxBounces;
	if (PathTracingData.MaxBounces != PrevMaxBounces)
	{
		NeedInvalidation = true;
		PrevMaxBounces = PathTracingData.MaxBounces;
	}

	// Changing the number of SSS bounces requires starting over
	static uint32 PrevMaxSSSBounces = PathTracingData.MaxSSSBounces;
	if (PathTracingData.MaxSSSBounces != PrevMaxSSSBounces)
	{
		NeedInvalidation = true;
		PrevMaxSSSBounces = PathTracingData.MaxSSSBounces;
	}

	// Changing MIS mode requires starting over
	static uint32 PreviousMISMode = PathTracingData.MISMode;
	if (PreviousMISMode != PathTracingData.MISMode)
	{
		NeedInvalidation = true;
		PreviousMISMode = PathTracingData.MISMode;
	}

	// Changing VisibleLights requires starting over
	static uint32 PreviousVisibleLights = VisibleLights;
	if (PreviousVisibleLights != VisibleLights)
	{
		NeedInvalidation = true;
		PreviousVisibleLights = VisibleLights;
	}

	// Changing MaxPathIntensity requires starting over
	static float PreviousMaxPathIntensity = PathTracingData.MaxPathIntensity;
	if (PreviousMaxPathIntensity != PathTracingData.MaxPathIntensity)
	{
		NeedInvalidation = true;
		PreviousMaxPathIntensity = PathTracingData.MaxPathIntensity;
	}

	// Changing approximate caustics requires starting over
	static uint32 PreviousApproximateCaustics = PathTracingData.ApproximateCaustics;
	if (PreviousApproximateCaustics != PathTracingData.ApproximateCaustics)
	{
		NeedInvalidation = true;
		PreviousApproximateCaustics = PathTracingData.ApproximateCaustics;
	}

	// Changing filter width requires starting over
	static float PreviousFilterWidth = PathTracingData.FilterWidth;
	if (PreviousFilterWidth != PathTracingData.FilterWidth)
	{
		NeedInvalidation = true;
		PreviousFilterWidth = PathTracingData.FilterWidth;
	}

	// Changing backface culling status requires starting over
	static uint32 PreviousBackfaceCulling = PathTracingData.EnableCameraBackfaceCulling;
	if (PreviousBackfaceCulling != PathTracingData.EnableCameraBackfaceCulling)
	{
		NeedInvalidation = true;
		PreviousBackfaceCulling = PathTracingData.EnableCameraBackfaceCulling;
	}

	// Changing direct lighting requires starting over
	static uint32 PreviousEnableDirectLighting = PathTracingData.EnableDirectLighting;
	if (PreviousEnableDirectLighting != PathTracingData.EnableDirectLighting)
	{
		NeedInvalidation = true;
		PreviousEnableDirectLighting = PathTracingData.EnableDirectLighting;
	}

	// Changing enable emissive requires starting over
	static uint32 PreviousEnableEmissive = PathTracingData.EnableEmissive;
	if (PreviousEnableEmissive != PathTracingData.EnableEmissive)
	{
		NeedInvalidation = true;
		PreviousEnableEmissive = PathTracingData.EnableEmissive;
	}

	// Changing sampler type requires starting over
	static uint32 PreviousSamplerType = PathTracingData.SamplerType;
	if (PreviousSamplerType != PathTracingData.SamplerType)
	{
		NeedInvalidation = true;
		PreviousSamplerType = PathTracingData.SamplerType;
	}

	// Changing visualization mode requires starting over
	static int32 PreviousVisualizeLightGrid = PathTracingData.VisualizeLightGrid;
	if (PreviousVisualizeLightGrid != PathTracingData.VisualizeLightGrid)
	{
		NeedInvalidation = true;
		PreviousVisualizeLightGrid = PathTracingData.VisualizeLightGrid;
	}

	// the rest of PathTracingData and AdaptiveSamplingData is filled in by SetParameters below
	return NeedInvalidation;
}

class FPathTracingSkylightPrepareCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FPathTracingSkylightPrepareCS)
	SHADER_USE_PARAMETER_STRUCT(FPathTracingSkylightPrepareCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		//OutEnvironment.CompilerFlags.Add(CFLAG_WarningsAsErrors);
		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_X"), FComputeShaderUtils::kGolden2DGroupSize);
		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_Y"), FComputeShaderUtils::kGolden2DGroupSize);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_TEXTURE(TextureCube, SkyLightCubemap0)
		SHADER_PARAMETER_TEXTURE(TextureCube, SkyLightCubemap1)
		SHADER_PARAMETER_SAMPLER(SamplerState, SkyLightCubemapSampler0)
		SHADER_PARAMETER_SAMPLER(SamplerState, SkyLightCubemapSampler1)
		SHADER_PARAMETER(float, SkylightBlendFactor)
		SHADER_PARAMETER(float, SkylightInvResolution)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, SkylightTextureOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, SkylightTexturePdf)
		SHADER_PARAMETER(FVector, SkyColor)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_SHADER_TYPE(, FPathTracingSkylightPrepareCS, TEXT("/Engine/Private/PathTracing/PathTracingSkylightPrepare.usf"), TEXT("PathTracingSkylightPrepareCS"), SF_Compute);

class FPathTracingSkylightMISCompensationCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FPathTracingSkylightMISCompensationCS)
	SHADER_USE_PARAMETER_STRUCT(FPathTracingSkylightMISCompensationCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		//OutEnvironment.CompilerFlags.Add(CFLAG_WarningsAsErrors);
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_X"), FComputeShaderUtils::kGolden2DGroupSize);
		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_Y"), FComputeShaderUtils::kGolden2DGroupSize);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, SkylightTexturePdfAverage)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, SkylightTextureOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, SkylightTexturePdf)
		SHADER_PARAMETER(FVector, SkyColor)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_SHADER_TYPE(, FPathTracingSkylightMISCompensationCS, TEXT("/Engine/Private/PathTracing/PathTracingSkylightMISCompensation.usf"), TEXT("PathTracingSkylightMISCompensationCS"), SF_Compute);

// this struct holds a light grid for both building or rendering
BEGIN_SHADER_PARAMETER_STRUCT(FPathTracingLightGrid, RENDERER_API)
	SHADER_PARAMETER(uint32, SceneInfiniteLightCount)
	SHADER_PARAMETER(FVector, SceneLightsBoundMin)
	SHADER_PARAMETER(FVector, SceneLightsBoundMax)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, LightGrid)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, LightGridData)
	SHADER_PARAMETER(unsigned, LightGridResolution)
	SHADER_PARAMETER(unsigned, LightGridMaxCount)
	SHADER_PARAMETER(int, LightGridAxis)
END_SHADER_PARAMETER_STRUCT()

class FPathTracingBuildLightGridCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FPathTracingBuildLightGridCS)
	SHADER_USE_PARAMETER_STRUCT(FPathTracingBuildLightGridCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		//OutEnvironment.CompilerFlags.Add(CFLAG_WarningsAsErrors);
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_X"), FComputeShaderUtils::kGolden2DGroupSize);
		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_Y"), FComputeShaderUtils::kGolden2DGroupSize);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPathTracingLight>, SceneLights)
		SHADER_PARAMETER(uint32, SceneLightCount)
		SHADER_PARAMETER_STRUCT_INCLUDE(FPathTracingLightGrid, LightGridParameters)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWLightGrid)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWLightGridData)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_SHADER_TYPE(, FPathTracingBuildLightGridCS, TEXT("/Engine/Private/PathTracing/PathTracingBuildLightGrid.usf"), TEXT("PathTracingBuildLightGridCS"), SF_Compute);


class FPathTracingRG : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FPathTracingRG)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FPathTracingRG, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform)
			&& FDataDrivenShaderPlatformInfo::GetSupportsPathTracing(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		//OutEnvironment.CompilerFlags.Add(CFLAG_WarningsAsErrors);
		OutEnvironment.SetDefine(TEXT("USE_RECT_LIGHT_TEXTURES"), 1);
	}


	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RadianceTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, AlbedoTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, NormalTexture)
		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_STRUCT_INCLUDE(FPathTracingData, PathTracingData)

		// scene lights
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPathTracingLight>, SceneLights)
		SHADER_PARAMETER(uint32, SceneLightCount)
		SHADER_PARAMETER(uint32, SceneVisibleLightCount)
		SHADER_PARAMETER_STRUCT_INCLUDE(FPathTracingLightGrid, LightGridParameters)

		// Skylight
		SHADER_PARAMETER_STRUCT_INCLUDE(FPathTracingSkylight, SkylightParameters)

		// IES Profiles
		SHADER_PARAMETER_RDG_TEXTURE(Texture2DArray, IESTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, IESTextureSampler) // Shared sampler for all IES profiles
		// Rect lights
		SHADER_PARAMETER_TEXTURE_ARRAY(Texture2D, RectLightTexture, [PATHTRACER_MAX_RECT_TEXTURES])
		SHADER_PARAMETER_SAMPLER(SamplerState, RectLightSampler) // Shared sampler for all rectlights
		// Subsurface data
		SHADER_PARAMETER_TEXTURE(Texture2D, SSProfilesTexture)
		// Used by multi-GPU rendering
		SHADER_PARAMETER(FIntVector, TileOffset)	
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FPathTracingRG, "/Engine/Private/PathTracing/PathTracing.usf", "PathTracingMainRG", SF_RayGen);

class FPathTracingIESAtlasCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FPathTracingIESAtlasCS)
	SHADER_USE_PARAMETER_STRUCT(FPathTracingIESAtlasCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		//OutEnvironment.CompilerFlags.Add(CFLAG_WarningsAsErrors);
		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_X"), FComputeShaderUtils::kGolden2DGroupSize);
		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_Y"), FComputeShaderUtils::kGolden2DGroupSize);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_TEXTURE(Texture2D, IESTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, IESSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray, IESAtlas)
		SHADER_PARAMETER(int32, IESAtlasSlice)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_SHADER_TYPE(, FPathTracingIESAtlasCS, TEXT("/Engine/Private/PathTracing/PathTracingIESAtlas.usf"), TEXT("PathTracingIESAtlasCS"), SF_Compute);

RENDERER_API void PrepareSkyTexture_Internal(
	FRDGBuilder& GraphBuilder,
	FReflectionUniformParameters& Parameters,
	uint32 Size,
	FLinearColor SkyColor,
	bool UseMISCompensation,

	// Out
	FRDGTextureRef& SkylightTexture,
	FRDGTextureRef& SkylightPdf,
	float& SkylightInvResolution,
	int32& SkylightMipCount
)
{
	FRDGTextureDesc SkylightTextureDesc = FRDGTextureDesc::Create2D(
		FIntPoint(Size, Size),
		PF_A32B32G32R32F, // half precision might be ok?
		FClearValueBinding::None,
		TexCreate_ShaderResource | TexCreate_UAV);

	SkylightTexture = GraphBuilder.CreateTexture(SkylightTextureDesc, TEXT("PathTracer.Skylight"), ERDGTextureFlags::None);

	FRDGTextureDesc SkylightPdfDesc = FRDGTextureDesc::Create2D(
		FIntPoint(Size, Size),
		PF_R32_FLOAT, // half precision might be ok?
		FClearValueBinding::None,
		TexCreate_ShaderResource | TexCreate_UAV,
		FMath::CeilLogTwo(Size) + 1);

	SkylightPdf = GraphBuilder.CreateTexture(SkylightPdfDesc, TEXT("PathTracer.SkylightPdf"), ERDGTextureFlags::None);

	SkylightInvResolution = 1.0f / Size;
	SkylightMipCount = SkylightPdfDesc.NumMips;

	// run a simple compute shader to sample the cubemap and prep the top level of the mipmap hierarchy
	{
		TShaderMapRef<FPathTracingSkylightPrepareCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FPathTracingSkylightPrepareCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPathTracingSkylightPrepareCS::FParameters>();
		PassParameters->SkyColor = FVector(SkyColor.R, SkyColor.G, SkyColor.B);
		PassParameters->SkyLightCubemap0 = Parameters.SkyLightCubemap;
		PassParameters->SkyLightCubemap1 = Parameters.SkyLightBlendDestinationCubemap;
		PassParameters->SkyLightCubemapSampler0 = Parameters.SkyLightCubemapSampler;
		PassParameters->SkyLightCubemapSampler1 = Parameters.SkyLightBlendDestinationCubemapSampler;
		PassParameters->SkylightBlendFactor = Parameters.SkyLightParameters.W;
		PassParameters->SkylightInvResolution = SkylightInvResolution;
		PassParameters->SkylightTextureOutput = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(SkylightTexture, 0));
		PassParameters->SkylightTexturePdf = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(SkylightPdf, 0));
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("SkylightPrepare"),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(FIntPoint(Size, Size), FComputeShaderUtils::kGolden2DGroupSize));
	}
	FGenerateMips::ExecuteCompute(GraphBuilder, SkylightPdf, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());

	if (UseMISCompensation)
	{
		TShaderMapRef<FPathTracingSkylightMISCompensationCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FPathTracingSkylightMISCompensationCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPathTracingSkylightMISCompensationCS::FParameters>();
		PassParameters->SkylightTexturePdfAverage = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(SkylightPdf, SkylightMipCount - 1));
		PassParameters->SkylightTextureOutput = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(SkylightTexture, 0));
		PassParameters->SkylightTexturePdf = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(SkylightPdf, 0));
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("SkylightMISCompensation"),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(FIntPoint(Size, Size), FComputeShaderUtils::kGolden2DGroupSize));
		FGenerateMips::ExecuteCompute(GraphBuilder, SkylightPdf, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
	}
}

RENDERER_API FRDGTexture* PrepareIESAtlas(const TMap<FTexture*, int>& InIESLightProfilesMap, FRDGBuilder& GraphBuilder)
{
	// We found some IES profiles to use -- upload them into a single atlas so we can access them easily in HLSL

	// TODO: This is redundant because all the IES textures are already on the GPU. Handling IES profiles via Miss shaders
	// would be cleaner.
	
	// TODO: This is also redundant with the logic in RayTracingLighting.cpp, but the latter is limitted to 1D profiles and 
	// does not consider the same set of lights as the path tracer. Longer term we should aim to unify the representation of lights
	// across both passes
	
	// TODO: This process is repeated every frame! More motivation to move to a Miss shader based implementation
	
	// This size matches the import resolution of light profiles (see FIESLoader::GetWidth)
	const int kIESAtlasSize = 256;
	const int NumSlices = InIESLightProfilesMap.Num();
	FRDGTextureDesc IESTextureDesc = FRDGTextureDesc::Create2DArray(
		FIntPoint(kIESAtlasSize, kIESAtlasSize),
		PF_R32_FLOAT,
		FClearValueBinding::None,
		TexCreate_ShaderResource | TexCreate_UAV,
		NumSlices);
	FRDGTexture* IESTexture = GraphBuilder.CreateTexture(IESTextureDesc, TEXT("PathTracer.IESAtlas"), ERDGTextureFlags::None);

	for (auto&& Entry : InIESLightProfilesMap)
	{
		FPathTracingIESAtlasCS::FParameters* AtlasPassParameters = GraphBuilder.AllocParameters<FPathTracingIESAtlasCS::FParameters>();
		const int Slice = Entry.Value;
		AtlasPassParameters->IESTexture = Entry.Key->TextureRHI;
		AtlasPassParameters->IESSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		AtlasPassParameters->IESAtlas = GraphBuilder.CreateUAV(IESTexture);
		AtlasPassParameters->IESAtlasSlice = Slice;
		TShaderMapRef<FPathTracingIESAtlasCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Path Tracing IES Atlas (Slice=%d)", Slice),
			ComputeShader,
			AtlasPassParameters,
			FComputeShaderUtils::GetGroupCount(FIntPoint(kIESAtlasSize, kIESAtlasSize), FComputeShaderUtils::kGolden2DGroupSize));
	}

	return IESTexture;
}

RDG_REGISTER_BLACKBOARD_STRUCT(FPathTracingSkylight)

bool PrepareSkyTexture(FRDGBuilder& GraphBuilder, FScene* Scene, const FViewInfo& View, bool SkylightEnabled, bool UseMISCompensation, FPathTracingSkylight* SkylightParameters)
{
	SkylightParameters->SkylightTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	FReflectionUniformParameters Parameters;
	SetupReflectionUniformParameters(View, Parameters);
	if (!SkylightEnabled || !(Parameters.SkyLightParameters.Y > 0))
	{
		// textures not ready, or skylight not active
		// just put in a placeholder
		SkylightParameters->SkylightTexture = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
		SkylightParameters->SkylightPdf = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
		SkylightParameters->SkylightInvResolution = 0;
		SkylightParameters->SkylightMipCount = 0;
		return false;
	}

	// the sky is actually enabled, lets see if someone already made use of it for this frame
	const FPathTracingSkylight* PreviousSkylightParameters = GraphBuilder.Blackboard.Get<FPathTracingSkylight>();
	if (PreviousSkylightParameters != nullptr)
	{
		*SkylightParameters = *PreviousSkylightParameters;
		return true;
	}

	// should we remember the skylight prep for the next frame?
	const bool IsSkylightCachingEnabled = CVarPathTracingSkylightCaching.GetValueOnAnyThread() != 0;

	if (!IsSkylightCachingEnabled)
	{
		// we don't want any caching - release what we might have been holding onto
		Scene->PathTracingSkylightTexture.SafeRelease();
		Scene->PathTracingSkylightPdf.SafeRelease();
	}

	if (Scene->PathTracingSkylightTexture.IsValid() &&
		Scene->PathTracingSkylightPdf.IsValid())
	{
		// we already have a valid texture and pdf, just re-use them!
		// it is the responsability of code that may invalidate the contents to reset these pointers
		SkylightParameters->SkylightTexture = GraphBuilder.RegisterExternalTexture(Scene->PathTracingSkylightTexture, TEXT("PathTracer.Skylight"));
		SkylightParameters->SkylightPdf = GraphBuilder.RegisterExternalTexture(Scene->PathTracingSkylightPdf, TEXT("PathTracer.SkylightPdf"));
		SkylightParameters->SkylightInvResolution = 1.0f / SkylightParameters->SkylightTexture->Desc.GetSize().X;
		SkylightParameters->SkylightMipCount = SkylightParameters->SkylightPdf->Desc.NumMips;
		return true;
	}
	RDG_EVENT_SCOPE(GraphBuilder, "Path Tracing SkylightPrepare");

	FLinearColor SkyColor = Scene->SkyLight->GetEffectiveLightColor();
	// since we are resampled into an octahedral layout, we multiply the cubemap resolution by 2 to get roughly the same number of texels
	uint32 Size = FMath::RoundUpToPowerOfTwo(2 * Scene->SkyLight->CaptureCubeMapResolution);
	
	RDG_GPU_MASK_SCOPE(GraphBuilder, 
		IsSkylightCachingEnabled ? FRHIGPUMask::All() : GraphBuilder.RHICmdList.GetGPUMask());

	PrepareSkyTexture_Internal(
		GraphBuilder,
		Parameters,
		Size,
		SkyColor,
		UseMISCompensation,
		// Out
		SkylightParameters->SkylightTexture,
		SkylightParameters->SkylightPdf,
		SkylightParameters->SkylightInvResolution,
		SkylightParameters->SkylightMipCount
	);

	// hang onto these for next time (if caching is enabled)
	if (IsSkylightCachingEnabled)
	{
		GraphBuilder.QueueTextureExtraction(SkylightParameters->SkylightTexture, &Scene->PathTracingSkylightTexture);
		GraphBuilder.QueueTextureExtraction(SkylightParameters->SkylightPdf, &Scene->PathTracingSkylightPdf);
	}

	// remember the skylight parameters for future passes within this frame
	GraphBuilder.Blackboard.Create<FPathTracingSkylight>() = *SkylightParameters;

	return true;
}

RENDERER_API void PrepareLightGrid(FRDGBuilder& GraphBuilder, FPathTracingLightGrid* LightGridParameters, const FPathTracingLight* Lights, uint32 NumLights, uint32 NumInfiniteLights, FRDGBufferSRV* LightsSRV)
{
	const float Inf = std::numeric_limits<float>::infinity();
	LightGridParameters->SceneInfiniteLightCount = NumInfiniteLights;
	LightGridParameters->SceneLightsBoundMin = FVector(+Inf, +Inf, +Inf);
	LightGridParameters->SceneLightsBoundMax = FVector(-Inf, -Inf, -Inf);
	LightGridParameters->LightGrid = nullptr;
	LightGridParameters->LightGridData = nullptr;

	int NumFiniteLights = NumLights - NumInfiniteLights;
	// if we have some finite lights -- build a light grid
	if (NumFiniteLights > 0)
	{
		// get bounding box of all finite lights
		const FPathTracingLight* FiniteLights = Lights + NumInfiniteLights;
		for (int Index = 0; Index < NumFiniteLights; Index++)
		{
			const FPathTracingLight& Light = FiniteLights[Index];
			LightGridParameters->SceneLightsBoundMin = LightGridParameters->SceneLightsBoundMin.ComponentMin(Light.BoundMin);
			LightGridParameters->SceneLightsBoundMax = LightGridParameters->SceneLightsBoundMax.ComponentMax(Light.BoundMax);
		}

		const uint32 Resolution = FMath::RoundUpToPowerOfTwo(CVarPathTracingLightGridResolution.GetValueOnRenderThread());
		const uint32 MaxCount = FMath::Clamp(
			CVarPathTracingLightGridMaxCount.GetValueOnRenderThread(),
			1,
			FMath::Min(NumFiniteLights, RAY_TRACING_LIGHT_COUNT_MAXIMUM)
		);
		LightGridParameters->LightGridResolution = Resolution;
		LightGridParameters->LightGridMaxCount = MaxCount;

		// pick the shortest axis
		FVector Diag = LightGridParameters->SceneLightsBoundMax - LightGridParameters->SceneLightsBoundMin;
		if (Diag.X < Diag.Y && Diag.X < Diag.Z)
		{
			LightGridParameters->LightGridAxis = 0;
		}
		else if (Diag.Y < Diag.Z)
		{
			LightGridParameters->LightGridAxis = 1;
		}
		else
		{
			LightGridParameters->LightGridAxis = 2;
		}

		FPathTracingBuildLightGridCS::FParameters* LightGridPassParameters = GraphBuilder.AllocParameters< FPathTracingBuildLightGridCS::FParameters>();

		FRDGTextureDesc LightGridDesc = FRDGTextureDesc::Create2D(
			FIntPoint(Resolution, Resolution),
			PF_R32_UINT,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV);
		FRDGTexture* LightGridTexture = GraphBuilder.CreateTexture(LightGridDesc, TEXT("PathTracer.LightGrid"), ERDGTextureFlags::None);
		LightGridPassParameters->RWLightGrid = GraphBuilder.CreateUAV(LightGridTexture);

		EPixelFormat LightGridDataFormat = PF_R32_UINT;
		size_t LightGridDataNumBytes = sizeof(uint32);
		if (NumLights <= (MAX_uint8 + 1))
		{
			LightGridDataFormat = PF_R8_UINT;
			LightGridDataNumBytes = sizeof(uint8);
		}
		else if (NumLights <= (MAX_uint16 + 1))
		{
			LightGridDataFormat = PF_R16_UINT;
			LightGridDataNumBytes = sizeof(uint16);
		}
		FRDGBufferDesc LightGridDataDesc = FRDGBufferDesc::CreateBufferDesc(LightGridDataNumBytes, MaxCount * Resolution * Resolution);
		FRDGBuffer* LightGridData = GraphBuilder.CreateBuffer(LightGridDataDesc, TEXT("PathTracer.LightGridData"));
		LightGridPassParameters->RWLightGridData = GraphBuilder.CreateUAV(LightGridData, LightGridDataFormat);
		LightGridPassParameters->LightGridParameters = *LightGridParameters;
		LightGridPassParameters->SceneLights = LightsSRV;
		LightGridPassParameters->SceneLightCount = NumLights;

		TShaderMapRef<FPathTracingBuildLightGridCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Light Grid Create (%u lights)", NumFiniteLights),
			ComputeShader,
			LightGridPassParameters,
			FComputeShaderUtils::GetGroupCount(FIntPoint(Resolution, Resolution), FComputeShaderUtils::kGolden2DGroupSize));

		// hookup to the actual rendering pass
		LightGridParameters->LightGrid = LightGridTexture;
		LightGridParameters->LightGridData = GraphBuilder.CreateSRV(LightGridData, LightGridDataFormat);


	}
	else
	{
		// light grid is not needed - just hookup dummy data
		LightGridParameters->LightGridResolution = 0;
		LightGridParameters->LightGridMaxCount = 0;
		LightGridParameters->LightGridAxis = 0;
		LightGridParameters->LightGrid = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
		FRDGBufferDesc LightGridDataDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1);
		FRDGBuffer* LightGridData = GraphBuilder.CreateBuffer(LightGridDataDesc, TEXT("PathTracer.LightGridData"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(LightGridData, PF_R32_UINT), 0);
		LightGridParameters->LightGridData = GraphBuilder.CreateSRV(LightGridData, PF_R32_UINT);
	}
}

void SetLightParameters(FRDGBuilder& GraphBuilder, FPathTracingRG::FParameters* PassParameters, FScene* Scene, const FViewInfo& View, bool UseMISCompensation)
{
	PassParameters->SceneVisibleLightCount = 0;

	// Lights
	uint32 MaxNumLights = 1 + Scene->Lights.Num(); // upper bound
	// Allocate from the graph builder so that we don't need to copy the data again when queuing the upload
	FPathTracingLight* Lights = (FPathTracingLight*) GraphBuilder.Alloc(sizeof(FPathTracingLight) * MaxNumLights, 16);
	uint32 NumLights = 0;

	// Prepend SkyLight to light buffer since it is not part of the regular light list
	const float Inf = std::numeric_limits<float>::infinity();
	if (PrepareSkyTexture(GraphBuilder, Scene, View, true, UseMISCompensation, &PassParameters->SkylightParameters))
	{
		check(Scene->SkyLight != nullptr);
		FPathTracingLight& DestLight = Lights[NumLights++];
		DestLight.Color = FVector(1, 1, 1); // not used (it is folded into the importance table directly)
		DestLight.Flags = Scene->SkyLight->bTransmission ? PATHTRACER_FLAG_TRANSMISSION_MASK : 0;
		DestLight.Flags |= PATHTRACER_FLAG_LIGHTING_CHANNEL_MASK;
		DestLight.Flags |= PATHTRACING_LIGHT_SKY;
		DestLight.Flags |= Scene->SkyLight->bCastShadows ? PATHTRACER_FLAG_CAST_SHADOW_MASK : 0;
		DestLight.IESTextureSlice = -1;
		DestLight.BoundMin = FVector(-Inf, -Inf, -Inf);
		DestLight.BoundMax = FVector( Inf,  Inf,  Inf);
		if (Scene->SkyLight->bRealTimeCaptureEnabled)
		{
			// When using the realtime capture system, always make the skylight visible
			// because this is our only way of "seeing" the atmo/clouds at the moment
			PassParameters->SceneVisibleLightCount = 1;
		}
	}

	// Add directional lights next (all lights with infinite bounds should come first)
	if (View.Family->EngineShowFlags.DirectionalLights)
	{
		for (auto Light : Scene->Lights)
		{
			ELightComponentType LightComponentType = (ELightComponentType)Light.LightSceneInfo->Proxy->GetLightType();

			if (LightComponentType != LightType_Directional)
			{
				continue;
			}

			FLightShaderParameters LightParameters;
			Light.LightSceneInfo->Proxy->GetLightShaderParameters(LightParameters);

			if (LightParameters.Color.IsZero())
			{
				continue;
			}

			FPathTracingLight& DestLight = Lights[NumLights++];
			uint32 Transmission = Light.LightSceneInfo->Proxy->Transmission();
			uint8 LightingChannelMask = Light.LightSceneInfo->Proxy->GetLightingChannelMask();

			DestLight.Flags = Transmission ? PATHTRACER_FLAG_TRANSMISSION_MASK : 0;
			DestLight.Flags |= LightingChannelMask & PATHTRACER_FLAG_LIGHTING_CHANNEL_MASK;
			DestLight.Flags |= Light.LightSceneInfo->Proxy->CastsDynamicShadow() ? PATHTRACER_FLAG_CAST_SHADOW_MASK : 0;
			DestLight.IESTextureSlice = -1;
			DestLight.RectLightTextureIndex = -1;

			// these mean roughly the same thing across all light types
			DestLight.Color = LightParameters.Color;
			DestLight.Position = LightParameters.Position;
			DestLight.Normal = -LightParameters.Direction;
			DestLight.dPdu = FVector::CrossProduct(LightParameters.Tangent, LightParameters.Direction);
			DestLight.dPdv = LightParameters.Tangent;
			DestLight.Attenuation = LightParameters.InvRadius;
			DestLight.FalloffExponent = 0;

			DestLight.Normal = LightParameters.Direction;
			DestLight.Dimensions = FVector(LightParameters.SourceRadius, LightParameters.SoftSourceRadius, 0.0f);
			DestLight.Flags |= PATHTRACING_LIGHT_DIRECTIONAL;

			DestLight.BoundMin = FVector(-Inf, -Inf, -Inf);
			DestLight.BoundMax = FVector( Inf,  Inf,  Inf);
		}
	}

	uint32 NumInfiniteLights = NumLights;

	int32 NextRectTextureIndex = 0;

	TMap<FTexture*, int> IESLightProfilesMap;
	for (auto Light : Scene->Lights)
	{
		ELightComponentType LightComponentType = (ELightComponentType)Light.LightSceneInfo->Proxy->GetLightType();

		if ( (LightComponentType == LightType_Directional) /* already handled by the loop above */  ||
			((LightComponentType == LightType_Rect       ) && !View.Family->EngineShowFlags.RectLights       ) ||
			((LightComponentType == LightType_Spot       ) && !View.Family->EngineShowFlags.SpotLights       ) ||
			((LightComponentType == LightType_Point      ) && !View.Family->EngineShowFlags.PointLights      ))
		{
			// This light type is not currently enabled
			continue;
		}

		FLightShaderParameters LightParameters;
		Light.LightSceneInfo->Proxy->GetLightShaderParameters(LightParameters);

		if (LightParameters.Color.IsZero())
		{
			continue;
		}

		FPathTracingLight& DestLight = Lights[NumLights++];

		uint32 Transmission = Light.LightSceneInfo->Proxy->Transmission();
		uint8 LightingChannelMask = Light.LightSceneInfo->Proxy->GetLightingChannelMask();

		DestLight.Flags = Transmission ? PATHTRACER_FLAG_TRANSMISSION_MASK : 0;
		DestLight.Flags |= LightingChannelMask & PATHTRACER_FLAG_LIGHTING_CHANNEL_MASK;
		DestLight.Flags |= Light.LightSceneInfo->Proxy->CastsDynamicShadow() ? PATHTRACER_FLAG_CAST_SHADOW_MASK : 0;
		DestLight.IESTextureSlice = -1;
		DestLight.RectLightTextureIndex = -1;

		if (View.Family->EngineShowFlags.TexturedLightProfiles)
		{
			FTexture* IESTexture = Light.LightSceneInfo->Proxy->GetIESTextureResource();
			if (IESTexture != nullptr)
			{
				// Only add a given texture once
				DestLight.IESTextureSlice = IESLightProfilesMap.FindOrAdd(IESTexture, IESLightProfilesMap.Num());
			}
		}

		// these mean roughly the same thing across all light types
		DestLight.Color = LightParameters.Color;
		DestLight.Position = LightParameters.Position;
		DestLight.Normal = -LightParameters.Direction;
		DestLight.dPdu = FVector::CrossProduct(LightParameters.Tangent, LightParameters.Direction);
		DestLight.dPdv = LightParameters.Tangent;
		DestLight.Attenuation = LightParameters.InvRadius;
		DestLight.FalloffExponent = 0;

		switch (LightComponentType)
		{
			case LightType_Rect:
			{
				DestLight.Dimensions = FVector(2.0f * LightParameters.SourceRadius, 2.0f * LightParameters.SourceLength, 0.0f);
				DestLight.Shaping = FVector2D(LightParameters.RectLightBarnCosAngle, LightParameters.RectLightBarnLength);
				DestLight.FalloffExponent = LightParameters.FalloffExponent;
				DestLight.Flags |= Light.LightSceneInfo->Proxy->IsInverseSquared() ? 0 : PATHTRACER_FLAG_NON_INVERSE_SQUARE_FALLOFF_MASK;
				DestLight.Flags |= PATHTRACING_LIGHT_RECT;
				if (Light.LightSceneInfo->Proxy->HasSourceTexture())
				{
					// there is an actual texture associated with this light, go look for it
					FLightShaderParameters ShaderParameters;
					Light.LightSceneInfo->Proxy->GetLightShaderParameters(ShaderParameters);
					FRHITexture* TextureRHI = ShaderParameters.SourceTexture;
					if (TextureRHI != nullptr)
					{
						// have we already given this texture an index?
						// NOTE: linear search is ok since max texture is small
						for (int Index = 0; Index < NextRectTextureIndex; Index++)
						{
							if (PassParameters->RectLightTexture[Index] == TextureRHI)
							{
								DestLight.RectLightTextureIndex = Index;
								break;
							}
						}
						if (DestLight.RectLightTextureIndex == -1 && NextRectTextureIndex < PATHTRACER_MAX_RECT_TEXTURES)
						{
							// first time we see this texture and we still have free slots available
							// assign texture to next slot and store it in the light
							DestLight.RectLightTextureIndex = NextRectTextureIndex;
							PassParameters->RectLightTexture[NextRectTextureIndex] = TextureRHI;
							NextRectTextureIndex++;
						}
					}
				}

				float Radius = 1.0f / LightParameters.InvRadius;
				FVector Center = DestLight.Position;
				FVector Normal = DestLight.Normal;
				FVector Disc = FVector(
					FMath::Sqrt(FMath::Clamp(1 - Normal.X * Normal.X, 0.0f, 1.0f)),
					FMath::Sqrt(FMath::Clamp(1 - Normal.Y * Normal.Y, 0.0f, 1.0f)),
					FMath::Sqrt(FMath::Clamp(1 - Normal.Z * Normal.Z, 0.0f, 1.0f))
				);
				// quad bbox is the bbox of the disc +  the tip of the hemisphere
				// TODO: is it worth trying to account for barndoors? seems unlikely to cut much empty space since the volume _inside_ the barndoor receives light
				FVector Tip = Center + Normal * Radius;
				DestLight.BoundMin = Tip.ComponentMin(Center - Radius * Disc);
				DestLight.BoundMax = Tip.ComponentMax(Center + Radius * Disc);
				break;
			}
			case LightType_Spot:
			{
				DestLight.Dimensions = FVector(LightParameters.SourceRadius, LightParameters.SoftSourceRadius, LightParameters.SourceLength);
				DestLight.Shaping = LightParameters.SpotAngles;
				DestLight.FalloffExponent = LightParameters.FalloffExponent;
				DestLight.Flags |= Light.LightSceneInfo->Proxy->IsInverseSquared() ? 0 : PATHTRACER_FLAG_NON_INVERSE_SQUARE_FALLOFF_MASK;
				DestLight.Flags |= PATHTRACING_LIGHT_SPOT;

				float Radius = 1.0f / LightParameters.InvRadius;
				FVector Center = DestLight.Position;
				FVector Normal = DestLight.Normal;
				FVector Disc = FVector(
					FMath::Sqrt(FMath::Clamp(1 - Normal.X * Normal.X, 0.0f, 1.0f)),
					FMath::Sqrt(FMath::Clamp(1 - Normal.Y * Normal.Y, 0.0f, 1.0f)),
					FMath::Sqrt(FMath::Clamp(1 - Normal.Z * Normal.Z, 0.0f, 1.0f))
				);
				// box around ray from light center to tip of the cone
				FVector Tip = Center + Normal * Radius;
				DestLight.BoundMin = Center.ComponentMin(Tip);
				DestLight.BoundMax = Center.ComponentMax(Tip);
				// expand by disc around the farthest part of the cone

				float CosOuter = LightParameters.SpotAngles.X;
				float SinOuter = FMath::Sqrt(1.0f - CosOuter * CosOuter);

				DestLight.BoundMin = DestLight.BoundMin.ComponentMin(Center + Radius * (Normal * CosOuter - Disc * SinOuter));
				DestLight.BoundMax = DestLight.BoundMax.ComponentMax(Center + Radius * (Normal * CosOuter + Disc * SinOuter));
				break;
			}
			case LightType_Point:
			{
				DestLight.Dimensions = FVector(LightParameters.SourceRadius, LightParameters.SoftSourceRadius, LightParameters.SourceLength);
				DestLight.FalloffExponent = LightParameters.FalloffExponent;
				DestLight.Flags |= Light.LightSceneInfo->Proxy->IsInverseSquared() ? 0 : PATHTRACER_FLAG_NON_INVERSE_SQUARE_FALLOFF_MASK;
				DestLight.Flags |= PATHTRACING_LIGHT_POINT;
				float Radius = 1.0f / LightParameters.InvRadius;
				FVector Center = DestLight.Position;
				// simple sphere of influence
				DestLight.BoundMin = Center - FVector(Radius, Radius, Radius);
				DestLight.BoundMax = Center + FVector(Radius, Radius, Radius);
				break;
			}
			default:
			{
				// Just in case someone adds a new light type one day ...
				checkNoEntry();
				break;
			}
		}
	}

	{
		// assign dummy textures to the remaining unused slots
		for (int32 Index = NextRectTextureIndex; Index < PATHTRACER_MAX_RECT_TEXTURES; Index++)
		{
			PassParameters->RectLightTexture[Index] = GWhiteTexture->TextureRHI;
		}
		PassParameters->RectLightSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	}

	PassParameters->SceneLightCount = NumLights;
	{
		// Upload the buffer of lights to the GPU
		uint32 NumCopyLights = FMath::Max(1u, NumLights); // need at least one since zero-sized buffers are not allowed
		size_t DataSize = sizeof(FPathTracingLight) * NumCopyLights;
		PassParameters->SceneLights = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(CreateStructuredBuffer(GraphBuilder, TEXT("PathTracer.LightsBuffer"), sizeof(FPathTracingLight), NumCopyLights, Lights, DataSize, ERDGInitialDataFlags::NoCopy)));
	}

	if (CVarPathTracingVisibleLights.GetValueOnRenderThread() != 0)
	{
		PassParameters->SceneVisibleLightCount = PassParameters->SceneLightCount;
	}

	if (IESLightProfilesMap.Num() > 0)
	{
		PassParameters->IESTexture = PrepareIESAtlas(IESLightProfilesMap, GraphBuilder);
	}
	else
	{
		PassParameters->IESTexture = GraphBuilder.RegisterExternalTexture(GSystemTextures.WhiteDummy);
	}


	PrepareLightGrid(GraphBuilder, &PassParameters->LightGridParameters, Lights, NumLights, NumInfiniteLights, PassParameters->SceneLights);
}

class FPathTracingCompositorPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FPathTracingCompositorPS)
	SHADER_USE_PARAMETER_STRUCT(FPathTracingCompositorPS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, RadianceTexture)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER(uint32, Iteration)
		SHADER_PARAMETER(uint32, MaxSamples)
		SHADER_PARAMETER(int, ProgressDisplayEnabled)

		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_SHADER_TYPE(, FPathTracingCompositorPS, TEXT("/Engine/Private/PathTracing/PathTracingCompositingPixelShader.usf"), TEXT("CompositeMain"), SF_Pixel);

void FDeferredShadingSceneRenderer::PreparePathTracing(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	if (View.RayTracingRenderMode == ERayTracingRenderMode::PathTracing
		&& FDataDrivenShaderPlatformInfo::GetSupportsPathTracing(View.GetShaderPlatform()))
	{
		// Declare all RayGen shaders that require material closest hit shaders to be bound
		auto RayGenShader = View.ShaderMap->GetShader<FPathTracingRG>();
		OutRayGenShaders.Add(RayGenShader.GetRayTracingShader());
	}
}

void FSceneViewState::PathTracingInvalidate()
{
	PathTracingRadianceRT.SafeRelease();
	PathTracingAlbedoRT.SafeRelease();
	PathTracingNormalRT.SafeRelease();
	PathTracingRadianceDenoisedRT.SafeRelease();
	PathTracingSampleIndex = 0;
}

BEGIN_SHADER_PARAMETER_STRUCT(FDenoiseTextureParameters, )
	RDG_TEXTURE_ACCESS(InputTexture, ERHIAccess::CopySrc)
	RDG_TEXTURE_ACCESS(InputAlbedo, ERHIAccess::CopySrc)
	RDG_TEXTURE_ACCESS(InputNormal, ERHIAccess::CopySrc)
	RDG_TEXTURE_ACCESS(OutputTexture, ERHIAccess::CopyDest)
END_SHADER_PARAMETER_STRUCT()

DECLARE_GPU_STAT_NAMED(Stat_GPU_PathTracing, TEXT("Path Tracing"));
void FDeferredShadingSceneRenderer::RenderPathTracing(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
	FRDGTextureRef SceneColorOutputTexture)
{
	RDG_GPU_STAT_SCOPE(GraphBuilder, Stat_GPU_PathTracing);
	RDG_EVENT_SCOPE(GraphBuilder, "Path Tracing");

	if (!ensureMsgf(FDataDrivenShaderPlatformInfo::GetSupportsPathTracing(View.GetShaderPlatform()),
		TEXT("Attempting to use path tracing on unsupported platform.")))
	{
		return;
	}

	bool bArgsChanged = false;

	// Get current value of MaxSPP and reset render if it has changed
	// NOTE: we ignore the CVar when using offline rendering
	int32 SamplesPerPixelCVar = View.bIsOfflineRender ? -1 : CVarPathTracingSamplesPerPixel.GetValueOnRenderThread();
	uint32 MaxSPP = SamplesPerPixelCVar > -1 ? SamplesPerPixelCVar : View.FinalPostProcessSettings.PathTracingSamplesPerPixel;
	MaxSPP = FMath::Max(MaxSPP, 1u);
	if (View.ViewState->PathTracingTargetSPP != MaxSPP)
	{
		// Store MaxSPP in the view state because we may have multiple views, each targetting a different sample count
		View.ViewState->PathTracingTargetSPP = MaxSPP;
		bArgsChanged = true;
	}
	// Changing FrameIndependentTemporalSeed requires starting over
	bool LockedSamplingPattern = CVarPathTracingFrameIndependentTemporalSeed.GetValueOnRenderThread() == 0;
	static bool PreviousLockedSamplingPattern = LockedSamplingPattern;
	if (PreviousLockedSamplingPattern != LockedSamplingPattern)
	{
		PreviousLockedSamplingPattern = LockedSamplingPattern;
		bArgsChanged = true;
	}

	// compute an integer code of what show flags related to lights are currently enabled so we can detect changes
	int CurrentLightShowFlags = 0;
	CurrentLightShowFlags |= View.Family->EngineShowFlags.SkyLighting           ? 1 << 0 : 0;
	CurrentLightShowFlags |= View.Family->EngineShowFlags.DirectionalLights     ? 1 << 1 : 0;
	CurrentLightShowFlags |= View.Family->EngineShowFlags.RectLights            ? 1 << 2 : 0;
	CurrentLightShowFlags |= View.Family->EngineShowFlags.SpotLights            ? 1 << 3 : 0;
	CurrentLightShowFlags |= View.Family->EngineShowFlags.PointLights           ? 1 << 4 : 0;
	CurrentLightShowFlags |= View.Family->EngineShowFlags.TexturedLightProfiles ? 1 << 5 : 0;

	static int PreviousLightShowFlags = CurrentLightShowFlags;
	if (PreviousLightShowFlags != CurrentLightShowFlags)
	{
		PreviousLightShowFlags = CurrentLightShowFlags;
		bArgsChanged = true;
	}

	bool UseMISCompensation = CVarPathTracingMISMode.GetValueOnRenderThread() == 2 && CVarPathTracingMISCompensation.GetValueOnRenderThread() != 0;
	static bool PreviousUseMISCompensation = UseMISCompensation;
	if (PreviousUseMISCompensation != UseMISCompensation)
	{
		PreviousUseMISCompensation = UseMISCompensation;
		bArgsChanged = true;
		// if the mode changes we need to rebuild the importance table
		Scene->PathTracingSkylightTexture.SafeRelease();
		Scene->PathTracingSkylightPdf.SafeRelease();
	}

	const uint32 LightGridResolution = FMath::RoundUpToPowerOfTwo(CVarPathTracingLightGridResolution.GetValueOnRenderThread());
	const uint32 LightGridMaxCount = FMath::Clamp(CVarPathTracingLightGridMaxCount.GetValueOnRenderThread(), 1, RAY_TRACING_LIGHT_COUNT_MAXIMUM);

	static uint32 PreviousLightGridResolution = LightGridResolution;
	if (PreviousLightGridResolution != LightGridResolution)
	{
		PreviousLightGridResolution = LightGridResolution;
		bArgsChanged = true;
	}

	static uint32_t PreviousLightGridMaxCount = LightGridMaxCount;
	if (PreviousLightGridMaxCount != LightGridMaxCount)
	{
		PreviousLightGridMaxCount = LightGridMaxCount;
		bArgsChanged = true;
	}

	// Get other basic path tracing settings and see if we need to invalidate the current state
	FPathTracingData PathTracingData;
	bArgsChanged |= PrepareShaderArgs(View, PathTracingData);

	// If the scene has changed in some way (camera move, object movement, etc ...)
	// we must invalidate the ViewState to start over from scratch
	if (bArgsChanged || View.ViewState->PathTracingRect != View.ViewRect)
	{
		View.ViewState->PathTracingInvalidate();
		View.ViewState->PathTracingRect = View.ViewRect;
	}

	// Setup temporal seed _after_ invalidation in case we got reset
	if (LockedSamplingPattern)
	{
		// Count samples from 0 for deterministic results
		PathTracingData.TemporalSeed = View.ViewState->PathTracingSampleIndex;
	}
	else
	{
		// Count samples from an ever-increasing counter to avoid screen-door effect
		PathTracingData.TemporalSeed = View.ViewState->PathTracingFrameIndex;
	}
	PathTracingData.Iteration = View.ViewState->PathTracingSampleIndex;
	PathTracingData.MaxSamples = MaxSPP;

	// Prepare radiance buffer (will be shared with display pass)
	FRDGTexture* RadianceTexture = nullptr;
	FRDGTexture* AlbedoTexture = nullptr;
	FRDGTexture* NormalTexture = nullptr;
	if (View.ViewState->PathTracingRadianceRT)
	{
		// we already have a valid radiance texture, re-use it
		RadianceTexture = GraphBuilder.RegisterExternalTexture(View.ViewState->PathTracingRadianceRT, TEXT("PathTracer.Radiance"));
		AlbedoTexture   = GraphBuilder.RegisterExternalTexture(View.ViewState->PathTracingAlbedoRT, TEXT("PathTracer.Albedo"));
		NormalTexture   = GraphBuilder.RegisterExternalTexture(View.ViewState->PathTracingNormalRT, TEXT("PathTracer.Normal"));
	}
	else
	{
		// First time through, need to make a new texture
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
			View.ViewRect.Size(),
			PF_A32B32G32R32F,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV);
		RadianceTexture = GraphBuilder.CreateTexture(Desc, TEXT("PathTracer.Radiance"), ERDGTextureFlags::MultiFrame);
		AlbedoTexture   = GraphBuilder.CreateTexture(Desc, TEXT("PathTracer.Albedo")  , ERDGTextureFlags::MultiFrame);
		NormalTexture   = GraphBuilder.CreateTexture(Desc, TEXT("PathTracer.Normal")  , ERDGTextureFlags::MultiFrame);
	}
	bool NeedsMoreRays = PathTracingData.Iteration < MaxSPP;

	if (NeedsMoreRays)
	{
		FPathTracingRG::FParameters* PassParameters = GraphBuilder.AllocParameters<FPathTracingRG::FParameters>();
		PassParameters->TLAS = View.RayTracingScene.RayTracingSceneRHI->GetShaderResourceView();
		PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
		PassParameters->PathTracingData = PathTracingData;
		// upload sky/lights data
		SetLightParameters(GraphBuilder, PassParameters, Scene, View, UseMISCompensation);
		if (PathTracingData.EnableDirectLighting == 0)
		{
			PassParameters->SceneVisibleLightCount = 0;
		}

		PassParameters->IESTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->RadianceTexture = GraphBuilder.CreateUAV(RadianceTexture);
		PassParameters->AlbedoTexture   = GraphBuilder.CreateUAV(AlbedoTexture);
		PassParameters->NormalTexture   = GraphBuilder.CreateUAV(NormalTexture);

		PassParameters->SSProfilesTexture = GetSubsufaceProfileTexture_RT(GraphBuilder.RHICmdList)->GetShaderResourceRHI();

		// TODO: in multi-gpu case, split image into tiles
		PassParameters->TileOffset.X = 0;
		PassParameters->TileOffset.Y = 0;

		TShaderMapRef<FPathTracingRG> RayGenShader(View.ShaderMap);
		ClearUnusedGraphResources(RayGenShader, PassParameters);
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("Path Tracer Compute (%d x %d) Sample=%d/%d NumLights=%d", View.ViewRect.Size().X, View.ViewRect.Size().Y, View.ViewState->PathTracingSampleIndex, MaxSPP, PassParameters->SceneLightCount),
			PassParameters,
			ERDGPassFlags::Compute,
			[PassParameters, RayGenShader, &View](FRHICommandListImmediate& RHICmdList)
		{
			FRHIRayTracingScene* RayTracingSceneRHI = View.RayTracingScene.RayTracingSceneRHI;
			
			// Round up to coherent path tracing tile size to simplify pixel shuffling
			// TODO: be careful not to write extra pixels past the boundary when using multi-gpu
			const int32 TS = PATHTRACER_COHERENT_TILE_SIZE;
			int32 DispatchSizeX = FMath::DivideAndRoundUp(View.ViewRect.Size().X, TS) * TS;
			int32 DispatchSizeY = FMath::DivideAndRoundUp(View.ViewRect.Size().Y, TS) * TS;

			FRayTracingShaderBindingsWriter GlobalResources;
			SetShaderParameters(GlobalResources, RayGenShader, *PassParameters);

			RHICmdList.RayTraceDispatch(
				View.RayTracingMaterialPipeline,
				RayGenShader.GetRayTracingShader(),
				RayTracingSceneRHI, GlobalResources,
				DispatchSizeX, DispatchSizeY
			);
		});

		// After we are done, make sure we remember our texture for next time so that we can accumulate samples across frames
		GraphBuilder.QueueTextureExtraction(RadianceTexture, &View.ViewState->PathTracingRadianceRT);
		GraphBuilder.QueueTextureExtraction(AlbedoTexture  , &View.ViewState->PathTracingAlbedoRT  );
		GraphBuilder.QueueTextureExtraction(NormalTexture  , &View.ViewState->PathTracingNormalRT  );

	}

	FRDGTexture* DenoisedRadianceTexture = nullptr;
	int DenoiserMode = CVarPathTracingDenoiser.GetValueOnRenderThread();
	if (DenoiserMode < 0)
	{
		DenoiserMode = View.FinalPostProcessSettings.PathTracingEnableDenoiser;
	}
	const bool IsDenoiserEnabled = DenoiserMode != 0 && GPathTracingDenoiserFunc != nullptr;
	static int PrevDenoiserMode = DenoiserMode;
	if (IsDenoiserEnabled)
	{
		// request denoise if this is the last sample
		bool NeedsDenoise = (PathTracingData.Iteration + 1) == MaxSPP;
		// also allow turning on the denoiser after the image has stopped accumulating samples
		if (!NeedsMoreRays)
		{
			// we aren't currently rendering, run the denoiser if we just turned it on
			NeedsDenoise |= DenoiserMode != PrevDenoiserMode;
		}

		if (View.ViewState->PathTracingRadianceDenoisedRT)
		{
			// we already have a texture for this
			DenoisedRadianceTexture = GraphBuilder.RegisterExternalTexture(View.ViewState->PathTracingRadianceDenoisedRT, TEXT("PathTracer.DenoisedRadiance"));
		}

		if (NeedsDenoise)
		{
			if (DenoisedRadianceTexture == nullptr)
			{
				// First time through, need to make a new texture
				FRDGTextureDesc RadianceTextureDesc = FRDGTextureDesc::Create2D(
					View.ViewRect.Size(),
					PF_A32B32G32R32F,
					FClearValueBinding::None,
					TexCreate_ShaderResource | TexCreate_UAV);
				DenoisedRadianceTexture = GraphBuilder.CreateTexture(RadianceTextureDesc, TEXT("PathTracer.DenoisedRadiance"), ERDGTextureFlags::MultiFrame);
			}

			FDenoiseTextureParameters* DenoiseParameters = GraphBuilder.AllocParameters<FDenoiseTextureParameters>();
			DenoiseParameters->InputTexture = RadianceTexture;
			DenoiseParameters->InputAlbedo = AlbedoTexture;
			DenoiseParameters->InputNormal = NormalTexture;
			DenoiseParameters->OutputTexture = DenoisedRadianceTexture;
			GraphBuilder.AddPass(RDG_EVENT_NAME("Path Tracer Denoiser Plugin"), DenoiseParameters, ERDGPassFlags::Readback, 
				[DenoiseParameters, DenoiserMode](FRHICommandListImmediate& RHICmdList)
				{
					GPathTracingDenoiserFunc(RHICmdList,
						DenoiseParameters->InputTexture->GetRHI()->GetTexture2D(),
						DenoiseParameters->InputAlbedo->GetRHI()->GetTexture2D(),
						DenoiseParameters->InputNormal->GetRHI()->GetTexture2D(),
						DenoiseParameters->OutputTexture->GetRHI()->GetTexture2D());
				}
			);

			GraphBuilder.QueueTextureExtraction(DenoisedRadianceTexture, &View.ViewState->PathTracingRadianceDenoisedRT);
		}
	}
	PrevDenoiserMode = DenoiserMode;

	// now add a pixel shader pass to display our Radiance buffer

	FPathTracingCompositorPS::FParameters* DisplayParameters = GraphBuilder.AllocParameters<FPathTracingCompositorPS::FParameters>();
	DisplayParameters->Iteration = PathTracingData.Iteration;
	DisplayParameters->MaxSamples = MaxSPP;
	DisplayParameters->ProgressDisplayEnabled = CVarPathTracingProgressDisplay.GetValueOnRenderThread();
	DisplayParameters->ViewUniformBuffer = View.ViewUniformBuffer;
	DisplayParameters->RadianceTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(DenoisedRadianceTexture ? DenoisedRadianceTexture : RadianceTexture));
	DisplayParameters->RenderTargets[0] = FRenderTargetBinding(SceneColorOutputTexture, ERenderTargetLoadAction::ELoad);

	FScreenPassTextureViewport Viewport(SceneColorOutputTexture, View.ViewRect);

	// wiper mode - reveals the render below the path tracing display
	// NOTE: we still path trace the full resolution even while wiping the cursor so that rendering does not get out of sync
	if (CVarPathTracingWiperMode.GetValueOnRenderThread() != 0)
	{
		float DPIScale = FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(View.CursorPos.X, View.CursorPos.Y);
		Viewport.Rect.Min.X = View.CursorPos.X / DPIScale;
	}

	TShaderMapRef<FPathTracingCompositorPS> PixelShader(View.ShaderMap);
	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("Path Tracer Display (%d x %d)", View.ViewRect.Size().X, View.ViewRect.Size().Y),
		View,
		Viewport,
		Viewport,
		PixelShader,
		DisplayParameters
	);

	// Bump counters for next frame
	++View.ViewState->PathTracingSampleIndex;
	++View.ViewState->PathTracingFrameIndex;
}

#endif
