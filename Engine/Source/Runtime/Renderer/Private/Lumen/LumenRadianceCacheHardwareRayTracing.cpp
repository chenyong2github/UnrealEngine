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

#include "LumenRadianceCache.h"

#if RHI_RAYTRACING

#include "RayTracing/RayTracingDeferredMaterials.h"
#include "RayTracing/RaytracingOptions.h"
#include "RayTracing/RayTracingLighting.h"
#include "LumenHardwareRayTracingCommon.h"

// Console variables
static TAutoConsoleVariable<int32> CVarLumenRadianceCacheHardwareRayTracing(
	TEXT("r.Lumen.RadianceCache.HardwareRayTracing"),
	1,
	TEXT("Enables hardware ray tracing for Lumen radiance cache (Default = 1)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenRadianceCacheHardwareRayTracingPersistentTracingGroupCount(
	TEXT("r.Lumen.RadianceCache.HardwareRayTracing.PersistentTracingGroupCount"),
	4096,
	TEXT("Determines the number of trace tile groups to submit in the 1D dispatch"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenRadianceCacheTemporaryBufferAllocationDownsampleFactor(
	TEXT("r.Lumen.RadianceCache.HardwareRayTracing.TemporaryBufferAllocationDownsampleFactor"),
	16,
	TEXT("Downsample factor on the temporary buffer used by Hardware Ray Tracing Radiance Cache.  Higher downsample factors save more transient allocator memory, but may cause overflow and artifacts."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenRadianceCacheHardwareRayTracingRetraceFarField(
	TEXT("r.Lumen.RadianceCache.HardwareRayTracing.Retrace.FarField"),
	1,
	TEXT("Determines whether a second trace will be fired for far-field contribution (Default = 1)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenRadianceCacheHardwareRayTracingIndirect(
	TEXT("r.Lumen.RadianceCache.HardwareRayTracing.Indirect"),
	1,
	TEXT("Enables indirect dispatch for hardware ray tracing for Lumen radiance cache (Default = 1)"),
	ECVF_RenderThreadSafe
);

#endif // RHI_RAYTRACING

namespace Lumen
{
	bool UseHardwareRayTracedRadianceCache()
	{
#if RHI_RAYTRACING
		return IsRayTracingEnabled()
			&& Lumen::UseHardwareRayTracing()
			&& (CVarLumenRadianceCacheHardwareRayTracing.GetValueOnRenderThread() != 0);
#else
		return false;
#endif
	}

	EHardwareRayTracingLightingMode GetRadianceCacheHardwareRayTracingLightingMode()
	{
#if RHI_RAYTRACING
		// Disable hit-lighting for the radiance cache
		return EHardwareRayTracingLightingMode::LightingFromSurfaceCache;
#else
		return EHardwareRayTracingLightingMode::LightingFromSurfaceCache;
#endif
	}
}

#if RHI_RAYTRACING

// Must match definition in LumenRadianceCacheHardwareRayTracing.usf
struct FTraceTileResultPacked
{
	uint32 PackedData[2];
};

class FLumenRadianceCacheHardwareRayTracingRGS : public FLumenHardwareRayTracingRGS
{
	DECLARE_GLOBAL_SHADER(FLumenRadianceCacheHardwareRayTracingRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FLumenRadianceCacheHardwareRayTracingRGS, FLumenHardwareRayTracingRGS)

	// Permutations
	class FLightingModeDim : SHADER_PERMUTATION_ENUM_CLASS("DIM_LIGHTING_MODE", LumenHWRTPipeline::ELightingMode);
	class FEnableNearFieldTracing : SHADER_PERMUTATION_BOOL("ENABLE_NEAR_FIELD_TRACING");
	class FEnableFarFieldTracing : SHADER_PERMUTATION_BOOL("ENABLE_FAR_FIELD_TRACING");
	class FIndirectDispatchDim : SHADER_PERMUTATION_BOOL("DIM_INDIRECT_DISPATCH");
	class FPackTraceDataDim : SHADER_PERMUTATION_BOOL("DIM_PACK_TRACE_DATA");
	class FSpecularOcclusionDim : SHADER_PERMUTATION_BOOL("DIM_SPECULAR_OCCLUSION");
	class FClipRayDim : SHADER_PERMUTATION_BOOL("DIM_CLIP_RAY");
	using FPermutationDomain = TShaderPermutationDomain<FLightingModeDim, FEnableNearFieldTracing, FEnableFarFieldTracing, FIndirectDispatchDim, FSpecularOcclusionDim, FPackTraceDataDim, FClipRayDim>;

	// Parameters
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenHardwareRayTracingRGS::FSharedParameters, SharedParameters)
		RDG_BUFFER_ACCESS(HardwareRayTracingIndirectArgs, ERHIAccess::IndirectArgs | ERHIAccess::SRVCompute)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, RayAllocatorBuffer)

		// Probe data
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenIndirectTracingParameters, IndirectTracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInterpolationParameters, RadianceCacheParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ProbeTraceTileAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint2>, ProbeTraceTileData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, ProbeTraceData)

		// Constants
		SHADER_PARAMETER(uint32, PersistentTracingGroupCount)
		SHADER_PARAMETER(float, FarFieldBias)
		SHADER_PARAMETER(float, FarFieldMaxTraceDistance)
		SHADER_PARAMETER(float, RayTracingCullingRadius)
		SHADER_PARAMETER(float, PullbackBias)
		SHADER_PARAMETER(int, MaxTranslucentSkipCount)
		SHADER_PARAMETER(uint32, MaxTraversalIterations)
		SHADER_PARAMETER(int, ApplySkyLight)

		SHADER_PARAMETER(FVector3f, FarFieldReferencePos)

		// Output
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<TraceTileResult>, RWTraceTileResultPackedBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<LumenHWRTPipeline::FTraceDataPacked>, RWRetraceDataPackedBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static uint32 GetGroupSize()
	{
		// Must match RADIANCE_CACHE_TRACE_TILE_SIZE_2D
		return 8;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FLumenHardwareRayTracingRGS::ModifyCompilationEnvironment(Parameters, Lumen::ESurfaceCacheSampling::AlwaysResidentPages, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_DISPATCH_1D"), 1);

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

		// Currently disable hit-lighting and specular occlusion modes
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		bool bSurfaceCacheLightingMode = PermutationVector.Get<FLightingModeDim>() == LumenHWRTPipeline::ELightingMode::SurfaceCache;
		bool bSpecularOcclusion = PermutationVector.Get<FSpecularOcclusionDim>();

		return bSurfaceCacheLightingMode && !bSpecularOcclusion;
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenRadianceCacheHardwareRayTracingRGS, "/Engine/Private/Lumen/LumenRadianceCacheHardwareRayTracing.usf", "LumenRadianceCacheHardwareRayTracingRGS", SF_RayGen);

class FLumenRadianceCacheHardwareRayTracingCS : public FLumenHardwareRayTracingCS
{
	DECLARE_GLOBAL_SHADER(FLumenRadianceCacheHardwareRayTracingCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenRadianceCacheHardwareRayTracingCS, FLumenHardwareRayTracingCS)

	class FEnableNearFieldTracing : SHADER_PERMUTATION_BOOL("ENABLE_NEAR_FIELD_TRACING");
	class FEnableFarFieldTracing : SHADER_PERMUTATION_BOOL("ENABLE_FAR_FIELD_TRACING");
	class FIndirectDispatchDim : SHADER_PERMUTATION_BOOL("DIM_INDIRECT_DISPATCH");
	class FPackTraceDataDim : SHADER_PERMUTATION_BOOL("DIM_PACK_TRACE_DATA");
	class FSpecularOcclusionDim : SHADER_PERMUTATION_BOOL("DIM_SPECULAR_OCCLUSION");
	class FClipRayDim : SHADER_PERMUTATION_BOOL("DIM_CLIP_RAY");
	using FPermutationDomain = TShaderPermutationDomain<FEnableNearFieldTracing, FEnableFarFieldTracing, FIndirectDispatchDim, FSpecularOcclusionDim, FPackTraceDataDim, FClipRayDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenRadianceCacheHardwareRayTracingRGS::FParameters, CommonParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenHardwareRayTracingCS::FInlineParameters, InlineParameters)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FLumenHardwareRayTracingCS::ModifyCompilationEnvironment(Parameters, Lumen::ESurfaceCacheSampling::AlwaysResidentPages, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("INLINE_RAY_TRACING_THREAD_GROUP_SIZE_X"), ThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("INLINE_RAY_TRACING_THREAD_GROUP_SIZE_Y"), ThreadGroupSizeY);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		if (!FLumenHardwareRayTracingCS::ShouldCompilePermutation(Parameters))
		{
			return false;
		}

		// Currently disable specular occlusion mode
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		bool bSpecularOcclusion = PermutationVector.Get<FSpecularOcclusionDim>();

		return !bSpecularOcclusion;
	}

	// Current inline ray tracing implementation requires 1:1 mapping between thread groups and waves and only supports wave32 mode.
	static constexpr uint32 ThreadGroupSizeX = 32;
	static constexpr uint32 ThreadGroupSizeY = 1;
};

IMPLEMENT_GLOBAL_SHADER(FLumenRadianceCacheHardwareRayTracingCS, "/Engine/Private/Lumen/LumenRadianceCacheHardwareRayTracing.usf", "LumenRadianceCacheHardwareRayTracingCS", SF_Compute);

class FLumenRadianceCacheHardwareRayTracingIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenRadianceCacheHardwareRayTracingIndirectArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenRadianceCacheHardwareRayTracingIndirectArgsCS, FGlobalShader)

	class FInlineRaytracing : SHADER_PERMUTATION_BOOL("DIM_INLINE_RAYTRACING");
	using FPermutationDomain = TShaderPermutationDomain<FInlineRaytracing>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, RayAllocatorBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWHardwareRayTracingIndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("INLINE_RAY_TRACING_THREAD_GROUP_SIZE_X"), FLumenRadianceCacheHardwareRayTracingCS::ThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("INLINE_RAY_TRACING_THREAD_GROUP_SIZE_Y"), FLumenRadianceCacheHardwareRayTracingCS::ThreadGroupSizeY);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenRadianceCacheHardwareRayTracingIndirectArgsCS, "/Engine/Private/Lumen/LumenRadianceCacheHardwareRayTracing.usf", "LumenRadianceCacheHardwareRayTracingIndirectArgsCS", SF_Compute);


class FSplatRadianceCacheIntoAtlasCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSplatRadianceCacheIntoAtlasCS)
	SHADER_USE_PARAMETER_STRUCT(FSplatRadianceCacheIntoAtlasCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWRadianceProbeAtlasTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWDepthProbeAtlasTexture)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTraceTileResultPacked>, TraceTileResultPackedBuffer)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenIndirectTracingParameters, IndirectTracingParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, ProbeTraceData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint2>, ProbeTraceTileData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ProbeTraceTileAllocator)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInterpolationParameters, RadianceCacheParameters)
		RDG_BUFFER_ACCESS(TraceProbesIndirectArgs, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER(uint32, TraceTileResultPackedBufferElementCount)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		// Must match RADIANCE_CACHE_TRACE_TILE_SIZE_2D
		return 8;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		// Workaround for an internal PC FXC compiler crash when compiling with disabled optimizations
		if (Parameters.Platform == SP_PCD3D_SM5)
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_ForceOptimization);
		}
	}
};

IMPLEMENT_GLOBAL_SHADER(FSplatRadianceCacheIntoAtlasCS, "/Engine/Private/Lumen/LumenRadianceCacheHardwareRayTracing.usf", "SplatRadianceCacheIntoAtlasCS", SF_Compute);

bool UseFarFieldForRadianceCache(const FSceneViewFamily& ViewFamily)
{
	return Lumen::UseFarField(ViewFamily) && CVarLumenRadianceCacheHardwareRayTracingRetraceFarField.GetValueOnRenderThread();
}

