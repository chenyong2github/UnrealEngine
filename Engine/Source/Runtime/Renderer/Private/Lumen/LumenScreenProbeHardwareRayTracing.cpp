// Copyright Epic Games, Inc. All Rights Reserved.

#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "PixelShaderUtils.h"
#include "ReflectionEnvironment.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "SceneTextureParameters.h"
#include "IndirectLightRendering.h"

// Actual screen-probe requirements..
#include "LumenRadianceCache.h"
#include "LumenScreenProbeGather.h"

#if RHI_RAYTRACING

#include "RayTracing/RayTracingDeferredMaterials.h"
#include "RayTracing/RaytracingOptions.h"
#include "RayTracing/RayTracingLighting.h"
#include "LumenHardwareRayTracingCommon.h"

static TAutoConsoleVariable<int32> CVarLumenScreenProbeGatherHardwareRayTracing(
	TEXT("r.Lumen.ScreenProbeGather.HardwareRayTracing"),
	1,
	TEXT("0. Software raytracing of diffuse indirect from Lumen cubemap tree.")
	TEXT("1. Enable hardware ray tracing of diffuse indirect. (Default)\n"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenScreenProbeGatherHardwareRayTracingIndirect(
	TEXT("r.Lumen.ScreenProbeGather.HardwareRayTracing.Indirect"),
	1,
	TEXT("Enables indirect ray tracing dispatch on compatible hardware (Default = 1)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenScreenProbeGatherHardwareRayTracingLightingMode(
	TEXT("r.Lumen.ScreenProbeGather.HardwareRayTracing.LightingMode"),
	0,
	TEXT("Determines the lighting mode (Default = 0)\n")
	TEXT("0: interpolate final lighting from the surface cache\n")
	TEXT("1: evaluate material, and interpolate irradiance and indirect irradiance from the surface cache\n")
	TEXT("2: evaluate material and direct lighting, and interpolate indirect irradiance from the surface cache"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenScreenProbeGatherHardwareRayTracingMaxTranslucentSkipCount(
	TEXT("r.Lumen.ScreenProbeGather.HardwareRayTracing.MaxTranslucentSkipCount"),
	2,
	TEXT("Determines the maximum number of translucent surfaces skipped during ray traversal (Default = 2)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarLumenHardwareRayTracingNormalBias(
	TEXT("r.Lumen.ScreenProbeGather.HardwareRayTracing.NormalBias"),
	.1f,
	TEXT("Bias along the shading normal, useful when the Ray Tracing geometry doesn't match the GBuffer (Nanite Proxy geometry)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarLumenHardwareRayTracingAvoidSelfIntersectionTraceDistance(
	TEXT("r.Lumen.ScreenProbeGather.HardwareRayTracing.AvoidSelfIntersectionTraceDistance"),
	5.0f,
	TEXT("Distance to trace with backface culling enabled, useful when the Ray Tracing geometry doesn't match the GBuffer (Nanite Proxy geometry)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenScreenProbeGatherHardwareRayTracingRetraceFarField(
	TEXT("r.Lumen.ScreenProbeGather.HardwareRayTracing.Retrace.FarField"),
	1,
	TEXT("Determines whether a second trace will be fired for far-field contribution (Default = 1)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenScreenProbeGatherHardwareRayTracingDefaultThreadCount(
	TEXT("r.Lumen.ScreenProbeGather.HardwareRayTracing.Default.ThreadCount"),
	32768,
	TEXT("Determines the active number of threads (Default = 32768)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenScreenProbeGatherHardwareRayTracingDefaultGroupCount(
	TEXT("r.Lumen.ScreenProbeGather.HardwareRayTracing.Default.GroupCount"),
	1,
	TEXT("Determines the active number of groups (Default = 1)"),
	ECVF_RenderThreadSafe
);

#endif // RHI_RAYTRACING

namespace Lumen
{
	bool UseHardwareRayTracedScreenProbeGather()
	{
#if RHI_RAYTRACING
		return IsRayTracingEnabled()
			&& Lumen::UseHardwareRayTracing()
			&& (CVarLumenScreenProbeGatherHardwareRayTracing.GetValueOnAnyThread() != 0);
#else
		return false;
#endif
	}

	EHardwareRayTracingLightingMode GetScreenProbeGatherHardwareRayTracingLightingMode()
	{
#if RHI_RAYTRACING
		// Disable hit-lighting for now.
		//return EHardwareRayTracingLightingMode(FMath::Clamp(CVarLumenScreenProbeGatherHardwareRayTracingLightingMode.GetValueOnRenderThread(), 0, 2));
		return EHardwareRayTracingLightingMode::LightingFromSurfaceCache;
#else
		return EHardwareRayTracingLightingMode::LightingFromSurfaceCache;
#endif
	}
}

#if RHI_RAYTRACING

class FConvertRayAllocatorCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FConvertRayAllocatorCS)
	SHADER_USE_PARAMETER_STRUCT(FConvertRayAllocatorCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, Allocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWRayAllocator)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

IMPLEMENT_GLOBAL_SHADER(FConvertRayAllocatorCS, "/Engine/Private/Lumen/LumenScreenProbeHardwareRayTracing.usf", "FConvertRayAllocatorCS", SF_Compute);

class FLumenScreenProbeGatherHardwareRayTracingRGS : public FLumenHardwareRayTracingRGS
{
	DECLARE_GLOBAL_SHADER(FLumenScreenProbeGatherHardwareRayTracingRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FLumenScreenProbeGatherHardwareRayTracingRGS, FLumenHardwareRayTracingRGS)

	class FLightingModeDim : SHADER_PERMUTATION_ENUM_CLASS("DIM_LIGHTING_MODE", LumenHWRTPipeline::ELightingMode);
	class FEnableNearFieldTracing : SHADER_PERMUTATION_BOOL("ENABLE_NEAR_FIELD_TRACING");
	class FEnableFarFieldTracing : SHADER_PERMUTATION_BOOL("ENABLE_FAR_FIELD_TRACING");
	class FRadianceCache : SHADER_PERMUTATION_BOOL("DIM_RADIANCE_CACHE");
	class FWriteFinalLightingDim : SHADER_PERMUTATION_BOOL("DIM_WRITE_FINAL_LIGHTING");
	class FIndirectDispatchDim : SHADER_PERMUTATION_BOOL("DIM_INDIRECT_DISPATCH");
	class FPackTraceDataDim : SHADER_PERMUTATION_BOOL("DIM_PACK_TRACE_DATA");
	class FStructuredImportanceSamplingDim : SHADER_PERMUTATION_BOOL("STRUCTURED_IMPORTANCE_SAMPLING");
	using FPermutationDomain = TShaderPermutationDomain<FLightingModeDim, FEnableNearFieldTracing, FEnableFarFieldTracing, FRadianceCache, FWriteFinalLightingDim, FIndirectDispatchDim, FStructuredImportanceSamplingDim, FPackTraceDataDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenHardwareRayTracingRGS::FSharedParameters, SharedParameters)
		RDG_BUFFER_ACCESS(HardwareRayTracingIndirectArgs, ERHIAccess::IndirectArgs | ERHIAccess::SRVCompute)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, RayAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint2>, TraceTexelDataPacked)

		// Screen probes
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenIndirectTracingParameters, IndirectTracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)

		// Constants
		SHADER_PARAMETER(int, ThreadCount)
		SHADER_PARAMETER(int, GroupCount)
		SHADER_PARAMETER(float, PullbackBias)
		SHADER_PARAMETER(float, NormalBias)
		SHADER_PARAMETER(float, AvoidSelfIntersectionTraceDistance)
		SHADER_PARAMETER(int, MaxTranslucentSkipCount)
		SHADER_PARAMETER(uint32, MaxTraversalIterations)
		SHADER_PARAMETER(int, ApplySkyLight)
		SHADER_PARAMETER(float, FarFieldBias)
		SHADER_PARAMETER(float, FarFieldMaxTraceDistance)
		SHADER_PARAMETER(FVector3f, FarFieldReferencePos)

		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInterpolationParameters, RadianceCacheParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FCompactedTraceParameters, CompactedTraceParameters)

		// Ray continuation buffer
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<LumenHWRTPipeline::FTraceDataPacked>, RWRetraceDataPackedBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FLumenHardwareRayTracingRGS::ModifyCompilationEnvironment(Parameters, Lumen::ESurfaceCacheSampling::AlwaysResidentPages, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_DISPATCH_1D"), 1);
		OutEnvironment.SetDefine(TEXT("USE_NEW_SHADER"), 1);

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FLightingModeDim>() == LumenHWRTPipeline::ELightingMode::SurfaceCache)
		{
			OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_LIGHTWEIGHT_CLOSEST_HIT_SHADER"), 1);
		}
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		if (!FLumenHardwareRayTracingRGS::ShouldCompilePermutation(Parameters))
		{
			return false;
		}

		// Currently disable hit-lighting
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		bool bSurfaceCacheLightingMode = PermutationVector.Get<FLightingModeDim>() == LumenHWRTPipeline::ELightingMode::SurfaceCache;
		bool bWriteFinalLighting = PermutationVector.Get<FWriteFinalLightingDim>();

		return bSurfaceCacheLightingMode && bWriteFinalLighting;
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenScreenProbeGatherHardwareRayTracingRGS, "/Engine/Private/Lumen/LumenScreenProbeHardwareRayTracing.usf", "LumenScreenProbeGatherHardwareRayTracingRGS", SF_RayGen);

class FLumenScreenProbeGatherHardwareRayTracingCS : public FLumenHardwareRayTracingCS
{
	DECLARE_GLOBAL_SHADER(FLumenScreenProbeGatherHardwareRayTracingCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenScreenProbeGatherHardwareRayTracingCS, FLumenHardwareRayTracingCS)

	class FEnableNearFieldTracing : SHADER_PERMUTATION_BOOL("ENABLE_NEAR_FIELD_TRACING");
	class FEnableFarFieldTracing : SHADER_PERMUTATION_BOOL("ENABLE_FAR_FIELD_TRACING");
	class FRadianceCache : SHADER_PERMUTATION_BOOL("DIM_RADIANCE_CACHE");
	class FWriteFinalLightingDim : SHADER_PERMUTATION_BOOL("DIM_WRITE_FINAL_LIGHTING");
	class FIndirectDispatchDim : SHADER_PERMUTATION_BOOL("DIM_INDIRECT_DISPATCH");
	class FPackTraceDataDim : SHADER_PERMUTATION_BOOL("DIM_PACK_TRACE_DATA");
	class FStructuredImportanceSamplingDim : SHADER_PERMUTATION_BOOL("STRUCTURED_IMPORTANCE_SAMPLING");
	using FPermutationDomain = TShaderPermutationDomain<FEnableNearFieldTracing, FEnableFarFieldTracing, FRadianceCache, FWriteFinalLightingDim, FIndirectDispatchDim, FStructuredImportanceSamplingDim, FPackTraceDataDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenScreenProbeGatherHardwareRayTracingRGS::FParameters, CommonParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenHardwareRayTracingCS::FInlineParameters, InlineParameters)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FLumenHardwareRayTracingCS::ModifyCompilationEnvironment(Parameters, Lumen::ESurfaceCacheSampling::AlwaysResidentPages, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("INLINE_RAY_TRACING_THREAD_GROUP_SIZE_X"), ThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("INLINE_RAY_TRACING_THREAD_GROUP_SIZE_Y"), ThreadGroupSizeY);
		OutEnvironment.SetDefine(TEXT("DIM_DEFERRED_MATERIAL_MODE"), 0);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		if (!FLumenHardwareRayTracingCS::ShouldCompilePermutation(Parameters))
		{
			return false;
		}

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		bool bWriteFinalLighting = PermutationVector.Get<FWriteFinalLightingDim>();

		return bWriteFinalLighting;
	}

	// Current inline ray tracing implementation requires 1:1 mapping between thread groups and waves and only supports wave32 mode.
	static constexpr uint32 ThreadGroupSizeX = 32;
	static constexpr uint32 ThreadGroupSizeY = 1;
};

IMPLEMENT_GLOBAL_SHADER(FLumenScreenProbeGatherHardwareRayTracingCS, "/Engine/Private/Lumen/LumenScreenProbeHardwareRayTracing.usf", "LumenScreenProbeGatherHardwareRayTracingCS", SF_Compute);

class FLumenScreenProbeGatherHardwareRayTracingIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenScreenProbeGatherHardwareRayTracingIndirectArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenScreenProbeGatherHardwareRayTracingIndirectArgsCS, FGlobalShader)

	class FInlineRaytracing : SHADER_PERMUTATION_BOOL("DIM_INLINE_RAYTRACING");
	using FPermutationDomain = TShaderPermutationDomain<FInlineRaytracing>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, RayAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWHardwareRayTracingIndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_1D"), GetThreadGroupSize1D());
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_2D"), GetThreadGroupSize2D());
		OutEnvironment.SetDefine(TEXT("INLINE_RAY_TRACING_THREAD_GROUP_SIZE_X"), FLumenScreenProbeGatherHardwareRayTracingCS::ThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("INLINE_RAY_TRACING_THREAD_GROUP_SIZE_Y"), FLumenScreenProbeGatherHardwareRayTracingCS::ThreadGroupSizeY);
	}

	static int32 GetThreadGroupSize1D() { return GetThreadGroupSize2D() * GetThreadGroupSize2D(); }
	static int32 GetThreadGroupSize2D() { return 8; }
};

