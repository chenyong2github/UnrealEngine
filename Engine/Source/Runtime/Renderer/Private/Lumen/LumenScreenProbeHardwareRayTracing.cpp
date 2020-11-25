// Copyright Epic Games, Inc. All Rights Reserved.

#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "LumenSceneUtils.h"
#include "PixelShaderUtils.h"
#include "ReflectionEnvironment.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "SceneTextureParameters.h"
#include "IndirectLightRendering.h"

// Actual screen-probe requirements..
#include "LumenRadianceCache.h"
#include "LumenScreenProbeGather.h"

#if RHI_RAYTRACING
#include "RayTracing/RaytracingOptions.h"
#include "RayTracing/RayTracingLighting.h"
#include "LumenHardwareRayTracingCommon.h"

static TAutoConsoleVariable<int32> CVarLumenScreenProbeGatherHardwareRayTracing(
	TEXT("r.Lumen.ScreenProbeGather.HardwareRayTracing"),
	0,
	TEXT("0. Software raytracing of diffuse indirect from Lumen cubemap tree. (Default)")
	TEXT("1. Enable hardware ray tracing of diffuse indirect.\n"),
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

static TAutoConsoleVariable<int32> CVarLumenScreenProbeGatherHardwareRayTracingNormalMode(
	TEXT("r.Lumen.ScreenProbeGather.HardwareRayTracing.NormalMode"),
	0,
	TEXT("Determines the tracing normal (Default = 0)\n")
	TEXT("0: SDF normal\n")
	TEXT("1: Geometry normal"),
	ECVF_RenderThreadSafe
);
#endif

namespace Lumen
{
	bool UseHardwareRayTracedScreenProbeGather()
	{
#if RHI_RAYTRACING
		return (CVarLumenScreenProbeGatherHardwareRayTracing.GetValueOnRenderThread() != 0) && IsRayTracingEnabled();
#else
		return false;
#endif
	}

	EHardwareRayTracingLightingMode GetScreenProbeGatherHardwareRayTracingLightingMode()
	{
#if RHI_RAYTRACING
		return EHardwareRayTracingLightingMode(CVarLumenScreenProbeGatherHardwareRayTracingLightingMode.GetValueOnRenderThread());
#else
		return EHardwareRayTracingLightingMode::LightingFromSurfaceCache;
#endif
	}
}

#if RHI_RAYTRACING

// A temporary hack for RGS to access array declaration.
// Workaround for error "subscripted value is not an array, matrix, or vector" in DXC when SHADER_PARAMETER_ARRAY is used in RGS
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FRGSRadianceCacheParameters, )
SHADER_PARAMETER_ARRAY(float, RadianceProbeClipmapTMin, [LumenRadianceCache::MaxClipmaps])
SHADER_PARAMETER_ARRAY(float, RadianceProbeClipmapSamplingJitter, [LumenRadianceCache::MaxClipmaps])
SHADER_PARAMETER_ARRAY(float, WorldPositionToRadianceProbeCoordScale, [LumenRadianceCache::MaxClipmaps])
SHADER_PARAMETER_ARRAY(FVector, WorldPositionToRadianceProbeCoordBias, [LumenRadianceCache::MaxClipmaps])
SHADER_PARAMETER_ARRAY(float, RadianceProbeCoordToWorldPositionScale, [LumenRadianceCache::MaxClipmaps])
SHADER_PARAMETER_ARRAY(FVector, RadianceProbeCoordToWorldPositionBias, [LumenRadianceCache::MaxClipmaps])
END_GLOBAL_SHADER_PARAMETER_STRUCT()

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FRGSRadianceCacheParameters, "RGSRadianceCacheParameters");