bool IsHardwareRayTracingRadianceCacheIndirectDispatch()
{
	return GRHISupportsRayTracingDispatchIndirect && (CVarLumenRadianceCacheHardwareRayTracingIndirect.GetValueOnRenderThread() == 1);
}

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingRadianceCache(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	if (Lumen::GetRadianceCacheHardwareRayTracingLightingMode() != Lumen::EHardwareRayTracingLightingMode::LightingFromSurfaceCache)
	{
		FLumenRadianceCacheHardwareRayTracingRGS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FLightingModeDim>(LumenHWRTPipeline::ELightingMode::HitLighting);
		PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FEnableNearFieldTracing>(true);
		PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FEnableFarFieldTracing>(false);
		PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FIndirectDispatchDim>(IsHardwareRayTracingRadianceCacheIndirectDispatch());
		PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FSpecularOcclusionDim>(false);
		PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FClipRayDim>(GetRayTracingCulling() != 0);
		TShaderRef<FLumenRadianceCacheHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenRadianceCacheHardwareRayTracingRGS>(PermutationVector);

		OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
	}
}

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingRadianceCacheDeferredMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
}

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingRadianceCacheLumenMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	Lumen::EHardwareRayTracingLightingMode LightingMode = Lumen::GetRadianceCacheHardwareRayTracingLightingMode();
	bool bUseMinimalPayload = LightingMode == Lumen::EHardwareRayTracingLightingMode::LightingFromSurfaceCache;

	if (Lumen::UseHardwareRayTracedRadianceCache() && bUseMinimalPayload)
	{
		// Default trace
		// We have to prepare with and without FPackTraceDataDim because FRadianceCacheConfiguration can change whether FarField gets used
		{
			FLumenRadianceCacheHardwareRayTracingRGS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FLightingModeDim>(LumenHWRTPipeline::ELightingMode::SurfaceCache);
			PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FEnableNearFieldTracing>(true);
			PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FEnableFarFieldTracing>(false);
			PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FIndirectDispatchDim>(IsHardwareRayTracingRadianceCacheIndirectDispatch());
			PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FSpecularOcclusionDim>(false);
			PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FPackTraceDataDim>(false);
			PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FClipRayDim>(GetRayTracingCulling() != 0);
			TShaderRef<FLumenRadianceCacheHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenRadianceCacheHardwareRayTracingRGS>(PermutationVector);

			OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
		}

		if (UseFarFieldForRadianceCache(*View.Family))
		{
			// Default trace
			{
				FLumenRadianceCacheHardwareRayTracingRGS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FLightingModeDim>(LumenHWRTPipeline::ELightingMode::SurfaceCache);
				PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FEnableNearFieldTracing>(true);
				PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FEnableFarFieldTracing>(false);
				PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FIndirectDispatchDim>(IsHardwareRayTracingRadianceCacheIndirectDispatch());
				PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FSpecularOcclusionDim>(false);
				PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FPackTraceDataDim>(true);
				PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FClipRayDim>(GetRayTracingCulling() != 0);
				TShaderRef<FLumenRadianceCacheHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenRadianceCacheHardwareRayTracingRGS>(PermutationVector);

				OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
			}

			// Far-field trace
			{
				FLumenRadianceCacheHardwareRayTracingRGS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FLightingModeDim>(LumenHWRTPipeline::ELightingMode::SurfaceCache);
				PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FEnableNearFieldTracing>(false);
				PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FEnableFarFieldTracing>(true);
				PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FIndirectDispatchDim>(IsHardwareRayTracingRadianceCacheIndirectDispatch());
				PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FSpecularOcclusionDim>(false);
				PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FPackTraceDataDim>(false);
				PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FClipRayDim>(GetRayTracingCulling() != 0);
				TShaderRef<FLumenRadianceCacheHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenRadianceCacheHardwareRayTracingRGS>(PermutationVector);
			
				OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
			}
		}
	}
}