IMPLEMENT_GLOBAL_SHADER(FLumenScreenProbeGatherHardwareRayTracingIndirectArgsCS, "/Engine/Private/Lumen/LumenScreenProbeHardwareRayTracing.usf", "FLumenScreenProbeHardwareRayTracingIndirectArgsCS", SF_Compute);

bool UseFarFieldForScreenProbeGather(const FSceneViewFamily& ViewFamily)
{
	return Lumen::UseFarField(ViewFamily) && CVarLumenScreenProbeGatherHardwareRayTracingRetraceFarField.GetValueOnRenderThread();
}

bool IsHitLightingForceEnabledForScreenProbeGather()
{
	return CVarLumenScreenProbeGatherHardwareRayTracingLightingMode.GetValueOnRenderThread() != 0;
}

bool IsHardwareRayTracingScreenProbeGatherIndirectDispatch()
{
	return GRHISupportsRayTracingDispatchIndirect && (CVarLumenScreenProbeGatherHardwareRayTracingIndirect.GetValueOnRenderThread() == 1);
}

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingScreenProbeGather(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	if (Lumen::UseHardwareRayTracedScreenProbeGather())
	{
		// Hit-lighting is disabled
		if (false)
		{
			bool bUseFarFieldForScreenProbeGather = UseFarFieldForScreenProbeGather(*View.Family);
			bool bApplySkyLight = !bUseFarFieldForScreenProbeGather;
			bool bUseRadianceCache = LumenScreenProbeGather::UseRadianceCache(View);
			const bool bIsForceHitLighting = IsHitLightingForceEnabledForScreenProbeGather();

			FLumenScreenProbeGatherHardwareRayTracingRGS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FLightingModeDim>(LumenHWRTPipeline::ELightingMode::HitLighting);
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FRadianceCache>(bUseRadianceCache);
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FEnableNearFieldTracing>(true);
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FEnableFarFieldTracing>(false);
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FWriteFinalLightingDim>(!bIsForceHitLighting || !bUseFarFieldForScreenProbeGather);
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FIndirectDispatchDim>(IsHardwareRayTracingScreenProbeGatherIndirectDispatch());
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FStructuredImportanceSamplingDim>(LumenScreenProbeGather::UseImportanceSampling(View));
			TShaderRef<FLumenScreenProbeGatherHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenScreenProbeGatherHardwareRayTracingRGS>(PermutationVector);

			OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
		}
	}
}

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingScreenProbeGatherDeferredMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
}

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingScreenProbeGatherLumenMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	if (Lumen::UseHardwareRayTracedScreenProbeGather())
	{
		const bool bUseFarFieldForScreenProbeGather = UseFarFieldForScreenProbeGather(*View.Family);

		// Default trace
		{
			bool bApplySkyLight = !bUseFarFieldForScreenProbeGather;
			bool bUseRadianceCache = LumenScreenProbeGather::UseRadianceCache(View);
			const bool bIsForceHitLighting = IsHitLightingForceEnabledForScreenProbeGather();

			FLumenScreenProbeGatherHardwareRayTracingRGS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FLightingModeDim>(LumenHWRTPipeline::ELightingMode::SurfaceCache);
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FRadianceCache>(bUseRadianceCache);
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FEnableNearFieldTracing>(true);
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FEnableFarFieldTracing>(false);
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FWriteFinalLightingDim>(!bIsForceHitLighting || !bUseFarFieldForScreenProbeGather);
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FIndirectDispatchDim>(IsHardwareRayTracingScreenProbeGatherIndirectDispatch());
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FStructuredImportanceSamplingDim>(LumenScreenProbeGather::UseImportanceSampling(View));
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FPackTraceDataDim>(bUseFarFieldForScreenProbeGather);
			TShaderRef<FLumenScreenProbeGatherHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenScreenProbeGatherHardwareRayTracingRGS>(PermutationVector);

			OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
		}

		// Far-field
		if (bUseFarFieldForScreenProbeGather)
		{
			bool bApplySkyLight = !bUseFarFieldForScreenProbeGather;
			bool bUseRadianceCache = LumenScreenProbeGather::UseRadianceCache(View);
			const bool bIsForceHitLighting = IsHitLightingForceEnabledForScreenProbeGather();

			FLumenScreenProbeGatherHardwareRayTracingRGS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FLightingModeDim>(LumenHWRTPipeline::ELightingMode::SurfaceCache);
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FRadianceCache>(bUseRadianceCache);
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FEnableNearFieldTracing>(false);
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FEnableFarFieldTracing>(true);
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FWriteFinalLightingDim>(true);
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FIndirectDispatchDim>(IsHardwareRayTracingScreenProbeGatherIndirectDispatch());
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FStructuredImportanceSamplingDim>(LumenScreenProbeGather::UseImportanceSampling(View));
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FPackTraceDataDim>(false);
			TShaderRef<FLumenScreenProbeGatherHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenScreenProbeGatherHardwareRayTracingRGS>(PermutationVector);

			OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
		}
	}
}

