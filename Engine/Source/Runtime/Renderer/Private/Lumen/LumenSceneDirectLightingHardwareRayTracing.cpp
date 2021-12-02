// Copyright Epic Games, Inc. All Rights Reserved.

#include "LumenSceneLighting.h"
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "VolumeLighting.h"
#include "DistanceFieldLightingShared.h"
#include "VirtualShadowMaps/VirtualShadowMapClipmap.h"
#include "VolumetricCloudRendering.h"

#if RHI_RAYTRACING

#include "RayTracing/RayTracingDeferredMaterials.h"
#include "RayTracing/RaytracingOptions.h"
#include "RayTracing/RayTracingLighting.h"
#include "LumenHardwareRayTracingCommon.h"

// Console variables
static TAutoConsoleVariable<int32> CVarLumenSceneDirectLightingHardwareRayTracing(
	TEXT("r.LumenScene.DirectLighting.HardwareRayTracing"),
	1,
	TEXT("Enables hardware ray tracing for Lumen direct lighting (Default = 1)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenSceneDirectLightingHardwareRayTracingIndirect(
	TEXT("r.LumenScene.DirectLighting.HardwareRayTracing.Indirect"),
	1,
	TEXT("Enables indirect dispatch for hardware ray tracing (Default = 1)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int> CVarLumenSceneDirectLightingHardwareRayTracingGroupCount(
	TEXT("r.LumenScene.DirectLighting.HardwareRayTracing.GroupCount"),
	8192,
	TEXT("Determines the dispatch group count\n"),
	ECVF_RenderThreadSafe
);

#endif // RHI_RAYTRACING

namespace Lumen
{
	bool UseHardwareRayTracedDirectLighting()
	{
#if RHI_RAYTRACING
		return IsRayTracingEnabled()
			&& Lumen::UseHardwareRayTracing()
			&& (CVarLumenSceneDirectLightingHardwareRayTracing.GetValueOnRenderThread() != 0);
#else
		return false;
#endif
	}
} // namespace Lumen

#if RHI_RAYTRACING

class FLumenDirectLightingHardwareRayTracingIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenDirectLightingHardwareRayTracingIndirectArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenDirectLightingHardwareRayTracingIndirectArgsCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTileScatterParameters, CardScatterParameters)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWHardwareRayTracingIndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenDirectLightingHardwareRayTracingIndirectArgsCS, "/Engine/Private/Lumen/LumenSceneDirectLightingHardwareRayTracing.usf", "FLumenDirectLightingHardwareRayTracingIndirectArgsCS", SF_Compute);


class FLumenDirectLightingHardwareRayTracingBatchedRGS : public FLumenHardwareRayTracingRGS
{
	DECLARE_GLOBAL_SHADER(FLumenDirectLightingHardwareRayTracingBatchedRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FLumenDirectLightingHardwareRayTracingBatchedRGS, FLumenHardwareRayTracingRGS)

	class FEnableFarFieldTracing : SHADER_PERMUTATION_BOOL("ENABLE_FAR_FIELD_TRACING");
	class FIndirectDispatchDim : SHADER_PERMUTATION_BOOL("DIM_INDIRECT_DISPATCH");
	using FPermutationDomain = TShaderPermutationDomain<FEnableFarFieldTracing, FIndirectDispatchDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenHardwareRayTracingRGS::FSharedParameters, SharedParameters)
		RDG_BUFFER_ACCESS(HardwareRayTracingIndirectArgs, ERHIAccess::IndirectArgs | ERHIAccess::SRVCompute)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTileScatterParameters, CardScatterParameters)
		SHADER_PARAMETER(uint32, CardScatterInstanceIndex)
		SHADER_PARAMETER_STRUCT_REF(FDeferredLightUniformStruct, DeferredLightUniforms)

		// Constants
		SHADER_PARAMETER(uint32, LumenLightType)
		SHADER_PARAMETER(float, PullbackBias)
		SHADER_PARAMETER(int, MaxTranslucentSkipCount)
		SHADER_PARAMETER(uint32, GroupCount)
		SHADER_PARAMETER(float, MaxTraceDistance)
		SHADER_PARAMETER(float, FarFieldMaxTraceDistance)
		SHADER_PARAMETER(FVector3f, FarFieldReferencePos)

		SHADER_PARAMETER(float, SurfaceBias)
		SHADER_PARAMETER(float, SlopeScaledSurfaceBias)

		// Output
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWShadowMaskTiles)
		SHADER_PARAMETER(uint32, ShadowMaskTilesOffset)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FLumenHardwareRayTracingRGS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_LIGHTWEIGHT_CLOSEST_HIT_SHADER"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenDirectLightingHardwareRayTracingBatchedRGS, "/Engine/Private/Lumen/LumenSceneDirectLightingHardwareRayTracing.usf", "LumenSceneDirectLightingHardwareRayTracingRGS", SF_RayGen);