void SetLumenHardwareRayTracingRadianceCacheParameters(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FSceneTextureParameters& SceneTextures,
	const FLumenCardTracingInputs& TracingInputs,
	const LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters,
	float DiffuseConeHalfAngle,
	bool bApplySkyLight,
	bool bEnableHitLighting,
	bool bEnableFarFieldTracing,
	FRDGBufferRef ProbeTraceTileAllocator,
	FRDGBufferRef ProbeTraceTileData,
	FRDGBufferRef ProbeTraceData,
	FRDGBufferRef RayAllocatorBuffer,
	FRDGBufferRef RetraceDataPackedBuffer,
	FRDGBufferRef TraceTileResultPackedBuffer,
	FRDGBufferRef HardwareRayTracingIndirectArgsBuffer,
	FLumenRadianceCacheHardwareRayTracingRGS::FParameters* PassParameters
)
{
	SetLumenHardwareRayTracingSharedParameters(
		GraphBuilder,
		SceneTextures,
		View,
		TracingInputs,
		&PassParameters->SharedParameters);

	SetupLumenDiffuseTracingParametersForProbe(View, PassParameters->IndirectTracingParameters, DiffuseConeHalfAngle);

	PassParameters->RadianceCacheParameters = RadianceCacheParameters;
	PassParameters->ProbeTraceData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceData, PF_A32B32G32R32F));
	PassParameters->ProbeTraceTileData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceTileData, PF_R32G32_UINT));
	PassParameters->ProbeTraceTileAllocator = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceTileAllocator, PF_R32_UINT));
	PassParameters->HardwareRayTracingIndirectArgs = HardwareRayTracingIndirectArgsBuffer;
	PassParameters->RayAllocatorBuffer = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(RayAllocatorBuffer, PF_R32_UINT));

	// Constants
	PassParameters->PersistentTracingGroupCount = CVarLumenRadianceCacheHardwareRayTracingPersistentTracingGroupCount.GetValueOnRenderThread();
	PassParameters->FarFieldBias = LumenHardwareRayTracing::GetFarFieldBias();
	PassParameters->FarFieldMaxTraceDistance = Lumen::GetFarFieldMaxTraceDistance();
	PassParameters->RayTracingCullingRadius = GetRayTracingCullingRadius();
	PassParameters->FarFieldReferencePos = (FVector3f)Lumen::GetFarFieldReferencePos();
	PassParameters->PullbackBias = Lumen::GetHardwareRayTracingPullbackBias();
	PassParameters->MaxTranslucentSkipCount = Lumen::GetMaxTranslucentSkipCount();
	PassParameters->MaxTraversalIterations = LumenHardwareRayTracing::GetMaxTraversalIterations();
	PassParameters->ApplySkyLight = bApplySkyLight;

	// Output
	PassParameters->RWTraceTileResultPackedBuffer = GraphBuilder.CreateUAV(TraceTileResultPackedBuffer);

	// Ray continuation buffer
	PassParameters->RWRetraceDataPackedBuffer = GraphBuilder.CreateUAV(RetraceDataPackedBuffer);
}

FLumenRadianceCacheHardwareRayTracingCS::FPermutationDomain ToComputePermutationVector(const FLumenRadianceCacheHardwareRayTracingRGS::FPermutationDomain& RGSPermutationVector)
{
	FLumenRadianceCacheHardwareRayTracingCS::FPermutationDomain PermutationVector;

	PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingCS::FEnableFarFieldTracing>(
		RGSPermutationVector.Get<FLumenRadianceCacheHardwareRayTracingRGS::FEnableFarFieldTracing>());

	PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingCS::FEnableNearFieldTracing>(
		RGSPermutationVector.Get<FLumenRadianceCacheHardwareRayTracingRGS::FEnableNearFieldTracing>());

	PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingCS::FIndirectDispatchDim>(
		RGSPermutationVector.Get<FLumenRadianceCacheHardwareRayTracingRGS::FIndirectDispatchDim>());

	PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingCS::FSpecularOcclusionDim>(
		RGSPermutationVector.Get<FLumenRadianceCacheHardwareRayTracingRGS::FSpecularOcclusionDim>());

	PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingCS::FPackTraceDataDim>(
		RGSPermutationVector.Get<FLumenRadianceCacheHardwareRayTracingRGS::FPackTraceDataDim>());

	PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingCS::FClipRayDim>(
		RGSPermutationVector.Get<FLumenRadianceCacheHardwareRayTracingRGS::FClipRayDim>());

	return PermutationVector;
}

namespace LumenRadianceCache {

	FString GenerateModeString(bool bEnableHitLighting, bool bEnableFarFieldTracing)
	{
		FString ModeStr = bEnableHitLighting ? FString::Printf(TEXT("[hit-lighting]")) :
			(bEnableFarFieldTracing ? FString::Printf(TEXT("[far-field]")) : FString::Printf(TEXT("[default]")));

		return ModeStr;
	}

