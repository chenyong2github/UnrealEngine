// Copyright Epic Games, Inc. All Rights Reserved.

#include "InstanceCulling/InstanceCullingContext.h"
#include "CoreMinimal.h"
#include "RHI.h"
#include "RendererModule.h"
#include "ShaderParameterMacros.h"
#include "RenderGraphResources.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneRendering.h"
#include "ScenePrivate.h"
#include "InstanceCulling/InstanceCullingManager.h"
#include "InstanceCullingLoadBalancer.h"

static TAutoConsoleVariable<int32> CVarCullInstances(
	TEXT("r.CullInstances"),
	1,
	TEXT("CullInstances."),
	ECVF_RenderThreadSafe);

IMPLEMENT_STATIC_UNIFORM_BUFFER_SLOT(InstanceCullingUbSlot);
IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FInstanceCullingGlobalUniforms, "InstanceCulling", InstanceCullingUbSlot);



static const TCHAR* BatchProcessingModeStr[] =
{
	TEXT("Generic"),
	TEXT("UnCulled"),
};

static_assert(UE_ARRAY_COUNT(BatchProcessingModeStr) == uint32(EBatchProcessingMode::Num), "BatchProcessingModeStr length does not match EBatchProcessingMode::Num, these must be kept in sync.");


FMeshDrawCommandOverrideArgs GetMeshDrawCommandOverrideArgs(const FInstanceCullingDrawParams& InstanceCullingDrawParams)
{
	FMeshDrawCommandOverrideArgs Result;
	Result.InstanceBuffer = InstanceCullingDrawParams.InstanceIdOffsetBuffer.GetBuffer() != nullptr ? InstanceCullingDrawParams.InstanceIdOffsetBuffer.GetBuffer()->GetRHI() : nullptr;
	Result.IndirectArgsBuffer = InstanceCullingDrawParams.DrawIndirectArgsBuffer.GetBuffer() != nullptr ? InstanceCullingDrawParams.DrawIndirectArgsBuffer.GetBuffer()->GetRHI() : nullptr;
	Result.InstanceDataByteOffset = InstanceCullingDrawParams.InstanceDataByteOffset;
	Result.IndirectArgsByteOffset = InstanceCullingDrawParams.IndirectArgsByteOffset;
	return Result;
}

static uint32 StepInstanceDataOffset(ERHIFeatureLevel::Type FeatureLevel, uint32 NumStepInstances, uint32 NumStepDraws)
{
	// mobile uses one instance step rate, on desktop step is once per draw
	if (FeatureLevel == ERHIFeatureLevel::ES3_1)
	{
		return NumStepInstances;
	}
	else
	{
		return NumStepDraws;
	}
}

uint32 FInstanceCullingContext::GetInstanceIdBufferStride(ERHIFeatureLevel::Type FeatureLevel)
{
	if (FeatureLevel == ERHIFeatureLevel::ES3_1)
	{
		return FPrimitiveIdDummyBufferMobile::BufferStride;
	}
	else
	{
		return sizeof(uint32);
	}
}

FInstanceCullingContext::FInstanceCullingContext(ERHIFeatureLevel::Type InFeatureLevel, FInstanceCullingManager* InInstanceCullingManager, TArrayView<const int32> InViewIds, enum EInstanceCullingMode InInstanceCullingMode, bool bInDrawOnlyVSMInvalidatingGeometry, EBatchProcessingMode InSingleInstanceProcessingMode) :
	InstanceCullingManager(InInstanceCullingManager),
	FeatureLevel(InFeatureLevel),
	ViewIds(InViewIds),
	bIsEnabled(InInstanceCullingManager == nullptr || InInstanceCullingManager->IsEnabled()),
	InstanceCullingMode(InInstanceCullingMode),
	bDrawOnlyVSMInvalidatingGeometry(bInDrawOnlyVSMInvalidatingGeometry),
	SingleInstanceProcessingMode(InSingleInstanceProcessingMode)
{
}

FInstanceCullingContext::~FInstanceCullingContext()
{
	for (auto& LoadBalancer : LoadBalancers)
	{
		if (LoadBalancer != nullptr)
		{
			delete LoadBalancer;
		}
	}
}

void FInstanceCullingContext::ResetCommands(int32 MaxNumCommands)
{
	IndirectArgs.Empty(MaxNumCommands);
	MeshDrawCommandInfos.Empty(MaxNumCommands);
	DrawCommandDescs.Empty(MaxNumCommands);
	InstanceIdOffsets.Empty(MaxNumCommands);
	TotalInstances = 0U;
}

uint32 FInstanceCullingContext::AllocateIndirectArgs(const FMeshDrawCommand *MeshDrawCommand)
{
	const uint32 NumPrimitives = MeshDrawCommand->NumPrimitives;
	if (ensure(MeshDrawCommand->PrimitiveType < PT_Num))
	{
		// default to PT_TriangleList & PT_RectList
		uint32 NumVerticesOrIndices = NumPrimitives * 3U;
		switch (MeshDrawCommand->PrimitiveType)
		{
		case PT_QuadList:
			NumVerticesOrIndices = NumPrimitives * 4U;
			break;
		case PT_TriangleStrip:
			NumVerticesOrIndices = NumPrimitives + 2U;
			break;
		case PT_LineList:
			NumVerticesOrIndices = NumPrimitives * 2U;
			break;
		case PT_PointList:
			NumVerticesOrIndices = NumPrimitives;
			break;
		default:
			break;
		}

		return IndirectArgs.Emplace(FRHIDrawIndexedIndirectParameters{ NumVerticesOrIndices, 0U, MeshDrawCommand->FirstIndex, int32(MeshDrawCommand->VertexParams.BaseVertexIndex), 0U });
	}
	return 0U;
}
// Key things to achieve
// 1. low-data handling of since ID/Primitive path
// 2. no redundant alloc upload of indirect cmd if none needed.
// 2.1 Only allocate indirect draw cmd if needed, 
// 3. 

void FInstanceCullingContext::AddInstancesToDrawCommand(uint32 IndirectArgsOffset, int32 InstanceDataOffset, bool bDynamicInstanceDataOffset, uint32 NumInstances)
{
	checkSlow(InstanceDataOffset >= 0);

	// We special-case the single-instance (i.e., regular primitives) as they don't need culling (again).
	// In actual fact this is not 100% true because dynamic path primitives may not have been culled.
	EBatchProcessingMode Mode = NumInstances == 1 ? SingleInstanceProcessingMode : EBatchProcessingMode::Generic;
	// NOTE: we pack the bDynamicInstanceDataOffset in the lowest bit because the load balancer steals the upper bits of the payload!
	LoadBalancers[uint32(Mode)]->Add(uint32(InstanceDataOffset), NumInstances, (IndirectArgsOffset << 1U) | uint32(bDynamicInstanceDataOffset));
	TotalInstances += NumInstances;
}

void FInstanceCullingContext::AddInstanceRunsToDrawCommand(uint32 IndirectArgsOffset, int32 InstanceDataOffset, bool bDynamicInstanceDataOffset, const uint32* Runs, uint32 NumRuns)
{
	// Add items to current generic batch as they are instanced for sure.
	for (uint32 Index = 0; Index < NumRuns; ++Index)
	{
		uint32 RunStart = Runs[Index * 2];
		uint32 RunEndIncl = Runs[Index * 2 + 1];
		uint32 NumInstances = (RunEndIncl + 1U) - RunStart;
		AddInstancesToDrawCommand(IndirectArgsOffset, InstanceDataOffset + RunStart, bDynamicInstanceDataOffset, NumInstances);
	}
}

class FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs);
	SHADER_USE_PARAMETER_STRUCT(FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs, FGlobalShader)

public:
	static constexpr int32 NumThreadsPerGroup = FInstanceProcessingGPULoadBalancer::ThreadGroupSize;

	// GPUCULL_TODO: remove once buffer is somehow unified
	class FOutputCommandIdDim : SHADER_PERMUTATION_BOOL("OUTPUT_COMMAND_IDS");
	class FSingleInstanceModeDim : SHADER_PERMUTATION_BOOL("SINGLE_INSTANCE_MODE");
	class FCullInstancesDim : SHADER_PERMUTATION_BOOL("CULL_INSTANCES");
	class FStereoModeDim : SHADER_PERMUTATION_BOOL("STEREO_CULLING_MODE");
	// This permutation should be used for all debug output etc that adds overhead not wanted in production. 
	// Individual debug features should be controlled by dynamic switches rather than adding more permutations.
	// TODO: maybe disable permutation in shipping builds?
	class FDebugModeDim : SHADER_PERMUTATION_BOOL("DEBUG_MODE");
	class FBatchedDim : SHADER_PERMUTATION_BOOL("ENABLE_BATCH_MODE");

	using FPermutationDomain = TShaderPermutationDomain<FOutputCommandIdDim, FSingleInstanceModeDim, FCullInstancesDim, FStereoModeDim, FDebugModeDim, FBatchedDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return UseGPUScene(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FInstanceProcessingGPULoadBalancer::SetShaderDefines(OutEnvironment);

		OutEnvironment.SetDefine(TEXT("INDIRECT_ARGS_NUM_WORDS"), FInstanceCullingContext::IndirectArgsNumWords);
		OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
		OutEnvironment.SetDefine(TEXT("USE_GLOBAL_GPU_SCENE_DATA"), 1);
		OutEnvironment.SetDefine(TEXT("USE_GLOBAL_GPU_LIGHTMAP_DATA"), 1);
		OutEnvironment.SetDefine(TEXT("NANITE_MULTI_VIEW"), 1);
		OutEnvironment.SetDefine(TEXT("PRIM_ID_DYNAMIC_FLAG"), GPrimIDDynamicFlag);

		OutEnvironment.SetDefine(TEXT("BATCH_PROCESSING_MODE_GENERIC"), uint32(EBatchProcessingMode::Generic));
		OutEnvironment.SetDefine(TEXT("BATCH_PROCESSING_MODE_UNCULLED"), uint32(EBatchProcessingMode::UnCulled));
		OutEnvironment.SetDefine(TEXT("BATCH_PROCESSING_MODE_NUM"), uint32(EBatchProcessingMode::Num));
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUSceneInstanceSceneData)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUScenePrimitiveSceneData)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUSceneLightmapData)
		SHADER_PARAMETER(uint32, InstanceSceneDataSOAStride)
		SHADER_PARAMETER(uint32, GPUSceneFrameNumber)
		SHADER_PARAMETER(uint32, GPUSceneNumInstances)
		SHADER_PARAMETER(uint32, GPUSceneNumPrimitives)
		SHADER_PARAMETER(uint32, GPUSceneNumLightmapDataItems)

		SHADER_PARAMETER_STRUCT_INCLUDE(FInstanceProcessingGPULoadBalancer::FShaderParameters, LoadBalancerParameters)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FInstanceCullingContext::FDrawCommandDesc >, DrawCommandDescs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint32 >, ViewIds)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< Nanite::FPackedView >, InViews)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FContextBatchInfo >, BatchInfos)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint32 >, BatchInds)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, InstanceIdOffsetBuffer)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, InstanceIdsBufferOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float4>, InstanceIdsBufferOutMobile)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, DrawCommandIdsBufferOut)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, DrawIndirectArgsBufferOut)

		SHADER_PARAMETER(uint32, NumViewIds)
		SHADER_PARAMETER(uint32, NumCullingViews)
		SHADER_PARAMETER(uint32, CurrentBatchProcessingMode)		
		SHADER_PARAMETER(int32, bDrawOnlyVSMInvalidatingGeometry)

		SHADER_PARAMETER(int32, DynamicInstanceIdOffset)
		SHADER_PARAMETER(int32, DynamicInstanceIdMax)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs, "/Engine/Private/InstanceCulling/BuildInstanceDrawCommands.usf", "InstanceCullBuildInstanceIdBufferCS", SF_Compute);

const TRDGUniformBufferRef<FInstanceCullingGlobalUniforms> FInstanceCullingContext::CreateDummyInstanceCullingUniformBuffer(FRDGBuilder& GraphBuilder)
{
	FInstanceCullingGlobalUniforms* InstanceCullingGlobalUniforms = GraphBuilder.AllocParameters<FInstanceCullingGlobalUniforms>();
	FRDGBufferRef DummyBuffer = GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, 4);
	InstanceCullingGlobalUniforms->InstanceIdsBuffer = GraphBuilder.CreateSRV(DummyBuffer);
	InstanceCullingGlobalUniforms->PageInfoBuffer = GraphBuilder.CreateSRV(DummyBuffer);
	InstanceCullingGlobalUniforms->BufferCapacity = 0;
	return GraphBuilder.CreateUniformBuffer(InstanceCullingGlobalUniforms);
}


class FInstanceCullingDeferredContext
{
public:
	FInstanceCullingDeferredContext(ERHIFeatureLevel::Type InFeatureLevel, FInstanceCullingManager* InInstanceCullingManager = nullptr) 
		: InstanceCullingManager(InInstanceCullingManager)
		, FeatureLevel(InFeatureLevel)
	{}

	FInstanceCullingManager* InstanceCullingManager;

	FRDGBufferRef DrawIndirectArgsBuffer = nullptr;
	FRDGBufferRef InstanceDataBuffer = nullptr;
	TRDGUniformBufferRef<FInstanceCullingGlobalUniforms> UniformBuffer = nullptr;

	/** Batches of GPU instance culling input data. */
	TArray<FInstanceCullingContext::FBatchItem, SceneRenderingAllocator> Batches;

	TArray<int32, SceneRenderingAllocator> ViewIds;
	//TArray<FMeshDrawCommandInfo, SceneRenderingAllocator> MeshDrawCommandInfos;
	TArray<FRHIDrawIndexedIndirectParameters, SceneRenderingAllocator> IndirectArgs;
	TArray<FInstanceCullingContext::FDrawCommandDesc, SceneRenderingAllocator> DrawCommandDescs;
	TArray<uint32, SceneRenderingAllocator> InstanceIdOffsets;

	FInstanceCullingContext::LoadBalancerArray LoadBalancers = FInstanceCullingContext::LoadBalancerArray(InPlace, nullptr);
	TStaticArray<TArray<uint32, SceneRenderingAllocator>, static_cast<uint32>(EBatchProcessingMode::Num)> BatchInds;
	TArray<FInstanceCullingContext::FContextBatchInfo, SceneRenderingAllocator> BatchInfos;

	ERHIFeatureLevel::Type FeatureLevel = ERHIFeatureLevel::Num;
	// Counters to sum up all sizes to facilitate pre-sizing
	uint32 InstanceIdBufferSize = 0U;
	TStaticArray<int32, uint32(EBatchProcessingMode::Num)> TotalBatches = TStaticArray<int32, uint32(EBatchProcessingMode::Num)>(InPlace, 0);
	TStaticArray<int32, uint32(EBatchProcessingMode::Num)> TotalItems = TStaticArray<int32, uint32(EBatchProcessingMode::Num)>(InPlace, 0);
	int32 TotalIndirectArgs = 0;
	int32 TotalViewIds = 0;

