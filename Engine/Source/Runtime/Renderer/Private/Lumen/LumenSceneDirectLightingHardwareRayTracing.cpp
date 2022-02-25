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
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, LightTileAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint2>, LightTiles)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FLumenPackedLight>, LumenPackedLights)

		// Constants
		SHADER_PARAMETER(float, PullbackBias)
		SHADER_PARAMETER(int, MaxTranslucentSkipCount)
		SHADER_PARAMETER(uint32, MaxTraversalIterations)
		SHADER_PARAMETER(uint32, GroupCount)
		SHADER_PARAMETER(float, MaxTraceDistance)
		SHADER_PARAMETER(float, FarFieldMaxTraceDistance)
		SHADER_PARAMETER(FVector3f, FarFieldReferencePos)

		SHADER_PARAMETER(float, HardwareRayTracingShadowRayBias)
		SHADER_PARAMETER(float, HeightfieldShadowReceiverBias)

		// Output
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWShadowMaskTiles)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FLumenHardwareRayTracingRGS::ModifyCompilationEnvironment(Parameters, Lumen::ESurfaceCacheSampling::AlwaysResidentPagesWithoutFeedback, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_LIGHTWEIGHT_CLOSEST_HIT_SHADER"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenDirectLightingHardwareRayTracingBatchedRGS, "/Engine/Private/Lumen/LumenSceneDirectLightingHardwareRayTracing.usf", "LumenSceneDirectLightingHardwareRayTracingRGS", SF_RayGen);

class FLumenDirectLightingHardwareRayTracingBatchedCS : public FLumenHardwareRayTracingCS
{
	DECLARE_GLOBAL_SHADER(FLumenDirectLightingHardwareRayTracingBatchedCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenDirectLightingHardwareRayTracingBatchedCS, FLumenHardwareRayTracingCS)

	class FEnableFarFieldTracing : SHADER_PERMUTATION_BOOL("ENABLE_FAR_FIELD_TRACING");
	class FIndirectDispatchDim : SHADER_PERMUTATION_BOOL("DIM_INDIRECT_DISPATCH");
	using FPermutationDomain = TShaderPermutationDomain<FEnableFarFieldTracing, FIndirectDispatchDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenDirectLightingHardwareRayTracingBatchedRGS::FParameters, CommonParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenHardwareRayTracingCS::FInlineParameters, InlineParameters)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return FLumenHardwareRayTracingCS::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FLumenHardwareRayTracingCS::ModifyCompilationEnvironment(Parameters, Lumen::ESurfaceCacheSampling::AlwaysResidentPagesWithoutFeedback, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("INLINE_RAY_TRACING_THREAD_GROUP_SIZE_X"), ThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("INLINE_RAY_TRACING_THREAD_GROUP_SIZE_Y"), ThreadGroupSizeY);
	}

	// Current inline ray tracing implementation requires 1:1 mapping between thread groups and waves and only supports wave32 mode.
	static constexpr uint32 ThreadGroupSizeX = 8;
	static constexpr uint32 ThreadGroupSizeY = 4;
};

IMPLEMENT_GLOBAL_SHADER(FLumenDirectLightingHardwareRayTracingBatchedCS, "/Engine/Private/Lumen/LumenSceneDirectLightingHardwareRayTracing.usf", "LumenSceneDirectLightingHardwareRayTracingCS", SF_Compute);

class FLumenDirectLightingHardwareRayTracingIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenDirectLightingHardwareRayTracingIndirectArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenDirectLightingHardwareRayTracingIndirectArgsCS, FGlobalShader)

	class FInlineRaytracing : SHADER_PERMUTATION_BOOL("DIM_INLINE_RAYTRACING");
	using FPermutationDomain = TShaderPermutationDomain<FInlineRaytracing>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, DispatchLightTilesIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWHardwareRayTracingIndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("INLINE_RAY_TRACING_THREAD_GROUP_SIZE_X"), FLumenDirectLightingHardwareRayTracingBatchedCS::ThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("INLINE_RAY_TRACING_THREAD_GROUP_SIZE_Y"), FLumenDirectLightingHardwareRayTracingBatchedCS::ThreadGroupSizeY);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenDirectLightingHardwareRayTracingIndirectArgsCS, "/Engine/Private/Lumen/LumenSceneDirectLightingHardwareRayTracing.usf", "LumenDirectLightingHardwareRayTracingIndirectArgsCS", SF_Compute);