	FString GenerateResolutionString(const FIntPoint& DispatchResolution)
	{
		FString ResolutionStr = IsHardwareRayTracingRadianceCacheIndirectDispatch() ? FString::Printf(TEXT("<indirect>")) :
			FString::Printf(TEXT("%ux%u"), DispatchResolution.X, DispatchResolution.Y);

		return ResolutionStr;
	};

} // namespace LumenRadianceCache

void DispatchComputeShader(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	const FSceneTextureParameters& SceneTextures,
	const FLumenCardTracingInputs& TracingInputs,
	const LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters,
	const FLumenRadianceCacheHardwareRayTracingCS::FPermutationDomain& PermutationVector,
	float DiffuseConeHalfAngle,
	int32 MaxNumProbes,
	int32 MaxProbeTraceTileResolution,
	bool bApplySkyLight,
	FRDGBufferRef ProbeTraceTileAllocator,
	FRDGBufferRef ProbeTraceTileData,
	FRDGBufferRef ProbeTraceData,
	FRDGBufferRef RayAllocatorBuffer,
	FRDGBufferRef RetraceDataPackedBuffer,
	FRDGBufferRef TraceTileResultPackedBuffer
)
{
	FRDGBufferRef HardwareRayTracingIndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Lumen.RadianceCache.HardwareRayTracing.IndirectArgsBuffer"));
	if (IsHardwareRayTracingRadianceCacheIndirectDispatch())
	{
		FLumenRadianceCacheHardwareRayTracingIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenRadianceCacheHardwareRayTracingIndirectArgsCS::FParameters>();
		{
			PassParameters->RayAllocatorBuffer = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(RayAllocatorBuffer, PF_R32_UINT));
			PassParameters->RWHardwareRayTracingIndirectArgs = GraphBuilder.CreateUAV(HardwareRayTracingIndirectArgsBuffer, PF_R32_UINT);
		}

		FLumenRadianceCacheHardwareRayTracingIndirectArgsCS::FPermutationDomain MyPermutationVector;
		MyPermutationVector.Set<FLumenRadianceCacheHardwareRayTracingIndirectArgsCS::FInlineRaytracing>(true);
		TShaderRef<FLumenRadianceCacheHardwareRayTracingIndirectArgsCS> ComputeShader = View.ShaderMap->GetShader<FLumenRadianceCacheHardwareRayTracingIndirectArgsCS>(MyPermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("HardwareRayTracingIndirectArgsCS"),
			ComputeShader,
			PassParameters,
			FIntVector(1, 1, 1));
	}

	bool bEnableHitLighting = false;
	bool bEnableFarFieldTracing = PermutationVector.Get<FLumenRadianceCacheHardwareRayTracingCS::FEnableFarFieldTracing>();

	FLumenRadianceCacheHardwareRayTracingCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenRadianceCacheHardwareRayTracingCS::FParameters>();
	SetLumenHardwareRayTracingRadianceCacheParameters(
		GraphBuilder,
		View,
		SceneTextures,
		TracingInputs,
		RadianceCacheParameters,
		DiffuseConeHalfAngle,
		bApplySkyLight,
		bEnableHitLighting,
		bEnableFarFieldTracing,
		ProbeTraceTileAllocator,
		ProbeTraceTileData,
		ProbeTraceData,
		RayAllocatorBuffer,
		RetraceDataPackedBuffer,
		TraceTileResultPackedBuffer,
		HardwareRayTracingIndirectArgsBuffer,
		&PassParameters->CommonParameters
	);
	PassParameters->InlineParameters.HitGroupData = View.LumenHardwareRayTracingHitDataBufferSRV;

	TShaderRef<FLumenRadianceCacheHardwareRayTracingCS> ComputeShader = View.ShaderMap->GetShader<FLumenRadianceCacheHardwareRayTracingCS>(PermutationVector);
	ClearUnusedGraphResources(ComputeShader, PassParameters);

	uint32 PersistentTracingGroupCount = CVarLumenRadianceCacheHardwareRayTracingPersistentTracingGroupCount.GetValueOnRenderThread();
	FIntPoint DispatchResolution(FLumenRadianceCacheHardwareRayTracingRGS::GetGroupSize() * FLumenRadianceCacheHardwareRayTracingRGS::GetGroupSize(), PersistentTracingGroupCount);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HardwareInlineRayTracing %s %s", *LumenRadianceCache::GenerateModeString(bEnableHitLighting, bEnableFarFieldTracing), *LumenRadianceCache::GenerateResolutionString(DispatchResolution)),
		PassParameters,
		ERDGPassFlags::Compute,
		[PassParameters, &View, ComputeShader, DispatchResolution](FRHIRayTracingCommandList& RHICmdList)
		{
			FRHIComputeShader* ShaderRHI = ComputeShader.GetComputeShader();
			SetComputePipelineState(RHICmdList, ShaderRHI);
			SetShaderParameters(RHICmdList, ComputeShader, ShaderRHI, *PassParameters);

			if (IsHardwareRayTracingRadianceCacheIndirectDispatch())
			{
				DispatchIndirectComputeShader(RHICmdList, ComputeShader.GetShader(), PassParameters->CommonParameters.HardwareRayTracingIndirectArgs->GetIndirectRHICallBuffer(), 0);
			}
			else
			{
				const FIntPoint GroupSize(FLumenRadianceCacheHardwareRayTracingCS::ThreadGroupSizeX, FLumenRadianceCacheHardwareRayTracingCS::ThreadGroupSizeY);
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
	const FViewInfo& View,
	const FSceneTextureParameters& SceneTextures,
	const FLumenCardTracingInputs& TracingInputs,
	const LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters,
	const FLumenRadianceCacheHardwareRayTracingRGS::FPermutationDomain& PermutationVector,
	float DiffuseConeHalfAngle,
	int32 MaxNumProbes,
	int32 MaxProbeTraceTileResolution,
	bool bApplySkyLight,
	FRDGBufferRef ProbeTraceTileAllocator,
	FRDGBufferRef ProbeTraceTileData,
	FRDGBufferRef ProbeTraceData,
	FRDGBufferRef RayAllocatorBuffer,
	FRDGBufferRef RetraceDataPackedBuffer,
	FRDGBufferRef TraceTileResultPackedBuffer
)
{
	FRDGBufferRef HardwareRayTracingIndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Lumen.RadianceCache.HardwareRayTracing.IndirectArgsBuffer"));
	if (IsHardwareRayTracingRadianceCacheIndirectDispatch())
	{
		FLumenRadianceCacheHardwareRayTracingIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenRadianceCacheHardwareRayTracingIndirectArgsCS::FParameters>();
		{
			PassParameters->RayAllocatorBuffer = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(RayAllocatorBuffer, PF_R32_UINT));
			PassParameters->RWHardwareRayTracingIndirectArgs = GraphBuilder.CreateUAV(HardwareRayTracingIndirectArgsBuffer, PF_R32_UINT);
		}

		FLumenRadianceCacheHardwareRayTracingIndirectArgsCS::FPermutationDomain MyPermutationVector;
		MyPermutationVector.Set<FLumenRadianceCacheHardwareRayTracingIndirectArgsCS::FInlineRaytracing>(false);
		TShaderRef<FLumenRadianceCacheHardwareRayTracingIndirectArgsCS> ComputeShader = View.ShaderMap->GetShader<FLumenRadianceCacheHardwareRayTracingIndirectArgsCS>(MyPermutationVector);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("HardwareRayTracingIndirectArgsCS"),
			ComputeShader,
			PassParameters,
			FIntVector(1, 1, 1));
	}

	bool bEnableHitLighting = PermutationVector.Get<FLumenRadianceCacheHardwareRayTracingRGS::FLightingModeDim>() == LumenHWRTPipeline::ELightingMode::HitLighting;
	bool bEnableFarFieldTracing = PermutationVector.Get<FLumenRadianceCacheHardwareRayTracingRGS::FEnableFarFieldTracing>();

	FLumenRadianceCacheHardwareRayTracingRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenRadianceCacheHardwareRayTracingRGS::FParameters>();
	SetLumenHardwareRayTracingRadianceCacheParameters(
		GraphBuilder,
		View,
		SceneTextures,
		TracingInputs,
		RadianceCacheParameters,
		DiffuseConeHalfAngle,
		bApplySkyLight,
		bEnableHitLighting,
		bEnableFarFieldTracing,
		ProbeTraceTileAllocator,
		ProbeTraceTileData,
		ProbeTraceData,
		RayAllocatorBuffer,
		RetraceDataPackedBuffer,
		TraceTileResultPackedBuffer,
		HardwareRayTracingIndirectArgsBuffer,
		PassParameters
	);

	TShaderRef<FLumenRadianceCacheHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenRadianceCacheHardwareRayTracingRGS>(PermutationVector);

	uint32 PersistentTracingGroupCount = CVarLumenRadianceCacheHardwareRayTracingPersistentTracingGroupCount.GetValueOnRenderThread();
	FIntPoint DispatchResolution(FLumenRadianceCacheHardwareRayTracingRGS::GetGroupSize() * FLumenRadianceCacheHardwareRayTracingRGS::GetGroupSize(), PersistentTracingGroupCount);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HardwareRayTracing %s %s", *LumenRadianceCache::GenerateModeString(bEnableHitLighting, bEnableFarFieldTracing), *LumenRadianceCache::GenerateResolutionString(DispatchResolution)),
		PassParameters,
		ERDGPassFlags::Compute,
		[PassParameters, &View, RayGenerationShader, bEnableHitLighting, DispatchResolution](FRHIRayTracingCommandList& RHICmdList)
		{
			FRayTracingShaderBindingsWriter GlobalResources;
			SetShaderParameters(GlobalResources, RayGenerationShader, *PassParameters);

			FRHIRayTracingScene* RayTracingSceneRHI = View.GetRayTracingSceneChecked();
			FRayTracingPipelineState* Pipeline = (bEnableHitLighting) ? View.RayTracingMaterialPipeline : View.LumenHardwareRayTracingMaterialPipeline;

			if (IsHardwareRayTracingRadianceCacheIndirectDispatch())
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

void RenderLumenHardwareRayTracingRadianceCacheTwoPass(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	const FLumenCardTracingInputs& TracingInputs,
	const LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters,
	FRadianceCacheConfiguration Configuration,
	float DiffuseConeHalfAngle,
	int32 MaxNumProbes,
	int32 MaxProbeTraceTileResolution,
	FRDGBufferRef ProbeTraceData,
	FRDGBufferRef ProbeTraceTileData,
	FRDGBufferRef ProbeTraceTileAllocator,
	FRDGBufferRef TraceProbesIndirectArgs,
	FRDGBufferRef HardwareRayTracingRayAllocatorBuffer,
	FRDGBufferRef RadianceCacheHardwareRayTracingIndirectArgs,
	FRDGTextureUAVRef RadianceProbeAtlasTextureUAV,
	FRDGTextureUAVRef DepthProbeTextureUAV
)
{
#if RHI_RAYTRACING
	// Must match usf
	const int32 TempAtlasTraceTileStride = 1024;
	extern int32 GRadianceCacheForceFullUpdate;
	// Overflow is possible however unlikely - only nearby probes trace at max resolution
	const int32 TemporaryBufferAllocationDownsampleFactor = GRadianceCacheForceFullUpdate ? 4 : CVarLumenRadianceCacheTemporaryBufferAllocationDownsampleFactor.GetValueOnRenderThread();
	const int32 TempAtlasNumTraceTiles = FMath::DivideAndRoundUp(MaxProbeTraceTileResolution * MaxProbeTraceTileResolution, TemporaryBufferAllocationDownsampleFactor);
	const FIntPoint WrappedTraceTileLayout(
		TempAtlasTraceTileStride,
		FMath::DivideAndRoundUp(MaxNumProbes * TempAtlasNumTraceTiles, TempAtlasTraceTileStride));

	uint32 TraceTileResultPackedBufferElementCount = MaxNumProbes * TempAtlasNumTraceTiles * FLumenRadianceCacheHardwareRayTracingRGS::GetGroupSize() * FLumenRadianceCacheHardwareRayTracingRGS::GetGroupSize();
	FRDGBufferRef TraceTileResultPackedBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FTraceTileResultPacked), TraceTileResultPackedBufferElementCount), TEXT("Lumen.RadianceCache.HardwareRayTracing.TraceTileResultPackedBuffer"));
	FRDGBufferRef RetraceDataPackedBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(LumenHWRTPipeline::FTraceDataPacked), TraceTileResultPackedBufferElementCount), TEXT("Lumen.RadianceCache.HardwareRayTracing.RetraceTilePackedBuffer"));
	uint32 MaxRayCount = TraceTileResultPackedBufferElementCount;

	const bool bInlineRayTracing = Lumen::UseHardwareInlineRayTracing();
	const bool bUseFarField = UseFarFieldForRadianceCache(*View.Family) && Configuration.bFarField;

	// Default tracing of near-field, extract surface cache and material-id
	{
		bool bApplySkyLight = !bUseFarField;

		FLumenRadianceCacheHardwareRayTracingRGS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FLightingModeDim>(LumenHWRTPipeline::ELightingMode::SurfaceCache);
		PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FEnableNearFieldTracing>(true);
		PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FEnableFarFieldTracing>(false);
		PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FIndirectDispatchDim>(IsHardwareRayTracingRadianceCacheIndirectDispatch());
		PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FSpecularOcclusionDim>(false);
		PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FPackTraceDataDim>(bUseFarField);
		PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FClipRayDim>(GetRayTracingCulling() != 0);

		if (bInlineRayTracing)
		{
			DispatchComputeShader(GraphBuilder, Scene, View, SceneTextures, TracingInputs, RadianceCacheParameters, ToComputePermutationVector(PermutationVector),
				DiffuseConeHalfAngle, MaxNumProbes, MaxProbeTraceTileResolution, bApplySkyLight,
				ProbeTraceTileAllocator, ProbeTraceTileData, ProbeTraceData,
				HardwareRayTracingRayAllocatorBuffer, RetraceDataPackedBuffer, TraceTileResultPackedBuffer);
		}
		else
		{
			DispatchRayGenShader(GraphBuilder, Scene, View, SceneTextures, TracingInputs, RadianceCacheParameters, PermutationVector,
				DiffuseConeHalfAngle, MaxNumProbes, MaxProbeTraceTileResolution, bApplySkyLight,
				ProbeTraceTileAllocator, ProbeTraceTileData, ProbeTraceData,
				HardwareRayTracingRayAllocatorBuffer, RetraceDataPackedBuffer, TraceTileResultPackedBuffer);
		}
	}

	FRDGBufferRef FarFieldRayAllocatorBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("Lumen.RadianceCache.HardwareRayTracing.FarFieldRayAllocatorBuffer"));
	FRDGBufferRef FarFieldRetraceDataPackedBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(LumenHWRTPipeline::FTraceDataPacked), TraceTileResultPackedBufferElementCount), TEXT("Lumen.RadianceCache.HardwareRayTracing.FarFieldRetraceDataPackedBuffer"));
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(FarFieldRayAllocatorBuffer, PF_R32_UINT), 0);

	if (bUseFarField)
	{
		LumenHWRTCompactRays(GraphBuilder, Scene, View, MaxRayCount, LumenHWRTPipeline::ECompactMode::FarFieldRetrace,
			HardwareRayTracingRayAllocatorBuffer, RetraceDataPackedBuffer,
			FarFieldRayAllocatorBuffer, FarFieldRetraceDataPackedBuffer);

		// Trace continuation rays
		{
			bool bApplySkyLight = true;

			FLumenRadianceCacheHardwareRayTracingRGS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FLightingModeDim>(LumenHWRTPipeline::ELightingMode::SurfaceCache);
			PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FEnableNearFieldTracing>(false);
			PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FEnableFarFieldTracing>(true);
			PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FIndirectDispatchDim>(IsHardwareRayTracingRadianceCacheIndirectDispatch());
			PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FSpecularOcclusionDim>(false);
			PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FPackTraceDataDim>(false);
			PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FClipRayDim>(GetRayTracingCulling() != 0);

			if (bInlineRayTracing)
			{
				DispatchComputeShader(GraphBuilder, Scene, View, SceneTextures, TracingInputs, RadianceCacheParameters, ToComputePermutationVector(PermutationVector),
					DiffuseConeHalfAngle, MaxNumProbes, MaxProbeTraceTileResolution, bApplySkyLight,
					ProbeTraceTileAllocator, ProbeTraceTileData, ProbeTraceData,
					FarFieldRayAllocatorBuffer, FarFieldRetraceDataPackedBuffer, TraceTileResultPackedBuffer);
			}
			else
			{
				DispatchRayGenShader(GraphBuilder, Scene, View, SceneTextures, TracingInputs, RadianceCacheParameters, PermutationVector,
					DiffuseConeHalfAngle, MaxNumProbes, MaxProbeTraceTileResolution, bApplySkyLight,
					ProbeTraceTileAllocator, ProbeTraceTileData, ProbeTraceData,
					FarFieldRayAllocatorBuffer, FarFieldRetraceDataPackedBuffer, TraceTileResultPackedBuffer);
			}
		}
	}

	// Reduce to Atlas
	{
		FSplatRadianceCacheIntoAtlasCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSplatRadianceCacheIntoAtlasCS::FParameters>();
		GetLumenCardTracingParameters(View, TracingInputs, PassParameters->TracingParameters);
		SetupLumenDiffuseTracingParametersForProbe(View, PassParameters->IndirectTracingParameters, -1.0f);
		PassParameters->RWRadianceProbeAtlasTexture = RadianceProbeAtlasTextureUAV;
		PassParameters->RWDepthProbeAtlasTexture = DepthProbeTextureUAV;
		PassParameters->TraceTileResultPackedBuffer = GraphBuilder.CreateSRV(TraceTileResultPackedBuffer);
		PassParameters->ProbeTraceData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceData, PF_A32B32G32R32F));
		PassParameters->ProbeTraceTileData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceTileData, PF_R32G32_UINT));
		PassParameters->ProbeTraceTileAllocator = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceTileAllocator, PF_R32_UINT));
		PassParameters->RadianceCacheParameters = RadianceCacheParameters;
		PassParameters->TraceProbesIndirectArgs = TraceProbesIndirectArgs;

		PassParameters->TraceTileResultPackedBufferElementCount = TraceTileResultPackedBufferElementCount;

		FSplatRadianceCacheIntoAtlasCS::FPermutationDomain PermutationVector;
		auto ComputeShader = View.ShaderMap->GetShader<FSplatRadianceCacheIntoAtlasCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("CompositeTracesIntoAtlas"),
			ComputeShader,
			PassParameters,
			PassParameters->TraceProbesIndirectArgs,
			0);
	}