void SetLumenHardwareRayTracingScreenProbeParameters(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	FScreenProbeParameters& ScreenProbeParameters,
	const FViewInfo& View,
	const FLumenCardTracingInputs& TracingInputs,
	FRDGBufferRef HardwareRayTracingIndirectArgsBuffer,
	FLumenIndirectTracingParameters& IndirectTracingParameters,
	const LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters,
	const FCompactedTraceParameters& CompactedTraceParameters,
	bool bApplySkyLight,
	bool bUseRadianceCache,
	bool bEnableHitLighting,
	bool bEnableFarFieldTracing,
	FRDGBufferRef RayAllocatorBuffer,
	FRDGBufferRef TraceTexelDataPackedBuffer,
	FRDGBufferRef RetraceDataPackedBuffer,
	FLumenScreenProbeGatherHardwareRayTracingRGS::FParameters* Parameters
)
{
	uint32 DefaultThreadCount = CVarLumenScreenProbeGatherHardwareRayTracingDefaultThreadCount.GetValueOnRenderThread();
	uint32 DefaultGroupCount = CVarLumenScreenProbeGatherHardwareRayTracingDefaultGroupCount.GetValueOnRenderThread();

	SetLumenHardwareRayTracingSharedParameters(
		GraphBuilder,
		SceneTextures,
		View,
		TracingInputs,
		&Parameters->SharedParameters
	);

	Parameters->HardwareRayTracingIndirectArgs = HardwareRayTracingIndirectArgsBuffer;
	Parameters->RayAllocator = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(RayAllocatorBuffer, PF_R32_UINT));
	Parameters->TraceTexelDataPacked = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(TraceTexelDataPackedBuffer));

	Parameters->IndirectTracingParameters = IndirectTracingParameters;
	Parameters->ScreenProbeParameters = ScreenProbeParameters;
	Parameters->RadianceCacheParameters = RadianceCacheParameters;
	Parameters->CompactedTraceParameters = CompactedTraceParameters;

	// Constants
	Parameters->ThreadCount = DefaultThreadCount;
	Parameters->GroupCount = DefaultGroupCount;
	Parameters->FarFieldBias = LumenHardwareRayTracing::GetFarFieldBias();
	Parameters->FarFieldMaxTraceDistance = Lumen::GetFarFieldMaxTraceDistance();
	Parameters->FarFieldReferencePos = Lumen::GetFarFieldReferencePos();
	Parameters->PullbackBias = Lumen::GetHardwareRayTracingPullbackBias();
	Parameters->NormalBias = CVarLumenHardwareRayTracingNormalBias.GetValueOnRenderThread();
	Parameters->AvoidSelfIntersectionTraceDistance = FMath::Max(CVarLumenHardwareRayTracingAvoidSelfIntersectionTraceDistance.GetValueOnRenderThread(), 0.0f);
	Parameters->MaxTranslucentSkipCount = CVarLumenScreenProbeGatherHardwareRayTracingMaxTranslucentSkipCount.GetValueOnRenderThread();
	Parameters->MaxTraversalIterations = LumenHardwareRayTracing::GetMaxTraversalIterations();
	Parameters->ApplySkyLight = bApplySkyLight;

	// Ray continuation buffer
	Parameters->RWRetraceDataPackedBuffer = GraphBuilder.CreateUAV(RetraceDataPackedBuffer);
}

