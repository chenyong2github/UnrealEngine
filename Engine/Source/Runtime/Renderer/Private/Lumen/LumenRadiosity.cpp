// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenRadiosity.cpp
=============================================================================*/

#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "PixelShaderUtils.h"
#include "ReflectionEnvironment.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "LumenRadianceCache.h"
#include "LumenSceneLighting.h"
#include "LumenTracingUtils.h"

#include "LumenHardwareRayTracingCommon.h"

int32 GLumenRadiosity = 1;
FAutoConsoleVariableRef CVarLumenRadiosity(
	TEXT("r.LumenScene.Radiosity"),
	GLumenRadiosity,
	TEXT("Whether to enable the Radiosity, which is an indirect lighting gather from the Surface Cache that provides multibounce diffuse."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenRadiosityProbeSpacing = 4;
FAutoConsoleVariableRef CVarLumenRadiosityProbeSpacing(
	TEXT("r.LumenScene.Radiosity.ProbeSpacing"),
	GLumenRadiosityProbeSpacing,
	TEXT("Distance between probes, in Surface Cache texels"),
	ECVF_RenderThreadSafe
);

int32 GLumenRadiosityHemisphereProbeResolution = 4;
FAutoConsoleVariableRef CVarLumenRadiosityHemisphereProbeResolution(
	TEXT("r.LumenScene.Radiosity.HemisphereProbeResolution"),
	GLumenRadiosityHemisphereProbeResolution,
	TEXT("Number of traces along one dimension of the hemisphere probe layout."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenRadiositySpatialFilterProbes = 1;
FAutoConsoleVariableRef CVarLumenRadiositySpatialFilterProbes(
	TEXT("r.LumenScene.Radiosity.SpatialFilterProbes"),
	GLumenRadiositySpatialFilterProbes,
	TEXT("Whether to spatially filter Radiosity probes.  Filtering reduces noise but increases leaking."),
	ECVF_RenderThreadSafe
);

int32 GLumenRadiositySpatialFilterProbesKernelSize = 1;
FAutoConsoleVariableRef CVarLumenRadiositySpatialFilterProbesKernelSize(
	TEXT("r.LumenScene.Radiosity.SpatialFilterProbes.KernelSize"),
	GLumenRadiositySpatialFilterProbesKernelSize,
	TEXT("Larger kernels reduce noise but increase leaking."),
	ECVF_RenderThreadSafe
);

int32 GRadiosityFilteringProbePlaneWeighting = 1;
FAutoConsoleVariableRef CVarRadiosityFilteringProbePlaneWeighting(
	TEXT("r.LumenScene.Radiosity.ProbePlaneWeighting"),
	GRadiosityFilteringProbePlaneWeighting,
	TEXT("Whether to weight Radiosity probes by plane distance, useful to prevent leaking."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GRadiosityFilteringProbeOcclusion = 1;
FAutoConsoleVariableRef CVarRadiosityFilteringProbeOcclusion(
	TEXT("r.LumenScene.Radiosity.ProbeOcclusion"),
	GRadiosityFilteringProbeOcclusion,
	TEXT("Whether to depth test against the probe hit depths during interpolation and filtering to reduce leaking.  Not available with Software Ray Tracing due to imprecision."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GRadiosityProbePlaneWeightingDepthScale = -100.0f;
FAutoConsoleVariableRef CVarRadiosityProbePlaneWeightingDepthScale(
	TEXT("r.LumenScene.Radiosity.SpatialFilterProbes.PlaneWeightingDepthScale"),
	GRadiosityProbePlaneWeightingDepthScale,
	TEXT("Controls the distance at which probes can be interpolated from.  Higher values introduce leaking."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenRadiosityMinTraceDistanceToSampleSurface = 10.0f;
FAutoConsoleVariableRef CVarLumenRadiosityMinTraceDistanceToSampleSurface(
	TEXT("r.LumenScene.Radiosity.MinTraceDistanceToSampleSurface"),
	GLumenRadiosityMinTraceDistanceToSampleSurface,
	TEXT("Ray hit distance from which we can start sampling surface cache in order to fix radiosity feedback loop where surface cache texel hits itself every frame."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenRadiosityMaxRayIntensity = 40.0f;
FAutoConsoleVariableRef CVarLumenRadiosityMaxRayIntensity(
	TEXT("r.LumenScene.Radiosity.MaxRayIntensity"),
	GLumenRadiosityMaxRayIntensity,
	TEXT("Clamps Radiosity trace intensity, relative to current view exposure.  Useful for reducing artifacts from small bright emissive sources, but loses energy and adds view dependence."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenRadiosityDistanceFieldSurfaceBias = 10.0f;
FAutoConsoleVariableRef CVarLumenRadiositySurfaceBias(
	TEXT("r.LumenScene.Radiosity.DistanceFieldSurfaceBias"),
	GLumenRadiosityDistanceFieldSurfaceBias,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenRadiosityDistanceFieldSurfaceSlopeBias = 5.0f;
FAutoConsoleVariableRef CVarLumenRadiosityDistanceFieldSurfaceBias(
	TEXT("r.LumenScene.Radiosity.DistanceFieldSurfaceSlopeBias"),
	GLumenRadiosityDistanceFieldSurfaceBias,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenRadiosityHardwareRayTracingSurfaceBias = 0.1f;
FAutoConsoleVariableRef CVarLumenRadiosityHardwareRayTracingSurfaceBias(
	TEXT("r.LumenScene.Radiosity.HardwareRayTracing.SurfaceBias"),
	GLumenRadiosityHardwareRayTracingSurfaceBias,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenRadiosityHardwareRayTracingSurfaceSlopeBias = 0.2f;
FAutoConsoleVariableRef CVarLumenRadiosityHardwareRayTracingSlopeSurfaceBias(
	TEXT("r.LumenScene.Radiosity.HardwareRayTracing.SlopeSurfaceBias"),
	GLumenRadiosityHardwareRayTracingSurfaceSlopeBias,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenRadiosityVoxelStepFactor = 1.0f;
FAutoConsoleVariableRef CVarRadiosityVoxelStepFactor(
	TEXT("r.LumenScene.Radiosity.VoxelStepFactor"),
	GLumenRadiosityVoxelStepFactor,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenRadiosityHardwareRayTracing(
	TEXT("r.LumenScene.Radiosity.HardwareRayTracing"),
	1,
	TEXT("Enables hardware ray tracing for radiosity (default = 1)."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenRadiosityHardwareRayTracingIndirect(
	TEXT("r.LumenScene.Radiosity.HardwareRayTracing.Indirect"),
	1,
	TEXT("Enables indirect dispatch for hardware ray tracing for radiosity (default = 1)."),
	ECVF_RenderThreadSafe
);

float GLumenRadiosityAvoidSelfIntersectionTraceDistance = 5.0f;
FAutoConsoleVariableRef CVarRadiosityAvoidSelfIntersectionTraceDistance(
	TEXT("r.LumenScene.Radiosity.HardwareRayTracing.AvoidSelfIntersectionTraceDistance"),
	GLumenRadiosityAvoidSelfIntersectionTraceDistance,
	TEXT("When greater than zero, a short trace skipping backfaces will be done to escape the surface, followed by the remaining trace that can hit backfaces."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenRadiosityTemporalAccumulation = 1;
FAutoConsoleVariableRef CVarLumenRadiosityTemporalAccumulation(
	TEXT("r.LumenScene.Radiosity.Temporal"),
	GLumenRadiosityTemporalAccumulation,
	TEXT("Whether to use temporal super sampling on Radiosity.  Increases quality, but also adds latency to the speed that lighting changes propagate, and animated noise in the results."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GLumenRadiosityTemporalMaxFramesAccumulated = 4;
FAutoConsoleVariableRef CVarLumenRadiosityTemporalMaxFramesAccumulated(
	TEXT("r.LumenScene.Radiosity.Temporal.MaxFramesAccumulated"),
	GLumenRadiosityTemporalMaxFramesAccumulated,
	TEXT("Lower values cause the temporal filter to propagate lighting changes faster, but also increase flickering from noise."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GLumenRadiosityFixedJitterIndex = -1;
FAutoConsoleVariableRef CVarLumenRadiosityFixedJitterIndex(
	TEXT("r.LumenScene.Radiosity.Temporal.FixedJitterIndex"),
	GLumenRadiosityFixedJitterIndex,
	TEXT("If zero or greater, overrides the temporal jitter index with a fixed index.  Useful for debugging and inspecting sampling patterns."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

namespace LumenRadiosity
{
	void AddRadiosityPass(
		FRDGBuilder& GraphBuilder,
		const FScene* Scene,
		const FViewInfo& View,
		bool bRenderSkylight,
		FLumenSceneData& LumenSceneData,
		FRDGTextureRef RadiosityAtlas,
		FRDGTextureRef RadiosityNumFramesAccumulatedAtlas,
		const FLumenCardTracingInputs& TracingInputs,
		const FLumenCardUpdateContext& CardUpdateContext);

	uint32 GetRadiosityProbeSpacing(const FViewInfo& View)
	{
		int32 RadiosityProbeSpacing = GLumenRadiosityProbeSpacing;

		if (View.FinalPostProcessSettings.LumenSceneLightingQuality >= 6)
		{
			RadiosityProbeSpacing /= 2;
		}

		return FMath::RoundUpToPowerOfTwo(FMath::Clamp<uint32>(RadiosityProbeSpacing, 1, Lumen::CardTileSize));
	}

	int32 GetHemisphereProbeResolution(const FViewInfo& View)
	{
		const float LumenSceneLightingQuality = FMath::Clamp<float>(View.FinalPostProcessSettings.LumenSceneLightingQuality, .5f, 4.0f);
		return FMath::Clamp<int32>(GLumenRadiosityHemisphereProbeResolution * FMath::Sqrt(LumenSceneLightingQuality), 1, 16);
	}

	bool UseTemporalAccumulation()
	{
		return GLumenRadiosityTemporalAccumulation != 0 && RHIIsTypedUAVLoadSupported(Lumen::GetIndirectLightingAtlasFormat()) && RHIIsTypedUAVLoadSupported(Lumen::GetNumFramesAccumulatedAtlasFormat());
	}
}

bool Lumen::UseHardwareRayTracedRadiosity(const FSceneViewFamily& ViewFamily)
{
#if RHI_RAYTRACING
	return IsRayTracingEnabled()
		&& Lumen::UseHardwareRayTracing()
		&& (CVarLumenRadiosityHardwareRayTracing.GetValueOnRenderThread() != 0);
#else
	return false;
#endif
}

bool Lumen::ShouldRenderRadiosityHardwareRayTracing(const FSceneViewFamily& ViewFamily)
{
	return UseHardwareRayTracedRadiosity(ViewFamily) && IsRadiosityEnabled(ViewFamily);
}

bool Lumen::IsRadiosityEnabled(const FSceneViewFamily& ViewFamily)
{
	return GLumenRadiosity != 0 
		&& ViewFamily.EngineShowFlags.LumenSecondaryBounces;
}

uint32 Lumen::GetRadiosityAtlasDownsampleFactor()
{
	// Must match RADIOSITY_ATLAS_DOWNSAMPLE_FACTOR
	return 1;
}

FIntPoint FLumenSceneData::GetRadiosityAtlasSize() const
{
	return PhysicalAtlasSize / Lumen::GetRadiosityAtlasDownsampleFactor();
}

class FBuildRadiosityTilesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FBuildRadiosityTilesCS);
	SHADER_USE_PARAMETER_STRUCT(FBuildRadiosityTilesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RDG_BUFFER_ACCESS(IndirectArgBuffer, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWCardTileAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWCardTileData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CardPageIndexAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CardPageIndexData)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}

	static int32 GetGroupSize()
	{
		return 8;
	}
};

IMPLEMENT_GLOBAL_SHADER(FBuildRadiosityTilesCS, "/Engine/Private/Lumen/Radiosity/LumenRadiosityCulling.usf", "BuildRadiosityTilesCS", SF_Compute);

class FSetRadiosityTileIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSetRadiosityTileIndirectArgsCS);
	SHADER_USE_PARAMETER_STRUCT(FSetRadiosityTileIndirectArgsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCardDispatchIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CardTileAllocator)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}

	static int32 GetGroupSize()
	{
		return 8;
	}
};

IMPLEMENT_GLOBAL_SHADER(FSetRadiosityTileIndirectArgsCS, "/Engine/Private/Lumen/Radiosity/LumenRadiosityCulling.usf", "SetRadiosityTileIndirectArgs", SF_Compute);

enum class ERadiosityIndirectArgs
{
	ThreadPerTrace = 0 * sizeof(FRHIDispatchIndirectParameters),
	ThreadPerProbe = 1 * sizeof(FRHIDispatchIndirectParameters),
	ThreadPerRadiosityTexel = 2 * sizeof(FRHIDispatchIndirectParameters),
	HardwareRayTracingThreadPerTrace = 3 * sizeof(FRHIDispatchIndirectParameters),
	MAX = 4
};

BEGIN_SHADER_PARAMETER_STRUCT(FLumenRadiosityTexelTraceParameters, )
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CardTileAllocator)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CardTileData)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, TraceRadianceAtlas)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, TraceHitDistanceAtlas)
	SHADER_PARAMETER(FIntPoint, RadiosityAtlasSize)
	SHADER_PARAMETER(uint32, ProbeSpacingInRadiosityTexels)
	SHADER_PARAMETER(uint32, ProbeSpacingInRadiosityTexelsDivideShift)
	SHADER_PARAMETER(uint32, RadiosityTileSize)
	SHADER_PARAMETER(uint32, HemisphereProbeResolution)
	SHADER_PARAMETER(uint32, NumTracesPerProbe)
	SHADER_PARAMETER(uint32, UseProbeOcclusion)
	SHADER_PARAMETER(int32, FixedJitterIndex)
	SHADER_PARAMETER(uint32, MaxFramesAccumulated)
END_SHADER_PARAMETER_STRUCT()

class FLumenRadiosityIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenRadiosityIndirectArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenRadiosityIndirectArgsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWIndirectArgs)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenRadiosityTexelTraceParameters, RadiosityTexelTraceParameters)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}

	static int32 GetGroupSize()
	{
		return 64;
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenRadiosityIndirectArgsCS, "/Engine/Private/Lumen/Radiosity/LumenRadiosity.usf", "LumenRadiosityIndirectArgsCS", SF_Compute);

class FLumenRadiosityDistanceFieldTracingCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenRadiosityDistanceFieldTracingCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenRadiosityDistanceFieldTracingCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenRadiosityTexelTraceParameters, RadiosityTexelTraceParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenIndirectTracingParameters, IndirectTracingParameters)
		SHADER_PARAMETER(float, MaxRayIntensity)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWTraceRadianceAtlas)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWTraceHitDistanceAtlas)
	END_SHADER_PARAMETER_STRUCT()

	class FTraceGlobalSDF : SHADER_PERMUTATION_BOOL("TRACE_GLOBAL_SDF");
	using FPermutationDomain = TShaderPermutationDomain<FTraceGlobalSDF>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
		OutEnvironment.SetDefine(TEXT("ENABLE_DYNAMIC_SKY_LIGHT"), 1);
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
	}

	static int32 GetGroupSize()
	{
		return 64;
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenRadiosityDistanceFieldTracingCS, "/Engine/Private/Lumen/Radiosity/LumenRadiosity.usf", "LumenRadiosityDistanceFieldTracingCS", SF_Compute);

#if RHI_RAYTRACING

class FLumenRadiosityHardwareRayTracingRGS : public FLumenHardwareRayTracingRGS
{
	DECLARE_GLOBAL_SHADER(FLumenRadiosityHardwareRayTracingRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FLumenRadiosityHardwareRayTracingRGS, FLumenHardwareRayTracingRGS)

	class FIndirectDispatchDim : SHADER_PERMUTATION_BOOL("DIM_INDIRECT_DISPATCH");
	class FAvoidSelfIntersectionTrace : SHADER_PERMUTATION_BOOL("DIM_AVOID_SELF_INTERSECTION_TRACE");
	using FPermutationDomain = TShaderPermutationDomain<FIndirectDispatchDim, FAvoidSelfIntersectionTrace>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenHardwareRayTracingRGS::FSharedParameters, SharedParameters)
		RDG_BUFFER_ACCESS(HardwareRayTracingIndirectArgs, ERHIAccess::IndirectArgs | ERHIAccess::SRVCompute)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenRadiosityTexelTraceParameters, RadiosityTexelTraceParameters)
		SHADER_PARAMETER(uint32, NumThreadsToDispatch)
		SHADER_PARAMETER(float, MinTraceDistance)
		SHADER_PARAMETER(float, MaxTraceDistance)
		SHADER_PARAMETER(float, SurfaceBias)
		SHADER_PARAMETER(float, HeightfieldSurfaceBias)
		SHADER_PARAMETER(float, AvoidSelfIntersectionTraceDistance)
		SHADER_PARAMETER(float, MaxRayIntensity)
		SHADER_PARAMETER(float, MinTraceDistanceToSampleSurface)
		SHADER_PARAMETER(int32, MaxTranslucentSkipCount)
		SHADER_PARAMETER(uint32, MaxTraversalIterations)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWTraceRadianceAtlas)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWTraceHitDistanceAtlas)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FLumenHardwareRayTracingRGS::ModifyCompilationEnvironment(Parameters, Lumen::ESurfaceCacheSampling::HighResPages, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
		OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_DISPATCH_1D"), 1);
		OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_LIGHTWEIGHT_CLOSEST_HIT_SHADER"), 1);
		OutEnvironment.SetDefine(TEXT("ENABLE_DYNAMIC_SKY_LIGHT"), 1);
	}

	static int32 GetGroupSize()
	{
		return 64;
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenRadiosityHardwareRayTracingRGS, "/Engine/Private/Lumen/Radiosity/LumenRadiosityHardwareRayTracing.usf", "LumenRadiosityHardwareRayTracingRGS", SF_RayGen);

bool IsHardwareRayTracingRadiosityIndirectDispatch()
{
	return GRHISupportsRayTracingDispatchIndirect && (CVarLumenRadiosityHardwareRayTracingIndirect.GetValueOnRenderThread() == 1);
}

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingRadiosityLumenMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	if (Lumen::ShouldRenderRadiosityHardwareRayTracing(*View.Family))
	{
		FLumenRadiosityHardwareRayTracingRGS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenRadiosityHardwareRayTracingRGS::FIndirectDispatchDim>(IsHardwareRayTracingRadiosityIndirectDispatch());
		PermutationVector.Set<FLumenRadiosityHardwareRayTracingRGS::FAvoidSelfIntersectionTrace>(GLumenRadiosityAvoidSelfIntersectionTraceDistance > 0.0f);
		TShaderRef<FLumenRadiosityHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenRadiosityHardwareRayTracingRGS>(PermutationVector);
		OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
	}
}
#endif // #if RHI_RAYTRACING


class FLumenRadiositySpatialFilterProbeRadiance : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenRadiositySpatialFilterProbeRadiance)
	SHADER_USE_PARAMETER_STRUCT(FLumenRadiositySpatialFilterProbeRadiance, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenRadiosityTexelTraceParameters, RadiosityTexelTraceParameters)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWFilteredTraceRadianceAtlas)
		SHADER_PARAMETER(float, ProbePlaneWeightingDepthScale)
	END_SHADER_PARAMETER_STRUCT()

	class FPlaneWeighting : SHADER_PERMUTATION_BOOL("FILTERING_PLANE_WEIGHTING");
	class FProbeOcclusion : SHADER_PERMUTATION_BOOL("FILTERING_PROBE_OCCLUSION");
	class FKernelSize : SHADER_PERMUTATION_INT("FILTERING_KERNEL_SIZE", 3);
	using FPermutationDomain = TShaderPermutationDomain<FPlaneWeighting, FProbeOcclusion, FKernelSize>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}

	static int32 GetGroupSize()
	{
		return 64;
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenRadiositySpatialFilterProbeRadiance, "/Engine/Private/Lumen/Radiosity/LumenRadiosity.usf", "LumenRadiositySpatialFilterProbeRadiance", SF_Compute);


class FLumenRadiosityConvertToSH : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenRadiosityConvertToSH)
	SHADER_USE_PARAMETER_STRUCT(FLumenRadiosityConvertToSH, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWRadiosityProbeSHRedAtlas)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWRadiosityProbeSHGreenAtlas)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWRadiosityProbeSHBlueAtlas)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenRadiosityTexelTraceParameters, RadiosityTexelTraceParameters)
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}

	static int32 GetGroupSize()
	{
		return 64;
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenRadiosityConvertToSH, "/Engine/Private/Lumen/Radiosity/LumenRadiosity.usf", "LumenRadiosityConvertToSH", SF_Compute);

class FLumenRadiosityIntegrateCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenRadiosityIntegrateCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenRadiosityIntegrateCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenRadiosityTexelTraceParameters, RadiosityTexelTraceParameters)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWRadiosityAtlas)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWRadiosityNumFramesAccumulatedAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RadiosityProbeSHRedAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RadiosityProbeSHGreenAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RadiosityProbeSHBlueAtlas)
		SHADER_PARAMETER(float, ProbePlaneWeightingDepthScale)
	END_SHADER_PARAMETER_STRUCT()

	class FPlaneWeighting : SHADER_PERMUTATION_BOOL("INTERPOLATION_PLANE_WEIGHTING");
	class FProbeOcclusion : SHADER_PERMUTATION_BOOL("INTERPOLATION_PROBE_OCCLUSION");
	class FTemporalAccumulation : SHADER_PERMUTATION_BOOL("TEMPORAL_ACCUMULATION");
	using FPermutationDomain = TShaderPermutationDomain<FPlaneWeighting, FProbeOcclusion, FTemporalAccumulation>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
	}

	static int32 GetGroupSize()
	{
		return 64;
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenRadiosityIntegrateCS, "/Engine/Private/Lumen/Radiosity/LumenRadiosity.usf", "LumenRadiosityIntegrateCS", SF_Compute);

