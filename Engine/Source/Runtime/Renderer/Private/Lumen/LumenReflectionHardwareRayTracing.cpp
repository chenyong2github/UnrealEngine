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

static TAutoConsoleVariable<int32> CVarLumenReflectionsHardwareRayTracingLightingMode(
	TEXT("r.Lumen.Reflections.HardwareRayTracing.LightingMode"),
	0,
	TEXT("Determines the lighting mode (Default = 0)\n")
	TEXT("0: interpolate final lighting from the surface cache\n")
	TEXT("1: evaluate material, and interpolate irradiance and indirect irradiance from the surface cache\n")
	TEXT("2: evaluate material and direct lighting, and interpolate indirect irradiance from the surface cache\n")
	TEXT("3: evaluate material, direct lighting, and unshadowed skylighting at the hit point"),
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

static TAutoConsoleVariable<int32> CVarLumenReflectionsHardwareRayTracingCompact(
	TEXT("r.Lumen.Reflections.HardwareRayTracing.Compact"),
	1,
	TEXT("Determines whether a second trace will be compacted before traversal (Default = 1)"),
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
	TEXT("Determines whether a second trace will be fired for hit-lighting for far-field contribution (Default = 1)"),
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
			&& (CVarLumenReflectionsHardwareRayTracing.GetValueOnRenderThread() != 0);
#else
		return false;
#endif
	}

	EHardwareRayTracingLightingMode GetReflectionsHardwareRayTracingLightingMode(const FViewInfo& View)
	{
#if RHI_RAYTRACING
		// Piecewise mapping for relative bias, interpolates through the following (LumenReflectionQuality, BiasValue):
		// (0.25, -2)
		// (0.5 , -1)
		// (1.0 ,  0)
		// (2.0 ,  1)
		// (4.0 ,  2)
		auto LinearMapping = [](float x) {
			return x / 2.0f;
		};
		auto SublinearMapping = [](float x) {
			return ((-8.0f / 3.0f) * x * x) + (6.0f * x) - (10.0f / 3.0f);
		};

		// LumenReflectionQuality acts as a biasing value to the LightingMode
		const float LumenReflectionQuality = View.FinalPostProcessSettings.LumenReflectionQuality;
		const int32 ReflectionQualityLightingModeBias = (LumenReflectionQuality > 1.0f) ?
			FMath::Clamp<int32>(FMath::FloorToInt(LinearMapping(LumenReflectionQuality)), 0, 2) :
			FMath::Clamp<int32>(FMath::FloorToInt(SublinearMapping(LumenReflectionQuality)), -2, 0);

		const int32 LightingModeCVar = CVarLumenReflectionsHardwareRayTracingLightingMode.GetValueOnRenderThread();
		int32 LightingModeInt = FMath::Clamp<int32>(LightingModeCVar + ReflectionQualityLightingModeBias, 0, 2);

		if (LightingModeCVar == 3)
		{
			// Only an explicit selection can enable Lighting Mode 3, which has unshadowed skylight and is not suitable for general use
			LightingModeInt = 3;
		}

		Lumen::EHardwareRayTracingLightingMode LightingMode = static_cast<Lumen::EHardwareRayTracingLightingMode>(LightingModeInt);
		return LightingMode;
#else
		return EHardwareRayTracingLightingMode::LightingFromSurfaceCache;
#endif
	}

	const TCHAR* GetRayTracedLightingModeName(EHardwareRayTracingLightingMode LightingMode)
	{
		switch (LightingMode)
		{
		case EHardwareRayTracingLightingMode::LightingFromSurfaceCache:
			return TEXT("LightingFromSurfaceCache");
		case EHardwareRayTracingLightingMode::EvaluateMaterial:
			return TEXT("EvaluateMaterial");
		case EHardwareRayTracingLightingMode::EvaluateMaterialAndDirectLighting:
			return TEXT("EvaluateMaterialAndDirectLighting");
		case EHardwareRayTracingLightingMode::EvaluateMaterialAndDirectLightingAndSkyLighting:
			return TEXT("EvaluateMaterialAndDirectLightingAndSkyLighting");
		default:
			checkf(0, TEXT("Unhandled EHardwareRayTracingLightingMode"));
		}
		return nullptr;
	}

#if RHI_RAYTRACING
	EHardwareRayTracingLightingMode GetNearFieldLightingMode(const FViewInfo& View)
	{
		EHardwareRayTracingLightingMode LightingMode = GetReflectionsHardwareRayTracingLightingMode(View);
		if (LightingMode == EHardwareRayTracingLightingMode::LightingFromSurfaceCache)
		{
			LightingMode = EHardwareRayTracingLightingMode::EvaluateMaterialAndDirectLighting;
		}
		return LightingMode;
	}
#endif // RHI_RAY_TRACING
} // namespace Lumen

#if RHI_RAYTRACING

namespace LumenReflection
{
	// Struct definitions much match those in LumenVisualizeHardwareRayTracing.usf

	enum class ELightingMode
	{
		// Permutations for tracing modes
		SurfaceCache,
		HitLighting,
		MAX
	};

	enum class ECompactMode
	{
		// Permutations for compaction modes
		HitLightingRetrace,
		FarFieldRetrace,
		ForceHitLighting,
		AppendRays,

		MAX
	};

	struct FTraceDataPacked
	{
		uint32 PackedData[2];
	};

} // namespace LumenReflection

class FLumenReflectionHardwareRayTracingIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenReflectionHardwareRayTracingIndirectArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenReflectionHardwareRayTracingIndirectArgsCS, FGlobalShader)

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
	}

	static int32 GetThreadGroupSize1D() { return GetThreadGroupSize2D() * GetThreadGroupSize2D(); }
	static int32 GetThreadGroupSize2D() { return 8; }
};