	bool bProcessed = false;

	void ProcessBatched(TStaticArray<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FParameters*, static_cast<uint32>(EBatchProcessingMode::Num)> PassParameters);


	~FInstanceCullingDeferredContext()
	{
		for (auto& LoadBalancer : LoadBalancers)
		{
			if (LoadBalancer != nullptr)
			{
				delete LoadBalancer;
			}
		}
	}
};


void FInstanceCullingContext::BuildRenderingCommands(
	FRDGBuilder& GraphBuilder,
	const FGPUScene& GPUScene,
	int32 DynamicInstanceIdOffset,
	int32 DynamicInstanceIdNum,
	FInstanceCullingResult& Results,
	FInstanceCullingDrawParams* InstanceCullingDrawParams) const
{
	Results = FInstanceCullingResult();

	if (!HasCullingCommands())
	{
		if (InstanceCullingManager)
		{
			Results.UniformBuffer = InstanceCullingManager->GetDummyInstanceCullingUniformBuffer();
		}
		return;
	}

	const uint32 InstanceIdBufferSize = TotalInstances * ViewIds.Num();
	if (InstanceCullingDrawParams && InstanceCullingManager && InstanceCullingManager->IsDeferredCullingActive())
	{
		FInstanceCullingDeferredContext *DeferredContext = InstanceCullingManager->DeferredContext;

		// If this is true, then RDG Execute or Drain has been called, and no further contexts can be deferred. 
		if (!DeferredContext->bProcessed)
		{
			Results.DrawIndirectArgsBuffer = DeferredContext->DrawIndirectArgsBuffer;
			Results.InstanceDataBuffer = DeferredContext->InstanceDataBuffer;
			Results.UniformBuffer = DeferredContext->UniformBuffer;
			DeferredContext->Batches.Add(FBatchItem{ this, InstanceCullingDrawParams, DynamicInstanceIdOffset, DynamicInstanceIdNum });


			// Accumulate the totals so the deferred processing can pre-size the arrays
			for (uint32 Mode = 0U; Mode < uint32(EBatchProcessingMode::Num); ++Mode)
			{
				LoadBalancers[Mode]->FinalizeBatches();
				DeferredContext->TotalBatches[Mode] += LoadBalancers[Mode]->GetBatches().Max();
				DeferredContext->TotalItems[Mode] += LoadBalancers[Mode]->GetItems().Max();
			}
			DeferredContext->TotalIndirectArgs += IndirectArgs.Num();
			DeferredContext->TotalViewIds += ViewIds.Num();
			DeferredContext->InstanceIdBufferSize += InstanceIdBufferSize;
		}
		return;
	}

	ensure(InstanceCullingMode == EInstanceCullingMode::Normal || ViewIds.Num() == 2);

	// If there is no manager, then there is no data on culling, so set flag to skip that and ignore buffers.
	const bool bCullInstances = InstanceCullingManager != nullptr && CVarCullInstances.GetValueOnRenderThread() != 0;

	RDG_EVENT_SCOPE(GraphBuilder, "BuildRenderingCommands(Culling=%s)", bCullInstances ? TEXT("On") : TEXT("Off"));

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);

	// Add any other conditions that needs debug code running here.
	const bool bUseDebugMode = bDrawOnlyVSMInvalidatingGeometry;

	FRDGBufferRef ViewIdsBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("InstanceCulling.ViewIds"), ViewIds);
	FRDGBufferRef InstanceIdsBuffer = nullptr;
	FRDGBufferUAVRef InstanceIdsBufferUAV = nullptr;
	const uint32 InstanceIdBufferStride = GetInstanceIdBufferStride(FeatureLevel);
	if (FeatureLevel == ERHIFeatureLevel::ES3_1)
	{
		// This buffer will be used as per-instance vertex buffer. AFAIK only DX11 does not allow structured buffer to be used as a vertex buffer 
		InstanceIdsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(InstanceIdBufferStride, InstanceIdBufferSize), TEXT("InstanceCulling.InstanceIdsBuffer"));
		InstanceIdsBufferUAV = GraphBuilder.CreateUAV(InstanceIdsBuffer, PF_A32B32G32R32F, ERDGUnorderedAccessViewFlags::SkipBarrier);
	}
	else
	{
		InstanceIdsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(InstanceIdBufferStride, InstanceIdBufferSize), TEXT("InstanceCulling.InstanceIdsBuffer"));
		InstanceIdsBufferUAV = GraphBuilder.CreateUAV(InstanceIdsBuffer, ERDGUnorderedAccessViewFlags::SkipBarrier);
	}

	FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FParameters PassParametersTmp;

	PassParametersTmp.DrawCommandDescs = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("InstanceCulling.DrawCommandDescs"), DrawCommandDescs));

	// Because the view uniforms are not set up by the time this runs
	// PassParametersTmp.View = View.ViewUniformBuffer;
	// Set up global GPU-scene data instead...
	PassParametersTmp.GPUSceneInstanceSceneData = GPUScene.InstanceSceneDataBuffer.SRV;
	PassParametersTmp.GPUScenePrimitiveSceneData = GPUScene.PrimitiveBuffer.SRV;
	PassParametersTmp.GPUSceneLightmapData = GPUScene.LightmapDataBuffer.SRV;
	PassParametersTmp.InstanceSceneDataSOAStride = GPUScene.InstanceSceneDataSOAStride;
	PassParametersTmp.GPUSceneFrameNumber = GPUScene.GetSceneFrameNumber();
	PassParametersTmp.GPUSceneNumInstances = GPUScene.GetNumInstances();
	PassParametersTmp.GPUSceneNumPrimitives = GPUScene.GetNumPrimitives();
	PassParametersTmp.GPUSceneNumLightmapDataItems = GPUScene.GetNumLightmapDataItems();
	PassParametersTmp.DynamicInstanceIdOffset = DynamicInstanceIdOffset;
	PassParametersTmp.DynamicInstanceIdMax = DynamicInstanceIdOffset + DynamicInstanceIdNum;


	// Create buffer for indirect args and upload draw arg data, also clears the instance to zero
	FRDGBufferRef DrawIndirectArgsRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(IndirectArgsNumWords * IndirectArgs.Num()), TEXT("InstanceCulling.DrawIndirectArgsBuffer"));
	GraphBuilder.QueueBufferUpload(DrawIndirectArgsRDG, IndirectArgs.GetData(), IndirectArgs.GetTypeSize() * IndirectArgs.Num());

	// Note: we redundantly clear the instance counts here as there is some issue with replays on certain consoles.
	AddClearIndirectArgInstanceCountPass(GraphBuilder, ShaderMap, DrawIndirectArgsRDG);

	// not using structured buffer as we have to get at it as a vertex buffer 
	FRDGBufferRef InstanceIdOffsetBufferRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), InstanceIdOffsets.Num()), TEXT("InstanceCulling.InstanceIdOffsetBuffer"));
	GraphBuilder.QueueBufferUpload(InstanceIdOffsetBufferRDG, InstanceIdOffsets.GetData(), InstanceIdOffsets.GetTypeSize() * InstanceIdOffsets.Num());

	PassParametersTmp.ViewIds = GraphBuilder.CreateSRV(ViewIdsBuffer);
	PassParametersTmp.NumCullingViews = 0;
	if (bCullInstances)
	{
		PassParametersTmp.InViews = GraphBuilder.CreateSRV(InstanceCullingManager->CullingIntermediate.CullingViews);
		PassParametersTmp.NumCullingViews = InstanceCullingManager->CullingIntermediate.NumViews;
	}
	PassParametersTmp.NumViewIds = ViewIds.Num();
	PassParametersTmp.bDrawOnlyVSMInvalidatingGeometry = bDrawOnlyVSMInvalidatingGeometry;
	// only one of these will be used in the shader
	PassParametersTmp.InstanceIdsBufferOut = InstanceIdsBufferUAV;
	PassParametersTmp.InstanceIdsBufferOutMobile = InstanceIdsBufferUAV;

	PassParametersTmp.DrawIndirectArgsBufferOut = GraphBuilder.CreateUAV(DrawIndirectArgsRDG, PF_R32_UINT, ERDGUnorderedAccessViewFlags::SkipBarrier);
	PassParametersTmp.InstanceIdOffsetBuffer = GraphBuilder.CreateSRV(InstanceIdOffsetBufferRDG, PF_R32_UINT);

	for (uint32 Mode = 0U; Mode < uint32(EBatchProcessingMode::Num); ++Mode)
	{
		FInstanceProcessingGPULoadBalancer* LoadBalancer = LoadBalancers[Mode];
		if (!LoadBalancer->IsEmpty())
		{
			FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FParameters* PassParameters = GraphBuilder.AllocParameters<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FParameters>();
			*PassParameters = PassParametersTmp;
			// Upload data etc
			auto GPUData = LoadBalancer->Upload(GraphBuilder);
			GPUData.GetShaderParameters(GraphBuilder, PassParameters->LoadBalancerParameters);
			PassParameters->CurrentBatchProcessingMode = Mode;

			// UnCulled bucket is used for a single instance mode
			check(EBatchProcessingMode(Mode) != EBatchProcessingMode::UnCulled || LoadBalancer->HasSingleInstanceItemsOnly());

			FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FPermutationDomain PermutationVector;
			PermutationVector.Set<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FOutputCommandIdDim>(0);
			PermutationVector.Set<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FSingleInstanceModeDim>(EBatchProcessingMode(Mode) == EBatchProcessingMode::UnCulled);
			PermutationVector.Set<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FCullInstancesDim>(bCullInstances && EBatchProcessingMode(Mode) != EBatchProcessingMode::UnCulled);
			PermutationVector.Set<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FStereoModeDim>(InstanceCullingMode == EInstanceCullingMode::Stereo);
			PermutationVector.Set<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FDebugModeDim>(bUseDebugMode);
			PermutationVector.Set<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FBatchedDim>(false);

			auto ComputeShader = ShaderMap->GetShader<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs>(PermutationVector);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("CullInstances(%s)", BatchProcessingModeStr[Mode]),
				ComputeShader,
				PassParameters,
				LoadBalancer->GetWrappedCsGroupCount()
			);
		}
	}
	Results.DrawIndirectArgsBuffer = DrawIndirectArgsRDG;
	
	if (FeatureLevel == ERHIFeatureLevel::ES3_1)
	{
		Results.InstanceDataBuffer = InstanceIdsBuffer;
	}
	else
	{
		Results.InstanceDataBuffer = InstanceIdOffsetBufferRDG;

		FInstanceCullingGlobalUniforms* UniformParameters = GraphBuilder.AllocParameters<FInstanceCullingGlobalUniforms>();
		UniformParameters->InstanceIdsBuffer = GraphBuilder.CreateSRV(InstanceIdsBuffer);
		UniformParameters->PageInfoBuffer = GraphBuilder.CreateSRV(InstanceIdsBuffer);
		UniformParameters->BufferCapacity = InstanceIdBufferSize;
		Results.UniformBuffer = GraphBuilder.CreateUniformBuffer(UniformParameters);
	}
}