bool IsHardwareRayTracedDirectLightingIndirectDispatch()
{
	return GRHISupportsRayTracingDispatchIndirect && (CVarLumenSceneDirectLightingHardwareRayTracingIndirect.GetValueOnRenderThread() == 1);
}

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingDirectLightingLumenMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	if (Lumen::UseHardwareRayTracedDirectLighting())
	{
		FLumenDirectLightingHardwareRayTracingBatchedRGS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenDirectLightingHardwareRayTracingBatchedRGS::FEnableFarFieldTracing>(Lumen::UseFarField(*View.Family));
		PermutationVector.Set<FLumenDirectLightingHardwareRayTracingBatchedRGS::FIndirectDispatchDim>(IsHardwareRayTracedDirectLightingIndirectDispatch());
		TShaderRef<FLumenDirectLightingHardwareRayTracingBatchedRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenDirectLightingHardwareRayTracingBatchedRGS>(PermutationVector);
		OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
	}
}

void SetLumenHardwareRayTracedDirectLightingShadowsParameters(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FLumenCardTracingInputs& TracingInputs,
	FRDGBufferRef LightTileAllocator,
	FRDGBufferRef LightTiles,
	FRDGBufferRef LumenPackedLights,
	FRDGBufferUAVRef ShadowMaskTilesUAV,
	FRDGBufferRef HardwareRayTracingIndirectArgsBuffer,
	FLumenDirectLightingHardwareRayTracingBatchedRGS::FParameters* Parameters
)
{
	SetLumenHardwareRayTracingSharedParameters(
		GraphBuilder,
		GetSceneTextureParameters(GraphBuilder),
		View,
		TracingInputs,
		&Parameters->SharedParameters
	);

	Parameters->HardwareRayTracingIndirectArgs = HardwareRayTracingIndirectArgsBuffer;
	Parameters->LightTileAllocator = GraphBuilder.CreateSRV(LightTileAllocator);
	Parameters->LightTiles = GraphBuilder.CreateSRV(LightTiles);
	Parameters->LumenPackedLights = GraphBuilder.CreateSRV(LumenPackedLights);

	Parameters->PullbackBias = 0.0f;
	Parameters->MaxTranslucentSkipCount = Lumen::GetMaxTranslucentSkipCount();
	Parameters->MaxTraversalIterations = LumenHardwareRayTracing::GetMaxTraversalIterations();
	Parameters->GroupCount = FMath::Max(CVarLumenSceneDirectLightingHardwareRayTracingGroupCount.GetValueOnRenderThread(), 1);
	Parameters->MaxTraceDistance = Lumen::GetSurfaceCacheOffscreenShadowingMaxTraceDistance(Lumen::UseFarField(*View.Family) ? (float)WORLD_MAX : View.FinalPostProcessSettings.LumenMaxTraceDistance);
	Parameters->FarFieldMaxTraceDistance = Lumen::GetFarFieldMaxTraceDistance();
	Parameters->FarFieldReferencePos = (FVector3f)Lumen::GetFarFieldReferencePos();
	
	Parameters->HardwareRayTracingShadowRayBias = LumenSceneDirectLighting::GetHardwareRayTracingShadowRayBias();
	Parameters->HeightfieldShadowReceiverBias = LumenSceneDirectLighting::GetHeightfieldShadowReceiverBias();

	// Output
	Parameters->RWShadowMaskTiles = ShadowMaskTilesUAV;
}

#endif // RHI_RAYTRACING