void SetupRGSRadianceCacheParametersNew(const LumenRadianceCache::FRadianceCacheParameters& RadianceCacheParameters,
	FRGSRadianceCacheParameters& RGSRadianceCacheParameters)
{
	for (int i = 0; i < LumenRadianceCache::MaxClipmaps; ++i)
	{
		RGSRadianceCacheParameters.RadianceProbeClipmapTMin[i] = RadianceCacheParameters.RadianceProbeClipmapTMin[i];
		RGSRadianceCacheParameters.RadianceProbeClipmapSamplingJitter[i] = RadianceCacheParameters.RadianceProbeClipmapSamplingJitter[i];
		RGSRadianceCacheParameters.WorldPositionToRadianceProbeCoordScale[i] = RadianceCacheParameters.WorldPositionToRadianceProbeCoordScale[i];
		RGSRadianceCacheParameters.WorldPositionToRadianceProbeCoordBias[i] = RadianceCacheParameters.WorldPositionToRadianceProbeCoordBias[i];
		RGSRadianceCacheParameters.RadianceProbeCoordToWorldPositionScale[i] = RadianceCacheParameters.RadianceProbeCoordToWorldPositionScale[i];
		RGSRadianceCacheParameters.RadianceProbeCoordToWorldPositionBias[i] = RadianceCacheParameters.RadianceProbeCoordToWorldPositionBias[i];
	}
}

class FLumenScreenProbeGatherHardwareRayTracingRGS : public FLumenHardwareRayTracingRGS
{
	DECLARE_GLOBAL_SHADER(FLumenScreenProbeGatherHardwareRayTracingRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FLumenScreenProbeGatherHardwareRayTracingRGS, FLumenHardwareRayTracingRGS)

	class FNormalModeDim : SHADER_PERMUTATION_BOOL("DIM_NORMAL_MODE");
	class FLightingModeDim : SHADER_PERMUTATION_INT("DIM_LIGHTING_MODE", static_cast<int32>(Lumen::EHardwareRayTracingLightingMode::MAX));
	class FRadianceCacheDim : SHADER_PERMUTATION_BOOL("DIM_RADIANCE_CACHE");
	class FStructuredImportanceSamplingDim : SHADER_PERMUTATION_BOOL("STRUCTURED_IMPORTANCE_SAMPLING");
	using FPermutationDomain = TShaderPermutationDomain<FNormalModeDim, FLightingModeDim, FRadianceCacheDim, FStructuredImportanceSamplingDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenHardwareRayTracingRGS::FSharedParameters, SharedParameters)

		// Screen probes
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenIndirectTracingParameters, IndirectTracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)

		// Radiance cache
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheParameters, RadianceCacheParameters)
		SHADER_PARAMETER_STRUCT_REF(FRGSRadianceCacheParameters, RGSRadianceCacheParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FCompactedTraceParameters, CompactedTraceParameters)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FLumenHardwareRayTracingRGS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_DISPATCH_1D"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenScreenProbeGatherHardwareRayTracingRGS, "/Engine/Private/Lumen/LumenScreenProbeHardwareRayTracing.usf", "LumenScreenProbeGatherHardwareRayTracingRGS", SF_RayGen);

void FDeferredShadingSceneRenderer::PrepareRayTracingScreenProbeGather(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	FLumenScreenProbeGatherHardwareRayTracingRGS::FPermutationDomain PermutationVector;

	for (int32 StructuredImportanceSampling = 0; StructuredImportanceSampling < 2; ++StructuredImportanceSampling)
	{
		PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FStructuredImportanceSamplingDim>(StructuredImportanceSampling != 0);

		for (int32 UseRadianceCache = 0; UseRadianceCache < 2; ++UseRadianceCache)
		{
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FRadianceCacheDim>(UseRadianceCache != 0);

			for (int32 LightingMode = 0; LightingMode < static_cast<int32>(Lumen::EHardwareRayTracingLightingMode::MAX); ++LightingMode)
			{
				PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FLightingModeDim>(LightingMode);

				for (int32 NormalMode = 0; NormalMode < 2; ++NormalMode)
				{
					PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FNormalModeDim>(NormalMode != 0);

					TShaderRef<FLumenScreenProbeGatherHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenScreenProbeGatherHardwareRayTracingRGS>(PermutationVector);
					OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
				}
			}
		}
	}
}