void FInstanceCullingDeferredContext::ProcessBatched(TStaticArray<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FParameters*, static_cast<uint32>(EBatchProcessingMode::Num)> PassParameters)
{
	if (bProcessed)
	{
		return;
	}

	bProcessed = true;
	for (uint32 Mode = 0U; Mode < uint32(EBatchProcessingMode::Num); ++Mode)
	{
		LoadBalancers[Mode]= new FInstanceProcessingGPULoadBalancer;
		LoadBalancers[Mode]->ReserveStorage(TotalBatches[Mode], TotalItems[Mode]);
	}
	// Pre-size all arrays
	IndirectArgs.Empty(TotalIndirectArgs);
	DrawCommandDescs.Empty(TotalIndirectArgs);
	InstanceIdOffsets.Empty(TotalIndirectArgs);
	ViewIds.Empty(TotalViewIds);

	BatchInfos.AddDefaulted(Batches.Num());
	uint32 InstanceIdBufferOffset = 0U;
	uint32 InstanceDataByteOffset = 0U;
	const uint32 InstanceIdBufferStride = FInstanceCullingContext::GetInstanceIdBufferStride(FeatureLevel);

	// Index that maps from each command to the corresponding batch - maybe not the utmost efficiency
	for (int32 BatchIndex = 0; BatchIndex < Batches.Num(); ++BatchIndex)
	{
		const FInstanceCullingContext::FBatchItem& BatchItem = Batches[BatchIndex];
		const FInstanceCullingContext& InstanceCullingContext = *BatchItem.Context;

		FInstanceCullingContext::FContextBatchInfo& BatchInfo = BatchInfos[BatchIndex];

		BatchInfo.IndirectArgsOffset = IndirectArgs.Num();
		//BatchInfo.NumIndirectArgs = InstanceCullingContext.IndirectArgs.Num();
		IndirectArgs.Append(InstanceCullingContext.IndirectArgs);

		check(InstanceCullingContext.DrawCommandDescs.Num() == InstanceCullingContext.IndirectArgs.Num());
		DrawCommandDescs.Append(InstanceCullingContext.DrawCommandDescs);

		check(InstanceCullingContext.InstanceIdOffsets.Num() == InstanceCullingContext.IndirectArgs.Num());
		InstanceIdOffsets.AddDefaulted(InstanceCullingContext.InstanceIdOffsets.Num());
		// TODO: perform offset on GPU
		// InstanceIdOffsets.Append(InstanceCullingContext.InstanceIdOffsets);
		for (int32 Index = 0; Index < InstanceCullingContext.InstanceIdOffsets.Num(); ++Index)
		{
			InstanceIdOffsets[BatchInfo.IndirectArgsOffset + Index] = InstanceCullingContext.InstanceIdOffsets[Index] + InstanceIdBufferOffset;
		}

		BatchInfo.ViewIdsOffset = ViewIds.Num();
		BatchInfo.NumViewIds = InstanceCullingContext.ViewIds.Num();
		ViewIds.Append(InstanceCullingContext.ViewIds);

		BatchInfo.DynamicInstanceIdOffset = BatchItem.DynamicInstanceIdOffset;
		BatchInfo.DynamicInstanceIdMax = BatchItem.DynamicInstanceIdOffset + BatchItem.DynamicInstanceIdNum;

		for (uint32 Mode = 0U; Mode < uint32(EBatchProcessingMode::Num); ++Mode)
		{
			int32 StartIndex = BatchInds[Mode].Num();
			FInstanceProcessingGPULoadBalancer* MergedLoadBalancer = LoadBalancers[Mode];
				
			BatchInfo.ItemDataOffset[Mode] = MergedLoadBalancer->GetItems().Num();
			FInstanceProcessingGPULoadBalancer* LoadBalancer = InstanceCullingContext.LoadBalancers[Mode];
			LoadBalancer->FinalizeBatches();

			// UnCulled bucket is used for a single instance mode
			check(EBatchProcessingMode(Mode) != EBatchProcessingMode::UnCulled || LoadBalancer->HasSingleInstanceItemsOnly());

			BatchInds[Mode].AddDefaulted(LoadBalancer->GetBatches().Num());

			MergedLoadBalancer->AppendData(*LoadBalancer);
			for (int32 Index = StartIndex; Index < BatchInds[Mode].Num(); ++Index)
			{
				BatchInds[Mode][Index] = BatchIndex;
			}
		}
		const uint32 BatchTotalInstances = InstanceCullingContext.TotalInstances * InstanceCullingContext.ViewIds.Num();
		const uint32 BatchTotalDraws = InstanceCullingContext.InstanceIdOffsets.Num();

		FInstanceCullingDrawParams& Result = *BatchItem.Result;
		Result.InstanceDataByteOffset = InstanceDataByteOffset;
		Result.IndirectArgsByteOffset = BatchInfo.IndirectArgsOffset * FInstanceCullingContext::IndirectArgsNumWords * sizeof(uint32);

		BatchInfo.InstanceDataWriteOffset = InstanceIdBufferOffset;
		InstanceIdBufferOffset += BatchTotalInstances;
		// Advance offset into per-instance buffer
		InstanceDataByteOffset += StepInstanceDataOffset(FeatureLevel, BatchTotalInstances, BatchTotalDraws) * InstanceIdBufferStride;
	}

	// Finalize culling pass parameters
	for (uint32 Mode = 0U; Mode < uint32(EBatchProcessingMode::Num); ++Mode)
	{
		PassParameters[Mode]->NumViewIds = ViewIds.Num();
		PassParameters[Mode]->LoadBalancerParameters.NumBatches = LoadBalancers[Mode]->GetBatches().Num();
		PassParameters[Mode]->LoadBalancerParameters.NumItems = LoadBalancers[Mode]->GetItems().Num();
		PassParameters[Mode]->NumCullingViews = InstanceCullingManager->GetCullingViews().Num();
	}
}