IMPLEMENT_GLOBAL_SHADER(FLumenReflectionHardwareRayTracingIndirectArgsCS, "/Engine/Private/Lumen/LumenReflectionHardwareRayTracing.usf", "FLumenReflectionHardwareRayTracingIndirectArgsCS", SF_Compute);

bool IsHardwareRayTracingReflectionsIndirectDispatch()
{
	return GRHISupportsRayTracingDispatchIndirect && (CVarLumenReflectionsHardwareRayTracingIndirect.GetValueOnRenderThread() == 1);
}

class FLumenReflectionHardwareRayTracingRGS : public FLumenHardwareRayTracingRGS
{
	DECLARE_GLOBAL_SHADER(FLumenReflectionHardwareRayTracingRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FLumenReflectionHardwareRayTracingRGS, FLumenHardwareRayTracingRGS)

	class FLightingModeDim : SHADER_PERMUTATION_ENUM_CLASS("DIM_LIGHTING_MODE", LumenReflection::ELightingMode);
	class FEnableNearFieldTracing : SHADER_PERMUTATION_BOOL("ENABLE_NEAR_FIELD_TRACING");
	class FEnableFarFieldTracing : SHADER_PERMUTATION_BOOL("ENABLE_FAR_FIELD_TRACING");
	class FRadianceCache : SHADER_PERMUTATION_BOOL("DIM_RADIANCE_CACHE");
	class FWriteFinalLightingDim : SHADER_PERMUTATION_BOOL("DIM_WRITE_FINAL_LIGHTING");
	class FIndirectDispatchDim : SHADER_PERMUTATION_BOOL("DIM_INDIRECT_DISPATCH");
	using FPermutationDomain = TShaderPermutationDomain<FLightingModeDim, FEnableNearFieldTracing, FEnableFarFieldTracing, FRadianceCache, FWriteFinalLightingDim, FIndirectDispatchDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenHardwareRayTracingRGS::FSharedParameters, SharedParameters)
		RDG_BUFFER_ACCESS(HardwareRayTracingIndirectArgs, ERHIAccess::IndirectArgs | ERHIAccess::SRVCompute)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, RayAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint2>, TraceTexelDataPacked)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<LumenReflection::FTraceDataPacked>, TraceDataPacked)

		// Constants
		SHADER_PARAMETER(int, ThreadCount)
		SHADER_PARAMETER(int, GroupCount)
		SHADER_PARAMETER(int, NearFieldLightingMode)

		SHADER_PARAMETER(float, MaxTraceDistance)
		SHADER_PARAMETER(float, FarFieldMaxTraceDistance)
		SHADER_PARAMETER(float, PullbackBias)
		SHADER_PARAMETER(int, MaxTranslucentSkipCount)
		SHADER_PARAMETER(int, ApplySkyLight)
		SHADER_PARAMETER(FVector3f, FarFieldReferencePos)

		// Reflection-specific includes (includes output targets)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenReflectionTracingParameters, ReflectionTracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenReflectionTileParameters, ReflectionTileParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInterpolationParameters, RadianceCacheParameters)

		// Ray continuation buffer
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<LumenReflection::FTraceDataPacked>, RWTraceDataPacked)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		if (!FLumenHardwareRayTracingRGS::ShouldCompilePermutation(Parameters))
		{
			return false;
		}

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		LumenReflection::ELightingMode LightingMode = PermutationVector.Get<FLightingModeDim>();
		if (LightingMode == LumenReflection::ELightingMode::SurfaceCache)
		{
			bool bEnableNearFieldTracing = PermutationVector.Get<FEnableNearFieldTracing>();
			bool bEnableFarFieldTracing = PermutationVector.Get<FEnableFarFieldTracing>();
			bool bWriteFinalLighting = PermutationVector.Get<FWriteFinalLightingDim>();

			return (bEnableNearFieldTracing && !bEnableFarFieldTracing) || 
				(!bEnableNearFieldTracing && bEnableFarFieldTracing && bWriteFinalLighting);
		}
		else if (LightingMode == LumenReflection::ELightingMode::HitLighting)
		{
			bool bEnableNearFieldTracing = PermutationVector.Get<FEnableNearFieldTracing>();
			bool bFWriteFinalLighting = PermutationVector.Get<FWriteFinalLightingDim>();

			return bEnableNearFieldTracing && bFWriteFinalLighting;
		}

		return false;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FLumenHardwareRayTracingRGS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_DISPATCH_1D"), 1);
		OutEnvironment.SetDefine(TEXT("SURFACE_CACHE_FEEDBACK"), 1);

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FLightingModeDim>() == LumenReflection::ELightingMode::SurfaceCache)
		{
			OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_LIGHTWEIGHT_CLOSEST_HIT_SHADER"), 1);
		}
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenReflectionHardwareRayTracingRGS, "/Engine/Private/Lumen/LumenReflectionHardwareRayTracing.usf", "LumenReflectionHardwareRayTracingRGS", SF_RayGen);


class FLumenReflectionCompactRaysIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenReflectionCompactRaysIndirectArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenReflectionCompactRaysIndirectArgsCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, RayAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCompactRaysIndirectArgs)
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
	}

	static int32 GetThreadGroupSize1D() { return GetThreadGroupSize2D() * GetThreadGroupSize2D(); }
	static int32 GetThreadGroupSize2D() { return 8; }
};

IMPLEMENT_GLOBAL_SHADER(FLumenReflectionCompactRaysIndirectArgsCS, "/Engine/Private/Lumen/LumenReflectionHardwareRayTracing.usf", "FLumenReflectionCompactRaysIndirectArgsCS", SF_Compute);

class FLumenReflectionCompactRaysCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenReflectionCompactRaysCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenReflectionCompactRaysCS, FGlobalShader);

	class FCompactModeDim : SHADER_PERMUTATION_ENUM_CLASS("DIM_COMPACT_MODE", LumenReflection::ECompactMode);
	using FPermutationDomain = TShaderPermutationDomain<FCompactModeDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// Input
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, RayAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint2>, TraceTexelDataPacked)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<LumenReflection::FTraceDataPacked>, TraceDataPacked)

		// Output
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWRayAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint2>, RWTraceTexelDataPacked)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<LumenReflection::FTraceDataPacked>, RWTraceDataPacked)

		// Indirect args
		RDG_BUFFER_ACCESS(CompactRaysIndirectArgs, ERHIAccess::IndirectArgs)
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
	}

	static int32 GetThreadGroupSize1D() { return GetThreadGroupSize2D() * GetThreadGroupSize2D(); }
	static int32 GetThreadGroupSize2D() { return 8; }
};

IMPLEMENT_GLOBAL_SHADER(FLumenReflectionCompactRaysCS, "/Engine/Private/Lumen/LumenReflectionHardwareRayTracing.usf", "FLumenReflectionCompactRaysCS", SF_Compute);


class FLumenReflectionBucketRaysByMaterialIdIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenReflectionBucketRaysByMaterialIdIndirectArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenReflectionBucketRaysByMaterialIdIndirectArgsCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, RayAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWBucketRaysByMaterialIdIndirectArgs)
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
	}

	static int32 GetThreadGroupSize1D() { return GetThreadGroupSize2D() * GetThreadGroupSize2D(); }
	static int32 GetThreadGroupSize2D() { return 16; }
};

IMPLEMENT_GLOBAL_SHADER(FLumenReflectionBucketRaysByMaterialIdIndirectArgsCS, "/Engine/Private/Lumen/LumenReflectionHardwareRayTracing.usf", "FLumenReflectionBucketRaysByMaterialIdIndirectArgsCS", SF_Compute);

class FLumenReflectionBucketRaysByMaterialIdCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenReflectionBucketRaysByMaterialIdCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenReflectionBucketRaysByMaterialIdCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// Input
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, RayAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint2>, TraceTexelDataPacked)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<LumenReflection::FTraceDataPacked>, TraceDataPacked)

		SHADER_PARAMETER(int, MaxRayAllocationCount)

		// Output
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint2>, RWTraceTexelDataPacked)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<LumenReflection::FTraceDataPacked>, RWTraceDataPacked)

		// Indirect args
		RDG_BUFFER_ACCESS(BucketRaysByMaterialIdIndirectArgs, ERHIAccess::IndirectArgs)
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
	}

	static int32 GetThreadGroupSize1D() { return GetThreadGroupSize2D() * GetThreadGroupSize2D(); }
	static int32 GetThreadGroupSize2D() { return 16; }
};

IMPLEMENT_GLOBAL_SHADER(FLumenReflectionBucketRaysByMaterialIdCS, "/Engine/Private/Lumen/LumenReflectionHardwareRayTracing.usf", "FLumenReflectionBucketRaysByMaterialIdCS", SF_Compute);