void TraceLumenHardwareRayTracedDirectLightingShadows(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	const FLumenCardTracingInputs& TracingInputs,
	FRDGBufferRef DispatchLightTilesIndirectArgs,
	FRDGBufferRef LightTileAllocator,
	FRDGBufferRef LightTiles,
	FRDGBufferRef LumenPackedLights,
	FRDGBufferUAVRef ShadowMaskTilesUAV)
{
#if RHI_RAYTRACING
	const bool bInlineRayTracing = Lumen::UseHardwareInlineRayTracing();

	FRDGBufferRef HardwareRayTracingIndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Lumen.Reflection.CompactTracingIndirectArgs"));
	if (IsHardwareRayTracedDirectLightingIndirectDispatch())
	{
		FLumenDirectLightingHardwareRayTracingIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenDirectLightingHardwareRayTracingIndirectArgsCS::FParameters>();
		{
			PassParameters->DispatchLightTilesIndirectArgs = GraphBuilder.CreateSRV(DispatchLightTilesIndirectArgs, PF_R32_UINT);
			PassParameters->RWHardwareRayTracingIndirectArgs = GraphBuilder.CreateUAV(HardwareRayTracingIndirectArgsBuffer, PF_R32_UINT);
		}

		FLumenDirectLightingHardwareRayTracingIndirectArgsCS::FPermutationDomain IndirectPermutationVector;
		IndirectPermutationVector.Set<FLumenDirectLightingHardwareRayTracingIndirectArgsCS::FInlineRaytracing>(bInlineRayTracing);
		TShaderRef<FLumenDirectLightingHardwareRayTracingIndirectArgsCS> ComputeShader = View.ShaderMap->GetShader<FLumenDirectLightingHardwareRayTracingIndirectArgsCS>(IndirectPermutationVector);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("FLumenDirectLightingHardwareRayTracingIndirectArgsCS"),
			ComputeShader,
			PassParameters,
			FIntVector(1, 1, 1));
	}

	if (bInlineRayTracing)
	{
		FLumenDirectLightingHardwareRayTracingBatchedCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenDirectLightingHardwareRayTracingBatchedCS::FParameters>();
		SetLumenHardwareRayTracedDirectLightingShadowsParameters(
			GraphBuilder,
			View,
			TracingInputs,
			LightTileAllocator,
			LightTiles,
			LumenPackedLights,
			ShadowMaskTilesUAV,
			HardwareRayTracingIndirectArgsBuffer,
			&PassParameters->CommonParameters
		);
		PassParameters->InlineParameters.HitGroupData = View.LumenHardwareRayTracingHitDataBufferSRV;

		FLumenDirectLightingHardwareRayTracingBatchedCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenDirectLightingHardwareRayTracingBatchedCS::FEnableFarFieldTracing>(Lumen::UseFarField(*View.Family));
		PermutationVector.Set<FLumenDirectLightingHardwareRayTracingBatchedCS::FIndirectDispatchDim>(IsHardwareRayTracedDirectLightingIndirectDispatch());
		TShaderRef<FLumenDirectLightingHardwareRayTracingBatchedCS> ComputeShader = View.ShaderMap->GetShader<FLumenDirectLightingHardwareRayTracingBatchedCS>(PermutationVector);

		ClearUnusedGraphResources(ComputeShader, PassParameters);

		FIntPoint DispatchResolution = FIntPoint(Lumen::CardTileSize * Lumen::CardTileSize, PassParameters->CommonParameters.GroupCount);
		FString Resolution = FString::Printf(TEXT("%ux%u"), DispatchResolution.X, DispatchResolution.Y);
		if (IsHardwareRayTracedDirectLightingIndirectDispatch())
		{
			Resolution = FString::Printf(TEXT("<indirect>"));
		}

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("LumenDirectLightingHardwareInlineRayTracingCS %s", *Resolution),
			PassParameters,
			ERDGPassFlags::Compute,
			[PassParameters, &View, ComputeShader, DispatchResolution](FRHIRayTracingCommandList& RHICmdList)
			{
				FRHIComputeShader* ShaderRHI = ComputeShader.GetComputeShader();
				SetComputePipelineState(RHICmdList, ShaderRHI);
				SetShaderParameters(RHICmdList, ComputeShader, ShaderRHI, *PassParameters);

				if (IsHardwareRayTracedDirectLightingIndirectDispatch())
				{
					DispatchIndirectComputeShader(RHICmdList, ComputeShader.GetShader(), PassParameters->CommonParameters.HardwareRayTracingIndirectArgs->GetIndirectRHICallBuffer(), 0);
				}
				else
				{
					const FIntPoint GroupSize(FLumenDirectLightingHardwareRayTracingBatchedCS::ThreadGroupSizeX, FLumenDirectLightingHardwareRayTracingBatchedCS::ThreadGroupSizeY);
					const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(DispatchResolution, GroupSize);
					DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), GroupCount.X, GroupCount.Y, 1);
				}

				UnsetShaderUAVs(RHICmdList, ComputeShader, ShaderRHI);
			}
		);
	}
	else
	{
		FLumenDirectLightingHardwareRayTracingBatchedRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenDirectLightingHardwareRayTracingBatchedRGS::FParameters>();
		SetLumenHardwareRayTracedDirectLightingShadowsParameters(
			GraphBuilder,
			View,
			TracingInputs,
			LightTileAllocator,
			LightTiles,
			LumenPackedLights,
			ShadowMaskTilesUAV,
			HardwareRayTracingIndirectArgsBuffer,
			PassParameters
		);

		FLumenDirectLightingHardwareRayTracingBatchedRGS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenDirectLightingHardwareRayTracingBatchedRGS::FEnableFarFieldTracing>(Lumen::UseFarField(*View.Family));
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
			RDG_EVENT_NAME("LumenDirectLightingHardwareRayTracingRGS %s", *Resolution),
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
	}

	
#else
	unimplemented();
#endif // RHI_RAYTRACING
}