template <typename DataType>
FORCEINLINE int32 GetArrayDataSize(const TArrayView<const DataType>& Array)
{
	return Array.GetTypeSize() * Array.Num();
}

template <typename DataType, typename AllocatorType>
FORCEINLINE int32 GetArrayDataSize(const TArray<DataType, AllocatorType>& Array)
{
	return Array.GetTypeSize() * Array.Num();
}

FInstanceCullingDeferredContext *FInstanceCullingContext::CreateDeferredContext(
	FRDGBuilder& GraphBuilder,
	FGPUScene& GPUScene,
	FInstanceCullingManager* InstanceCullingManager)
{
	check(InstanceCullingManager != nullptr);
#define INST_CULL_CALLBACK_MODE(CustomCode) \
	[PassParameters, DeferredContext, Mode]() \
	{ \
		DeferredContext->ProcessBatched(PassParameters); \
		return CustomCode; \
	}

#define INST_CULL_CALLBACK(CustomCode) \
	[PassParameters, DeferredContext]() \
	{ \
		DeferredContext->ProcessBatched(PassParameters); \
		return CustomCode; \
	}

#define INST_CULL_CREATE_STRUCT_BUFF_ARGS(ArrayName) \
	GraphBuilder, \
	TEXT("InstanceCulling.") TEXT(#ArrayName), \
	DeferredContext->ArrayName.GetTypeSize(), \
	INST_CULL_CALLBACK(DeferredContext->ArrayName.Num()), \
	INST_CULL_CALLBACK(DeferredContext->ArrayName.GetData()), \
	INST_CULL_CALLBACK(DeferredContext->ArrayName.Num() * DeferredContext->ArrayName.GetTypeSize())

#define INST_CULL_CREATE_STRUCT_BUFF_ARGS_MODE(ArrayName) \
	GraphBuilder, \
	TEXT("InstanceCulling.") TEXT(#ArrayName), \
	DeferredContext->ArrayName[Mode].GetTypeSize(), \
	INST_CULL_CALLBACK_MODE(DeferredContext->ArrayName[Mode].Num()), \
	INST_CULL_CALLBACK_MODE(DeferredContext->ArrayName[Mode].GetData()), \
	INST_CULL_CALLBACK_MODE(DeferredContext->ArrayName[Mode].Num() * DeferredContext->ArrayName[Mode].GetTypeSize())

	const ERHIFeatureLevel::Type FeatureLevel = GPUScene.GetFeatureLevel();

	FInstanceCullingDeferredContext* DeferredContext = GraphBuilder.AllocObject<FInstanceCullingDeferredContext>(FeatureLevel, InstanceCullingManager);

	const bool bCullInstances = CVarCullInstances.GetValueOnRenderThread() != 0;
	RDG_EVENT_SCOPE(GraphBuilder, "BuildRenderingCommandsDeferred(Culling=%s)", bCullInstances ? TEXT("On") : TEXT("Off"));

	TStaticArray<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FParameters*, static_cast<uint32>(EBatchProcessingMode::Num)> PassParameters;
	for (uint32 Mode = 0U; Mode < uint32(EBatchProcessingMode::Num); ++Mode)
	{
		PassParameters[Mode] = GraphBuilder.AllocParameters<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FParameters>();
	}

	FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FParameters PassParametersTmp;

	// Defer upload of views as these may be added during setup of passes.
	FRDGBufferRef ViewsRDG = CreateStructuredBuffer(GraphBuilder, 
		TEXT("InstanceCulling.Views"), InstanceCullingManager->CullingViews.GetTypeSize(), 
		INST_CULL_CALLBACK(DeferredContext->InstanceCullingManager->CullingViews.Num()),
		INST_CULL_CALLBACK(DeferredContext->InstanceCullingManager->CullingViews.GetData()),
		INST_CULL_CALLBACK(GetArrayDataSize(DeferredContext->InstanceCullingManager->CullingViews))
	);

	FRDGBufferRef DrawCommandDescsRDG = CreateStructuredBuffer(INST_CULL_CREATE_STRUCT_BUFF_ARGS(DrawCommandDescs));
	FRDGBufferRef ViewIdsRDG = CreateStructuredBuffer(INST_CULL_CREATE_STRUCT_BUFF_ARGS(ViewIds));
	FRDGBufferRef BatchInfosRDG = CreateStructuredBuffer(INST_CULL_CREATE_STRUCT_BUFF_ARGS(BatchInfos));

	DeferredContext->DrawIndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(), TEXT("InstanceCulling.DrawIndirectArgsBuffer"), INST_CULL_CALLBACK(IndirectArgsNumWords * DeferredContext->IndirectArgs.Num()));
	GraphBuilder.QueueBufferUpload(DeferredContext->DrawIndirectArgsBuffer, INST_CULL_CALLBACK(DeferredContext->IndirectArgs.GetData()), INST_CULL_CALLBACK(GetArrayDataSize(DeferredContext->IndirectArgs)));

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);

	// Note: we redundantly clear the instance counts here as there is some issue with replays on certain consoles.
	AddClearIndirectArgInstanceCountPass(GraphBuilder, ShaderMap, DeferredContext->DrawIndirectArgsBuffer, INST_CULL_CALLBACK(DeferredContext->IndirectArgs.Num()));

	// not using structured buffer as we want/have to get at it as a vertex buffer 
	FRDGBufferRef InstanceIdOffsetBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("InstanceCulling.InstanceIdOffsetBuffer"), INST_CULL_CALLBACK(DeferredContext->InstanceIdOffsets.Num()));
	GraphBuilder.QueueBufferUpload(InstanceIdOffsetBuffer, INST_CULL_CALLBACK(DeferredContext->InstanceIdOffsets.GetData()), INST_CULL_CALLBACK(DeferredContext->InstanceIdOffsets.GetTypeSize() * DeferredContext->InstanceIdOffsets.Num()));

	FRDGBufferRef InstanceIdsBuffer = nullptr;
	FRDGBufferUAVRef InstanceIdsBufferUAV = nullptr;
	const uint32 InstanceIdBufferStride = GetInstanceIdBufferStride(FeatureLevel);
	if (FeatureLevel == ERHIFeatureLevel::ES3_1)
	{
		// This buffer will be used as per-instance vertex buffer. AFAIK only DX11 does not allow structured buffer to be used as a vertex buffer 
		InstanceIdsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(InstanceIdBufferStride, 1), TEXT("InstanceCulling.InstanceIdsBuffer"), INST_CULL_CALLBACK(DeferredContext->InstanceIdBufferSize));
		InstanceIdsBufferUAV = GraphBuilder.CreateUAV(InstanceIdsBuffer, PF_A32B32G32R32F, ERDGUnorderedAccessViewFlags::SkipBarrier);
		DeferredContext->InstanceDataBuffer = InstanceIdsBuffer;
	}
	else
	{
		InstanceIdsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(InstanceIdBufferStride, 1), TEXT("InstanceCulling.InstanceIdsBuffer"), INST_CULL_CALLBACK(DeferredContext->InstanceIdBufferSize));
		InstanceIdsBufferUAV = GraphBuilder.CreateUAV(InstanceIdsBuffer, ERDGUnorderedAccessViewFlags::SkipBarrier);
		DeferredContext->InstanceDataBuffer = InstanceIdOffsetBuffer;
	}

	// Because the view uniforms are not set up by the time this runs
	// PassParameters->View = View.ViewUniformBuffer;
	// Set up global GPU-scene data instead...
	PassParametersTmp.GPUSceneInstanceSceneData = GPUScene.InstanceSceneDataBuffer.SRV;
	PassParametersTmp.GPUScenePrimitiveSceneData = GPUScene.PrimitiveBuffer.SRV;
	PassParametersTmp.GPUSceneLightmapData = GPUScene.LightmapDataBuffer.SRV;
	PassParametersTmp.InstanceSceneDataSOAStride = GPUScene.InstanceSceneDataSOAStride;
	PassParametersTmp.GPUSceneFrameNumber = GPUScene.GetSceneFrameNumber();
	PassParametersTmp.GPUSceneNumInstances = GPUScene.GetNumInstances();
	PassParametersTmp.GPUSceneNumPrimitives = GPUScene.GetNumPrimitives();
	PassParametersTmp.GPUSceneNumLightmapDataItems = GPUScene.GetNumLightmapDataItems();

	PassParametersTmp.DrawCommandDescs = GraphBuilder.CreateSRV(DrawCommandDescsRDG);
	PassParametersTmp.BatchInfos = GraphBuilder.CreateSRV(BatchInfosRDG);
	PassParametersTmp.ViewIds = GraphBuilder.CreateSRV(ViewIdsRDG);
	// only one of these will be used in the shader
	PassParametersTmp.InstanceIdsBufferOut = InstanceIdsBufferUAV;
	PassParametersTmp.InstanceIdsBufferOutMobile = InstanceIdsBufferUAV;

	PassParametersTmp.DrawIndirectArgsBufferOut = GraphBuilder.CreateUAV(DeferredContext->DrawIndirectArgsBuffer, PF_R32_UINT, ERDGUnorderedAccessViewFlags::SkipBarrier);
	PassParametersTmp.InstanceIdOffsetBuffer = GraphBuilder.CreateSRV(InstanceIdOffsetBuffer, PF_R32_UINT);
	if (bCullInstances)
	{
		PassParametersTmp.InViews = GraphBuilder.CreateSRV(ViewsRDG);
		// Note: not the final number
		PassParametersTmp.NumCullingViews = InstanceCullingManager->CullingIntermediate.NumViews;
	}

	for (uint32 Mode = 0U; Mode < uint32(EBatchProcessingMode::Num); ++Mode)
	{
		*PassParameters[Mode] = PassParametersTmp;

		FRDGBufferRef BatchIndsRDG = CreateStructuredBuffer(INST_CULL_CREATE_STRUCT_BUFF_ARGS_MODE(BatchInds));
		PassParameters[Mode]->BatchInds = GraphBuilder.CreateSRV(BatchIndsRDG);

		// 
		FInstanceProcessingGPULoadBalancer::FGPUData Result;
		FRDGBufferRef BatchBuffer = CreateStructuredBuffer(
			GraphBuilder,
			TEXT("InstanceCullingLoadBalancer.Batches"),
			sizeof(FInstanceProcessingGPULoadBalancer::FPackedBatch),
			INST_CULL_CALLBACK_MODE(DeferredContext->LoadBalancers[Mode]->GetBatches().Num()),
			INST_CULL_CALLBACK_MODE(DeferredContext->LoadBalancers[Mode]->GetBatches().GetData()),
			INST_CULL_CALLBACK_MODE(GetArrayDataSize(DeferredContext->LoadBalancers[Mode]->GetBatches())));

		FRDGBufferRef ItemBuffer = CreateStructuredBuffer(
			GraphBuilder,
			TEXT("InstanceCullingLoadBalancer.Items"),
			sizeof(FInstanceProcessingGPULoadBalancer::FPackedItem),
			INST_CULL_CALLBACK_MODE(DeferredContext->LoadBalancers[Mode]->GetItems().Num()),
			INST_CULL_CALLBACK_MODE(DeferredContext->LoadBalancers[Mode]->GetItems().GetData()),
			INST_CULL_CALLBACK_MODE(GetArrayDataSize(DeferredContext->LoadBalancers[Mode]->GetItems())));

		PassParameters[Mode]->LoadBalancerParameters.BatchBuffer = GraphBuilder.CreateSRV(BatchBuffer);
		PassParameters[Mode]->LoadBalancerParameters.ItemBuffer = GraphBuilder.CreateSRV(ItemBuffer);
		PassParameters[Mode]->CurrentBatchProcessingMode = Mode;

		FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FPermutationDomain PermutationVector;
		PermutationVector.Set<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FBatchedDim>(true);
		PermutationVector.Set<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FSingleInstanceModeDim>(EBatchProcessingMode(Mode) == EBatchProcessingMode::UnCulled);
		PermutationVector.Set<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FCullInstancesDim>(bCullInstances && EBatchProcessingMode(Mode) != EBatchProcessingMode::UnCulled);

		auto ComputeShader = ShaderMap->GetShader<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("CullInstances(%s)", BatchProcessingModeStr[Mode]),
			ComputeShader,
			PassParameters[Mode],
			INST_CULL_CALLBACK_MODE(DeferredContext->LoadBalancers[Mode]->GetWrappedCsGroupCount()));
	}
	
	if (FeatureLevel > ERHIFeatureLevel::ES3_1)
	{
		FInstanceCullingGlobalUniforms* UniformParameters = GraphBuilder.AllocParameters<FInstanceCullingGlobalUniforms>();
		UniformParameters->InstanceIdsBuffer = GraphBuilder.CreateSRV(InstanceIdsBuffer);
		UniformParameters->PageInfoBuffer = GraphBuilder.CreateSRV(InstanceIdsBuffer);
		UniformParameters->BufferCapacity = 0U; // TODO: this is not used at the moment, but is intended for range checks so would have been good.
		DeferredContext->UniformBuffer = GraphBuilder.CreateUniformBuffer(UniformParameters);
	}