bool UseFarFieldForReflections()
{
	return Lumen::UseFarField() && CVarLumenReflectionsHardwareRayTracingRetraceFarField.GetValueOnRenderThread();
}

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingReflections(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	if (Lumen::UseHardwareRayTracedReflections())
	{
		{
			FLumenReflectionHardwareRayTracingRGS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FLightingModeDim>(LumenReflection::ELightingMode::HitLighting);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FRadianceCache>(true);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FEnableNearFieldTracing>(true);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FEnableFarFieldTracing>(Lumen::UseFarField());
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FWriteFinalLightingDim>(true);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FIndirectDispatchDim>(IsHardwareRayTracingReflectionsIndirectDispatch());
			TShaderRef<FLumenReflectionHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingRGS>(PermutationVector);
			OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
		}

		{
			FLumenReflectionHardwareRayTracingRGS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FLightingModeDim>(LumenReflection::ELightingMode::HitLighting);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FRadianceCache>(false);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FEnableNearFieldTracing>(true);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FEnableFarFieldTracing>(Lumen::UseFarField());
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
		const bool bIsForceHitLighting = CVarLumenReflectionsHardwareRayTracingLightingMode.GetValueOnRenderThread() != 0;
		// Default
		{
			FLumenReflectionHardwareRayTracingRGS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FLightingModeDim>(LumenReflection::ELightingMode::SurfaceCache);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FRadianceCache>(true);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FEnableNearFieldTracing>(true);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FEnableFarFieldTracing>(false);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FWriteFinalLightingDim>(!bIsForceHitLighting || !UseFarFieldForReflections());
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FIndirectDispatchDim>(IsHardwareRayTracingReflectionsIndirectDispatch());
			TShaderRef<FLumenReflectionHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingRGS>(PermutationVector);
			OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
		}

		// Far-field continuation
		if (UseFarFieldForReflections())
		{
			FLumenReflectionHardwareRayTracingRGS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FLightingModeDim>(LumenReflection::ELightingMode::SurfaceCache);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FRadianceCache>(true);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FEnableNearFieldTracing>(false);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FEnableFarFieldTracing>(true);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FWriteFinalLightingDim>(true);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FIndirectDispatchDim>(IsHardwareRayTracingReflectionsIndirectDispatch());
			TShaderRef<FLumenReflectionHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingRGS>(PermutationVector);
			OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
		}

		// Default
		{
			FLumenReflectionHardwareRayTracingRGS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FLightingModeDim>(LumenReflection::ELightingMode::SurfaceCache);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FRadianceCache>(false);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FEnableNearFieldTracing>(true);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FEnableFarFieldTracing>(false);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FWriteFinalLightingDim>(!bIsForceHitLighting || !UseFarFieldForReflections());
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FIndirectDispatchDim>(IsHardwareRayTracingReflectionsIndirectDispatch());
			TShaderRef<FLumenReflectionHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingRGS>(PermutationVector);
			OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
		}

		// Far-field continuation
		if (UseFarFieldForReflections())
		{
			FLumenReflectionHardwareRayTracingRGS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FLightingModeDim>(LumenReflection::ELightingMode::SurfaceCache);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FRadianceCache>(false);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FEnableNearFieldTracing>(false);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FEnableFarFieldTracing>(true);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FWriteFinalLightingDim>(true);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FIndirectDispatchDim>(IsHardwareRayTracingReflectionsIndirectDispatch());
			TShaderRef<FLumenReflectionHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingRGS>(PermutationVector);
			OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
		}
	}
}
#endif

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

	FRDGBufferRef TraceDataPackedBufferCached = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(LumenReflection::FTraceDataPacked), RayCount), TEXT("Lumen.Reflections.TraceDataPacked"));
	FRDGBufferRef RayAllocatorBufferCached = CompactedTraceParameters.CompactedTraceTexelAllocator->Desc.Buffer;
	FRDGBufferRef TraceTexelDataPackedBufferCached = CompactedTraceParameters.CompactedTraceTexelData->Desc.Buffer;

	const bool bIsForceHitLighting = CVarLumenReflectionsHardwareRayTracingLightingMode.GetValueOnRenderThread() != 0;

	// Default tracing of near-field, extract surface cache and material-id
	{
		FRDGBufferRef HardwareRayTracingIndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Lumen.Reflection.CompactTracingIndirectArgs"));
		{
			FLumenReflectionHardwareRayTracingIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenReflectionHardwareRayTracingIndirectArgsCS::FParameters>();
			{
				PassParameters->RayAllocator = GraphBuilder.CreateSRV(RayAllocatorBufferCached, PF_R32_UINT);
				PassParameters->RWHardwareRayTracingIndirectArgs = GraphBuilder.CreateUAV(HardwareRayTracingIndirectArgsBuffer, PF_R32_UINT);
			}

			TShaderRef<FLumenReflectionHardwareRayTracingIndirectArgsCS> ComputeShader = View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingIndirectArgsCS>();
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ReflectionCompactRaysIndirectArgs"),
				ComputeShader,
				PassParameters,
				FIntVector(1, 1, 1));
		}

		uint32 DefaultThreadCount = CVarLumenReflectionsHardwareRayTracingDefaultThreadCount.GetValueOnRenderThread();
		uint32 DefaultGroupCount = CVarLumenReflectionsHardwareRayTracingDefaultGroupCount.GetValueOnRenderThread();

		FLumenReflectionHardwareRayTracingRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenReflectionHardwareRayTracingRGS::FParameters>();
		{
			SetLumenHardwareRayTracingSharedParameters(
				GraphBuilder,
				SceneTextures,
				View,
				TracingInputs,
				&PassParameters->SharedParameters
			);
			PassParameters->RayAllocator = GraphBuilder.CreateSRV(RayAllocatorBufferCached, PF_R32_UINT);
			PassParameters->HardwareRayTracingIndirectArgs = HardwareRayTracingIndirectArgsBuffer;
			PassParameters->TraceTexelDataPacked = GraphBuilder.CreateSRV(TraceTexelDataPackedBufferCached, PF_R32G32_UINT);

			// Constants
			PassParameters->ThreadCount = DefaultThreadCount;
			PassParameters->GroupCount = DefaultGroupCount;
			PassParameters->NearFieldLightingMode = static_cast<int>(Lumen::GetNearFieldLightingMode(View));
			PassParameters->MaxTraceDistance = MaxVoxelTraceDistance;
			PassParameters->FarFieldMaxTraceDistance = Lumen::GetFarFieldMaxTraceDistance();
			PassParameters->FarFieldReferencePos = Lumen::GetFarFieldReferencePos();
			PassParameters->PullbackBias = Lumen::GetHardwareRayTracingPullbackBias();
			PassParameters->MaxTranslucentSkipCount = CVarLumenReflectionsHardwareRayTracingMaxTranslucentSkipCount.GetValueOnRenderThread();
			PassParameters->ApplySkyLight = !UseFarFieldForReflections();

			// Reflection-specific
			PassParameters->ReflectionTracingParameters = ReflectionTracingParameters;
			PassParameters->ReflectionTileParameters = ReflectionTileParameters;
			PassParameters->RadianceCacheParameters = RadianceCacheParameters;

			// Ray continuation buffer
			PassParameters->RWTraceDataPacked = GraphBuilder.CreateUAV(TraceDataPackedBufferCached);
		}

		FLumenReflectionHardwareRayTracingRGS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FLightingModeDim>(LumenReflection::ELightingMode::SurfaceCache);
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FRadianceCache>(bUseRadianceCache);
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FEnableNearFieldTracing>(true);
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FEnableFarFieldTracing>(false);
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FWriteFinalLightingDim>(!bIsForceHitLighting || !UseFarFieldForReflections());
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FIndirectDispatchDim>(IsHardwareRayTracingReflectionsIndirectDispatch());
		TShaderRef<FLumenReflectionHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingRGS>(PermutationVector);

		FIntPoint DispatchResolution = FIntPoint(DefaultThreadCount, DefaultGroupCount);
		FString Resolution = FString::Printf(TEXT("%ux%u"), DispatchResolution.X, DispatchResolution.Y);
		if (IsHardwareRayTracingReflectionsIndirectDispatch())
		{
			Resolution = FString::Printf(TEXT("<indirect>"));
		}
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("ReflectionHardwareRayTracing %s", *Resolution),
			PassParameters,
			ERDGPassFlags::Compute,
			[PassParameters, &View, RayGenerationShader, DispatchResolution](FRHIRayTracingCommandList& RHICmdList)
			{
				FRayTracingShaderBindingsWriter GlobalResources;
				SetShaderParameters(GlobalResources, RayGenerationShader, *PassParameters);

				FRHIRayTracingScene* RayTracingSceneRHI = View.GetRayTracingSceneChecked();
				FRayTracingPipelineState* Pipeline = View.LumenHardwareRayTracingMaterialPipeline;

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

	// Default tracing of far-field, extract material-id
	FRDGBufferRef FarFieldRayAllocatorBuffer = RayAllocatorBufferCached;
	FRDGBufferRef FarFieldTraceTexelDataPackedBuffer = TraceTexelDataPackedBufferCached;
	FRDGBufferRef FarFieldTraceDataPackedBuffer = TraceDataPackedBufferCached;
	if (UseFarFieldForReflections())
	{
		// Compact rays which need far-field continuation
		if (CVarLumenReflectionsHardwareRayTracingCompact.GetValueOnRenderThread())
		{
			FRDGBufferRef CompactRaysIndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Lumen.Reflection.CompactTracingIndirectArgs"));
			{
				FLumenReflectionCompactRaysIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenReflectionCompactRaysIndirectArgsCS::FParameters>();
				{
					PassParameters->RayAllocator = GraphBuilder.CreateSRV(FarFieldRayAllocatorBuffer, PF_R32_UINT);
					PassParameters->RWCompactRaysIndirectArgs = GraphBuilder.CreateUAV(CompactRaysIndirectArgsBuffer, PF_R32_UINT);
				}

				TShaderRef<FLumenReflectionCompactRaysIndirectArgsCS> ComputeShader = View.ShaderMap->GetShader<FLumenReflectionCompactRaysIndirectArgsCS>();
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("ReflectionCompactRaysIndirectArgs"),
					ComputeShader,
					PassParameters,
					FIntVector(1, 1, 1));
			}

			FRDGBufferRef CompactedRayAllocatorBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("Lumen.Reflection.CompactedRayAllocator"));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(CompactedRayAllocatorBuffer, PF_R32_UINT), 0);

			FRDGBufferRef CompactedTexelTraceDataPackedBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32) * 2, RayCount), TEXT("Lumen.Reflection.BucketedTexelTraceDataPackedBuffer"));
			FRDGBufferRef CompactedTraceDataPackedBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(LumenReflection::FTraceDataPacked), RayCount), TEXT("Lumen.Reflection.CompactedTraceDataPacked"));
			{
				FLumenReflectionCompactRaysCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenReflectionCompactRaysCS::FParameters>();
				{
					// Input
					PassParameters->RayAllocator = GraphBuilder.CreateSRV(FarFieldRayAllocatorBuffer, PF_R32_UINT);
					PassParameters->TraceTexelDataPacked = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(FarFieldTraceTexelDataPackedBuffer, PF_R32G32_UINT));
					PassParameters->TraceDataPacked = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(FarFieldTraceDataPackedBuffer));

					// Output
					PassParameters->RWRayAllocator = GraphBuilder.CreateUAV(CompactedRayAllocatorBuffer, PF_R32_UINT);
					PassParameters->RWTraceTexelDataPacked = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(CompactedTexelTraceDataPackedBuffer, PF_R32G32_UINT));
					PassParameters->RWTraceDataPacked = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(CompactedTraceDataPackedBuffer));

					// Indirect args
					PassParameters->CompactRaysIndirectArgs = CompactRaysIndirectArgsBuffer;
				}

				FLumenReflectionCompactRaysCS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FLumenReflectionCompactRaysCS::FCompactModeDim>(LumenReflection::ECompactMode::FarFieldRetrace);
				TShaderRef<FLumenReflectionCompactRaysCS> ComputeShader = View.ShaderMap->GetShader<FLumenReflectionCompactRaysCS>(PermutationVector);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("ReflectionCompactRays"),
					ComputeShader,
					PassParameters,
					PassParameters->CompactRaysIndirectArgs,
					0);
			}
			FarFieldRayAllocatorBuffer = CompactedRayAllocatorBuffer;
			FarFieldTraceTexelDataPackedBuffer = CompactedTexelTraceDataPackedBuffer;
			FarFieldTraceDataPackedBuffer = CompactedTraceDataPackedBuffer;
		}

		// Trace continuation rays
		{
			FRDGBufferRef HardwareRayTracingIndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Lumen.Reflection.CompactTracingIndirectArgs"));
			{
				FLumenReflectionHardwareRayTracingIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenReflectionHardwareRayTracingIndirectArgsCS::FParameters>();
				{
					PassParameters->RayAllocator = GraphBuilder.CreateSRV(FarFieldRayAllocatorBuffer, PF_R32_UINT);
					PassParameters->RWHardwareRayTracingIndirectArgs = GraphBuilder.CreateUAV(HardwareRayTracingIndirectArgsBuffer, PF_R32_UINT);
				}

				TShaderRef<FLumenReflectionHardwareRayTracingIndirectArgsCS> ComputeShader = View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingIndirectArgsCS>();
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("ReflectionCompactRaysIndirectArgs"),
					ComputeShader,
					PassParameters,
					FIntVector(1, 1, 1));
			}

			uint32 DefaultThreadCount = CVarLumenReflectionsHardwareRayTracingRetraceThreadCount.GetValueOnRenderThread();
			uint32 DefaultGroupCount = CVarLumenReflectionsHardwareRayTracingRetraceGroupCount.GetValueOnRenderThread();

			FLumenReflectionHardwareRayTracingRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenReflectionHardwareRayTracingRGS::FParameters>();
			{
				SetLumenHardwareRayTracingSharedParameters(
					GraphBuilder,
					SceneTextures,
					View,
					TracingInputs,
					&PassParameters->SharedParameters
				);
				PassParameters->RayAllocator = GraphBuilder.CreateSRV(FarFieldRayAllocatorBuffer, PF_R32_UINT);
				PassParameters->HardwareRayTracingIndirectArgs = HardwareRayTracingIndirectArgsBuffer;
				PassParameters->TraceTexelDataPacked = GraphBuilder.CreateSRV(FarFieldTraceTexelDataPackedBuffer, PF_R32G32_UINT);
				PassParameters->TraceDataPacked = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(FarFieldTraceDataPackedBuffer));

				// Constants
				PassParameters->ThreadCount = DefaultThreadCount;
				PassParameters->GroupCount = DefaultGroupCount;
				PassParameters->NearFieldLightingMode = static_cast<int>(Lumen::GetNearFieldLightingMode(View));
				PassParameters->MaxTraceDistance = MaxVoxelTraceDistance;
				PassParameters->FarFieldMaxTraceDistance = Lumen::GetFarFieldMaxTraceDistance();
				PassParameters->FarFieldReferencePos = Lumen::GetFarFieldReferencePos();
				PassParameters->PullbackBias = Lumen::GetHardwareRayTracingPullbackBias();
				PassParameters->MaxTranslucentSkipCount = CVarLumenReflectionsHardwareRayTracingMaxTranslucentSkipCount.GetValueOnRenderThread();
				PassParameters->ApplySkyLight = 1;

				// Reflection-specific
				PassParameters->ReflectionTracingParameters = ReflectionTracingParameters;
				PassParameters->ReflectionTileParameters = ReflectionTileParameters;
				PassParameters->RadianceCacheParameters = RadianceCacheParameters;

				// Ray continuation buffer
				PassParameters->RWTraceDataPacked = GraphBuilder.CreateUAV(FarFieldTraceDataPackedBuffer);
			}

			FLumenReflectionHardwareRayTracingRGS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FLightingModeDim>(LumenReflection::ELightingMode::SurfaceCache);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FRadianceCache>(bUseRadianceCache);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FEnableNearFieldTracing>(false);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FEnableFarFieldTracing>(true);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FWriteFinalLightingDim>(true);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FIndirectDispatchDim>(IsHardwareRayTracingReflectionsIndirectDispatch());
			TShaderRef<FLumenReflectionHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingRGS>(PermutationVector);

			FIntPoint DispatchResolution = FIntPoint(DefaultThreadCount, DefaultGroupCount);
			FString Resolution = FString::Printf(TEXT("%ux%u"), DispatchResolution.X, DispatchResolution.Y);
			if (IsHardwareRayTracingReflectionsIndirectDispatch())
			{
				Resolution = FString::Printf(TEXT("<indirect>"));
			}
			GraphBuilder.AddPass(
				RDG_EVENT_NAME("ReflectionHardwareRayTracing [retrace with hit-lighting] %s", *Resolution),
				PassParameters,
				ERDGPassFlags::Compute,
				[PassParameters, &View, RayGenerationShader, DispatchResolution](FRHIRayTracingCommandList& RHICmdList)
				{
					FRayTracingShaderBindingsWriter GlobalResources;
					SetShaderParameters(GlobalResources, RayGenerationShader, *PassParameters);

					FRHIRayTracingScene* RayTracingSceneRHI = View.GetRayTracingSceneChecked();
					FRayTracingPipelineState* Pipeline = View.LumenHardwareRayTracingMaterialPipeline;
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
	}

	// Re-trace for hit-lighting
	FRDGBufferRef RayAllocatorBuffer = RayAllocatorBufferCached;
	FRDGBufferRef TraceTexelDataPackedBuffer = TraceTexelDataPackedBufferCached;
	FRDGBufferRef TraceDataPackedBuffer = TraceDataPackedBufferCached;
	if (bIsForceHitLighting || CVarLumenReflectionsHardwareRayTracingRetraceHitLighting.GetValueOnRenderThread())
	{
		// Compact rays which need to be re-traced
		if (CVarLumenReflectionsHardwareRayTracingCompact.GetValueOnRenderThread())
		{
			FRDGBufferRef CompactRaysIndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Lumen.Reflection.CompactTracingIndirectArgs"));
			{
				FLumenReflectionCompactRaysIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenReflectionCompactRaysIndirectArgsCS::FParameters>();
				{
					PassParameters->RayAllocator = GraphBuilder.CreateSRV(RayAllocatorBuffer, PF_R32_UINT);
					PassParameters->RWCompactRaysIndirectArgs = GraphBuilder.CreateUAV(CompactRaysIndirectArgsBuffer, PF_R32_UINT);
				}

				TShaderRef<FLumenReflectionCompactRaysIndirectArgsCS> ComputeShader = View.ShaderMap->GetShader<FLumenReflectionCompactRaysIndirectArgsCS>();
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("ReflectionCompactRaysIndirectArgs"),
					ComputeShader,
					PassParameters,
					FIntVector(1, 1, 1));
			}

			FRDGBufferRef CompactedRayAllocatorBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("Lumen.Reflection.CompactedRayAllocator"));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(CompactedRayAllocatorBuffer, PF_R32_UINT), 0);

			FRDGBufferRef CompactedTexelTraceDataPackedBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32) * 2, RayCount), TEXT("Lumen.Reflection.BucketedTexelTraceDataPackedBuffer"));
			FRDGBufferRef CompactedTraceDataPackedBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(LumenReflection::FTraceDataPacked), RayCount), TEXT("Lumen.Reflection.CompactedTraceDataPacked"));
			{
				FLumenReflectionCompactRaysCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenReflectionCompactRaysCS::FParameters>();
				{
					// Input
					PassParameters->RayAllocator = GraphBuilder.CreateSRV(RayAllocatorBuffer, PF_R32_UINT);
					PassParameters->TraceTexelDataPacked = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(TraceTexelDataPackedBuffer, PF_R32G32_UINT));
					PassParameters->TraceDataPacked = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(TraceDataPackedBuffer));

					// Output
					PassParameters->RWRayAllocator = GraphBuilder.CreateUAV(CompactedRayAllocatorBuffer, PF_R32_UINT);
					PassParameters->RWTraceTexelDataPacked = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(CompactedTexelTraceDataPackedBuffer, PF_R32G32_UINT));
					PassParameters->RWTraceDataPacked = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(CompactedTraceDataPackedBuffer));

					// Indirect args
					PassParameters->CompactRaysIndirectArgs = CompactRaysIndirectArgsBuffer;
				}

				FLumenReflectionCompactRaysCS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FLumenReflectionCompactRaysCS::FCompactModeDim>(bIsForceHitLighting ?
					LumenReflection::ECompactMode::ForceHitLighting : LumenReflection::ECompactMode::HitLightingRetrace);
				TShaderRef<FLumenReflectionCompactRaysCS> ComputeShader = View.ShaderMap->GetShader<FLumenReflectionCompactRaysCS>(PermutationVector);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("ReflectionCompactRays"),
					ComputeShader,
					PassParameters,
					PassParameters->CompactRaysIndirectArgs,
					0);
			}
			RayAllocatorBuffer = CompactedRayAllocatorBuffer;
			TraceTexelDataPackedBuffer = CompactedTexelTraceDataPackedBuffer;
			TraceDataPackedBuffer = CompactedTraceDataPackedBuffer;
		}

		// Append far-field rays
		if (UseFarFieldForReflections())
		{
			FRDGBufferRef CompactRaysIndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Lumen.Reflection.CompactTracingIndirectArgs"));
			{
				FLumenReflectionCompactRaysIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenReflectionCompactRaysIndirectArgsCS::FParameters>();
				{
					PassParameters->RayAllocator = GraphBuilder.CreateSRV(FarFieldRayAllocatorBuffer, PF_R32_UINT);
					PassParameters->RWCompactRaysIndirectArgs = GraphBuilder.CreateUAV(CompactRaysIndirectArgsBuffer, PF_R32_UINT);
				}

				TShaderRef<FLumenReflectionCompactRaysIndirectArgsCS> ComputeShader = View.ShaderMap->GetShader<FLumenReflectionCompactRaysIndirectArgsCS>();
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("ReflectionCompactRaysIndirectArgs"),
					ComputeShader,
					PassParameters,
					FIntVector(1, 1, 1));
			}

			{
				FLumenReflectionCompactRaysCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenReflectionCompactRaysCS::FParameters>();
				{
					// Input
					PassParameters->RayAllocator = GraphBuilder.CreateSRV(FarFieldRayAllocatorBuffer, PF_R32_UINT);
					PassParameters->TraceTexelDataPacked = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(FarFieldTraceTexelDataPackedBuffer, PF_R32G32_UINT));
					PassParameters->TraceDataPacked = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(FarFieldTraceDataPackedBuffer));

					// Output
					PassParameters->RWRayAllocator = GraphBuilder.CreateUAV(RayAllocatorBuffer, PF_R32_UINT);
					PassParameters->RWTraceTexelDataPacked = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(TraceTexelDataPackedBuffer, PF_R32G32_UINT));
					PassParameters->RWTraceDataPacked = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(TraceDataPackedBuffer));

					// Indirect args
					PassParameters->CompactRaysIndirectArgs = CompactRaysIndirectArgsBuffer;
				}

				FLumenReflectionCompactRaysCS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FLumenReflectionCompactRaysCS::FCompactModeDim>(LumenReflection::ECompactMode::AppendRays);
				TShaderRef<FLumenReflectionCompactRaysCS> ComputeShader = View.ShaderMap->GetShader<FLumenReflectionCompactRaysCS>(PermutationVector);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("ReflectionCompactRays"),
					ComputeShader,
					PassParameters,
					PassParameters->CompactRaysIndirectArgs,
					0);
			}
		}

		// Sort rays by material
		if (CVarLumenReflectionsHardwareRayTracingBucketMaterials.GetValueOnRenderThread())
		{
			FRDGBufferRef BucketRaysByMaterialIdIndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Lumen.Reflections.BucketRaysByMaterialIdIndirectArgsBuffer"));
			{
				FLumenReflectionBucketRaysByMaterialIdIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenReflectionBucketRaysByMaterialIdIndirectArgsCS::FParameters>();
				{
					PassParameters->RayAllocator = GraphBuilder.CreateSRV(RayAllocatorBuffer, PF_R32_UINT);
					PassParameters->RWBucketRaysByMaterialIdIndirectArgs = GraphBuilder.CreateUAV(BucketRaysByMaterialIdIndirectArgsBuffer, PF_R32_UINT);
				}

				TShaderRef<FLumenReflectionBucketRaysByMaterialIdIndirectArgsCS> ComputeShader = View.ShaderMap->GetShader<FLumenReflectionBucketRaysByMaterialIdIndirectArgsCS>();
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("ReflectionBucketRaysByMaterialIdIndirectArgs"),
					ComputeShader,
					PassParameters,
					FIntVector(1, 1, 1));
			}

			FRDGBufferRef BucketedTexelTraceDataPackedBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32) * 2, RayCount), TEXT("Lumen.Reflections.BucketedTexelTraceDataPackedBuffer"));
			FRDGBufferRef BucketedTraceDataPackedBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(LumenReflection::FTraceDataPacked), RayCount), TEXT("Lumen.Reflections.BucketedTraceDataPacked"));
			{
				FLumenReflectionBucketRaysByMaterialIdCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenReflectionBucketRaysByMaterialIdCS::FParameters>();
				{
					// Input
					PassParameters->RayAllocator = GraphBuilder.CreateSRV(RayAllocatorBuffer, PF_R32_UINT);
					PassParameters->TraceTexelDataPacked = GraphBuilder.CreateSRV(TraceTexelDataPackedBuffer, PF_R32G32_UINT);
					PassParameters->TraceDataPacked = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(TraceDataPackedBuffer));
					PassParameters->MaxRayAllocationCount = RayCount;

					// Output
					PassParameters->RWTraceTexelDataPacked = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(BucketedTexelTraceDataPackedBuffer, PF_R32G32_UINT));
					PassParameters->RWTraceDataPacked = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(BucketedTraceDataPackedBuffer));

					// Indirect args
					PassParameters->BucketRaysByMaterialIdIndirectArgs = BucketRaysByMaterialIdIndirectArgsBuffer;
				}

				TShaderRef<FLumenReflectionBucketRaysByMaterialIdCS> ComputeShader = View.ShaderMap->GetShader<FLumenReflectionBucketRaysByMaterialIdCS>();
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("ReflectionBucketRaysByMaterialId"),
					ComputeShader,
					PassParameters,
					PassParameters->BucketRaysByMaterialIdIndirectArgs,
					0);

				TraceTexelDataPackedBuffer = BucketedTexelTraceDataPackedBuffer;
				TraceDataPackedBuffer = BucketedTraceDataPackedBuffer;
			}
		}

		// Trace with hit-lighting
		{
			FRDGBufferRef HardwareRayTracingIndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Lumen.Reflection.CompactTracingIndirectArgs"));
			{
				FLumenReflectionHardwareRayTracingIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenReflectionHardwareRayTracingIndirectArgsCS::FParameters>();
				{
					PassParameters->RayAllocator = GraphBuilder.CreateSRV(RayAllocatorBuffer, PF_R32_UINT);
					PassParameters->RWHardwareRayTracingIndirectArgs = GraphBuilder.CreateUAV(HardwareRayTracingIndirectArgsBuffer, PF_R32_UINT);
				}

				TShaderRef<FLumenReflectionHardwareRayTracingIndirectArgsCS> ComputeShader = View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingIndirectArgsCS>();
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("ReflectionCompactRaysIndirectArgs"),
					ComputeShader,
					PassParameters,
					FIntVector(1, 1, 1));
			}

			uint32 DefaultThreadCount = CVarLumenReflectionsHardwareRayTracingRetraceThreadCount.GetValueOnRenderThread();
			uint32 DefaultGroupCount = CVarLumenReflectionsHardwareRayTracingRetraceGroupCount.GetValueOnRenderThread();

			FLumenReflectionHardwareRayTracingRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenReflectionHardwareRayTracingRGS::FParameters>();
			{
				SetLumenHardwareRayTracingSharedParameters(
					GraphBuilder,
					SceneTextures,
					View,
					TracingInputs,
					&PassParameters->SharedParameters
				);
				PassParameters->RayAllocator = GraphBuilder.CreateSRV(RayAllocatorBuffer, PF_R32_UINT);
				PassParameters->HardwareRayTracingIndirectArgs = HardwareRayTracingIndirectArgsBuffer;
				PassParameters->TraceTexelDataPacked = GraphBuilder.CreateSRV(TraceTexelDataPackedBuffer, PF_R32G32_UINT);
				PassParameters->TraceDataPacked = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(TraceDataPackedBuffer));

				// Constants
				PassParameters->ThreadCount = DefaultThreadCount;
				PassParameters->GroupCount = DefaultGroupCount;
				PassParameters->NearFieldLightingMode = static_cast<int>(Lumen::GetNearFieldLightingMode(View));
				PassParameters->MaxTraceDistance = MaxVoxelTraceDistance;
				PassParameters->FarFieldMaxTraceDistance = Lumen::GetFarFieldMaxTraceDistance();
				PassParameters->FarFieldReferencePos = Lumen::GetFarFieldReferencePos();
				PassParameters->PullbackBias = Lumen::GetHardwareRayTracingPullbackBias();
				PassParameters->MaxTranslucentSkipCount = CVarLumenReflectionsHardwareRayTracingMaxTranslucentSkipCount.GetValueOnRenderThread();
				PassParameters->ApplySkyLight = 1;

				// Reflection-specific
				PassParameters->ReflectionTracingParameters = ReflectionTracingParameters;
				PassParameters->ReflectionTileParameters = ReflectionTileParameters;
				PassParameters->RadianceCacheParameters = RadianceCacheParameters;
			}

			FLumenReflectionHardwareRayTracingRGS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FLightingModeDim>(LumenReflection::ELightingMode::HitLighting);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FRadianceCache>(bUseRadianceCache);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FEnableNearFieldTracing>(true);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FEnableFarFieldTracing>(Lumen::UseFarField());
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FWriteFinalLightingDim>(true);
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FIndirectDispatchDim>(IsHardwareRayTracingReflectionsIndirectDispatch());
			TShaderRef<FLumenReflectionHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingRGS>(PermutationVector);

			FIntPoint DispatchResolution = FIntPoint(DefaultThreadCount, DefaultGroupCount);
			FString Resolution = FString::Printf(TEXT("%ux%u"), DispatchResolution.X, DispatchResolution.Y);
			if (IsHardwareRayTracingReflectionsIndirectDispatch())
			{
				Resolution = FString::Printf(TEXT("<indirect>"));
			}
			GraphBuilder.AddPass(
				RDG_EVENT_NAME("ReflectionHardwareRayTracing [retrace with hit-lighting] %s", *Resolution),
				PassParameters,
				ERDGPassFlags::Compute,
				[PassParameters, &View, RayGenerationShader, DispatchResolution](FRHIRayTracingCommandList& RHICmdList)
				{
					FRayTracingShaderBindingsWriter GlobalResources;
					SetShaderParameters(GlobalResources, RayGenerationShader, *PassParameters);

					FRHIRayTracingScene* RayTracingSceneRHI = View.GetRayTracingSceneChecked();
					FRayTracingPipelineState* Pipeline = View.RayTracingMaterialPipeline;
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
	}
#else
	unimplemented();
#endif
}