FLumenScreenProbeGatherHardwareRayTracingCS::FPermutationDomain ToComputePermutationVector(const FLumenScreenProbeGatherHardwareRayTracingRGS::FPermutationDomain& RGSPermutationVector)
{
	FLumenScreenProbeGatherHardwareRayTracingCS::FPermutationDomain PermutationVector;

	PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingCS::FRadianceCache>(
		RGSPermutationVector.Get<FLumenScreenProbeGatherHardwareRayTracingRGS::FRadianceCache>());

	PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingCS::FEnableFarFieldTracing>(
		RGSPermutationVector.Get<FLumenScreenProbeGatherHardwareRayTracingRGS::FEnableFarFieldTracing>());

	PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingCS::FEnableNearFieldTracing>(
		RGSPermutationVector.Get<FLumenScreenProbeGatherHardwareRayTracingRGS::FEnableNearFieldTracing>());

	PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingCS::FIndirectDispatchDim>(
		RGSPermutationVector.Get<FLumenScreenProbeGatherHardwareRayTracingRGS::FIndirectDispatchDim>());

	PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingCS::FStructuredImportanceSamplingDim>(
		RGSPermutationVector.Get<FLumenScreenProbeGatherHardwareRayTracingRGS::FStructuredImportanceSamplingDim>());

	PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingCS::FWriteFinalLightingDim>(
		RGSPermutationVector.Get<FLumenScreenProbeGatherHardwareRayTracingRGS::FWriteFinalLightingDim>());

	PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingCS::FPackTraceDataDim>(
		RGSPermutationVector.Get<FLumenScreenProbeGatherHardwareRayTracingRGS::FPackTraceDataDim>());

	return PermutationVector;
}

