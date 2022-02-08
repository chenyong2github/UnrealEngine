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
#include "LumenReflections.h"

#if RHI_RAYTRACING
#include "RayTracing/RayTracingDeferredMaterials.h"
#include "RayTracing/RaytracingOptions.h"
#include "RayTracing/RayTracingLighting.h"
#include "LumenHardwareRayTracingCommon.h"

static TAutoConsoleVariable<int32> CVarLumenReflectionsHardwareRayTracing(
	TEXT("r.Lumen.Reflections.HardwareRayTracing"),
	1,
	TEXT("Enables hardware ray tracing for Lumen reflections (Default = 1)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenReflectionsHardwareRayTracingIndirect(
	TEXT("r.Lumen.Reflections.HardwareRayTracing.Indirect"),
	1,
	TEXT("Enables indirect ray tracing dispatch on compatible hardware (Default = 1)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenReflectionsHardwareRayTracingMaxTranslucentSkipCount(
	TEXT("r.Lumen.Reflections.HardwareRayTracing.MaxTranslucentSkipCount"),
	2,
	TEXT("Determines the maximum number of translucent surfaces skipped during ray traversal (Default = 2)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenReflectionsHardwareRayTracingDefaultThreadCount(
	TEXT("r.Lumen.Reflections.HardwareRayTracing.Default.ThreadCount"),
	32768,
	TEXT("Determines the active number of threads (Default = 32768)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenReflectionsHardwareRayTracingDefaultGroupCount(
	TEXT("r.Lumen.Reflections.HardwareRayTracing.Default.GroupCount"),
	1,
	TEXT("Determines the active number of groups (Default = 1)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenReflectionsHardwareRayTracingBucketMaterials(
	TEXT("r.Lumen.Reflections.HardwareRayTracing.BucketMaterials"),
	1,
	TEXT("Determines whether a secondary traces will be bucketed for coherent material access (default = 1"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenReflectionsHardwareRayTracingRetraceHitLighting(
	TEXT("r.Lumen.Reflections.HardwareRayTracing.Retrace.HitLighting"),
	0,
	TEXT("Determines whether a second trace will be fired for hit-lighting for invalid surface-cache hits (Default = 0)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenReflectionsHardwareRayTracingRetraceFarField(
	TEXT("r.Lumen.Reflections.HardwareRayTracing.Retrace.FarField"),
	1,
	TEXT("Determines whether a second trace will be fired for far-field contribution (Default = 1)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenReflectionsHardwareRayTracingRetraceThreadCount(
	TEXT("r.Lumen.Reflections.HardwareRayTracing.Retrace.ThreadCount"),
	32768,
	TEXT("Determines the active number of threads for re-traces (Default = 32768)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenReflectionsHardwareRayTracingRetraceGroupCount(
	TEXT("r.Lumen.Reflections.HardwareRayTracing.Retrace.GroupCount"),
	1,
	TEXT("Determines the active number of groups for re-traces (Default = 1)"),
	ECVF_RenderThreadSafe
);

#endif // RHI_RAYTRACING

namespace Lumen
{
	bool UseHardwareRayTracedReflections()
	{
#if RHI_RAYTRACING
		return IsRayTracingEnabled() 
			&& Lumen::UseHardwareRayTracing() 
			&& (CVarLumenReflectionsHardwareRayTracing.GetValueOnAnyThread() != 0);
#else
		return false;
#endif
	}
} // namespace Lumen

#if RHI_RAYTRACING

bool IsHardwareRayTracingReflectionsIndirectDispatch()
{
	return GRHISupportsRayTracingDispatchIndirect && (CVarLumenReflectionsHardwareRayTracingIndirect.GetValueOnRenderThread() == 1);
}

class FLumenReflectionHardwareRayTracingRGS : public FLumenHardwareRayTracingRGS
{
	DECLARE_GLOBAL_SHADER(FLumenReflectionHardwareRayTracingRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FLumenReflectionHardwareRayTracingRGS, FLumenHardwareRayTracingRGS)

	class FLightingModeDim : SHADER_PERMUTATION_ENUM_CLASS("DIM_LIGHTING_MODE", LumenHWRTPipeline::ELightingMode);
	class FEnableNearFieldTracing : SHADER_PERMUTATION_BOOL("ENABLE_NEAR_FIELD_TRACING");
	class FEnableFarFieldTracing : SHADER_PERMUTATION_BOOL("ENABLE_FAR_FIELD_TRACING");
	class FRadianceCache : SHADER_PERMUTATION_BOOL("DIM_RADIANCE_CACHE");
	class FWriteFinalLightingDim : SHADER_PERMUTATION_BOOL("DIM_WRITE_FINAL_LIGHTING");
	class FPackTraceDataDim : SHADER_PERMUTATION_BOOL("DIM_PACK_TRACE_DATA");
	class FIndirectDispatchDim : SHADER_PERMUTATION_BOOL("DIM_INDIRECT_DISPATCH");
	using FPermutationDomain = TShaderPermutationDomain<FLightingModeDim, FEnableNearFieldTracing, FEnableFarFieldTracing, FRadianceCache, FWriteFinalLightingDim, FIndirectDispatchDim, FPackTraceDataDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenHardwareRayTracingRGS::FSharedParameters, SharedParameters)
		RDG_BUFFER_ACCESS(HardwareRayTracingIndirectArgs, ERHIAccess::IndirectArgs | ERHIAccess::SRVCompute)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, RayAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint2>, TraceTexelDataPacked)

		// Constants
		SHADER_PARAMETER(int, ThreadCount)
		SHADER_PARAMETER(int, GroupCount)
		SHADER_PARAMETER(int, NearFieldLightingMode)

		SHADER_PARAMETER(float, FarFieldBias)
		SHADER_PARAMETER(float, FarFieldMaxTraceDistance)
		SHADER_PARAMETER(float, FarFieldDitheredStartDistanceFactor)
		SHADER_PARAMETER(float, PullbackBias)
		SHADER_PARAMETER(int, MaxTranslucentSkipCount)
		SHADER_PARAMETER(uint32, MaxTraversalIterations)
		SHADER_PARAMETER(int, ApplySkyLight)
		SHADER_PARAMETER(FVector3f, FarFieldReferencePos)

		// Reflection-specific includes (includes output targets)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenReflectionTracingParameters, ReflectionTracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenReflectionTileParameters, ReflectionTileParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInterpolationParameters, RadianceCacheParameters)

		// Ray continuation buffer
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<LumenHWRTPipeline::FTraceDataPacked>, RWTraceDataPacked)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		if (!FLumenHardwareRayTracingRGS::ShouldCompilePermutation(Parameters))
		{
			return false;
		}

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		LumenHWRTPipeline::ELightingMode LightingMode = PermutationVector.Get<FLightingModeDim>();
		bool bEnableNearFieldTracing = PermutationVector.Get<FEnableNearFieldTracing>();

		if (PermutationVector.Get<FRadianceCache>() && !bEnableNearFieldTracing)
		{
			return false;
		}

		if (LightingMode == LumenHWRTPipeline::ELightingMode::SurfaceCache)
		{
			bool bEnableFarFieldTracing = PermutationVector.Get<FEnableFarFieldTracing>();
			bool bWriteFinalLighting = PermutationVector.Get<FWriteFinalLightingDim>();

			return (bEnableNearFieldTracing && !bEnableFarFieldTracing) ||
				(!bEnableNearFieldTracing && bEnableFarFieldTracing && bWriteFinalLighting);
		}
		else if (LightingMode == LumenHWRTPipeline::ELightingMode::HitLighting)
		{
			bool bFWriteFinalLighting = PermutationVector.Get<FWriteFinalLightingDim>();
			bool bFPackTraceData = PermutationVector.Get<FPackTraceDataDim>();

			return bEnableNearFieldTracing && bFWriteFinalLighting && !bFPackTraceData;
		}

		return false;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FLumenHardwareRayTracingRGS::ModifyCompilationEnvironment(Parameters, Lumen::ESurfaceCacheSampling::HighResPages, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_DISPATCH_1D"), 1);

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FLightingModeDim>() == LumenHWRTPipeline::ELightingMode::SurfaceCache)
		{
			OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_LIGHTWEIGHT_CLOSEST_HIT_SHADER"), 1);
		}
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenReflectionHardwareRayTracingRGS, "/Engine/Private/Lumen/LumenReflectionHardwareRayTracing.usf", "LumenReflectionHardwareRayTracingRGS", SF_RayGen);

class FLumenReflectionHardwareRayTracingCS : public FLumenHardwareRayTracingCS
{
	DECLARE_GLOBAL_SHADER(FLumenReflectionHardwareRayTracingCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenReflectionHardwareRayTracingCS, FLumenHardwareRayTracingCS)

	class FEnableNearFieldTracing : SHADER_PERMUTATION_BOOL("ENABLE_NEAR_FIELD_TRACING");
	class FEnableFarFieldTracing : SHADER_PERMUTATION_BOOL("ENABLE_FAR_FIELD_TRACING");
	class FRadianceCache : SHADER_PERMUTATION_BOOL("DIM_RADIANCE_CACHE");
	class FWriteFinalLightingDim : SHADER_PERMUTATION_BOOL("DIM_WRITE_FINAL_LIGHTING");
	class FPackTraceDataDim : SHADER_PERMUTATION_BOOL("DIM_PACK_TRACE_DATA");
	class FIndirectDispatchDim : SHADER_PERMUTATION_BOOL("DIM_INDIRECT_DISPATCH");
	using FPermutationDomain = TShaderPermutationDomain<FEnableNearFieldTracing, FEnableFarFieldTracing, FRadianceCache, FWriteFinalLightingDim, FIndirectDispatchDim, FPackTraceDataDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenReflectionHardwareRayTracingRGS::FParameters, CommonParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenHardwareRayTracingCS::FInlineParameters, InlineParameters)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		if (!FLumenHardwareRayTracingCS::ShouldCompilePermutation(Parameters))
		{
			return false;
		}

		FPermutationDomain PermutationVector(Parameters.PermutationId);

		bool bEnableNearFieldTracing = PermutationVector.Get<FEnableNearFieldTracing>();
		bool bEnableFarFieldTracing = PermutationVector.Get<FEnableFarFieldTracing>();
		bool bWriteFinalLighting = PermutationVector.Get<FWriteFinalLightingDim>();

		if (PermutationVector.Get<FRadianceCache>() && !bEnableNearFieldTracing)
		{
			return false;
		}

		return (bEnableNearFieldTracing && !bEnableFarFieldTracing) ||
			(!bEnableNearFieldTracing && bEnableFarFieldTracing && bWriteFinalLighting);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FLumenHardwareRayTracingCS::ModifyCompilationEnvironment(Parameters, Lumen::ESurfaceCacheSampling::HighResPages, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("INLINE_RAY_TRACING_THREAD_GROUP_SIZE_X"), ThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("INLINE_RAY_TRACING_THREAD_GROUP_SIZE_Y"), ThreadGroupSizeY);
	}

	// Current inline ray tracing implementation requires 1:1 mapping between thread groups and waves and only supports wave32 mode.
	static constexpr uint32 ThreadGroupSizeX = 32;
	static constexpr uint32 ThreadGroupSizeY = 1;
};

IMPLEMENT_GLOBAL_SHADER(FLumenReflectionHardwareRayTracingCS, "/Engine/Private/Lumen/LumenReflectionHardwareRayTracing.usf", "LumenReflectionHardwareRayTracingCS", SF_Compute);

class FLumenReflectionHardwareRayTracingIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenReflectionHardwareRayTracingIndirectArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenReflectionHardwareRayTracingIndirectArgsCS, FGlobalShader)

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
		OutEnvironment.SetDefine(TEXT("INLINE_RAY_TRACING_THREAD_GROUP_SIZE_X"), FLumenReflectionHardwareRayTracingCS::ThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("INLINE_RAY_TRACING_THREAD_GROUP_SIZE_Y"), FLumenReflectionHardwareRayTracingCS::ThreadGroupSizeY);
	}

	static int32 GetThreadGroupSize1D() { return GetThreadGroupSize2D() * GetThreadGroupSize2D(); }
	static int32 GetThreadGroupSize2D() { return 8; }
};

IMPLEMENT_GLOBAL_SHADER(FLumenReflectionHardwareRayTracingIndirectArgsCS, "/Engine/Private/Lumen/LumenReflectionHardwareRayTracing.usf", "FLumenReflectionHardwareRayTracingIndirectArgsCS", SF_Compute);

bool LumenReflections::UseFarFieldForReflections(const FSceneViewFamily& ViewFamily)
{
	return Lumen::UseFarField(ViewFamily) && CVarLumenReflectionsHardwareRayTracingRetraceFarField.GetValueOnRenderThread();
}

bool LumenReflections::IsHitLightingForceEnabled(const FViewInfo& View)
{
	return Lumen::GetHardwareRayTracingLightingMode(View) != Lumen::EHardwareRayTracingLightingMode::LightingFromSurfaceCache;
}

bool LumenReflections::UseHitLightingForReflections(const FViewInfo& View)
{
	return IsHitLightingForceEnabled(View) || (CVarLumenReflectionsHardwareRayTracingRetraceHitLighting.GetValueOnRenderThread() != 0);
}

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingReflections(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	if (Lumen::UseHardwareRayTracedReflections())
	{
		for (int RadianceCacheDim = 0; RadianceCacheDim < FLumenReflectionHardwareRayTracingRGS::FRadianceCache::PermutationCount; ++RadianceCacheDim)
		{
			FLumenReflectionHardwareRayTracingRGS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FLightingModeDim>(LumenHWRTPipeline::ELightingMode::HitLighting);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FRadianceCache>(RadianceCacheDim != 0);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FEnableNearFieldTracing>(true);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FEnableFarFieldTracing>(Lumen::UseFarField(*View.Family));
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FWriteFinalLightingDim>(true);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FIndirectDispatchDim>(IsHardwareRayTracingReflectionsIndirectDispatch());
			TShaderRef<FLumenReflectionHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingRGS>(PermutationVector);
			OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
		}
	}
}

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingReflectionsDeferredMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
}

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingReflectionsLumenMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	if (Lumen::UseHardwareRayTracedReflections())
	{
		const bool bUseFarFieldForReflections = LumenReflections::UseFarFieldForReflections(*View.Family);
		const bool bIsHitLightingForceEnabled = LumenReflections::IsHitLightingForceEnabled(View);

		// Default
		for (int RadianceCacheDim = 0; RadianceCacheDim < FLumenReflectionHardwareRayTracingRGS::FRadianceCache::PermutationCount; ++RadianceCacheDim)
		{
			FLumenReflectionHardwareRayTracingRGS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FLightingModeDim>(LumenHWRTPipeline::ELightingMode::SurfaceCache);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FRadianceCache>(RadianceCacheDim != 0);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FEnableNearFieldTracing>(true);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FEnableFarFieldTracing>(false);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FWriteFinalLightingDim>(!bIsHitLightingForceEnabled || !bUseFarFieldForReflections);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FIndirectDispatchDim>(IsHardwareRayTracingReflectionsIndirectDispatch());
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FPackTraceDataDim>(bIsHitLightingForceEnabled || bUseFarFieldForReflections);
			TShaderRef<FLumenReflectionHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingRGS>(PermutationVector);
			OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
		}

		// Far-field continuation
		if (bUseFarFieldForReflections)
		{
			FLumenReflectionHardwareRayTracingRGS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FLightingModeDim>(LumenHWRTPipeline::ELightingMode::SurfaceCache);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FRadianceCache>(false);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FEnableNearFieldTracing>(false);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FEnableFarFieldTracing>(true);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FWriteFinalLightingDim>(true);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FIndirectDispatchDim>(IsHardwareRayTracingReflectionsIndirectDispatch());
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FPackTraceDataDim>(LumenReflections::UseHitLightingForReflections(View));
			TShaderRef<FLumenReflectionHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingRGS>(PermutationVector);
			OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
		}
	}
}

