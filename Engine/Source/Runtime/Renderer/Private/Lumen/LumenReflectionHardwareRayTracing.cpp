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
	0,
	TEXT("Determines the tracing normal (Default = 0)\n")
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
	bool bUseDeferredMaterial = CVarLumenReflectionsHardwareRayTracingDeferredMaterial.GetValueOnRenderThread() != 0;
	int NormalMode = CVarLumenReflectionsHardwareRayTracingNormalMode.GetValueOnRenderThread();
	int LightingMode = CVarLumenReflectionsHardwareRayTracingLightingMode.GetValueOnRenderThread();

	FLumenReflectionHardwareRayTracingRGS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FDeferredMaterialModeDim>(bUseDeferredMaterial);
	PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FNormalModeDim>(NormalMode != 0);
	PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FLightingModeDim>(LightingMode);
	TShaderRef<FLumenReflectionHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingRGS>(PermutationVector);

	OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
}

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingReflectionsDeferredMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	FLumenReflectionHardwareRayTracingDeferredMaterialRGS::FPermutationDomain PermutationVector;
	TShaderRef<FLumenReflectionHardwareRayTracingDeferredMaterialRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingDeferredMaterialRGS>(PermutationVector);
	OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
}

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingReflectionsLumenMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	Lumen::EHardwareRayTracingLightingMode LightingMode = static_cast<Lumen::EHardwareRayTracingLightingMode>(CVarLumenReflectionsHardwareRayTracingLightingMode.GetValueOnRenderThread());
	bool bUseMinimalPayload = LightingMode == Lumen::EHardwareRayTracingLightingMode::LightingFromSurfaceCache;

	if (Lumen::UseHardwareRayTracedReflections() && bUseMinimalPayload)
	{
		FLumenReflectionHardwareRayTracingRGS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FLightingModeDim>(0);
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FNormalModeDim>(0);
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FDeferredMaterialModeDim>(0);
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
	FRDGBufferRef DeferredMaterialBuffer = GraphBuilder.CreateBuffer(Desc, TEXT("LumenVisualizeHardwareRayTracingDeferredMaterialBuffer"));

	Lumen::EHardwareRayTracingLightingMode LightingMode = static_cast<Lumen::EHardwareRayTracingLightingMode>(CVarLumenReflectionsHardwareRayTracingLightingMode.GetValueOnRenderThread());
	bool bUseMinimalPayload = LightingMode == Lumen::EHardwareRayTracingLightingMode::LightingFromSurfaceCache;
	bool bUseDeferredMaterial = (CVarLumenReflectionsHardwareRayTracingDeferredMaterial.GetValueOnRenderThread() != 0) && !bUseMinimalPayload;
	if (bUseDeferredMaterial)
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
		//int LightingMode = CVarLumenReflectionsHardwareRayTracingLightingMode.GetValueOnRenderThread();
		int NormalMode = CVarLumenReflectionsHardwareRayTracingNormalMode.GetValueOnRenderThread();

		PassParameters->ReflectionTracingParameters = ReflectionTracingParameters;
		PassParameters->ReflectionTileParameters = ReflectionTileParameters;

		FLumenReflectionHardwareRayTracingRGS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FDeferredMaterialModeDim>(bUseDeferredMaterial);
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FNormalModeDim>(NormalMode != 0);
		PermutationVector.Set<FLumenReflectionHardwareRayTracingRGS::FLightingModeDim>(static_cast<int>(LightingMode));

		TShaderRef<FLumenReflectionHardwareRayTracingRGS> RayGenerationShader =
			View.ShaderMap->GetShader<FLumenReflectionHardwareRayTracingRGS>(PermutationVector);
		ClearUnusedGraphResources(RayGenerationShader, PassParameters);

		FIntPoint DispatchResolution = FIntPoint(ReflectionTracingParameters.ReflectionTracingViewSize.X * ReflectionTracingParameters.ReflectionTracingViewSize.Y, 1);
		if (bUseDeferredMaterial)
		{
			DispatchResolution = FIntPoint(DeferredMaterialBufferNumElements, 1);
		}
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("LumenReflectionHardwareRayTracingRGS %ux%u LightingMode=%s, DeferredMaterial=%u", DispatchResolution.X, DispatchResolution.Y, Lumen::GetRayTracedLightingModeName(LightingMode), bUseDeferredMaterial),
			PassParameters,
			ERDGPassFlags::Compute,
			[PassParameters, &View, RayGenerationShader, DispatchResolution, bUseMinimalPayload](FRHICommandList& RHICmdList)
			{
				FRayTracingShaderBindingsWriter GlobalResources;
				SetShaderParameters(GlobalResources, RayGenerationShader, *PassParameters);

				FRHIRayTracingScene* RayTracingSceneRHI = View.RayTracingScene.RayTracingSceneRHI;
				FRayTracingPipelineState* RayTracingPipeline = bUseMinimalPayload ? View.LumenHardwareRayTracingMaterialPipeline : View.RayTracingMaterialPipeline;

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