void DispatchLumenScreenProbeGatherHardwareRayTracingIndirectArgs(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGBufferRef HardwareRayTracingIndirectArgsBuffer, FRDGBufferRef RayAllocatorBuffer, bool bInlineRayTracing)
{
	FLumenScreenProbeGatherHardwareRayTracingIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenScreenProbeGatherHardwareRayTracingIndirectArgsCS::FParameters>();

	PassParameters->RayAllocator = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(RayAllocatorBuffer, PF_R32_UINT));
	PassParameters->RWHardwareRayTracingIndirectArgs = GraphBuilder.CreateUAV(HardwareRayTracingIndirectArgsBuffer, PF_R32_UINT);

	FLumenScreenProbeGatherHardwareRayTracingIndirectArgsCS::FPermutationDomain IndirectPermutationVector;
	IndirectPermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingIndirectArgsCS::FInlineRaytracing>(bInlineRayTracing);
	TShaderRef<FLumenScreenProbeGatherHardwareRayTracingIndirectArgsCS> ComputeShader = View.ShaderMap->GetShader<FLumenScreenProbeGatherHardwareRayTracingIndirectArgsCS>(IndirectPermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("LumenScreenProbeGatherHardwareRayTracingIndirectArgsCS"),
		ComputeShader,
		PassParameters,
		FIntVector(1, 1, 1));
}