FRDGTextureRef RegisterOrCreateRadiosityAtlas(
	FRDGBuilder& GraphBuilder,
	const TRefCountPtr<IPooledRenderTarget>& AtlasRT,
	const TCHAR* AtlasName,
	FIntPoint AtlasSize,
	EPixelFormat AtlasFormat)
{
	FRDGTextureRef AtlasTexture = AtlasRT ? GraphBuilder.RegisterExternalTexture(AtlasRT) : nullptr;

	if (!AtlasTexture || AtlasTexture->Desc.Extent != AtlasSize)
	{
		AtlasTexture = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(AtlasSize, AtlasFormat, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			AtlasName);
	}

	return AtlasTexture;
}

void LumenRadiosity::AddRadiosityPass(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	bool bRenderSkylight,
	FLumenSceneData& LumenSceneData,
	FRDGTextureRef RadiosityAtlas,
	FRDGTextureRef RadiosityNumFramesAccumulatedAtlas,
	const FLumenCardTracingInputs& TracingInputs,
	const FLumenCardUpdateContext& CardUpdateContext)
{
	const int32 ProbeSpacing = LumenRadiosity::GetRadiosityProbeSpacing(View);
	const int32 HemisphereProbeResolution = LumenRadiosity::GetHemisphereProbeResolution(View);
	const uint32 RadiosityTileSize = Lumen::CardTileSize / ProbeSpacing;

	FIntPoint RadiosityProbeAtlasSize;
	RadiosityProbeAtlasSize.X = FMath::DivideAndRoundUp<uint32>(LumenSceneData.GetPhysicalAtlasSize().X, ProbeSpacing);
	RadiosityProbeAtlasSize.Y = FMath::DivideAndRoundUp<uint32>(LumenSceneData.GetPhysicalAtlasSize().Y, ProbeSpacing);

	FIntPoint RadiosityProbeTracingAtlasSize = RadiosityProbeAtlasSize * FIntPoint(HemisphereProbeResolution, HemisphereProbeResolution);

	FRDGTextureRef TraceRadianceAtlas = RegisterOrCreateRadiosityAtlas(
		GraphBuilder, 
		LumenSceneData.RadiosityTraceRadianceAtlas, 
		TEXT("Lumen.Radiosity.TraceRadianceAtlas"), 
		RadiosityProbeTracingAtlasSize, 
		PF_FloatRGB);

	const bool bUseProbeOcclusion = GRadiosityFilteringProbeOcclusion != 0 
		// Self intersection from grazing angle traces causes noise that breaks probe occlusion
		&& Lumen::UseHardwareRayTracedRadiosity(*View.Family);

	FRDGTextureRef TraceHitDistanceAtlas = nullptr;
	
	if (bUseProbeOcclusion)
	{
		TraceHitDistanceAtlas = RegisterOrCreateRadiosityAtlas(
			GraphBuilder, 
			LumenSceneData.RadiosityTraceHitDistanceAtlas, 
			TEXT("Lumen.Radiosity.TraceHitDistanceAtlas"), 
			RadiosityProbeTracingAtlasSize, 
			PF_R16F);
	}
	else
	{
		TraceHitDistanceAtlas = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(FIntPoint(1, 1), PF_R16F, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), TEXT("Dummy"));
	}

	const uint32 MaxCardTilesX = FMath::DivideAndRoundUp<uint32>(LumenSceneData.GetPhysicalAtlasSize().X, Lumen::CardTileSize);
	const uint32 MaxCardTilesY = FMath::DivideAndRoundUp<uint32>(LumenSceneData.GetPhysicalAtlasSize().Y, Lumen::CardTileSize);
	const uint32 MaxCardTiles = MaxCardTilesX * MaxCardTilesY;

	FRDGBufferRef CardTileAllocator = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1), TEXT("Lumen.Radiosity.CardTileAllocator"));
	FRDGBufferRef CardTiles = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), MaxCardTiles), TEXT("Lumen.Radiosity.CardTiles"));
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(CardTileAllocator), 0);

	// Setup common radiosity tracing parameters
	FLumenRadiosityTexelTraceParameters RadiosityTexelTraceParameters;
	{
		RadiosityTexelTraceParameters.CardTileAllocator = GraphBuilder.CreateSRV(CardTileAllocator);
		RadiosityTexelTraceParameters.CardTileData = GraphBuilder.CreateSRV(CardTiles);
		RadiosityTexelTraceParameters.TraceRadianceAtlas = TraceRadianceAtlas;
		RadiosityTexelTraceParameters.TraceHitDistanceAtlas = TraceHitDistanceAtlas;
		RadiosityTexelTraceParameters.RadiosityAtlasSize = LumenSceneData.GetRadiosityAtlasSize();
		RadiosityTexelTraceParameters.ProbeSpacingInRadiosityTexels = ProbeSpacing;
		RadiosityTexelTraceParameters.ProbeSpacingInRadiosityTexelsDivideShift = FMath::FloorLog2(ProbeSpacing);
		RadiosityTexelTraceParameters.RadiosityTileSize = RadiosityTileSize;
		RadiosityTexelTraceParameters.HemisphereProbeResolution = HemisphereProbeResolution;
		RadiosityTexelTraceParameters.NumTracesPerProbe = HemisphereProbeResolution * HemisphereProbeResolution;
		RadiosityTexelTraceParameters.UseProbeOcclusion = bUseProbeOcclusion ? 1 : 0;
		RadiosityTexelTraceParameters.FixedJitterIndex = GLumenRadiosityFixedJitterIndex;
		RadiosityTexelTraceParameters.MaxFramesAccumulated = LumenRadiosity::UseTemporalAccumulation() ? GLumenRadiosityTemporalMaxFramesAccumulated : 1;
	}

	// Build a list of radiosity tiles for future processing
	{
		FBuildRadiosityTilesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FBuildRadiosityTilesCS::FParameters>();
		PassParameters->IndirectArgBuffer = CardUpdateContext.DispatchCardPageIndicesIndirectArgs;
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->LumenCardScene = TracingInputs.LumenCardSceneUniformBuffer;
		PassParameters->RWCardTileAllocator = GraphBuilder.CreateUAV(CardTileAllocator);
		PassParameters->RWCardTileData = GraphBuilder.CreateUAV(CardTiles);
		PassParameters->CardPageIndexAllocator = GraphBuilder.CreateSRV(CardUpdateContext.CardPageIndexAllocator);
		PassParameters->CardPageIndexData = GraphBuilder.CreateSRV(CardUpdateContext.CardPageIndexData);
		auto ComputeShader = View.ShaderMap->GetShader<FBuildRadiosityTilesCS>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("BuildRadiosityTiles"),
			ComputeShader,
			PassParameters,
			CardUpdateContext.DispatchCardPageIndicesIndirectArgs,
			FLumenCardUpdateContext::EIndirectArgOffset::ThreadPerTile);
	}

	FRDGBufferRef RadiosityIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>((int)ERadiosityIndirectArgs::MAX), TEXT("Lumen.RadiosityIndirectArgs"));

	// Setup indirect args for future passes
	{
		FLumenRadiosityIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenRadiosityIndirectArgsCS::FParameters>();
		PassParameters->RWIndirectArgs = GraphBuilder.CreateUAV(RadiosityIndirectArgs);
		PassParameters->RadiosityTexelTraceParameters = RadiosityTexelTraceParameters;

		auto ComputeShader = View.ShaderMap->GetShader<FLumenRadiosityIndirectArgsCS>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("IndirectArgs"),
			ComputeShader,
			PassParameters,
			FIntVector(1, 1, 1));
	}

	// Trace rays from surface cache texels
	if (Lumen::UseHardwareRayTracedRadiosity(*View.Family))
	{
#if RHI_RAYTRACING

		FLumenRadiosityHardwareRayTracingRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenRadiosityHardwareRayTracingRGS::FParameters>();
		SetLumenHardwareRayTracingSharedParameters(
			GraphBuilder,
			GetSceneTextureParameters(GraphBuilder),
			View,
			TracingInputs,
			&PassParameters->SharedParameters
		);
		PassParameters->HardwareRayTracingIndirectArgs = RadiosityIndirectArgs;

		PassParameters->RadiosityTexelTraceParameters = RadiosityTexelTraceParameters;
		PassParameters->RWTraceRadianceAtlas = GraphBuilder.CreateUAV(TraceRadianceAtlas);
		PassParameters->RWTraceHitDistanceAtlas = GraphBuilder.CreateUAV(TraceHitDistanceAtlas);

		const uint32 NumThreadsToDispatch = GRHIPersistentThreadGroupCount * FLumenRadiosityHardwareRayTracingRGS::GetGroupSize();
		PassParameters->NumThreadsToDispatch = NumThreadsToDispatch;
		PassParameters->SurfaceBias = FMath::Clamp(GLumenRadiosityHardwareRayTracingSurfaceSlopeBias, 0.0f, 1000.0f);
		PassParameters->HeightfieldSurfaceBias = Lumen::GetHeightfieldReceiverBias();
		PassParameters->AvoidSelfIntersectionTraceDistance = FMath::Clamp(GLumenRadiosityAvoidSelfIntersectionTraceDistance, 0.0f, 1000000.0f);
		PassParameters->MaxRayIntensity = FMath::Clamp(GLumenRadiosityMaxRayIntensity, 0.0f, 1000000.0f);
		PassParameters->MinTraceDistance = FMath::Clamp(GLumenRadiosityHardwareRayTracingSurfaceBias, 0.0f, 1000.0f);
		PassParameters->MaxTraceDistance = Lumen::GetMaxTraceDistance(View);
		PassParameters->MinTraceDistanceToSampleSurface = GLumenRadiosityMinTraceDistanceToSampleSurface;
		PassParameters->MaxTranslucentSkipCount = Lumen::GetMaxTranslucentSkipCount();
		PassParameters->MaxTraversalIterations = LumenHardwareRayTracing::GetMaxTraversalIterations();

		FLumenRadiosityHardwareRayTracingRGS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenRadiosityHardwareRayTracingRGS::FIndirectDispatchDim>(IsHardwareRayTracingRadiosityIndirectDispatch());
		PermutationVector.Set<FLumenRadiosityHardwareRayTracingRGS::FAvoidSelfIntersectionTrace>(GLumenRadiosityAvoidSelfIntersectionTraceDistance > 0.0f);
		TShaderRef<FLumenRadiosityHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenRadiosityHardwareRayTracingRGS>(PermutationVector);

		ClearUnusedGraphResources(RayGenerationShader, PassParameters);

		const FIntPoint DispatchResolution = FIntPoint(NumThreadsToDispatch, 1);
		FString Resolution = FString::Printf(TEXT("%ux%u"), DispatchResolution.X, DispatchResolution.Y);
		if (IsHardwareRayTracingRadiosityIndirectDispatch())
		{
			Resolution = FString::Printf(TEXT("<indirect>"));
		}
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("HardwareRayTracing %s %ux%u probes at %u spacing", *Resolution, HemisphereProbeResolution, HemisphereProbeResolution, ProbeSpacing),
			PassParameters,
			ERDGPassFlags::Compute,
			[PassParameters, &View, RayGenerationShader, DispatchResolution](FRHIRayTracingCommandList& RHICmdList)
			{
				FRayTracingShaderBindingsWriter GlobalResources;
				SetShaderParameters(GlobalResources, RayGenerationShader, *PassParameters);

				FRHIRayTracingScene* RayTracingSceneRHI = View.GetRayTracingSceneChecked();
				FRayTracingPipelineState* RayTracingPipeline = View.LumenHardwareRayTracingMaterialPipeline;

				if (IsHardwareRayTracingRadiosityIndirectDispatch())
				{
					PassParameters->HardwareRayTracingIndirectArgs->MarkResourceAsUsed();
					RHICmdList.RayTraceDispatchIndirect(RayTracingPipeline, RayGenerationShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources,
						PassParameters->HardwareRayTracingIndirectArgs->GetIndirectRHICallBuffer(), (int)ERadiosityIndirectArgs::HardwareRayTracingThreadPerTrace);
				}
				else
				{
					RHICmdList.RayTraceDispatch(RayTracingPipeline, RayGenerationShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources,
						DispatchResolution.X, DispatchResolution.Y);
				}
			}
		);
