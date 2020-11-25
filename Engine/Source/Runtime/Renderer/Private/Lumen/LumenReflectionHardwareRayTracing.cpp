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
#include "LumenReflections.h"

#if RHI_RAYTRACING
#include "RayTracing/RaytracingOptions.h"
#include "RayTracing/RayTracingLighting.h"
#include "LumenHardwareRayTracingCommon.h"

static TAutoConsoleVariable<int32> CVarLumenReflectionsHardwareRayTracing(
	TEXT("r.Lumen.Reflections.HardwareRayTracing"),
	0,
	TEXT("Enables hardware ray tracing for Lumen reflections (Default = 0)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenReflectionsHardwareRayTracingLightingMode(
	TEXT("r.Lumen.Reflections.HardwareRayTracing.LightingMode"),
	0,
	TEXT("Determines the lighting mode (Default = 0)\n")
	TEXT("0: interpolate final lighting from the surface cache\n")
	TEXT("1: evaluate material, and interpolate irradiance and indirect irradiance from the surface cache\n")
	TEXT("2: evaluate material and direct lighting, and interpolate indirect irradiance from the surface cache"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenReflectionsHardwareRayTracingNormalMode(
	TEXT("r.Lumen.Reflections.HardwareRayTracing.NormalMode"),
	0,
	TEXT("Determines the tracing normal (Default = 0)\n")
	TEXT("0: SDF normal\n")
	TEXT("1: Geometry normal"),
	ECVF_RenderThreadSafe
);
#endif // RHI_RAYTRACING

namespace Lumen
{
	bool UseHardwareRayTracedReflections()
	{
#if RHI_RAYTRACING
		return (CVarLumenReflectionsHardwareRayTracing.GetValueOnRenderThread() != 0) && IsRayTracingEnabled();
#else
		return false;
#endif
	}

	EHardwareRayTracingLightingMode GetReflectionsHardwareRayTracingLightingMode()
	{
#if RHI_RAYTRACING
		return EHardwareRayTracingLightingMode(CVarLumenReflectionsHardwareRayTracingLightingMode.GetValueOnRenderThread());
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
		default:
			checkf(0, TEXT("Unhandled EHardwareRayTracingLightingMode"));
		}
		return nullptr;
	}
}

#if RHI_RAYTRACING
class FLumenReflectionHardwareRayTracingRGS : public FLumenHardwareRayTracingRGS
{
	DECLARE_GLOBAL_SHADER(FLumenReflectionHardwareRayTracingRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FLumenReflectionHardwareRayTracingRGS, FLumenHardwareRayTracingRGS)

	class FNormalModeDim : SHADER_PERMUTATION_BOOL("DIM_NORMAL_MODE");
	class FLightingModeDim : SHADER_PERMUTATION_INT("DIM_LIGHTING_MODE", static_cast<int32>(Lumen::EHardwareRayTracingLightingMode::MAX));
	using FPermutationDomain = TShaderPermutationDomain<FNormalModeDim, FLightingModeDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenHardwareRayTracingRGS::FSharedParameters, SharedParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FCompactedReflectionTraceParameters, CompactedTraceParameters)
		
		// Constants
		SHADER_PARAMETER(float, MaxTraceDistance)

		// Reflection-specific includes (includes output targets)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenReflectionTracingParameters, ReflectionTracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenReflectionTileParameters, ReflectionTileParameters)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FLumenHardwareRayTracingRGS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_DISPATCH_1D"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenReflectionHardwareRayTracingRGS, "/Engine/Private/Lumen/LumenReflectionHardwareRayTracing.usf", "LumenReflectionHardwareRayTracingRGS", SF_RayGen);

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingReflections(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	FLumenReflectionHardwareRayTracingRGS::FPermutationDomain PermutationVector;

	for (int32 LightingMode = 0; LightingMode < static_cast<int32>(Lumen::EHardwareRayTracingLightingMode::MAX); ++LightingMode)
	{
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FLightingModeDim>(LightingMode);

		for (int32 NormalMode = 0; NormalMode < 2; ++NormalMode)
		{
			PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FNormalModeDim>(NormalMode != 0);

			TShaderRef<FLumenReflectionHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingRGS>(PermutationVector);
			OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
		}
	}
}
#endif

void RenderLumenHardwareRayTracingReflections(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	const FLumenReflectionTracingParameters& ReflectionTracingParameters,
	const FLumenReflectionTileParameters& ReflectionTileParameters,
	const FLumenCardTracingInputs& TracingInputs,
	const FLumenMeshSDFGridParameters& MeshSDFGridParameters,
	const FCompactedReflectionTraceParameters& CompactedTraceParameters,
	float MaxVoxelTraceDistance
)
{
#if RHI_RAYTRACING
	FLumenReflectionHardwareRayTracingRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenReflectionHardwareRayTracingRGS::FParameters>();
	SetLumenHardwareRayTracingSharedParameters(
		GraphBuilder,
		SceneTextures,
		View,
		TracingInputs,
		MeshSDFGridParameters,
		&PassParameters->SharedParameters
	);
	PassParameters->CompactedTraceParameters = CompactedTraceParameters;

	PassParameters->MaxTraceDistance = MaxVoxelTraceDistance;
	int LightingMode = CVarLumenReflectionsHardwareRayTracingLightingMode.GetValueOnRenderThread();
	int NormalMode = CVarLumenReflectionsHardwareRayTracingNormalMode.GetValueOnRenderThread();

	PassParameters->ReflectionTracingParameters = ReflectionTracingParameters;
	PassParameters->ReflectionTileParameters = ReflectionTileParameters;

	FLumenReflectionHardwareRayTracingRGS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FNormalModeDim>(NormalMode != 0);
	PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FLightingModeDim>(LightingMode);

	TShaderRef<FLumenReflectionHardwareRayTracingRGS> RayGenerationShader =
		View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingRGS>(PermutationVector);
	ClearUnusedGraphResources(RayGenerationShader, PassParameters);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HardwareRayTracing %ux%u LightingMode=%s", ReflectionTracingParameters.ReflectionTracingViewSize.X, ReflectionTracingParameters.ReflectionTracingViewSize.Y, Lumen::GetRayTracedLightingModeName((Lumen::EHardwareRayTracingLightingMode)LightingMode)),
		PassParameters,
		ERDGPassFlags::Compute,
		[PassParameters, &View, RayGenerationShader, ReflectionTracingParameters](FRHICommandList& RHICmdList)
		{
			FRayTracingShaderBindingsWriter GlobalResources;
			SetShaderParameters(GlobalResources, RayGenerationShader, *PassParameters);

			const uint32 NumTracingThreads = ReflectionTracingParameters.ReflectionTracingViewSize.X * ReflectionTracingParameters.ReflectionTracingViewSize.Y;
			FRHIRayTracingScene* RayTracingSceneRHI = View.RayTracingScene.RayTracingSceneRHI;
			RHICmdList.RayTraceDispatch(View.RayTracingMaterialPipeline, RayGenerationShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, NumTracingThreads, 1);
		}
	);
#else
	unimplemented();
#endif // RHI_RAYTRACING
}