void DispatchComputeShader(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	FScreenProbeParameters& ScreenProbeParameters,
	const FLumenCardTracingInputs& TracingInputs,
	FLumenIndirectTracingParameters& IndirectTracingParameters,
	const FCompactedTraceParameters& CompactedTraceParameters,
	const LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters,
	const FLumenScreenProbeGatherHardwareRayTracingCS::FPermutationDomain& PermutationVector,
	uint32 RayCount,
	bool bApplySkyLight,
	bool bUseRadianceCache,
	FRDGBufferRef RayAllocatorBuffer,
	FRDGBufferRef TraceTexelDataPackedBuffer,
	FRDGBufferRef RetraceDataPackedBuffer
)
{
	FRDGBufferRef HardwareRayTracingIndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Lumen.ScreenProbeGather.HardwareRayTracing.IndirectArgsCS"));
	DispatchLumenScreenProbeGatherHardwareRayTracingIndirectArgs(GraphBuilder, View, HardwareRayTracingIndirectArgsBuffer, RayAllocatorBuffer, true);

	uint32 DefaultThreadCount = CVarLumenScreenProbeGatherHardwareRayTracingDefaultThreadCount.GetValueOnRenderThread();
	uint32 DefaultGroupCount = CVarLumenScreenProbeGatherHardwareRayTracingDefaultGroupCount.GetValueOnRenderThread();

	bool bEnableHitLighting = false;
	bool bEnableFarFieldTracing = PermutationVector.Get<FLumenScreenProbeGatherHardwareRayTracingCS::FEnableFarFieldTracing>();

	FLumenScreenProbeGatherHardwareRayTracingCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenScreenProbeGatherHardwareRayTracingCS::FParameters>();
	SetLumenHardwareRayTracingScreenProbeParameters(GraphBuilder,
		SceneTextures,
		ScreenProbeParameters,
		View,
		TracingInputs,
		HardwareRayTracingIndirectArgsBuffer,
		IndirectTracingParameters,
		RadianceCacheParameters,
		CompactedTraceParameters,
		bApplySkyLight,
		bUseRadianceCache,
		bEnableHitLighting,
		bEnableFarFieldTracing,
		RayAllocatorBuffer,
		TraceTexelDataPackedBuffer,
		RetraceDataPackedBuffer,
		&PassParameters->CommonParameters
	);
	PassParameters->InlineParameters.HitGroupData = View.LumenHardwareRayTracingHitDataBufferSRV;

	TShaderRef<FLumenScreenProbeGatherHardwareRayTracingCS> ComputeShader = View.ShaderMap->GetShader<FLumenScreenProbeGatherHardwareRayTracingCS>(PermutationVector);
	ClearUnusedGraphResources(ComputeShader, PassParameters);

	auto GenerateModeString = [bEnableHitLighting, bEnableFarFieldTracing]()
	{
		FString ModeStr = bEnableHitLighting ? FString::Printf(TEXT("[hit-lighting]")) :
			(bEnableFarFieldTracing ? FString::Printf(TEXT("[far-field]")) : FString::Printf(TEXT("[default]")));

		return ModeStr;
	};

	FIntPoint DispatchResolution = FIntPoint(DefaultThreadCount, DefaultGroupCount);
	auto GenerateResolutionString = [DispatchResolution]()
	{
		FString ResolutionStr = IsHardwareRayTracingScreenProbeGatherIndirectDispatch() ? FString::Printf(TEXT("<indirect>")) :
			FString::Printf(TEXT("%ux%u"), DispatchResolution.X, DispatchResolution.Y);

		return ResolutionStr;
	};

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HardwareInlineRayTracing %s %s", *GenerateModeString(), *GenerateResolutionString()),
		PassParameters,
		ERDGPassFlags::Compute,
		[PassParameters, &View, ComputeShader, DispatchResolution](FRHIRayTracingCommandList& RHICmdList)
		{
			FRHIComputeShader* ShaderRHI = ComputeShader.GetComputeShader();
			RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());
			SetShaderParameters(RHICmdList, ComputeShader, ShaderRHI, *PassParameters);

			if (IsHardwareRayTracingScreenProbeGatherIndirectDispatch())
			{
				DispatchIndirectComputeShader(RHICmdList, ComputeShader.GetShader(), PassParameters->CommonParameters.HardwareRayTracingIndirectArgs->GetIndirectRHICallBuffer(), 0);
			}
			else
			{
				const FIntPoint GroupSize(FLumenScreenProbeGatherHardwareRayTracingCS::ThreadGroupSizeX, FLumenScreenProbeGatherHardwareRayTracingCS::ThreadGroupSizeY);
				const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(DispatchResolution, GroupSize);
				DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), GroupCount.X, GroupCount.Y, 1);
			}

			UnsetShaderUAVs(RHICmdList, ComputeShader, ShaderRHI);
		}
	);
}

