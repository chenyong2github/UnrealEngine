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
	TEXT("2: evaluate material and direct lighting, and interpolate indirect irradiance from the surface cache"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenReflectionsHardwareRayTracingNormalMode(
	TEXT("r.Lumen.Reflections.HardwareRayTracing.NormalMode"),
	1,
	TEXT("Determines the tracing normal (Default = 1)\n")
	TEXT("0: SDF normal\n")
	TEXT("1: Geometry normal"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenReflectionsHardwareRayTracingDeferredMaterial(
	TEXT("r.Lumen.Reflections.HardwareRayTracing.DeferredMaterial"),
	1,
	TEXT("Enables deferred material pipeline (Default = 1)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenReflectionsHardwareRayTracingDeferredMaterialTileSize(
	TEXT("r.Lumen.Reflections.HardwareRayTracing.DeferredMaterial.TileDimension"),
	64,
	TEXT("Determines the tile dimension for material sorting (Default = 64)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenReflectionsHardwareRayTracingMaxTranslucentSkipCount(
	TEXT("r.Lumen.Reflections.HardwareRayTracing.MaxTranslucentSkipCount"),
	2,
	TEXT("Determines the maximum number of translucent surfaces skipped during ray traversal (Default = 2)"),
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
		Lumen::EHardwareRayTracingLightingMode LightingMode = static_cast<Lumen::EHardwareRayTracingLightingMode>(FMath::Clamp<int32>(LightingModeCVar + ReflectionQualityLightingModeBias, 0, 2));
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
		default:
			checkf(0, TEXT("Unhandled EHardwareRayTracingLightingMode"));
		}
		return nullptr;
	}

#if RHI_RAYTRACING
	FHardwareRayTracingPermutationSettings GetReflectionsHardwareRayTracingPermutationSettings(const FViewInfo& View)
	{
		FHardwareRayTracingPermutationSettings ModesAndPermutationSettings;
		ModesAndPermutationSettings.LightingMode = GetReflectionsHardwareRayTracingLightingMode(View);
		ModesAndPermutationSettings.NormalMode = CVarLumenReflectionsHardwareRayTracingNormalMode.GetValueOnRenderThread();
		ModesAndPermutationSettings.bUseMinimalPayload = (ModesAndPermutationSettings.LightingMode == Lumen::EHardwareRayTracingLightingMode::LightingFromSurfaceCache);
		ModesAndPermutationSettings.bUseDeferredMaterial = (CVarLumenReflectionsHardwareRayTracingDeferredMaterial.GetValueOnRenderThread()) != 0 && !ModesAndPermutationSettings.bUseMinimalPayload;
		return ModesAndPermutationSettings;
	}
#endif
}

#if RHI_RAYTRACING
class FLumenReflectionHardwareRayTracingRGS : public FLumenHardwareRayTracingRGS
{
	DECLARE_GLOBAL_SHADER(FLumenReflectionHardwareRayTracingRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FLumenReflectionHardwareRayTracingRGS, FLumenHardwareRayTracingRGS)

	class FDeferredMaterialModeDim : SHADER_PERMUTATION_BOOL("DIM_DEFERRED_MATERIAL_MODE");
	class FNormalModeDim : SHADER_PERMUTATION_BOOL("DIM_NORMAL_MODE");
	class FLightingModeDim : SHADER_PERMUTATION_INT("DIM_LIGHTING_MODE", static_cast<int32>(Lumen::EHardwareRayTracingLightingMode::MAX));
	using FPermutationDomain = TShaderPermutationDomain<FDeferredMaterialModeDim, FNormalModeDim, FLightingModeDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenHardwareRayTracingRGS::FSharedParameters, SharedParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FCompactedReflectionTraceParameters, CompactedTraceParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FDeferredMaterialPayload>, DeferredMaterialBuffer)		
		SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, RayTraceDispatchIndirectArgs)

		// Constants
		SHADER_PARAMETER(float, MaxTraceDistance)
		SHADER_PARAMETER(int, MaxTranslucentSkipCount)

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

class FLumenReflectionHardwareRayTracingDeferredMaterialRGS : public FLumenHardwareRayTracingDeferredMaterialRGS
{
	DECLARE_GLOBAL_SHADER(FLumenReflectionHardwareRayTracingDeferredMaterialRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FLumenReflectionHardwareRayTracingDeferredMaterialRGS, FLumenHardwareRayTracingDeferredMaterialRGS)

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenHardwareRayTracingDeferredMaterialRGS::FDeferredMaterialParameters, DeferredMaterialParameters)

		SHADER_PARAMETER_STRUCT_INCLUDE(FCompactedReflectionTraceParameters, CompactedTraceParameters)
		SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, RayTraceDispatchIndirectArgs)

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
		OutEnvironment.SetDefine(TEXT("DIM_DEFERRED_MATERIAL_MODE"), 0);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenReflectionHardwareRayTracingDeferredMaterialRGS, "/Engine/Private/Lumen/LumenReflectionHardwareRayTracing.usf", "LumenReflectionHardwareRayTracingDeferredMaterialRGS", SF_RayGen);

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingReflections(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	if (Lumen::UseHardwareRayTracedReflections())
	{
		Lumen::FHardwareRayTracingPermutationSettings PermutationSettings = Lumen::GetReflectionsHardwareRayTracingPermutationSettings(View);

		FLumenReflectionHardwareRayTracingRGS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FDeferredMaterialModeDim>(PermutationSettings.bUseDeferredMaterial);
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FNormalModeDim>(PermutationSettings.NormalMode != 0);
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FLightingModeDim>(static_cast<int>(PermutationSettings.LightingMode));
		TShaderRef<FLumenReflectionHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingRGS>(PermutationVector);

		OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
	}
}

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingReflectionsDeferredMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	Lumen::FHardwareRayTracingPermutationSettings PermutationSettings = Lumen::GetReflectionsHardwareRayTracingPermutationSettings(View);

	if (Lumen::UseHardwareRayTracedReflections() && PermutationSettings.bUseDeferredMaterial)
	{
		FLumenReflectionHardwareRayTracingDeferredMaterialRGS::FPermutationDomain PermutationVector;
		TShaderRef<FLumenReflectionHardwareRayTracingDeferredMaterialRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingDeferredMaterialRGS>(PermutationVector);
		OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
	}
}

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingReflectionsLumenMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	Lumen::FHardwareRayTracingPermutationSettings PermutationSettings = Lumen::GetReflectionsHardwareRayTracingPermutationSettings(View);

	if (Lumen::UseHardwareRayTracedReflections() && PermutationSettings.bUseMinimalPayload)
	{
		FLumenReflectionHardwareRayTracingRGS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FLightingModeDim>(static_cast<int>(PermutationSettings.LightingMode));
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FNormalModeDim>(PermutationSettings.NormalMode != 0);
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FDeferredMaterialModeDim>(PermutationSettings.bUseDeferredMaterial);
		TShaderRef<FLumenReflectionHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingRGS>(PermutationVector);
		OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
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
	FIntPoint RayTracingResolution = ReflectionTracingParameters.ReflectionTracingViewSize;

	int TileSize = CVarLumenReflectionsHardwareRayTracingDeferredMaterialTileSize.GetValueOnRenderThread();
	FIntPoint DeferredMaterialBufferResolution = RayTracingResolution;
	DeferredMaterialBufferResolution = FIntPoint::DivideAndRoundUp(DeferredMaterialBufferResolution, TileSize) * TileSize;

	int DeferredMaterialBufferNumElements = DeferredMaterialBufferResolution.X * DeferredMaterialBufferResolution.Y;
	FRDGBufferDesc Desc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FDeferredMaterialPayload), DeferredMaterialBufferNumElements);
	FRDGBufferRef DeferredMaterialBuffer = GraphBuilder.CreateBuffer(Desc, TEXT("Lumen.Reflections.VisualizeHardwareRayTracingDeferredMaterialBuffer"));

	Lumen::FHardwareRayTracingPermutationSettings PermutationSettings = Lumen::GetReflectionsHardwareRayTracingPermutationSettings(View);
	if (PermutationSettings.bUseDeferredMaterial)
	{
		FLumenReflectionHardwareRayTracingDeferredMaterialRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenReflectionHardwareRayTracingDeferredMaterialRGS::FParameters>();
		SetLumenHardwareRayTracingSharedParameters(
			GraphBuilder,
			SceneTextures,
			View,
			TracingInputs,
			MeshSDFGridParameters,
			&PassParameters->DeferredMaterialParameters.SharedParameters);
		PassParameters->CompactedTraceParameters = CompactedTraceParameters;
		PassParameters->MaxTraceDistance = MaxVoxelTraceDistance;
		PassParameters->RayTraceDispatchIndirectArgs = CompactedTraceParameters.RayTraceDispatchIndirectArgs;

		PassParameters->ReflectionTracingParameters = ReflectionTracingParameters;
		PassParameters->ReflectionTileParameters = ReflectionTileParameters;

		// Compact tracing becomes a 1D buffer..
		DeferredMaterialBufferResolution = FIntPoint(DeferredMaterialBufferNumElements, 1);

		// Output..
		PassParameters->DeferredMaterialParameters.RWDeferredMaterialBuffer = GraphBuilder.CreateUAV(DeferredMaterialBuffer);
		PassParameters->DeferredMaterialParameters.DeferredMaterialBufferResolution = DeferredMaterialBufferResolution;
		PassParameters->DeferredMaterialParameters.TileSize = TileSize;

		// Permutation settings
		FLumenReflectionHardwareRayTracingDeferredMaterialRGS::FPermutationDomain PermutationVector;
		TShaderRef<FLumenReflectionHardwareRayTracingDeferredMaterialRGS> RayGenerationShader =
			View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingDeferredMaterialRGS>(PermutationVector);
		ClearUnusedGraphResources(RayGenerationShader, PassParameters);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("LumenReflectionHardwareRayTracingDeferredMaterialRGS %ux%u", DeferredMaterialBufferResolution.X, DeferredMaterialBufferResolution.Y),
			PassParameters,
			ERDGPassFlags::Compute,
			[PassParameters, &View, RayGenerationShader, DeferredMaterialBufferResolution](FRHICommandList& RHICmdList)
			{
				FRayTracingShaderBindingsWriter GlobalResources;
				SetShaderParameters(GlobalResources, RayGenerationShader, *PassParameters);

				FRHIRayTracingScene* RayTracingSceneRHI = View.RayTracingScene.RayTracingSceneRHI;

				if (GRHISupportsRayTracingDispatchIndirect && CVarLumenReflectionsHardwareRayTracingIndirect.GetValueOnRenderThread() == 1)
				{
					PassParameters->RayTraceDispatchIndirectArgs->MarkResourceAsUsed();
					RHICmdList.RayTraceDispatchIndirect(View.RayTracingMaterialGatherPipeline, RayGenerationShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources,
						PassParameters->RayTraceDispatchIndirectArgs->GetIndirectRHICallBuffer(), 0);
				}
				else
				{
					RHICmdList.RayTraceDispatch(View.RayTracingMaterialGatherPipeline, RayGenerationShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources,
						DeferredMaterialBufferResolution.X, DeferredMaterialBufferResolution.Y);
				}
			}
		);

		// Sort by material-id
		const uint32 SortSize = 5; // 4096 elements
		SortDeferredMaterials(GraphBuilder, View, SortSize, DeferredMaterialBufferNumElements, DeferredMaterialBuffer);
	}

	// Trace and shade
	{
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
		PassParameters->DeferredMaterialBuffer = GraphBuilder.CreateSRV(DeferredMaterialBuffer);
		PassParameters->RayTraceDispatchIndirectArgs = CompactedTraceParameters.RayTraceDispatchIndirectArgs;
		PassParameters->MaxTraceDistance = MaxVoxelTraceDistance;
		PassParameters->MaxTranslucentSkipCount = CVarLumenReflectionsHardwareRayTracingMaxTranslucentSkipCount.GetValueOnRenderThread();

		PassParameters->ReflectionTracingParameters = ReflectionTracingParameters;
		PassParameters->ReflectionTileParameters = ReflectionTileParameters;

		FLumenReflectionHardwareRayTracingRGS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FDeferredMaterialModeDim>(PermutationSettings.bUseDeferredMaterial);
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FNormalModeDim>(PermutationSettings.NormalMode != 0);
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FLightingModeDim>(static_cast<int>(PermutationSettings.LightingMode));

		TShaderRef<FLumenReflectionHardwareRayTracingRGS> RayGenerationShader =
			View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingRGS>(PermutationVector);
		ClearUnusedGraphResources(RayGenerationShader, PassParameters);

		FIntPoint DispatchResolution = FIntPoint(ReflectionTracingParameters.ReflectionTracingViewSize.X * ReflectionTracingParameters.ReflectionTracingViewSize.Y, 1);
		if (PermutationSettings.bUseDeferredMaterial)
		{
			DispatchResolution = FIntPoint(DeferredMaterialBufferNumElements, 1);
		}
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("LumenReflectionHardwareRayTracingRGS %ux%u LightingMode=%s, NormalMode=%s, DeferredMaterial=%u", DispatchResolution.X, DispatchResolution.Y, Lumen::GetRayTracedLightingModeName(PermutationSettings.LightingMode), Lumen::GetRayTracedNormalModeName(PermutationSettings.NormalMode), PermutationSettings.bUseDeferredMaterial),
			PassParameters,
			ERDGPassFlags::Compute,
			[PassParameters, &View, RayGenerationShader, DispatchResolution, PermutationSettings](FRHICommandList& RHICmdList)
			{
				FRayTracingShaderBindingsWriter GlobalResources;
				SetShaderParameters(GlobalResources, RayGenerationShader, *PassParameters);

				FRHIRayTracingScene* RayTracingSceneRHI = View.RayTracingScene.RayTracingSceneRHI;
				FRayTracingPipelineState* RayTracingPipeline = PermutationSettings.bUseMinimalPayload ? View.LumenHardwareRayTracingMaterialPipeline : View.RayTracingMaterialPipeline;

				if (GRHISupportsRayTracingDispatchIndirect && CVarLumenReflectionsHardwareRayTracingIndirect.GetValueOnRenderThread() == 1)
				{
					PassParameters->RayTraceDispatchIndirectArgs->MarkResourceAsUsed();
					RHICmdList.RayTraceDispatchIndirect(RayTracingPipeline, RayGenerationShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources,
						PassParameters->RayTraceDispatchIndirectArgs->GetIndirectRHICallBuffer(), 0);
				}
				else
				{
					RHICmdList.RayTraceDispatch(RayTracingPipeline, RayGenerationShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources,
						DispatchResolution.X, DispatchResolution.Y);
				}
			}
		);
	}
#else
	unimplemented();
#endif // RHI_RAYTRACING
}