void SetLumenHardwareRayTracingReflectionParameters(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	const FLumenCardTracingInputs& TracingInputs,
	const FLumenReflectionTracingParameters& ReflectionTracingParameters,
	const FLumenReflectionTileParameters& ReflectionTileParameters,
	const LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters,
	float MaxTraceDistance,
	bool bApplySkyLight,
	bool bEnableHitLighting,
	bool bEnableFarFieldTracing,
	FRDGBufferRef HardwareRayTracingIndirectArgsBuffer,
	FRDGBufferRef RayAllocatorBuffer,
	FRDGBufferRef TraceTexelDataPackedBuffer,
	FRDGBufferRef TraceDataPackedBuffer,
	FLumenReflectionHardwareRayTracingRGS::FParameters* Parameters
)
{
	uint32 DefaultThreadCount = CVarLumenReflectionsHardwareRayTracingDefaultThreadCount.GetValueOnRenderThread();
	uint32 DefaultGroupCount = CVarLumenReflectionsHardwareRayTracingDefaultGroupCount.GetValueOnRenderThread();

	SetLumenHardwareRayTracingSharedParameters(
		GraphBuilder,
		SceneTextures,
		View,
		TracingInputs,
		&Parameters->SharedParameters
	);
	Parameters->RayAllocator = GraphBuilder.CreateSRV(RayAllocatorBuffer, PF_R32_UINT);
	Parameters->HardwareRayTracingIndirectArgs = HardwareRayTracingIndirectArgsBuffer;
	Parameters->TraceTexelDataPacked = GraphBuilder.CreateSRV(TraceTexelDataPackedBuffer, PF_R32G32_UINT);

	// Constants
	Parameters->ThreadCount = DefaultThreadCount;
	Parameters->GroupCount = DefaultGroupCount;
	Parameters->NearFieldLightingMode = static_cast<int32>(Lumen::GetHardwareRayTracingLightingMode(View));
	Parameters->FarFieldBias = LumenHardwareRayTracing::GetFarFieldBias();
	Parameters->FarFieldMaxTraceDistance = Lumen::GetFarFieldMaxTraceDistance();
	Parameters->FarFieldDitheredStartDistanceFactor = LumenReflections::UseFarFieldForReflections(*View.Family) ? Lumen::GetFarFieldDitheredStartDistanceFactor() : 1.0;
	Parameters->FarFieldReferencePos = (FVector3f)Lumen::GetFarFieldReferencePos();
	Parameters->PullbackBias = Lumen::GetHardwareRayTracingPullbackBias();
	Parameters->MaxTranslucentSkipCount = CVarLumenReflectionsHardwareRayTracingMaxTranslucentSkipCount.GetValueOnRenderThread();
	Parameters->MaxTraversalIterations = LumenHardwareRayTracing::GetMaxTraversalIterations();
	Parameters->ApplySkyLight = bApplySkyLight;

	// Reflection-specific
	Parameters->ReflectionTracingParameters = ReflectionTracingParameters;
	Parameters->ReflectionTileParameters = ReflectionTileParameters;
	Parameters->RadianceCacheParameters = RadianceCacheParameters;

	// Ray continuation buffer
	Parameters->RWTraceDataPacked = GraphBuilder.CreateUAV(TraceDataPackedBuffer);
}