void DispatchRayGenShader(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	FScreenProbeParameters& ScreenProbeParameters,
	const FLumenCardTracingInputs& TracingInputs,
	FLumenIndirectTracingParameters& IndirectTracingParameters,
	const FCompactedTraceParameters& CompactedTraceParameters,
	const LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters,
	const FLumenScreenProbeGatherHardwareRayTracingRGS::FPermutationDomain& PermutationVector,
	uint32 RayCount,
	bool bApplySkyLight,
	bool bUseRadianceCache,
	FRDGBufferRef RayAllocatorBuffer,
	FRDGBufferRef TraceTexelDataPackedBuffer,
	FRDGBufferRef RetraceDataPackedBuffer
)
{
	FRDGBufferRef HardwareRayTracingIndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Lumen.ScreenProbeGather.HardwareRayTracing.IndirectArgsCS"));
	DispatchLumenScreenProbeGatherHardwareRayTracingIndirectArgs(GraphBuilder, View, HardwareRayTracingIndirectArgsBuffer, RayAllocatorBuffer, false);

	uint32 DefaultThreadCount = CVarLumenScreenProbeGatherHardwareRayTracingDefaultThreadCount.GetValueOnRenderThread();
	uint32 DefaultGroupCount = CVarLumenScreenProbeGatherHardwareRayTracingDefaultGroupCount.GetValueOnRenderThread();

	bool bEnableHitLighting = PermutationVector.Get<FLumenScreenProbeGatherHardwareRayTracingRGS::FLightingModeDim>() == LumenHWRTPipeline::ELightingMode::HitLighting;
	bool bEnableFarFieldTracing = PermutationVector.Get<FLumenScreenProbeGatherHardwareRayTracingRGS::FEnableFarFieldTracing>();

	FLumenScreenProbeGatherHardwareRayTracingRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenScreenProbeGatherHardwareRayTracingRGS::FParameters>();
	SetLumenHardwareRayTracingScreenProbeParameters(GraphBuilder,
		SceneTextures,
		ScreenProbeParameters,
		View,
		TracingInputs,
		HardwareRayTracingIndirectArgsBuffer,
		IndirectTracingParameters,
		RadianceCacheParameters,
		CompactedTraceParameters,
		bApplySkyLight,
		bUseRadianceCache,
		bEnableHitLighting,
		bEnableFarFieldTracing,
		RayAllocatorBuffer,
		TraceTexelDataPackedBuffer,
		RetraceDataPackedBuffer,
		PassParameters
	);

	TShaderRef<FLumenScreenProbeGatherHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenScreenProbeGatherHardwareRayTracingRGS>(PermutationVector);
	ClearUnusedGraphResources(RayGenerationShader, PassParameters);

	auto GenerateModeString = [bEnableHitLighting, bEnableFarFieldTracing]()
	{
		FString ModeStr = bEnableHitLighting ? FString::Printf(TEXT("[hit-lighting]")) :
			(bEnableFarFieldTracing ? FString::Printf(TEXT("[far-field]")) : FString::Printf(TEXT("[default]")));

		return ModeStr;
	};

	FIntPoint DispatchResolution = FIntPoint(DefaultThreadCount, DefaultGroupCount);
	auto GenerateResolutionString = [DispatchResolution]()
	{
		FString ResolutionStr = IsHardwareRayTracingScreenProbeGatherIndirectDispatch() ? FString::Printf(TEXT("<indirect>")) :
			FString::Printf(TEXT("%ux%u"), DispatchResolution.X, DispatchResolution.Y);

		return ResolutionStr;
	};

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HardwareRayTracing %s %s", *GenerateModeString(), *GenerateResolutionString()),
		PassParameters,
		ERDGPassFlags::Compute,
		[PassParameters, &View, RayGenerationShader, bEnableHitLighting, DispatchResolution](FRHIRayTracingCommandList& RHICmdList)
		{
			FRayTracingShaderBindingsWriter GlobalResources;
			SetShaderParameters(GlobalResources, RayGenerationShader, *PassParameters);

			FRHIRayTracingScene* RayTracingSceneRHI = View.GetRayTracingSceneChecked();
			FRayTracingPipelineState* Pipeline = (bEnableHitLighting) ? View.RayTracingMaterialPipeline : View.LumenHardwareRayTracingMaterialPipeline;

			if (IsHardwareRayTracingScreenProbeGatherIndirectDispatch())
			{
				PassParameters->HardwareRayTracingIndirectArgs->MarkResourceAsUsed();
				RHICmdList.RayTraceDispatchIndirect(Pipeline, RayGenerationShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources,
					PassParameters->HardwareRayTracingIndirectArgs->GetIndirectRHICallBuffer(), 0);
			}
			else
			{
				RHICmdList.RayTraceDispatch(Pipeline, RayGenerationShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources,
					DispatchResolution.X, DispatchResolution.Y);
			}
		}
	);
}

#endif // RHI_RAYTRACING

void RenderHardwareRayTracingScreenProbe(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FSceneTextureParameters& SceneTextures,
	FScreenProbeParameters& ScreenProbeParameters,
	const FViewInfo& View,
	const FLumenCardTracingInputs& TracingInputs,
	FLumenIndirectTracingParameters& IndirectTracingParameters,
	const LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters,
	const FCompactedTraceParameters& CompactedTraceParameters)