#undef INST_CULL_CREATE_STRUCT_BUFF_ARGS
#undef INST_CULL_CALLBACK
#undef INST_CULL_CALLBACK_MODE
#undef INST_CULL_CREATE_STRUCT_BUFF_ARGS_MODE

	return DeferredContext;
}



class FClearIndirectArgInstanceCountCs : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearIndirectArgInstanceCountCs);
	SHADER_USE_PARAMETER_STRUCT(FClearIndirectArgInstanceCountCs, FGlobalShader)

public:
	static constexpr int32 NumThreadsPerGroup = 64;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return UseGPUScene(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FInstanceProcessingGPULoadBalancer::SetShaderDefines(OutEnvironment);

		OutEnvironment.SetDefine(TEXT("INDIRECT_ARGS_NUM_WORDS"), FInstanceCullingContext::IndirectArgsNumWords);
		OutEnvironment.SetDefine(TEXT("NUM_THREADS_PER_GROUP"), NumThreadsPerGroup);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, DrawIndirectArgsBufferOut)
		SHADER_PARAMETER(uint32, NumIndirectArgs)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FClearIndirectArgInstanceCountCs, "/Engine/Private/InstanceCulling/BuildInstanceDrawCommands.usf", "ClearIndirectArgInstanceCountCS", SF_Compute);