FLumenReflectionHardwareRayTracingCS::FPermutationDomain ToComputePermutationVector(const FLumenReflectionHardwareRayTracingRGS::FPermutationDomain& RGSPermutationVector)
{
	FLumenReflectionHardwareRayTracingCS::FPermutationDomain PermutationVector;

	PermutationVector.Set<FLumenReflectionHardwareRayTracingCS::FRadianceCache>(
		RGSPermutationVector.Get<FLumenReflectionHardwareRayTracingRGS::FRadianceCache>());

	PermutationVector.Set<FLumenReflectionHardwareRayTracingCS::FEnableFarFieldTracing>(
		RGSPermutationVector.Get<FLumenReflectionHardwareRayTracingRGS::FEnableFarFieldTracing>());

	PermutationVector.Set<FLumenReflectionHardwareRayTracingCS::FEnableNearFieldTracing>(
		RGSPermutationVector.Get<FLumenReflectionHardwareRayTracingRGS::FEnableNearFieldTracing>());

	PermutationVector.Set<FLumenReflectionHardwareRayTracingCS::FIndirectDispatchDim>(
		RGSPermutationVector.Get<FLumenReflectionHardwareRayTracingRGS::FIndirectDispatchDim>());

	PermutationVector.Set<FLumenReflectionHardwareRayTracingCS::FWriteFinalLightingDim>(
		RGSPermutationVector.Get<FLumenReflectionHardwareRayTracingRGS::FWriteFinalLightingDim>());

	PermutationVector.Set<FLumenReflectionHardwareRayTracingCS::FPackTraceDataDim>(
		RGSPermutationVector.Get<FLumenReflectionHardwareRayTracingRGS::FPackTraceDataDim>());

	return PermutationVector;
}

