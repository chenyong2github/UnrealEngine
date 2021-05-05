// Copyright Epic Games, Inc. All Rights Reserved.

#include "InstanceCullingManager.h"
#include "CoreMinimal.h"
#include "RHI.h"
#include "RendererModule.h"
#include "ShaderParameterMacros.h"
#include "RenderGraphResources.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneRendering.h"
#include "ScenePrivate.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

#include "InstanceCulling/InstanceCullingContext.h"

static TAutoConsoleVariable<int32> CVarCullInstances(
	TEXT("r.CullInstances"),
	1,
	TEXT("CullInstances."),
	ECVF_RenderThreadSafe);


FInstanceCullingManager::~FInstanceCullingManager()
{
}

int32 FInstanceCullingManager::RegisterView(const FViewInfo& ViewInfo)
{
	if (!bIsEnabled)
	{
		return 0;
	}

	Nanite::FPackedViewParams Params;
	Params.ViewMatrices = ViewInfo.ViewMatrices;
	Params.PrevViewMatrices = ViewInfo.PrevViewInfo.ViewMatrices;
	Params.ViewRect = ViewInfo.ViewRect;
	// TODO: faking this here (not needed for culling, until we start involving multi-view and HZB)
	Params.RasterContextSize = ViewInfo.ViewRect.Size();
	return RegisterView(Params);
}



int32 FInstanceCullingManager::RegisterView(const Nanite::FPackedViewParams& Params)
{
	if (!bIsEnabled)
	{
		return 0;
	}
	CullingViews.Add(CreatePackedView(Params));
	return CullingViews.Num() - 1;
}

#if GPUCULL_TODO

class FCullInstancesCs : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCullInstancesCs);
	SHADER_USE_PARAMETER_STRUCT(FCullInstancesCs, FGlobalShader)
public:

	static constexpr int32 NumThreadsPerGroup = 64;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return UseGPUScene(Parameters.Platform);
	}

	/**
	 */
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("INDIRECT_ARGS_NUM_WORDS"), FInstanceCullingContext::IndirectArgsNumWords);
		OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
		OutEnvironment.SetDefine(TEXT("USE_GLOBAL_GPU_SCENE_DATA"), 1);
		OutEnvironment.SetDefine(TEXT("NUM_THREADS_PER_GROUP"), NumThreadsPerGroup);
		OutEnvironment.SetDefine(TEXT("NANITE_MULTI_VIEW"), 1);
	}


	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUSceneInstanceSceneData)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUScenePrimitiveSceneData)
		SHADER_PARAMETER(uint32, InstanceDataSOAStride)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< Nanite::FPackedView >, InViews)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, InstanceVisibilityFlagsOut)

		SHADER_PARAMETER(int32, NumInstances)
		SHADER_PARAMETER(int32, NumInstanceFlagWords)
		SHADER_PARAMETER(int32, NumViews)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FCullInstancesCs, "/Engine/Private/InstanceCulling/CullInstances.usf", "CullInstancesCs", SF_Compute);

#endif // GPUCULL_TODO

void FInstanceCullingManager::CullInstances(FRDGBuilder& GraphBuilder, FGPUScene& GPUScene)
{
#if GPUCULL_TODO
	TRACE_CPUPROFILER_EVENT_SCOPE(FInstanceCullingManager::CullInstances);
	int32 NumViews = CullingViews.Num();
	int32 NumInstances = GPUScene.InstanceDataAllocator.GetMaxSize();
	RDG_EVENT_SCOPE(GraphBuilder, "CullInstances [%d Views X %d Instances]", NumViews, NumInstances);

	check(!CullingIntermediate.InstanceIdOutOffsetBuffer);
	check(!CullingIntermediate.VisibleInstanceFlags);

	FRDGBufferUploader BufferUploader;

	TArray<uint32> NullArray;
	NullArray.AddZeroed(1);
	CullingIntermediate.InstanceIdOutOffsetBuffer = CreateStructuredBuffer(GraphBuilder, BufferUploader, TEXT("InstanceCulling.OutputOffsetBufferOut"), NullArray);

	int32 NumInstanceFlagWords = FMath::DivideAndRoundUp(NumInstances, int32(sizeof(uint32) * 8));

	CullingIntermediate.NumInstances = NumInstances;
	CullingIntermediate.NumViews = NumViews;

	if (NumInstances && NumViews)
	{
		// Create a buffer to record one bit for each instance per view
		CullingIntermediate.VisibleInstanceFlags = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumInstanceFlagWords * NumViews), TEXT("InstanceCulling.VisibleInstanceFlags"));
		FRDGBufferUAVRef VisibleInstanceFlagsUAV = GraphBuilder.CreateUAV(CullingIntermediate.VisibleInstanceFlags);

		if (CVarCullInstances.GetValueOnRenderThread() != 0)
		{
			AddClearUAVPass(GraphBuilder, VisibleInstanceFlagsUAV, 0);

			FCullInstancesCs::FParameters* PassParameters = GraphBuilder.AllocParameters<FCullInstancesCs::FParameters>();

			PassParameters->GPUSceneInstanceSceneData = GPUScene.InstanceDataBuffer.SRV;
			PassParameters->GPUScenePrimitiveSceneData = GPUScene.PrimitiveBuffer.SRV;
			PassParameters->InstanceDataSOAStride = GPUScene.InstanceDataSOAStride;
			PassParameters->NumInstances = NumInstances;
			PassParameters->NumInstanceFlagWords = NumInstanceFlagWords;

			PassParameters->InViews = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, BufferUploader, TEXT("InstanceCulling.CullingViews"), CullingViews));
			PassParameters->NumViews = NumViews;


			PassParameters->InstanceVisibilityFlagsOut = VisibleInstanceFlagsUAV;

			auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FCullInstancesCs>();

			BufferUploader.Submit(GraphBuilder);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("CullInstancesCs"),
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(NumInstances, FCullInstancesCs::NumThreadsPerGroup)
			);
		}
		else
		{
			// All are visible
			AddClearUAVPass(GraphBuilder, VisibleInstanceFlagsUAV, 0xFFFFFFFF);
		}
	}

	BufferUploader.Submit(GraphBuilder);
#endif // GPUCULL_TODO
}