#endif // RHI_RAYTRACING

void RenderHardwareRayTracingScreenProbe(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FSceneTextureParameters& SceneTextures,
	FScreenProbeParameters& ScreenProbeParameters,
	const FViewInfo& View,
	const FLumenCardTracingInputs& TracingInputs,
	const FLumenMeshSDFGridParameters& MeshSDFGridParameters,
	FLumenIndirectTracingParameters& IndirectTracingParameters,
	const LumenRadianceCache::FRadianceCacheParameters& RadianceCacheParameters,
	const FCompactedTraceParameters& CompactedTraceParameters)
#if RHI_RAYTRACING
{
	FLumenScreenProbeGatherHardwareRayTracingRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenScreenProbeGatherHardwareRayTracingRGS::FParameters>();

	SetLumenHardwareRayTracingSharedParameters(
		GraphBuilder,
		SceneTextures,
		View,
		TracingInputs,
		MeshSDFGridParameters,
		&PassParameters->SharedParameters
	);

	// Screen-probe gather arguments
	PassParameters->IndirectTracingParameters = IndirectTracingParameters;
	PassParameters->ScreenProbeParameters = ScreenProbeParameters;

	// Radiance cache arguments
	FRGSRadianceCacheParameters RGSRadianceCacheParameters;
	SetupRGSRadianceCacheParametersNew(RadianceCacheParameters, RGSRadianceCacheParameters);
	PassParameters->RGSRadianceCacheParameters = CreateUniformBufferImmediate(RGSRadianceCacheParameters, UniformBuffer_SingleFrame);
	PassParameters->RadianceCacheParameters = RadianceCacheParameters;
	PassParameters->CompactedTraceParameters = CompactedTraceParameters;

	// Constants!
	int LightingMode = CVarLumenScreenProbeGatherHardwareRayTracingLightingMode.GetValueOnRenderThread();
	int NormalMode = CVarLumenScreenProbeGatherHardwareRayTracingNormalMode.GetValueOnRenderThread();

	FLumenScreenProbeGatherHardwareRayTracingRGS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FNormalModeDim>(NormalMode != 0);
	PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FLightingModeDim>(LightingMode);
	PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FRadianceCacheDim>(LumenScreenProbeGather::UseRadianceCache(View));
	PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FStructuredImportanceSamplingDim>(LumenScreenProbeGather::UseImportanceSampling());

	TShaderRef<FLumenScreenProbeGatherHardwareRayTracingRGS> RayGenerationShader =
		View.ShaderMap->GetShader<FLumenScreenProbeGatherHardwareRayTracingRGS>(PermutationVector);
	ClearUnusedGraphResources(RayGenerationShader, PassParameters);

	auto RayTracingResolution = ScreenProbeParameters.ScreenProbeAtlasViewSize * ScreenProbeParameters.ScreenProbeTracingOctahedronResolution;
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HardwareRayTracing %ux%u LightingMode=%s", RayTracingResolution.X, RayTracingResolution.Y, Lumen::GetRayTracedLightingModeName((Lumen::EHardwareRayTracingLightingMode)LightingMode)),
		PassParameters,
		ERDGPassFlags::Compute,
		[PassParameters, &View, RayGenerationShader, RayTracingResolution](FRHICommandList& RHICmdList)
		{
			FRayTracingShaderBindingsWriter GlobalResources;
			SetShaderParameters(GlobalResources, RayGenerationShader, *PassParameters);

			FRHIRayTracingScene* RayTracingSceneRHI = View.RayTracingScene.RayTracingSceneRHI;
			RHICmdList.RayTraceDispatch(View.RayTracingMaterialPipeline, RayGenerationShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, RayTracingResolution.X, RayTracingResolution.Y);
		}
	);
}
#else // RHI_RAYTRACING
{
	unimplemented();
}
#endif // RHI_RAYTRACING