#endif
	}
	else
	{
		FLumenRadiosityDistanceFieldTracingCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenRadiosityDistanceFieldTracingCS::FParameters>();
		PassParameters->IndirectArgs = RadiosityIndirectArgs;
		PassParameters->RadiosityTexelTraceParameters = RadiosityTexelTraceParameters;
		PassParameters->RWTraceRadianceAtlas = GraphBuilder.CreateUAV(TraceRadianceAtlas);
		PassParameters->RWTraceHitDistanceAtlas = GraphBuilder.CreateUAV(TraceHitDistanceAtlas);

		GetLumenCardTracingParameters(View, TracingInputs, PassParameters->TracingParameters);
		SetupLumenDiffuseTracingParametersForProbe(View, PassParameters->IndirectTracingParameters, 0.0f);
		PassParameters->IndirectTracingParameters.SurfaceBias = FMath::Clamp(GLumenRadiosityDistanceFieldSurfaceSlopeBias, 0.0f, 1000.0f);
		PassParameters->IndirectTracingParameters.MinTraceDistance = FMath::Clamp(GLumenRadiosityDistanceFieldSurfaceBias, 0.0f, 1000.0f);
		PassParameters->IndirectTracingParameters.MaxTraceDistance = Lumen::GetMaxTraceDistance(View);
		PassParameters->IndirectTracingParameters.VoxelStepFactor = FMath::Clamp(GLumenRadiosityVoxelStepFactor, 0.1f, 10.0f);
		PassParameters->MaxRayIntensity = FMath::Clamp(GLumenRadiosityMaxRayIntensity, 0.0f, 1000000.0f);

		FLumenRadiosityDistanceFieldTracingCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenRadiosityDistanceFieldTracingCS::FTraceGlobalSDF>(Lumen::UseGlobalSDFTracing(*View.Family));
		auto ComputeShader = View.ShaderMap->GetShader<FLumenRadiosityDistanceFieldTracingCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("DistanceFieldTracing %ux%u probes at %u spacing", HemisphereProbeResolution, HemisphereProbeResolution, ProbeSpacing),
			ComputeShader,
			PassParameters,
			RadiosityIndirectArgs,
			(int)ERadiosityIndirectArgs::ThreadPerTrace);
	}

	FRDGTextureRef RadiosityProbeSHRedAtlas = RegisterOrCreateRadiosityAtlas(
		GraphBuilder, 
		LumenSceneData.RadiosityProbeSHRedAtlas, 
		TEXT("Lumen.Radiosity.ProbeSHRedAtlas"), 
		RadiosityProbeAtlasSize, 
		PF_FloatRGBA);

	FRDGTextureRef RadiosityProbeSHGreenAtlas = RegisterOrCreateRadiosityAtlas(
		GraphBuilder, 
		LumenSceneData.RadiosityProbeSHGreenAtlas, 
		TEXT("Lumen.Radiosity.ProbeSHGreenAtlas"), 
		RadiosityProbeAtlasSize, 
		PF_FloatRGBA);

	FRDGTextureRef RadiosityProbeSHBlueAtlas = RegisterOrCreateRadiosityAtlas(
		GraphBuilder, 
		LumenSceneData.RadiosityProbeSHBlueAtlas, 
		TEXT("Lumen.Radiosity.ProbeSHBlueAtlas"), 
		RadiosityProbeAtlasSize, 
		PF_FloatRGBA);

	if (GLumenRadiositySpatialFilterProbes && GLumenRadiositySpatialFilterProbesKernelSize > 0)
	{
		{
			//@todo - use temporary buffer based off of CardUpdateContext.UpdateAtlasSize which is smaller
			FRDGTextureRef FilteredTraceRadianceAtlas = GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create2D(RadiosityProbeTracingAtlasSize, PF_FloatRGB, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
				TEXT("Lumen.Radiosity.FilteredTraceRadianceAtlas"));

			FLumenRadiositySpatialFilterProbeRadiance::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenRadiositySpatialFilterProbeRadiance::FParameters>();
			PassParameters->RWFilteredTraceRadianceAtlas = GraphBuilder.CreateUAV(FilteredTraceRadianceAtlas);
			PassParameters->IndirectArgs = RadiosityIndirectArgs;
			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->LumenCardScene = TracingInputs.LumenCardSceneUniformBuffer;
			PassParameters->RadiosityTexelTraceParameters = RadiosityTexelTraceParameters;
			PassParameters->ProbePlaneWeightingDepthScale = GRadiosityProbePlaneWeightingDepthScale;

			FLumenRadiositySpatialFilterProbeRadiance::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenRadiositySpatialFilterProbeRadiance::FPlaneWeighting>(GRadiosityFilteringProbePlaneWeighting != 0);
			PermutationVector.Set<FLumenRadiositySpatialFilterProbeRadiance::FProbeOcclusion>(bUseProbeOcclusion);
			PermutationVector.Set<FLumenRadiositySpatialFilterProbeRadiance::FKernelSize>(FMath::Clamp<int32>(GLumenRadiositySpatialFilterProbesKernelSize, 0, 2));
			auto ComputeShader = View.ShaderMap->GetShader<FLumenRadiositySpatialFilterProbeRadiance>(PermutationVector);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("SpatialFilterProbes"),
				ComputeShader,
				PassParameters,
				RadiosityIndirectArgs,
				(int)ERadiosityIndirectArgs::ThreadPerTrace);

			RadiosityTexelTraceParameters.TraceRadianceAtlas = FilteredTraceRadianceAtlas;
		}
	}

	// Convert traces to SH and store in persistent SH atlas
	{
		FLumenRadiosityConvertToSH::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenRadiosityConvertToSH::FParameters>();
		PassParameters->RWRadiosityProbeSHRedAtlas = GraphBuilder.CreateUAV(RadiosityProbeSHRedAtlas);
		PassParameters->RWRadiosityProbeSHGreenAtlas = GraphBuilder.CreateUAV(RadiosityProbeSHGreenAtlas);
		PassParameters->RWRadiosityProbeSHBlueAtlas = GraphBuilder.CreateUAV(RadiosityProbeSHBlueAtlas);
		PassParameters->IndirectArgs = RadiosityIndirectArgs;
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->LumenCardScene = TracingInputs.LumenCardSceneUniformBuffer;
		PassParameters->RadiosityTexelTraceParameters = RadiosityTexelTraceParameters;

		auto ComputeShader = View.ShaderMap->GetShader<FLumenRadiosityConvertToSH>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ConvertToSH"),
			ComputeShader,
			PassParameters,
			RadiosityIndirectArgs,
			(int)ERadiosityIndirectArgs::ThreadPerProbe);
	}

	{
		FLumenRadiosityIntegrateCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenRadiosityIntegrateCS::FParameters>();
		PassParameters->IndirectArgs = RadiosityIndirectArgs;
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->LumenCardScene = TracingInputs.LumenCardSceneUniformBuffer;
		PassParameters->RadiosityTexelTraceParameters = RadiosityTexelTraceParameters;
		PassParameters->RWRadiosityAtlas = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(RadiosityAtlas));
		PassParameters->RWRadiosityNumFramesAccumulatedAtlas = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(RadiosityNumFramesAccumulatedAtlas));
		PassParameters->RadiosityProbeSHRedAtlas = RadiosityProbeSHRedAtlas;
		PassParameters->RadiosityProbeSHGreenAtlas = RadiosityProbeSHGreenAtlas;
		PassParameters->RadiosityProbeSHBlueAtlas = RadiosityProbeSHBlueAtlas;
		PassParameters->ProbePlaneWeightingDepthScale = GRadiosityProbePlaneWeightingDepthScale;

		FLumenRadiosityIntegrateCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenRadiosityIntegrateCS::FPlaneWeighting>(GRadiosityFilteringProbePlaneWeighting != 0);
		PermutationVector.Set<FLumenRadiosityIntegrateCS::FProbeOcclusion>(bUseProbeOcclusion);
		PermutationVector.Set<FLumenRadiosityIntegrateCS::FTemporalAccumulation>(LumenRadiosity::UseTemporalAccumulation());
		auto ComputeShader = View.ShaderMap->GetShader<FLumenRadiosityIntegrateCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Integrate"),
			ComputeShader,
			PassParameters,
			RadiosityIndirectArgs,
			(int)ERadiosityIndirectArgs::ThreadPerRadiosityTexel);
	}

	// Note: extracting source TraceRadianceAtlas and not the filtered one
	LumenSceneData.RadiosityTraceRadianceAtlas = GraphBuilder.ConvertToExternalTexture(TraceRadianceAtlas);
	LumenSceneData.RadiosityTraceHitDistanceAtlas = GraphBuilder.ConvertToExternalTexture(TraceHitDistanceAtlas);
	LumenSceneData.RadiosityProbeSHRedAtlas = GraphBuilder.ConvertToExternalTexture(RadiosityProbeSHRedAtlas);
	LumenSceneData.RadiosityProbeSHGreenAtlas = GraphBuilder.ConvertToExternalTexture(RadiosityProbeSHGreenAtlas);
	LumenSceneData.RadiosityProbeSHBlueAtlas = GraphBuilder.ConvertToExternalTexture(RadiosityProbeSHBlueAtlas);
}