void FInstanceCullingContext::AddClearIndirectArgInstanceCountPass(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, FRDGBufferRef DrawIndirectArgsBuffer, TFunction<int32()> NumIndirectArgsCallback)
{
	FClearIndirectArgInstanceCountCs::FParameters* PassParameters = GraphBuilder.AllocParameters<FClearIndirectArgInstanceCountCs::FParameters>();
	// Upload data etc
	PassParameters->DrawIndirectArgsBufferOut = GraphBuilder.CreateUAV(DrawIndirectArgsBuffer, PF_R32_UINT);
	PassParameters->NumIndirectArgs = DrawIndirectArgsBuffer->Desc.NumElements / FInstanceCullingContext::IndirectArgsNumWords;

	auto ComputeShader = ShaderMap->GetShader<FClearIndirectArgInstanceCountCs>();

	if (NumIndirectArgsCallback)
	{
		const FShaderParametersMetadata* ParametersMetadata = FClearIndirectArgInstanceCountCs::FParameters::FTypeInfo::GetStructMetadata();
		ClearUnusedGraphResources(ComputeShader, ParametersMetadata, PassParameters);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("ClearIndirectArgInstanceCount"),
			ParametersMetadata,
			PassParameters,
			ERDGPassFlags::Compute,
			[ParametersMetadata, PassParameters, ComputeShader, NumIndirectArgsCallback = MoveTemp(NumIndirectArgsCallback)](FRHIComputeCommandList& RHICmdList)
		{
			int32 NumIndirectArgs = NumIndirectArgsCallback();
			PassParameters->NumIndirectArgs = NumIndirectArgs;
			const FIntVector GroupCount = FComputeShaderUtils::GetGroupCountWrapped(NumIndirectArgs, FClearIndirectArgInstanceCountCs::NumThreadsPerGroup);
			if (GroupCount.X > 0 && GroupCount.Y > 0 && GroupCount.Z > 0)
			{
				FComputeShaderUtils::ValidateGroupCount(GroupCount);
				FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, ParametersMetadata, *PassParameters, GroupCount);
			}
		});
	}
	else
	{
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ClearIndirectArgInstanceCount"),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCountWrapped(PassParameters->NumIndirectArgs, FClearIndirectArgInstanceCountCs::NumThreadsPerGroup)
		);
	}
}

/**
 * Allocate indirect arg slots for all meshes to use instancing,
 * add commands that populate the indirect calls and index & id buffers, and
 * Collapse all commands that share the same state bucket ID
 * NOTE: VisibleMeshDrawCommandsInOut can only become shorter.
 */
