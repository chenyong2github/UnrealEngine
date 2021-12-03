// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenTranslucencyVolumeHardwareRayTracing.cpp
=============================================================================*/

#include "LumenTranslucencyVolumeLighting.h"
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "VolumeLighting.h"
#include "LumenTracingUtils.h"
#include "LumenRadianceCache.h"

#if RHI_RAYTRACING

#include "RayTracing/RayTracingDeferredMaterials.h"
#include "RayTracing/RaytracingOptions.h"
#include "RayTracing/RayTracingLighting.h"
#include "LumenHardwareRayTracingCommon.h"

// Console variables
static TAutoConsoleVariable<int32> CVarLumenTranslucencyVolumeHardwareRayTracing(
	TEXT("r.Lumen.TranslucencyVolume.HardwareRayTracing"),
	1,
	TEXT("Enables hardware ray tracing for Lumen translucency volume (Default = 1)"),
	ECVF_RenderThreadSafe
);

#endif // RHI_RAYTRACING

namespace Lumen
{
	bool UseHardwareRayTracedTranslucencyVolume()
	{
#if RHI_RAYTRACING
		return IsRayTracingEnabled()
			&& Lumen::UseHardwareRayTracing()
			&& (CVarLumenTranslucencyVolumeHardwareRayTracing.GetValueOnRenderThread() != 0);
#else
		return false;
#endif
	}
}

#if RHI_RAYTRACING

class FLumenTranslucencyVolumeHardwareRayTracingRGS : public FLumenHardwareRayTracingRGS
{
	DECLARE_GLOBAL_SHADER(FLumenTranslucencyVolumeHardwareRayTracingRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FLumenTranslucencyVolumeHardwareRayTracingRGS, FLumenHardwareRayTracingRGS)

	class FRadianceCache : SHADER_PERMUTATION_BOOL("USE_RADIANCE_CACHE");

	using FPermutationDomain = TShaderPermutationDomain<FRadianceCache>;

	// Parameters
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float3>, RWVolumeTraceRadiance)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, RWVolumeTraceHitDistance)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenHardwareRayTracingRGS::FSharedParameters, SharedParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInterpolationParameters, RadianceCacheParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenTranslucencyLightingVolumeParameters, VolumeParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenTranslucencyLightingVolumeTraceSetupParameters, TraceSetupParameters)
		SHADER_PARAMETER(uint32, MaxTraversalIterations)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FLumenHardwareRayTracingRGS::ModifyCompilationEnvironment(Parameters, Lumen::ESurfaceCacheSampling::AlwaysResidentPages, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_DISPATCH_1D"), 1);
		OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_LIGHTWEIGHT_CLOSEST_HIT_SHADER"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenTranslucencyVolumeHardwareRayTracingRGS, "/Engine/Private/Lumen/LumenTranslucencyVolumeHardwareRayTracing.usf", "LumenTranslucencyVolumeHardwareRayTracingRGS", SF_RayGen);

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingTranslucencyVolume(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	extern int32 GLumenTranslucencyVolumeRadianceCache;
	{
		FLumenTranslucencyVolumeHardwareRayTracingRGS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenTranslucencyVolumeHardwareRayTracingRGS::FRadianceCache>(GLumenTranslucencyVolumeRadianceCache != 0);
		TShaderRef<FLumenTranslucencyVolumeHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenTranslucencyVolumeHardwareRayTracingRGS>(PermutationVector);

		OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
	}
}

#endif // RHI_RAYTRACING

void HardwareRayTraceTranslucencyVolume(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FLumenCardTracingInputs& TracingInputs,
	LumenRadianceCache::FRadianceCacheInterpolationParameters RadianceCacheParameters,
	FLumenTranslucencyLightingVolumeParameters VolumeParameters,
	FLumenTranslucencyLightingVolumeTraceSetupParameters TraceSetupParameters,
	FRDGTextureRef VolumeTraceRadiance,
	FRDGTextureRef VolumeTraceHitDistance
)
{
#if RHI_RAYTRACING
	bool bUseMinimalPayload = true;

	// Cast rays
	{
		FLumenTranslucencyVolumeHardwareRayTracingRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenTranslucencyVolumeHardwareRayTracingRGS::FParameters>();

		SetLumenHardwareRayTracingSharedParameters(
			GraphBuilder,
			GetSceneTextureParameters(GraphBuilder),
			View,
			TracingInputs,
			&PassParameters->SharedParameters);

		PassParameters->RWVolumeTraceRadiance = GraphBuilder.CreateUAV(VolumeTraceRadiance);
		PassParameters->RWVolumeTraceHitDistance = GraphBuilder.CreateUAV(VolumeTraceHitDistance);
		PassParameters->VolumeParameters = VolumeParameters;
		PassParameters->TraceSetupParameters = TraceSetupParameters;
		PassParameters->MaxTraversalIterations = LumenHardwareRayTracing::GetMaxTraversalIterations();

		PassParameters->RadianceCacheParameters = RadianceCacheParameters;

		FLumenTranslucencyVolumeHardwareRayTracingRGS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenTranslucencyVolumeHardwareRayTracingRGS::FRadianceCache>(RadianceCacheParameters.RadianceProbeIndirectionTexture != nullptr);
		TShaderRef<FLumenTranslucencyVolumeHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenTranslucencyVolumeHardwareRayTracingRGS>(PermutationVector);

		ClearUnusedGraphResources(RayGenerationShader, PassParameters);

		const FIntPoint DispatchResolution(VolumeTraceRadiance->Desc.Extent * FIntPoint(VolumeTraceRadiance->Desc.Depth, 1));

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("HardwareRayTracing %ux%u", DispatchResolution.X, DispatchResolution.Y),
			PassParameters,
			ERDGPassFlags::Compute,
			[PassParameters, &View, RayGenerationShader, DispatchResolution, bUseMinimalPayload](FRHIRayTracingCommandList& RHICmdList)
			{
				FRayTracingShaderBindingsWriter GlobalResources;
				SetShaderParameters(GlobalResources, RayGenerationShader, *PassParameters);

				FRHIRayTracingScene* RayTracingSceneRHI = View.GetRayTracingSceneChecked();
				FRayTracingPipelineState* RayTracingPipeline = bUseMinimalPayload ? View.LumenHardwareRayTracingMaterialPipeline : View.RayTracingMaterialPipeline;
				RHICmdList.RayTraceDispatch(RayTracingPipeline, RayGenerationShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, DispatchResolution.X, DispatchResolution.Y);
			}
		);
	}

#else
	unimplemented();
#endif // RHI_RAYTRACING
}