void FDeferredShadingSceneRenderer::RenderRadiosityForLumenScene(
	FRDGBuilder& GraphBuilder, 
	const FLumenCardTracingInputs& TracingInputs, 
	FGlobalShaderMap* GlobalShaderMap, 
	FRDGTextureRef RadiosityAtlas,
	FRDGTextureRef RadiosityNumFramesAccumulatedAtlas,
	const FLumenCardUpdateContext& CardUpdateContext)
{
	LLM_SCOPE_BYTAG(Lumen);

	const FViewInfo& View = Views[0];
	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	extern int32 GLumenSceneRecaptureLumenSceneEveryFrame;

	if (Lumen::IsRadiosityEnabled(ViewFamily) 
		&& LumenSceneData.bFinalLightingAtlasContentsValid)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "Radiosity");

		FLumenCardTracingInputs LocalTracingInputs = TracingInputs;

		if (LocalTracingInputs.NumClipmapLevels == 0 && !Lumen::UseHardwareRayTracedRadiosity(ViewFamily))
		{
			// First frame since enabling, initialize voxel lighting since we won't have anything from last frame
			ComputeLumenSceneVoxelLighting(GraphBuilder, LocalTracingInputs, GlobalShaderMap);
		}

		const bool bRenderSkylight = Lumen::ShouldHandleSkyLight(Scene, ViewFamily);

		LumenRadiosity::AddRadiosityPass(
			GraphBuilder,
			Scene,
			View,
			bRenderSkylight,
			LumenSceneData,
			RadiosityAtlas,
			RadiosityNumFramesAccumulatedAtlas,
			LocalTracingInputs,
			CardUpdateContext);

		// Update Final Lighting
		Lumen::CombineLumenSceneLighting(
			Scene,
			View,
			GraphBuilder,
			LocalTracingInputs,
			CardUpdateContext);
	}
	else
	{
		AddClearRenderTargetPass(GraphBuilder, RadiosityAtlas);
	}
}