void DispatchLumenReflectionHardwareRayTracingIndirectArgs(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGBufferRef HardwareRayTracingIndirectArgsBuffer, FRDGBufferRef RayAllocatorBuffer, bool bInlineRayTracing)
{
	FLumenReflectionHardwareRayTracingIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenReflectionHardwareRayTracingIndirectArgsCS::FParameters>();
	
	PassParameters->RayAllocator = GraphBuilder.CreateSRV(RayAllocatorBuffer, PF_R32_UINT);
	PassParameters->RWHardwareRayTracingIndirectArgs = GraphBuilder.CreateUAV(HardwareRayTracingIndirectArgsBuffer, PF_R32_UINT);

	FLumenReflectionHardwareRayTracingIndirectArgsCS::FPermutationDomain IndirectPermutationVector;
	IndirectPermutationVector.Set<FLumenReflectionHardwareRayTracingIndirectArgsCS::FInlineRaytracing>(bInlineRayTracing);
	TShaderRef<FLumenReflectionHardwareRayTracingIndirectArgsCS> ComputeShader = View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingIndirectArgsCS>(IndirectPermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("ReflectionCompactRaysIndirectArgs"),
		ComputeShader,
		PassParameters,
		FIntVector(1, 1, 1));
}

void DispatchComputeShader(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FScene* Scene,
	const FViewInfo& View,
	const FLumenReflectionTracingParameters& ReflectionTracingParameters,
	const FLumenReflectionTileParameters& ReflectionTileParameters,
	const FLumenCardTracingInputs& TracingInputs,
	const FCompactedReflectionTraceParameters& CompactedTraceParameters,
	const LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters,
	const FLumenReflectionHardwareRayTracingCS::FPermutationDomain& PermutationVector,
	float MaxTraceDistance,
	uint32 RayCount,
	bool bApplySkyLight,
	bool bUseRadianceCache,
	FRDGBufferRef RayAllocatorBuffer,
	FRDGBufferRef TraceTexelDataPackedBuffer,
	FRDGBufferRef TraceDataPackedBuffer
)
{
	FRDGBufferRef HardwareRayTracingIndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Lumen.Reflection.CompactTracingIndirectArgs"));
	DispatchLumenReflectionHardwareRayTracingIndirectArgs(GraphBuilder, View, HardwareRayTracingIndirectArgsBuffer, RayAllocatorBuffer, true);

	uint32 DefaultThreadCount = CVarLumenReflectionsHardwareRayTracingDefaultThreadCount.GetValueOnRenderThread();
	uint32 DefaultGroupCount = CVarLumenReflectionsHardwareRayTracingDefaultGroupCount.GetValueOnRenderThread();

	bool bEnableFarFieldTracing = PermutationVector.Get<FLumenReflectionHardwareRayTracingCS::FEnableFarFieldTracing>();
	bool bEnableHitLighting = false;

	FLumenReflectionHardwareRayTracingCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenReflectionHardwareRayTracingCS::FParameters>();
	SetLumenHardwareRayTracingReflectionParameters(
		GraphBuilder,
		SceneTextures,
		View,
		TracingInputs,
		ReflectionTracingParameters,
		ReflectionTileParameters,
		RadianceCacheParameters,
		MaxTraceDistance,
		bApplySkyLight,
		bEnableHitLighting,
		bEnableFarFieldTracing,
		HardwareRayTracingIndirectArgsBuffer,
		RayAllocatorBuffer,
		TraceTexelDataPackedBuffer,
		TraceDataPackedBuffer,
		&PassParameters->CommonParameters
	);

	PassParameters->InlineParameters.HitGroupData = View.LumenHardwareRayTracingHitDataBufferSRV;

	TShaderRef<FLumenReflectionHardwareRayTracingCS> ComputeShader = View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingCS>(PermutationVector);
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
		FString ResolutionStr = IsHardwareRayTracingReflectionsIndirectDispatch() ? FString::Printf(TEXT("<indirect>")) :
			FString::Printf(TEXT("%ux%u"), DispatchResolution.X, DispatchResolution.Y);

		return ResolutionStr;
	};

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("ReflectionHardwareInlineRayTracing %s %s", *GenerateModeString(), *GenerateResolutionString()),
		PassParameters,
		ERDGPassFlags::Compute,
		[PassParameters, &View, ComputeShader, DispatchResolution](FRHIRayTracingCommandList& RHICmdList)
		{
			FRHIComputeShader* ShaderRHI = ComputeShader.GetComputeShader();
			SetComputePipelineState(RHICmdList, ShaderRHI);
			SetShaderParameters(RHICmdList, ComputeShader, ShaderRHI, *PassParameters);

			if (IsHardwareRayTracingReflectionsIndirectDispatch())
			{
				DispatchIndirectComputeShader(RHICmdList, ComputeShader.GetShader(), PassParameters->CommonParameters.HardwareRayTracingIndirectArgs->GetIndirectRHICallBuffer(), 0);
			}
			else
			{
				const FIntPoint GroupSize(FLumenReflectionHardwareRayTracingCS::ThreadGroupSizeX, FLumenReflectionHardwareRayTracingCS::ThreadGroupSizeY);
				const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(DispatchResolution, GroupSize);
				DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), GroupCount.X, GroupCount.Y, 1);
			}

			UnsetShaderUAVs(RHICmdList, ComputeShader, ShaderRHI);
		}
	);
}

