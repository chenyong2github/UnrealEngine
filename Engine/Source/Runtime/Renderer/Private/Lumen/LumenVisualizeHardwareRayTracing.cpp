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

static TAutoConsoleVariable<int32> CVarLumenVisualizeHardwareRayTracing(
	TEXT("r.Lumen.Visualize.HardwareRayTracing"),
	0,
	TEXT("Enables visualization of hardware ray tracing (Default = 0)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenVisualizeHardwareRayTracingLightingMode(
	TEXT("r.Lumen.Visualize.HardwareRayTracing.LightingMode"),
	0,
	TEXT("Determines the lighting mode (Default = 0)\n")
	TEXT("0: interpolate final lighting from the surface cache\n")
	TEXT("1: evaluate material, and interpolate irradiance and indirect irradiance from the surface cache\n")
	TEXT("2: evaluate material and direct lighting, and interpolate indirect irradiance from the surface cache"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenVisualizeHardwareRayTracingNormalMode(
	TEXT("r.Lumen.Visualize.HardwareRayTracing.NormalMode"),
	0,
	TEXT("Determines the tracing normal (Default = 0)\n")
	TEXT("0: SDF normal\n")
	TEXT("1: Geometry normal"),
	ECVF_RenderThreadSafe
);
#endif // RHI_RAYTRACING

namespace Lumen
{
	EHardwareRayTracingLightingMode GetVisualizeHardwareRayTracingLightingMode()
	{
#if RHI_RAYTRACING
		return EHardwareRayTracingLightingMode(CVarLumenVisualizeHardwareRayTracingLightingMode.GetValueOnRenderThread());
#else
		return EHardwareRayTracingLightingMode::LightingFromSurfaceCache;
#endif
	}

	bool ShouldVisualizeHardwareRayTracing()
	{
		bool bVisualize = false;
#if RHI_RAYTRACING
		bVisualize = (CVarLumenVisualizeHardwareRayTracing.GetValueOnRenderThread() != 0) && IsRayTracingEnabled();
#endif
		return bVisualize;
	}
}

#if RHI_RAYTRACING

class FLumenVisualizeHardwareRayTracingRGS : public FLumenHardwareRayTracingRGS
{
	DECLARE_GLOBAL_SHADER(FLumenVisualizeHardwareRayTracingRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FLumenVisualizeHardwareRayTracingRGS, FLumenHardwareRayTracingRGS)

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenHardwareRayTracingRGS::FSharedParameters, SharedParameters)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, RWRadiance)
		SHADER_PARAMETER(int, LightingMode)
		SHADER_PARAMETER(int, NormalMode)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FLumenVisualizeHardwareRayTracingRGS, "/Engine/Private/Lumen/LumenVisualizeHardwareRayTracing.usf", "LumenVisualizeHardwareRayTracingRGS", SF_RayGen);

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingVisualize(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	FLumenVisualizeHardwareRayTracingRGS::FPermutationDomain PermutationVector;

	TShaderRef<FLumenVisualizeHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenVisualizeHardwareRayTracingRGS>(PermutationVector);
	OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
}

void SetLumenHardwareRayTracingSharedParameters(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	const FLumenCardTracingInputs& TracingInputs,
	const FLumenMeshSDFGridParameters& MeshSDFGridParameters,
	FLumenHardwareRayTracingRGS::FSharedParameters* SharedParameters
)
{
	SharedParameters->SceneTextures = SceneTextures;
	//SharedParameters->ViewUniformBuffer = View.ViewUniformBuffer;
	SharedParameters->TLAS = View.RayTracingScene.RayTracingSceneRHI->GetShaderResourceView();

	// Lighting data
	SharedParameters->LightDataPacked = View.RayTracingLightData.UniformBuffer;
	SharedParameters->LightDataBuffer = View.RayTracingLightData.LightBufferSRV;
	SharedParameters->SSProfilesTexture = GraphBuilder.RegisterExternalTexture(View.RayTracingSubSurfaceProfileTexture);

	// Use surface cache, instead
	GetLumenCardTracingParameters(View, TracingInputs, SharedParameters->TracingParameters);
	SharedParameters->MeshSDFGridParameters = MeshSDFGridParameters;
}

#endif // RHI_RAYTRACING

void VisualizeHardwareRayTracing(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	const FLumenCardTracingInputs& TracingInputs,
	const FLumenMeshSDFGridParameters& MeshSDFGridParameters,
	FLumenIndirectTracingParameters& IndirectTracingParameters,
	const LumenRadianceCache::FRadianceCacheParameters& RadianceCacheParameters,
	FRDGTextureRef SceneColor
)
#if RHI_RAYTRACING
{
	FLumenVisualizeHardwareRayTracingRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenVisualizeHardwareRayTracingRGS::FParameters>();

	SetLumenHardwareRayTracingSharedParameters(
		GraphBuilder,
		SceneTextures,
		View,
		TracingInputs,
		MeshSDFGridParameters,
		&PassParameters->SharedParameters);

	// Constants!
	PassParameters->LightingMode = CVarLumenVisualizeHardwareRayTracingLightingMode.GetValueOnRenderThread();
	PassParameters->NormalMode = CVarLumenVisualizeHardwareRayTracingNormalMode.GetValueOnRenderThread();

	// Output..
	PassParameters->RWRadiance = GraphBuilder.CreateUAV(SceneColor);

	FLumenVisualizeHardwareRayTracingRGS::FPermutationDomain PermutationVector;
	TShaderRef<FLumenVisualizeHardwareRayTracingRGS> RayGenerationShader =
		View.ShaderMap->GetShader<FLumenVisualizeHardwareRayTracingRGS>(PermutationVector);
	ClearUnusedGraphResources(RayGenerationShader, PassParameters);

	auto RayTracingResolution = View.ViewRect.Size();
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("VisualizeHardwareRayTracing %ux%u LightingMode=%s", RayTracingResolution.X, RayTracingResolution.Y, TEXT("Lighting Mode name here!")),
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
#else
{
	unimplemented();
}
#endif