#if RHI_RAYTRACING
{
	const uint32 NumTracesPerProbe = ScreenProbeParameters.ScreenProbeTracingOctahedronResolution * ScreenProbeParameters.ScreenProbeTracingOctahedronResolution;
	FIntPoint RayTracingResolution = FIntPoint(ScreenProbeParameters.ScreenProbeAtlasViewSize.X * ScreenProbeParameters.ScreenProbeAtlasViewSize.Y * NumTracesPerProbe, 1);
	int32 MaxRayCount = RayTracingResolution.X * RayTracingResolution.Y;

	FRDGBufferRef RayAllocatorBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("Lumen.ScreenProbeGather.HardwareRayTracing.RayAllocatorBuffer"));
	{
		FConvertRayAllocatorCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FConvertRayAllocatorCS::FParameters>();
		{
			PassParameters->Allocator = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(CompactedTraceParameters.CompactedTraceTexelAllocator->Desc.Buffer));
			PassParameters->RWRayAllocator = GraphBuilder.CreateUAV(RayAllocatorBuffer, PF_R32_UINT);
		}

		TShaderRef<FConvertRayAllocatorCS> ComputeShader = View.ShaderMap->GetShader<FConvertRayAllocatorCS>();
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("FConvertRayAllocatorCS"),
			ComputeShader,
			PassParameters,
			FIntVector(1, 1, 1));
	}

	FRDGBufferRef RetraceDataPackedBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(LumenHWRTPipeline::FTraceDataPacked), MaxRayCount), TEXT("Lumen.ScreenProbeGather.HardwareRayTracing.TraceDataPacked"));
	FRDGBufferRef TraceTexelDataPackedBuffer = CompactedTraceParameters.CompactedTraceTexelData->Desc.Buffer;

	const bool bUseFarFieldForScreenProbeGather = UseFarFieldForScreenProbeGather(*View.Family);
	const bool bIsForceHitLighting = IsHitLightingForceEnabledForScreenProbeGather();
	const bool bInlineRayTracing = Lumen::UseHardwareInlineRayTracing();
	const bool bUseRadianceCache = LumenScreenProbeGather::UseRadianceCache(View);

	// Default tracing of near-field, extract surface cache and material-id
	{
		bool bApplySkyLight = !bUseFarFieldForScreenProbeGather;

		FLumenScreenProbeGatherHardwareRayTracingRGS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FLightingModeDim>(LumenHWRTPipeline::ELightingMode::SurfaceCache);
		PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FRadianceCache>(bUseRadianceCache);
		PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FEnableNearFieldTracing>(true);
		PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FEnableFarFieldTracing>(false);
		PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FWriteFinalLightingDim>(!bIsForceHitLighting || !bUseFarFieldForScreenProbeGather);
		PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FIndirectDispatchDim>(IsHardwareRayTracingScreenProbeGatherIndirectDispatch());
		PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FStructuredImportanceSamplingDim>(LumenScreenProbeGather::UseImportanceSampling(View));
		PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FPackTraceDataDim>(bUseFarFieldForScreenProbeGather);

		if (bInlineRayTracing)
		{
			DispatchComputeShader(GraphBuilder, Scene, SceneTextures, View, ScreenProbeParameters, TracingInputs, IndirectTracingParameters, CompactedTraceParameters, RadianceCacheParameters,
				ToComputePermutationVector(PermutationVector), MaxRayCount, bApplySkyLight, bUseRadianceCache,
				RayAllocatorBuffer, TraceTexelDataPackedBuffer, RetraceDataPackedBuffer);
		}
		else
		{
			DispatchRayGenShader(GraphBuilder, Scene, SceneTextures, View, ScreenProbeParameters, TracingInputs, IndirectTracingParameters, CompactedTraceParameters, RadianceCacheParameters,
				PermutationVector, MaxRayCount, bApplySkyLight, bUseRadianceCache,
				RayAllocatorBuffer, TraceTexelDataPackedBuffer, RetraceDataPackedBuffer);
		}
	}

	FRDGBufferRef FarFieldRayAllocatorBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("Lumen.ScreenProbeGather.HardwareRayTracing.FarFieldRayAllocatorBuffer"));
	FRDGBufferRef FarFieldRetraceDataPackedBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(LumenHWRTPipeline::FTraceDataPacked), MaxRayCount), TEXT("Lumen.ScreenProbeGather.HardwareRayTracing.FarFieldRetraceDataPackedBuffer"));
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(FarFieldRayAllocatorBuffer, PF_R32_UINT), 0);
	if (bUseFarFieldForScreenProbeGather)
	{
		LumenHWRTCompactRays(GraphBuilder, Scene, View, MaxRayCount, LumenHWRTPipeline::ECompactMode::FarFieldRetrace,
			RayAllocatorBuffer, RetraceDataPackedBuffer,
			FarFieldRayAllocatorBuffer, FarFieldRetraceDataPackedBuffer);

		bool bApplySkyLight = true;

		FLumenScreenProbeGatherHardwareRayTracingRGS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FLightingModeDim>(LumenHWRTPipeline::ELightingMode::SurfaceCache);
		PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FRadianceCache>(bUseRadianceCache);
		PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FEnableNearFieldTracing>(false);
		PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FEnableFarFieldTracing>(true);
		PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FWriteFinalLightingDim>(true);
		PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FIndirectDispatchDim>(IsHardwareRayTracingScreenProbeGatherIndirectDispatch());
		PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FStructuredImportanceSamplingDim>(LumenScreenProbeGather::UseImportanceSampling(View));
		PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FPackTraceDataDim>(false);

		// Trace continuation rays
		if (bInlineRayTracing)
		{
			DispatchComputeShader(GraphBuilder, Scene, SceneTextures, View, ScreenProbeParameters, TracingInputs, IndirectTracingParameters, CompactedTraceParameters, RadianceCacheParameters,
				ToComputePermutationVector(PermutationVector), MaxRayCount, bApplySkyLight, bUseRadianceCache,
				FarFieldRayAllocatorBuffer, TraceTexelDataPackedBuffer, FarFieldRetraceDataPackedBuffer);
		}
		else
		{
			DispatchRayGenShader(GraphBuilder, Scene, SceneTextures, View, ScreenProbeParameters, TracingInputs, IndirectTracingParameters, CompactedTraceParameters, RadianceCacheParameters,
				PermutationVector, MaxRayCount, bApplySkyLight, bUseRadianceCache,
				FarFieldRayAllocatorBuffer, TraceTexelDataPackedBuffer, FarFieldRetraceDataPackedBuffer);
		}
	}
}
#else // RHI_RAYTRACING
{
	unimplemented();
}
#endif // RHI_RAYTRACING