void DispatchRayGenShader(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FScene* Scene,
	const FViewInfo& View,
	const FLumenReflectionTracingParameters& ReflectionTracingParameters,
	const FLumenReflectionTileParameters& ReflectionTileParameters,
	const FLumenCardTracingInputs& TracingInputs,
	const FCompactedReflectionTraceParameters& CompactedTraceParameters,
	const LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters,
	const FLumenReflectionHardwareRayTracingRGS::FPermutationDomain& PermutationVector,
	float MaxTraceDistance,
	uint32 RayCount,
	bool bApplySkyLight,
	bool bUseRadianceCache,
	FRDGBufferRef RayAllocatorBuffer,
	FRDGBufferRef TraceTexelDataPackedBuffer,
	FRDGBufferRef TraceDataPackedBuffer
)
{
	FRDGBufferRef HardwareRayTracingIndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Lumen.Reflection.CompactTracingIndirectArgs"));
	DispatchLumenReflectionHardwareRayTracingIndirectArgs(GraphBuilder, View, HardwareRayTracingIndirectArgsBuffer, RayAllocatorBuffer, false);

	uint32 DefaultThreadCount = CVarLumenReflectionsHardwareRayTracingDefaultThreadCount.GetValueOnRenderThread();
	uint32 DefaultGroupCount = CVarLumenReflectionsHardwareRayTracingDefaultGroupCount.GetValueOnRenderThread();

	bool bEnableHitLighting = PermutationVector.Get<FLumenReflectionHardwareRayTracingRGS::FLightingModeDim>() == LumenHWRTPipeline::ELightingMode::HitLighting;
	bool bEnableFarFieldTracing = PermutationVector.Get<FLumenReflectionHardwareRayTracingRGS::FEnableFarFieldTracing>();

	FLumenReflectionHardwareRayTracingRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenReflectionHardwareRayTracingRGS::FParameters>();
	SetLumenHardwareRayTracingReflectionParameters(
		GraphBuilder,
		SceneTextures,
		View,
		TracingInputs,
		ReflectionTracingParameters,
		ReflectionTileParameters,
		RadianceCacheParameters,
		MaxTraceDistance,
		bApplySkyLight,
		bEnableHitLighting,
		bEnableFarFieldTracing,
		HardwareRayTracingIndirectArgsBuffer,
		RayAllocatorBuffer,
		TraceTexelDataPackedBuffer,
		TraceDataPackedBuffer,
		PassParameters
	);

	TShaderRef<FLumenReflectionHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingRGS>(PermutationVector);
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
		FString ResolutionStr = IsHardwareRayTracingReflectionsIndirectDispatch() ? FString::Printf(TEXT("<indirect>")) :
			FString::Printf(TEXT("%ux%u"), DispatchResolution.X, DispatchResolution.Y);

		return ResolutionStr;
	};

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("ReflectionHardwareRayTracing %s %s", *GenerateModeString(), *GenerateResolutionString()),
		PassParameters,
		ERDGPassFlags::Compute,
		[PassParameters, &View, RayGenerationShader, bEnableHitLighting, DispatchResolution](FRHIRayTracingCommandList& RHICmdList)
		{
			FRayTracingShaderBindingsWriter GlobalResources;
			SetShaderParameters(GlobalResources, RayGenerationShader, *PassParameters);

			FRHIRayTracingScene* RayTracingSceneRHI = View.GetRayTracingSceneChecked();
			FRayTracingPipelineState* Pipeline = (bEnableHitLighting) ? View.RayTracingMaterialPipeline : View.LumenHardwareRayTracingMaterialPipeline;

			if (IsHardwareRayTracingReflectionsIndirectDispatch())
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

void RenderLumenHardwareRayTracingReflections(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FScene* Scene,
	const FViewInfo& View,
	const FLumenReflectionTracingParameters& ReflectionTracingParameters,
	const FLumenReflectionTileParameters& ReflectionTileParameters,
	const FLumenCardTracingInputs& TracingInputs,
	const FCompactedReflectionTraceParameters& CompactedTraceParameters,
	float MaxVoxelTraceDistance,
	bool bUseRadianceCache,
	const LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters
)
{
#if RHI_RAYTRACING
	FIntPoint BufferSize = ReflectionTracingParameters.ReflectionTracingBufferSize;
	int32 RayCount = BufferSize.X * BufferSize.Y;

	FRDGBufferRef TraceDataPackedBufferCached = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(LumenHWRTPipeline::FTraceDataPacked), RayCount), TEXT("Lumen.Reflections.TraceDataPacked"));
	FRDGBufferRef RayAllocatorBufferCached = CompactedTraceParameters.CompactedTraceTexelAllocator->Desc.Buffer;
	FRDGBufferRef TraceTexelDataPackedBufferCached = CompactedTraceParameters.CompactedTraceTexelData->Desc.Buffer;

	const bool bIsForceHitLighting = LumenReflections::IsHitLightingForceEnabled(View);
	const bool bInlineRayTracing = Lumen::UseHardwareInlineRayTracing();
	const bool bUseFarFieldForReflections = LumenReflections::UseFarFieldForReflections(*View.Family);

	// Default tracing of near-field, extract surface cache and material-id
	{
		bool bApplySkyLight = !bUseFarFieldForReflections;

		FLumenReflectionHardwareRayTracingRGS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FLightingModeDim>(LumenHWRTPipeline::ELightingMode::SurfaceCache);
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FRadianceCache>(bUseRadianceCache);
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FEnableNearFieldTracing>(true);
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FEnableFarFieldTracing>(false);
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FWriteFinalLightingDim>(!bIsForceHitLighting || !bUseFarFieldForReflections);
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FIndirectDispatchDim>(IsHardwareRayTracingReflectionsIndirectDispatch());
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FPackTraceDataDim>(bIsForceHitLighting || bUseFarFieldForReflections);

		if (bInlineRayTracing)
		{
			check(!bIsForceHitLighting);

			DispatchComputeShader(GraphBuilder, SceneTextures, Scene, View, ReflectionTracingParameters, ReflectionTileParameters, TracingInputs, CompactedTraceParameters, RadianceCacheParameters,
				ToComputePermutationVector(PermutationVector), MaxVoxelTraceDistance, RayCount, bApplySkyLight, bUseRadianceCache,
				RayAllocatorBufferCached, TraceTexelDataPackedBufferCached, TraceDataPackedBufferCached);
		}
		else
		{
			DispatchRayGenShader(GraphBuilder, SceneTextures, Scene, View, ReflectionTracingParameters, ReflectionTileParameters, TracingInputs, CompactedTraceParameters, RadianceCacheParameters,
				PermutationVector, MaxVoxelTraceDistance, RayCount, bApplySkyLight, bUseRadianceCache,
				RayAllocatorBufferCached, TraceTexelDataPackedBufferCached, TraceDataPackedBufferCached);
		}
	}

	// Default tracing of far-field, extract material-id
	FRDGBufferRef FarFieldRayAllocatorBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("Lumen.Reflection.FarFieldRayAllocator"));
	//FRDGBufferRef FarFieldTraceTexelDataPackedBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32) * 2, RayCount), TEXT("Lumen.Reflection.FarFieldTraceTexelDataPacked"));
	FRDGBufferRef FarFieldTraceDataPackedBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(LumenHWRTPipeline::FTraceDataPacked), RayCount), TEXT("Lumen.Reflection.FarFieldTraceDataPacked"));
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(FarFieldRayAllocatorBuffer, PF_R32_UINT), 0);
	if (bUseFarFieldForReflections)
	{
		LumenHWRTCompactRays(GraphBuilder, Scene, View, RayCount, LumenHWRTPipeline::ECompactMode::FarFieldRetrace,
			RayAllocatorBufferCached, TraceDataPackedBufferCached,
			FarFieldRayAllocatorBuffer, FarFieldTraceDataPackedBuffer);

		bool bApplySkyLight = true;

		FLumenReflectionHardwareRayTracingRGS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FLightingModeDim>(LumenHWRTPipeline::ELightingMode::SurfaceCache);
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FRadianceCache>(false);
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FEnableNearFieldTracing>(false);
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FEnableFarFieldTracing>(true);
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FWriteFinalLightingDim>(true);
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FIndirectDispatchDim>(IsHardwareRayTracingReflectionsIndirectDispatch());
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FPackTraceDataDim>(LumenReflections::UseHitLightingForReflections(View));

		// Trace continuation rays
		if (bInlineRayTracing)
		{
			DispatchComputeShader(GraphBuilder, SceneTextures, Scene, View, ReflectionTracingParameters, ReflectionTileParameters, TracingInputs, CompactedTraceParameters, RadianceCacheParameters,
				ToComputePermutationVector(PermutationVector), MaxVoxelTraceDistance, RayCount, bApplySkyLight, bUseRadianceCache,
				FarFieldRayAllocatorBuffer, TraceTexelDataPackedBufferCached, FarFieldTraceDataPackedBuffer);
		}
		else
		{
			DispatchRayGenShader(GraphBuilder, SceneTextures, Scene, View, ReflectionTracingParameters, ReflectionTileParameters, TracingInputs, CompactedTraceParameters, RadianceCacheParameters,
				PermutationVector, MaxVoxelTraceDistance, RayCount, bApplySkyLight, bUseRadianceCache,
				FarFieldRayAllocatorBuffer, TraceTexelDataPackedBufferCached, FarFieldTraceDataPackedBuffer);
		}
	}

	// Re-trace for hit-lighting
	if (LumenReflections::UseHitLightingForReflections(View))
	{
		FRDGBufferRef RayAllocatorBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("Lumen.Reflection.CompactedRayAllocator"));
		//FRDGBufferRef TraceTexelDataPackedBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32) * 2, RayCount), TEXT("Lumen.Reflection.BucketedTexelTraceDataPackedBuffer"));
		FRDGBufferRef TraceDataPackedBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(LumenHWRTPipeline::FTraceDataPacked), RayCount), TEXT("Lumen.Reflection.CompactedTraceDataPacked"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(RayAllocatorBuffer, PF_R32_UINT), 0);

		LumenHWRTPipeline::ECompactMode CompactMode = bIsForceHitLighting ?
			LumenHWRTPipeline::ECompactMode::ForceHitLighting : LumenHWRTPipeline::ECompactMode::HitLightingRetrace;
		LumenHWRTCompactRays(GraphBuilder, Scene, View, RayCount, CompactMode,
			RayAllocatorBufferCached, TraceDataPackedBufferCached,
			RayAllocatorBuffer, TraceDataPackedBuffer);

		// Append far-field rays
		if (bUseFarFieldForReflections)
		{
			LumenHWRTCompactRays(GraphBuilder, Scene, View, RayCount, LumenHWRTPipeline::ECompactMode::AppendRays,
				FarFieldRayAllocatorBuffer, FarFieldTraceDataPackedBuffer,
				RayAllocatorBuffer, TraceDataPackedBuffer);
		}

		// Sort rays by material
		if (CVarLumenReflectionsHardwareRayTracingBucketMaterials.GetValueOnRenderThread())
		{
			LumenHWRTBucketRaysByMaterialID(GraphBuilder, Scene, View, RayCount, RayAllocatorBuffer, TraceDataPackedBuffer);
		}

		// Trace with hit-lighting
		{
			bool bApplySkyLight = true;

			FLumenReflectionHardwareRayTracingRGS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FLightingModeDim>(LumenHWRTPipeline::ELightingMode::HitLighting);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FRadianceCache>(bUseRadianceCache);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FEnableNearFieldTracing>(true);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FEnableFarFieldTracing>(bUseFarFieldForReflections);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FWriteFinalLightingDim>(true);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FIndirectDispatchDim>(IsHardwareRayTracingReflectionsIndirectDispatch());
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FPackTraceDataDim>(false);

			DispatchRayGenShader(GraphBuilder, SceneTextures, Scene, View, ReflectionTracingParameters, ReflectionTileParameters, TracingInputs, CompactedTraceParameters, RadianceCacheParameters,
				PermutationVector, MaxVoxelTraceDistance, RayCount, bApplySkyLight, bUseRadianceCache,
				RayAllocatorBuffer, TraceTexelDataPackedBufferCached, TraceDataPackedBuffer);
		}
	}
#else
	unimplemented();
#endif
}