#else
	unimplemented();
#endif // RHI_RAYTRACING
}


void RenderLumenHardwareRayTracingRadianceCache(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	const FLumenCardTracingInputs& TracingInputs,
	const LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters,
	FRadianceCacheConfiguration Configuration,
	float DiffuseConeHalfAngle,
	int32 MaxNumProbes,
	int32 MaxProbeTraceTileResolution,
	FRDGBufferRef ProbeTraceData,
	FRDGBufferRef ProbeTraceTileData,
	FRDGBufferRef ProbeTraceTileAllocator,
	FRDGBufferRef TraceProbesIndirectArgs,
	FRDGBufferRef HardwareRayTracingRayAllocatorBuffer,
	FRDGBufferRef RadianceCacheHardwareRayTracingIndirectArgs,
	FRDGTextureUAVRef RadianceProbeAtlasTextureUAV,
	FRDGTextureUAVRef DepthProbeTextureUAV
)
{
#if RHI_RAYTRACING
	return RenderLumenHardwareRayTracingRadianceCacheTwoPass(
		GraphBuilder,
		Scene,
		SceneTextures,
		View,
		TracingInputs,
		RadianceCacheParameters,
		Configuration,
		DiffuseConeHalfAngle,
		MaxNumProbes,
		MaxProbeTraceTileResolution,
		ProbeTraceData,
		ProbeTraceTileData,
		ProbeTraceTileAllocator,
		TraceProbesIndirectArgs,
		HardwareRayTracingRayAllocatorBuffer,
		RadianceCacheHardwareRayTracingIndirectArgs,
		RadianceProbeAtlasTextureUAV,
		DepthProbeTextureUAV
	);
#else
	unimplemented();
#endif // RHI_RAYTRACING
}