bool IsHardwareRayTracedDirectLightingIndirectDispatch()
{
	return GRHISupportsRayTracingDispatchIndirect && (CVarLumenSceneDirectLightingHardwareRayTracingIndirect.GetValueOnRenderThread() == 1);
}

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingDirectLightingLumenMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	if (Lumen::UseHardwareRayTracedDirectLighting())
	{
		FLumenDirectLightingHardwareRayTracingBatchedRGS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenDirectLightingHardwareRayTracingBatchedRGS::FEnableFarFieldTracing>(Lumen::UseFarField());
		PermutationVector.Set<FLumenDirectLightingHardwareRayTracingBatchedRGS::FIndirectDispatchDim>(IsHardwareRayTracedDirectLightingIndirectDispatch());
		TShaderRef<FLumenDirectLightingHardwareRayTracingBatchedRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenDirectLightingHardwareRayTracingBatchedRGS>(PermutationVector);
		OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
	}
}

#endif // RHI_RAYTRACING

void TraceLumenHardwareRayTracedDirectLightingShadows(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	const FLumenCardTracingInputs& TracingInputs,
	const FLumenLight& LumenLight,
	const FLumenCardScatterContext& CardScatterContext,
	FRDGBufferUAVRef ShadowMaskTilesUAV)
{
#if RHI_RAYTRACING
	check(LumenLight.HasShadowMask());

	FRDGBufferRef HardwareRayTracingIndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Lumen.Reflection.CompactTracingIndirectArgs"));
	if (IsHardwareRayTracedDirectLightingIndirectDispatch())
	{
		FLumenDirectLightingHardwareRayTracingIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenDirectLightingHardwareRayTracingIndirectArgsCS::FParameters>();
		{
			PassParameters->CardScatterParameters = CardScatterContext.CardTileParameters;
			PassParameters->RWHardwareRayTracingIndirectArgs = GraphBuilder.CreateUAV(HardwareRayTracingIndirectArgsBuffer, PF_R32_UINT);
		}

		TShaderRef<FLumenDirectLightingHardwareRayTracingIndirectArgsCS> ComputeShader = View.ShaderMap->GetShader<FLumenDirectLightingHardwareRayTracingIndirectArgsCS>();
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("FLumenDirectLightingHardwareRayTracingIndirectArgsCS"),
			ComputeShader,
			PassParameters,
			FIntVector(1, 1, 1));
	}

	FLumenDirectLightingHardwareRayTracingBatchedRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenDirectLightingHardwareRayTracingBatchedRGS::FParameters>();
	SetLumenHardwareRayTracingSharedParameters(
		GraphBuilder,
		GetSceneTextureParameters(GraphBuilder),
		View,
		TracingInputs,
		&PassParameters->SharedParameters
	);

	PassParameters->CardScatterParameters = CardScatterContext.CardTileParameters;
	PassParameters->CardScatterInstanceIndex = LumenLight.CardScatterInstanceIndex;
	Lumen::SetDirectLightingDeferredLightUniformBuffer(View, LumenLight.LightSceneInfo, PassParameters->DeferredLightUniforms);
	PassParameters->HardwareRayTracingIndirectArgs = HardwareRayTracingIndirectArgsBuffer;

	PassParameters->PullbackBias = 0.0f;
	PassParameters->MaxTranslucentSkipCount = 1; // TODO: CVarLumenReflectionsHardwareRayTracingMaxTranslucentSkipCount.GetValueOnRenderThread();
	PassParameters->GroupCount = FMath::Max(CVarLumenSceneDirectLightingHardwareRayTracingGroupCount.GetValueOnRenderThread(), 1);
	PassParameters->MaxTraceDistance = Lumen::GetSurfaceCacheOffscreenShadowingMaxTraceDistance();
	PassParameters->FarFieldMaxTraceDistance = Lumen::GetFarFieldMaxTraceDistance();
	PassParameters->FarFieldReferencePos = Lumen::GetFarFieldReferencePos();

	PassParameters->LumenLightType = (uint32) LumenLight.Type;
	PassParameters->SurfaceBias = 1.0f;
	PassParameters->SlopeScaledSurfaceBias = 1.0f;

	// Output
	PassParameters->RWShadowMaskTiles = ShadowMaskTilesUAV;
	PassParameters->ShadowMaskTilesOffset = LumenLight.ShadowMaskTilesOffset;

	FLumenDirectLightingHardwareRayTracingBatchedRGS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FLumenDirectLightingHardwareRayTracingBatchedRGS::FEnableFarFieldTracing>(Lumen::UseFarField());
	PermutationVector.Set<FLumenDirectLightingHardwareRayTracingBatchedRGS::FIndirectDispatchDim>(IsHardwareRayTracedDirectLightingIndirectDispatch());
	TShaderRef<FLumenDirectLightingHardwareRayTracingBatchedRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenDirectLightingHardwareRayTracingBatchedRGS>(PermutationVector);

	ClearUnusedGraphResources(RayGenerationShader, PassParameters);

	FIntPoint DispatchResolution = FIntPoint(Lumen::CardTileSize * Lumen::CardTileSize, PassParameters->GroupCount);
	FString Resolution = FString::Printf(TEXT("%ux%u"), DispatchResolution.X, DispatchResolution.Y);
	if (IsHardwareRayTracedDirectLightingIndirectDispatch())
	{
		Resolution = FString::Printf(TEXT("<indirect>"));
	}

	FString LightName;
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("LumenDirectLightingHardwareRayTracingRGS %s %s", *LumenLight.Name, *Resolution),
		PassParameters,
		ERDGPassFlags::Compute,
		[PassParameters, &View, RayGenerationShader, DispatchResolution](FRHIRayTracingCommandList& RHICmdList)
		{
			FRayTracingShaderBindingsWriter GlobalResources;
			SetShaderParameters(GlobalResources, RayGenerationShader, *PassParameters);

			FRHIRayTracingScene* RayTracingSceneRHI = View.GetRayTracingSceneChecked();
			FRayTracingPipelineState* RayTracingPipeline = View.LumenHardwareRayTracingMaterialPipeline;

			if (IsHardwareRayTracedDirectLightingIndirectDispatch())
			{
				PassParameters->HardwareRayTracingIndirectArgs->MarkResourceAsUsed();
				RHICmdList.RayTraceDispatchIndirect(RayTracingPipeline, RayGenerationShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources,
					PassParameters->HardwareRayTracingIndirectArgs->GetIndirectRHICallBuffer(), 0);
			}
			else
			{
				RHICmdList.RayTraceDispatch(RayTracingPipeline, RayGenerationShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources,
					DispatchResolution.X, DispatchResolution.Y);
			}
		}
	);
#else
	unimplemented();
#endif // RHI_RAYTRACING
}