void FInstanceCullingContext::SetupDrawCommands(
	FMeshCommandOneFrameArray& VisibleMeshDrawCommandsInOut,
	bool bCompactIdenticalCommands,
	// Stats
	int32& MaxInstances,
	int32& VisibleMeshDrawCommandsNum,
	int32& NewPassVisibleMeshDrawCommandsNum)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_BuildMeshDrawCommandPrimitiveIdBuffer);

	FVisibleMeshDrawCommand* RESTRICT PassVisibleMeshDrawCommands = VisibleMeshDrawCommandsInOut.GetData();

	// TODO: make VSM set this for now to force the processing down a single batch (to simplify), maybe.
	const bool bForceGenericProcessing = false;
	const bool bMultiView = ViewIds.Num() > 1 && !(ViewIds.Num() == 2 && InstanceCullingMode == EInstanceCullingMode::Stereo);
	if (bMultiView || bForceGenericProcessing)
	{
		// multi-view defaults to culled path to make cube-maps more efficient
		SingleInstanceProcessingMode = EBatchProcessingMode::Generic;
	}

	QUICK_SCOPE_CYCLE_COUNTER(STAT_DynamicInstancingOfVisibleMeshDrawCommands);

	ResetCommands(VisibleMeshDrawCommandsInOut.Num());
	for (auto& LoadBalancer : LoadBalancers)
	{
		if (LoadBalancer == nullptr)
		{
			LoadBalancer = new FInstanceProcessingGPULoadBalancer;
		}
		check(LoadBalancer->IsEmpty());
	}

	int32 CurrentStateBucketId = -1;
	MaxInstances = 1;
	// Only used to supply stats
	int32 CurrentAutoInstanceCount = 1;
	// Scan through and compact away all with consecutive statebucked ID, and record primitive IDs in GPU-scene culling command
	const int32 NumDrawCommandsIn = VisibleMeshDrawCommandsInOut.Num();
	int32 NumDrawCommandsOut = 0;
	uint32 CurrentIndirectArgsOffset = 0U;
	const int32 NumViews = ViewIds.Num();
	const bool bAlwaysUseIndirectDraws = (SingleInstanceProcessingMode != EBatchProcessingMode::UnCulled);
	const uint32 InstanceIdBufferStride = GetInstanceIdBufferStride(FeatureLevel);

	// Allocate conservatively for all commands, may not use all.
	for (int32 DrawCommandIndex = 0; DrawCommandIndex < NumDrawCommandsIn; ++DrawCommandIndex)
	{
		const FVisibleMeshDrawCommand& RESTRICT VisibleMeshDrawCommand = PassVisibleMeshDrawCommands[DrawCommandIndex];
		const FMeshDrawCommand* RESTRICT MeshDrawCommand = VisibleMeshDrawCommand.MeshDrawCommand;

		const bool bSupportsGPUSceneInstancing = EnumHasAnyFlags(VisibleMeshDrawCommand.Flags, EFVisibleMeshDrawCommandFlags::HasPrimitiveIdStreamIndex);
		const bool bMaterialMayModifyPosition = EnumHasAnyFlags(VisibleMeshDrawCommand.Flags, EFVisibleMeshDrawCommandFlags::MaterialMayModifyPosition);
		const bool bUseIndirectDraw = bAlwaysUseIndirectDraws || (VisibleMeshDrawCommand.NumRuns > 0 || MeshDrawCommand->NumInstances > 1);

		if (bCompactIdenticalCommands && CurrentStateBucketId != -1 && VisibleMeshDrawCommand.StateBucketId == CurrentStateBucketId)
		{
			// Drop since previous covers for this

			// Update auto-instance count (only needed for logging)
			CurrentAutoInstanceCount++;
			MaxInstances = FMath::Max(CurrentAutoInstanceCount, MaxInstances);

			FMeshDrawCommandInfo& RESTRICT DrawCmd = MeshDrawCommandInfos.Last();
			if (DrawCmd.bUseIndirect == 0)
			{
				DrawCmd.IndirectArgsOffsetOrNumInstances += 1;
		}
		}
		else
		{
			// Reset auto-instance count (only needed for logging)
			CurrentAutoInstanceCount = 1;

			// kept 1:1 with the retained (not compacted) mesh draw commands, implicitly clears num instances
			FMeshDrawCommandInfo& RESTRICT DrawCmd = MeshDrawCommandInfos.AddZeroed_GetRef();

			// TODO: redundantly create an indirect arg slot for every draw command (even thoug those that don't support GPU-scene don't need one)
			//       the unsupported ones are skipped in FMeshDrawCommand::SubmitDrawBegin/End.
			//       in the future pipe through draw command info to submit, such that they may be skipped.
			//if (bSupportsGPUSceneInstancing)
			{
				DrawCmd.bUseIndirect = bUseIndirectDraw;
				
				CurrentIndirectArgsOffset = AllocateIndirectArgs(MeshDrawCommand);
				DrawCommandDescs.Emplace(FDrawCommandDesc{bMaterialMayModifyPosition});

				if (bUseIndirectDraw)
				{
					DrawCmd.IndirectArgsOffsetOrNumInstances = CurrentIndirectArgsOffset * FInstanceCullingContext::IndirectArgsNumWords * sizeof(uint32);
				}
				else
				{
					DrawCmd.IndirectArgsOffsetOrNumInstances = 1;
				}

				// drawcall specific offset into per-instance buffer
				DrawCmd.InstanceDataByteOffset = InstanceIdOffsets.Num() * sizeof(uint32);
				
				const uint32 CurrentNumInstances = (TotalInstances * NumViews);
				const uint32 CurrentNumDraws = InstanceIdOffsets.Num();
				// drawcall specific offset into per-instance buffer
				DrawCmd.InstanceDataByteOffset = StepInstanceDataOffset(FeatureLevel, CurrentNumInstances, CurrentNumDraws) * InstanceIdBufferStride;

				InstanceIdOffsets.Emplace(CurrentNumInstances);
			}
			
			// Record the last bucket ID (may be -1)
			CurrentStateBucketId = VisibleMeshDrawCommand.StateBucketId;

			// If we have dropped any we need to move up to maintain 1:1
			if (DrawCommandIndex > NumDrawCommandsOut)
			{
				PassVisibleMeshDrawCommands[NumDrawCommandsOut] = PassVisibleMeshDrawCommands[DrawCommandIndex];
			}
			NumDrawCommandsOut++;
		}

		if (bSupportsGPUSceneInstancing)
		{
			// append 'culling command' targeting the current slot
			// This will cause all instances belonging to the Primitive to be added to the command, if they are visible etc (GPU-Scene knows all - sees all)
			if (VisibleMeshDrawCommand.RunArray)
			{
				AddInstanceRunsToDrawCommand(CurrentIndirectArgsOffset, VisibleMeshDrawCommand.PrimitiveIdInfo.InstanceSceneDataOffset, VisibleMeshDrawCommand.PrimitiveIdInfo.bIsDynamicPrimitive, VisibleMeshDrawCommand.RunArray, VisibleMeshDrawCommand.NumRuns);
			}
			else 
			{
				AddInstancesToDrawCommand(CurrentIndirectArgsOffset, VisibleMeshDrawCommand.PrimitiveIdInfo.InstanceSceneDataOffset, VisibleMeshDrawCommand.PrimitiveIdInfo.bIsDynamicPrimitive, VisibleMeshDrawCommand.MeshDrawCommand->NumInstances);
			}
		}
	}
	check(bCompactIdenticalCommands || NumDrawCommandsIn == NumDrawCommandsOut);
	checkf(NumDrawCommandsOut == MeshDrawCommandInfos.Num(), TEXT("There must be a 1:1 mapping between MeshDrawCommandInfos and mesh draw commands, as this assumption is made in SubmitDrawCommands."));

	// Setup instancing stats for logging.
	VisibleMeshDrawCommandsNum = VisibleMeshDrawCommandsInOut.Num();
	NewPassVisibleMeshDrawCommandsNum = NumDrawCommandsOut;

	// Resize array post-compaction of dynamic instances
	VisibleMeshDrawCommandsInOut.SetNum(NumDrawCommandsOut, false);
}

void FInstanceCullingContext::SubmitDrawCommands(
	const FMeshCommandOneFrameArray& VisibleMeshDrawCommands,
	const FGraphicsMinimalPipelineStateSet& GraphicsMinimalPipelineStateSet,
	const FMeshDrawCommandOverrideArgs& OverrideArgs,
	int32 StartIndex,
	int32 NumMeshDrawCommands,
	uint32 InInstanceFactor,
	FRHICommandList& RHICmdList) const
{
	if (VisibleMeshDrawCommands.Num() == 0)
	{
		// FIXME: looks like parallel rendering can spawn empty FDrawVisibleMeshCommandsAnyThreadTask
		return;
	}
	
	if (IsEnabled())
	{
		check(MeshDrawCommandInfos.Num() >= (StartIndex + NumMeshDrawCommands));
	
		FMeshDrawCommandStateCache StateCache;
		INC_DWORD_STAT_BY(STAT_MeshDrawCalls, NumMeshDrawCommands);

		for (int32 DrawCommandIndex = StartIndex; DrawCommandIndex < StartIndex + NumMeshDrawCommands; DrawCommandIndex++)
		{
			//SCOPED_CONDITIONAL_DRAW_EVENTF(RHICmdList, MeshEvent, GEmitMeshDrawEvent != 0, TEXT("Mesh Draw"));
			const FVisibleMeshDrawCommand& VisibleMeshDrawCommand = VisibleMeshDrawCommands[DrawCommandIndex];
			const FMeshDrawCommandInfo& DrawCommandInfo = MeshDrawCommandInfos[DrawCommandIndex];
			
			uint32 InstanceFactor = InInstanceFactor;
			uint32 IndirectArgsByteOffset = 0;
			FRHIBuffer* IndirectArgsBuffer = nullptr;
			if (DrawCommandInfo.bUseIndirect)
			{
				IndirectArgsByteOffset = OverrideArgs.IndirectArgsByteOffset + DrawCommandInfo.IndirectArgsOffsetOrNumInstances;
				IndirectArgsBuffer = OverrideArgs.IndirectArgsBuffer;
			}
			else
			{
				// TODO: need a better way to override number of instances
				InstanceFactor = InInstanceFactor * DrawCommandInfo.IndirectArgsOffsetOrNumInstances;
			}
			
			const int32 InstanceDataByteOffset = OverrideArgs.InstanceDataByteOffset + DrawCommandInfo.InstanceDataByteOffset;

			FMeshDrawCommand::SubmitDraw(*VisibleMeshDrawCommand.MeshDrawCommand, GraphicsMinimalPipelineStateSet, OverrideArgs.InstanceBuffer, InstanceDataByteOffset, InstanceFactor, RHICmdList, StateCache, IndirectArgsBuffer, IndirectArgsByteOffset);
		}
	}
	else
	{
		SubmitMeshDrawCommandsRange(VisibleMeshDrawCommands, GraphicsMinimalPipelineStateSet, nullptr, 0, 0, false, StartIndex, NumMeshDrawCommands, InInstanceFactor, RHICmdList);
	}
}
