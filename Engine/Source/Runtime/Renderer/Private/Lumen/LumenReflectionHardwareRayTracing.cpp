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

static TAutoConsoleVariable<int32> CVarLumenReflectionsHardwareRayTracingNormalType(
	TEXT("r.Lumen.Reflections.HardwareRayTracing.NormalMode"),
	0,
	TEXT("Determines the tracing normal (Default = 0)\n")
	TEXT("0: SDF normal\n")
	TEXT("1: Geometry normal"),
	ECVF_RenderThreadSafe
);
#endif

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

	EHardwareRayTracedReflectionsLightingMode GetHardwareRayTracedReflectionsLightingMode()
	{
#if RHI_RAYTRACING
		return EHardwareRayTracedReflectionsLightingMode(CVarLumenReflectionsHardwareRayTracingLightingMode.GetValueOnRenderThread());
#else
		return EHardwareRayTracedReflectionsLightingMode::LightingFromSurfaceCache;
#endif
	}

	const TCHAR* GetLightingModeName(EHardwareRayTracedReflectionsLightingMode LightingMode)
	{
		switch (LightingMode)
		{
		case EHardwareRayTracedReflectionsLightingMode::LightingFromSurfaceCache:
			return TEXT("LightingFromSurfaceCache");
		case EHardwareRayTracedReflectionsLightingMode::EvaluateMaterial:
			return TEXT("EvaluateMaterial");
		case EHardwareRayTracedReflectionsLightingMode::EvaluateMaterialAndDirectLighting:
			return TEXT("EvaluateMaterialAndDirectLighting");
		default:
			checkf(0, TEXT("Unhandled EHardwareRayTracedReflectionsLightingMode"));
		}
		return nullptr;
	}
}

#if RHI_RAYTRACING
class FLumenReflectionHardwareRayTracingRGS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenReflectionHardwareRayTracingRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FLumenReflectionHardwareRayTracingRGS, FGlobalShader)

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// Scene includes
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		//SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)

		// Lighting structures
		SHADER_PARAMETER_STRUCT_REF(FRaytracingLightDataPacked, LightDataPacked)
		SHADER_PARAMETER_SRV(StructuredBuffer<FRTLightingData>, LightDataBuffer)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SSProfilesTexture)

		// Surface cache
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenMeshSDFGridParameters, MeshSDFGridParameters)
		
		// Constants
		SHADER_PARAMETER(float, MaxTraceDistance)
		SHADER_PARAMETER(int, LightingMode)
		SHADER_PARAMETER(int, NormalType)

		// Reflection-specific includes (includes output targets)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenReflectionTracingParameters, ReflectionTracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenReflectionTileParameters, ReflectionTileParameters)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		bool bEnableMissShaderLighting = false;
		OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_DYNAMIC_MISS_SHADER"), bEnableMissShaderLighting);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);//DoesPlatformSupportLumenGI(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenReflectionHardwareRayTracingRGS, "/Engine/Private/Lumen/LumenReflectionHardwareRayTracing.usf", "LumenReflectionHardwareRayTracingRGS", SF_RayGen);

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingReflections(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	FLumenReflectionHardwareRayTracingRGS::FPermutationDomain PermutationVector;

	TShaderRef<FLumenReflectionHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingRGS>(PermutationVector);
	OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
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
	float MaxCardTraceDistance,
	float MaxVoxelTraceDistance
)
{
#if RHI_RAYTRACING
	FLumenReflectionHardwareRayTracingRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenReflectionHardwareRayTracingRGS::FParameters>();
	PassParameters->SceneTextures = SceneTextures;
	PassParameters->TLAS = View.RayTracingScene.RayTracingSceneRHI->GetShaderResourceView();

	// Lighting data
	PassParameters->LightDataPacked = View.RayTracingLightData.UniformBuffer;
	PassParameters->LightDataBuffer = View.RayTracingLightData.LightBufferSRV;
	PassParameters->SSProfilesTexture = GraphBuilder.RegisterExternalTexture(View.RayTracingSubSurfaceProfileTexture);
	PassParameters->LightingMode = CVarLumenReflectionsHardwareRayTracingLightingMode.GetValueOnRenderThread();
	PassParameters->NormalType = CVarLumenReflectionsHardwareRayTracingNormalType.GetValueOnRenderThread();

	// Use surface cache, instead
	GetLumenCardTracingParameters(View, TracingInputs, PassParameters->TracingParameters);
	PassParameters->MeshSDFGridParameters = MeshSDFGridParameters;

	PassParameters->ReflectionTracingParameters = ReflectionTracingParameters;
	PassParameters->ReflectionTileParameters = ReflectionTileParameters;
	PassParameters->MaxTraceDistance = MaxVoxelTraceDistance;

	FLumenReflectionHardwareRayTracingRGS::FPermutationDomain PermutationVector;
	TShaderRef<FLumenReflectionHardwareRayTracingRGS> RayGenerationShader =
		View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingRGS>(PermutationVector);
	ClearUnusedGraphResources(RayGenerationShader, PassParameters);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HardwareRayTracing %ux%u LightingMode=%s", ReflectionTracingParameters.ReflectionTracingViewSize.X, ReflectionTracingParameters.ReflectionTracingViewSize.Y, Lumen::GetLightingModeName((Lumen::EHardwareRayTracedReflectionsLightingMode)PassParameters->LightingMode)),
		PassParameters,
		ERDGPassFlags::Compute,
		[PassParameters, &View, RayGenerationShader, ReflectionTracingParameters](FRHICommandList& RHICmdList)
		{
			FRayTracingShaderBindingsWriter GlobalResources;
			SetShaderParameters(GlobalResources, RayGenerationShader, *PassParameters);

			FRHIRayTracingScene* RayTracingSceneRHI = View.RayTracingScene.RayTracingSceneRHI;
			RHICmdList.RayTraceDispatch(View.RayTracingMaterialPipeline, RayGenerationShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, ReflectionTracingParameters.ReflectionTracingViewSize.X, ReflectionTracingParameters.ReflectionTracingViewSize.Y);
		}
	);
#else
	unimplemented();
#endif // RHI_RAYTRACING
}
