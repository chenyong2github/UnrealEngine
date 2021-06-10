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


IMPLEMENT_STATIC_UNIFORM_BUFFER_SLOT(InstanceCullingUbSlot);
IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FInstanceCullingGlobalUniforms, "InstanceCulling", InstanceCullingUbSlot);

static int32 GAllowBatchedBuildRenderingCommands = 1;
static FAutoConsoleVariableRef CVarAllowBatchedBuildRenderingCommands(
	TEXT("r.InstanceCulling.AllowBatchedBuildRenderingCommands"),
	GAllowBatchedBuildRenderingCommands,
	TEXT("Whether to allow batching BuildRenderingCommands for GPU instance culling"),
	ECVF_RenderThreadSafe);

FInstanceCullingContext::FInstanceCullingContext(FInstanceCullingManager* InInstanceCullingManager, TArrayView<const int32> InViewIds, enum EInstanceCullingMode InInstanceCullingMode, bool bInDrawOnlyVSMInvalidatingGeometry) :
	InstanceCullingManager(InInstanceCullingManager),
	ViewIds(InViewIds),
	bIsEnabled(InInstanceCullingManager == nullptr || InInstanceCullingManager->IsEnabled()),
	InstanceCullingMode(InInstanceCullingMode),
	bDrawOnlyVSMInvalidatingGeometry(bInDrawOnlyVSMInvalidatingGeometry)
{
}


void FInstanceCullingContext::ResetCommands(int32 MaxNumCommands)
{
	CullingCommands.Empty(MaxNumCommands);
	InstanceRuns.Reset();
	PrimitiveIds.Reset();
	TotalInstances = 0U;
}

void FInstanceCullingContext::BeginCullingCommand(EPrimitiveType BatchType, uint32 BaseVertexIndex, uint32 FirstIndex, uint32 NumPrimitives, bool bInMaterialMayModifyPosition)
{
	if (ensure(BatchType < PT_Num))
	{
		// default to PT_TriangleList & PT_RectList
		int32 NumVerticesOrIndices = NumPrimitives * 3;
		switch (BatchType)
		{
		case PT_QuadList:
			NumVerticesOrIndices = NumPrimitives * 4;
			break;
		case PT_TriangleStrip:
			NumVerticesOrIndices = NumPrimitives + 2;
			break;
		case PT_LineList:
			NumVerticesOrIndices = NumPrimitives * 2;
			break;
		case PT_PointList:
			NumVerticesOrIndices = NumPrimitives;
			break;
		default:
			break;
		}
		FPrimCullingCommand& CullingCommand = CullingCommands.AddDefaulted_GetRef();
		CullingCommand.BaseVertexIndex = BaseVertexIndex;
		CullingCommand.FirstIndex = FirstIndex;
		CullingCommand.NumVerticesOrIndices = NumVerticesOrIndices;
		CullingCommand.FirstPrimitiveIdOffset = PrimitiveIds.Num();
		CullingCommand.FirstInstanceRunOffset = InstanceRuns.Num();
		CullingCommand.bMaterialMayModifyPosition = bInMaterialMayModifyPosition;
	}
}

void FInstanceCullingContext::AddPrimitiveToCullingCommand(int32 ScenePrimitiveId, uint32 NumInstances)
{
	TotalInstances += NumInstances;
	PrimitiveIds.Add(ScenePrimitiveId);
}

void FInstanceCullingContext::AddInstanceRunToCullingCommand(int32 ScenePrimitiveId, const uint32* Runs, uint32 NumRuns)
{
	//InstanceRuns.AddDefaulted(NumRuns);
	for (uint32 Index = 0; Index < NumRuns; ++Index)
	{
		uint32 RunStart = Runs[Index * 2];
		uint32 RunEndIncl = Runs[Index * 2 + 1];
		TotalInstances += (RunEndIncl + 1U) - RunStart;
		InstanceRuns.Add(FInstanceRun{ RunStart, RunEndIncl, ScenePrimitiveId });
	}
}

class FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs);
	SHADER_USE_PARAMETER_STRUCT(FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs, FGlobalShader)

public:
	static constexpr int32 NumThreadsPerGroup = 64;

	// GPUCULL_TODO: remove once buffer is somehow unified
	class FOutputCommandIdDim : SHADER_PERMUTATION_BOOL("OUTPUT_COMMAND_IDS");
	class FCullInstancesDim : SHADER_PERMUTATION_BOOL("CULL_INSTANCES");
	class FStereoModeDim : SHADER_PERMUTATION_BOOL("STEREO_CULLING_MODE");
	// This permutation should be used for all debug output etc that adds overhead not wanted in production. 
	// Individual debug features should be controlled by dynamic switches rather than adding more permutations.
	// TODO: maybe disable permutation in shipping builds?
	class FDebugModeDim : SHADER_PERMUTATION_BOOL("DEBUG_MODE");
	class FBatchedDim : SHADER_PERMUTATION_BOOL("ENABLE_BATCH_MODE");

	using FPermutationDomain = TShaderPermutationDomain<FOutputCommandIdDim, FCullInstancesDim, FStereoModeDim, FDebugModeDim, FBatchedDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return UseGPUScene(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("INDIRECT_ARGS_NUM_WORDS"), FInstanceCullingContext::IndirectArgsNumWords);
		OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
		OutEnvironment.SetDefine(TEXT("USE_GLOBAL_GPU_SCENE_DATA"), 1);
		OutEnvironment.SetDefine(TEXT("NUM_THREADS_PER_GROUP"), NumThreadsPerGroup);
		OutEnvironment.SetDefine(TEXT("NANITE_MULTI_VIEW"), 1);
		OutEnvironment.SetDefine(TEXT("PRIM_ID_DYNAMIC_FLAG"), GPrimIDDynamicFlag);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUSceneInstanceSceneData)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUScenePrimitiveSceneData)
		SHADER_PARAMETER(uint32, InstanceDataSOAStride)
		SHADER_PARAMETER(uint32, GPUSceneFrameNumber)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FInstanceCullingContext::FPrimCullingCommand >, PrimitiveCullingCommands)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< int32 >, PrimitiveIds)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FInstanceCullingContext::FInstanceRun >, InstanceRuns)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint32 >, VisibleInstanceFlags)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint32 >, ViewIds)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FBatchInfo >, BatchInfos)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint32 >, BatchInds)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutputOffsetBufferOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, InstanceIdOffsetBufferOut)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, InstanceIdsBufferOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, DrawCommandIdsBufferOut)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, DrawIndirectArgsBufferOut)
		SHADER_PARAMETER(int32, NumPrimitiveIds)
		SHADER_PARAMETER(int32, NumInstanceRuns)		
		SHADER_PARAMETER(int32, NumCommands)
		SHADER_PARAMETER(uint32, NumInstanceFlagWords)
		SHADER_PARAMETER(uint32, NumCulledInstances)
		SHADER_PARAMETER(uint32, NumCulledViews)
		SHADER_PARAMETER(int32, NumViewIds)
		SHADER_PARAMETER(int32, bDrawOnlyVSMInvalidatingGeometry)

		SHADER_PARAMETER(int32, DynamicPrimitiveIdOffset)
		SHADER_PARAMETER(int32, DynamicPrimitiveIdMax)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs, "/Engine/Private/InstanceCulling/BuildInstanceDrawCommands.usf", "BuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs", SF_Compute);

const TRDGUniformBufferRef<FInstanceCullingGlobalUniforms> FInstanceCullingContext::CreateDummyInstanceCullingUniformBuffer(FRDGBuilder& GraphBuilder)
{
	FInstanceCullingGlobalUniforms* InstanceCullingGlobalUniforms = GraphBuilder.AllocParameters<FInstanceCullingGlobalUniforms>();
	FRDGBufferRef DummyBuffer = GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, 4);
	InstanceCullingGlobalUniforms->InstanceIdsBuffer = GraphBuilder.CreateSRV(DummyBuffer);
	InstanceCullingGlobalUniforms->PageInfoBuffer = GraphBuilder.CreateSRV(DummyBuffer);
	InstanceCullingGlobalUniforms->BufferCapacity = 0;
	return GraphBuilder.CreateUniformBuffer(InstanceCullingGlobalUniforms);
}

void FInstanceCullingContext::BuildRenderingCommands(
	FRDGBuilder& GraphBuilder,
	const FGPUScene& GPUScene,
	const TRange<int32>& DynamicPrimitiveIdRange,
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

	if (InstanceCullingDrawParams && InstanceCullingManager && InstanceCullingManager->IsDeferredCullingActive())
	{
		Results.DrawIndirectArgsBuffer = InstanceCullingManager->BatchedCullingScratch.DrawIndirectArgsBuffer;
		Results.InstanceIdOffsetBuffer = InstanceCullingManager->BatchedCullingScratch.InstanceIdOffsetBuffer;
		Results.UniformBuffer = InstanceCullingManager->BatchedCullingScratch.UniformBuffer;
		InstanceCullingManager->BatchedCullingScratch.Batches.Add(FBatchItem{ this, InstanceCullingDrawParams, DynamicPrimitiveIdRange });
		return;
	}

	ensure(InstanceCullingMode == EInstanceCullingMode::Normal || ViewIds.Num() == 2);

	RDG_EVENT_SCOPE(GraphBuilder, "BuildRenderingCommands");

	const ERHIFeatureLevel::Type FeatureLevel = GPUScene.GetFeatureLevel();
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);

	// Note: use start at zero offset if there is no instance culling manager, this means each build rendering commands pass will overwrite the same ID range. Which is only ok assuming correct barriers (should be erring on this side by default).
	TArray<uint32, TInlineAllocator<1> > NullArray;
	NullArray.AddZeroed(1);
	FRDGBufferRef InstanceIdOutOffsetBufferRDG = CreateStructuredBuffer(GraphBuilder, TEXT("InstanceCulling.OutputOffsetBufferOut"), NullArray);
	// If there is no manager, then there is no data on culling, so set flag to skip that and ignore buffers.
	FRDGBufferRef VisibleInstanceFlagsRDG = InstanceCullingManager != nullptr ? InstanceCullingManager->CullingIntermediate.VisibleInstanceFlags : nullptr;
	const bool bCullInstances = InstanceCullingManager != nullptr;
	const uint32 NumCulledInstances = InstanceCullingManager != nullptr ? uint32(InstanceCullingManager->CullingIntermediate.NumInstances) : 0U;
	const uint32 NumCulledViews = InstanceCullingManager != nullptr ? uint32(InstanceCullingManager->CullingIntermediate.NumViews) : 0U;
	uint32 NumInstanceFlagWords = FMath::DivideAndRoundUp(NumCulledInstances, uint32(sizeof(uint32) * 8));

	// Add any other conditions that needs debug code running here.
	const bool bUseDebugMode = bDrawOnlyVSMInvalidatingGeometry;

	FRDGBufferRef PrimitiveCullingCommandsBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("InstanceCulling.PrimitiveCullingCommands"), CullingCommands);
	FRDGBufferRef PrimitiveIdsBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("InstanceCulling.PrimitiveIds"), PrimitiveIds);
	FRDGBufferRef PrimitiveInstanceRunsBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("InstanceCulling.InstanceRuns"), InstanceRuns);
	FRDGBufferRef ViewIdsBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("InstanceCulling.ViewIds"), ViewIds);

	const uint32 InstanceIdBufferSize = TotalInstances * ViewIds.Num();
	FRDGBufferRef InstanceIdsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), InstanceIdBufferSize), TEXT("InstanceCulling.InstanceIdsBuffer"));

	FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FParameters* PassParameters = GraphBuilder.AllocParameters<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FParameters>();

	// Because the view uniforms are not set up by the time this runs
	// PassParameters->View = View.ViewUniformBuffer;
	// Set up global GPU-scene data instead...
	PassParameters->GPUSceneInstanceSceneData = GPUScene.InstanceDataBuffer.SRV;
	PassParameters->GPUScenePrimitiveSceneData = GPUScene.PrimitiveBuffer.SRV;
	PassParameters->InstanceDataSOAStride = GPUScene.InstanceDataSOAStride;
	PassParameters->GPUSceneFrameNumber = GPUScene.GetSceneFrameNumber();

	// Upload data etc
	PassParameters->PrimitiveCullingCommands = GraphBuilder.CreateSRV(PrimitiveCullingCommandsBuffer);

	PassParameters->PrimitiveIds = GraphBuilder.CreateSRV(PrimitiveIdsBuffer);
	PassParameters->DynamicPrimitiveIdOffset = DynamicPrimitiveIdRange.GetLowerBoundValue();
	PassParameters->DynamicPrimitiveIdMax = DynamicPrimitiveIdRange.GetUpperBoundValue();

	PassParameters->InstanceRuns = GraphBuilder.CreateSRV(PrimitiveInstanceRunsBuffer);

	PassParameters->OutputOffsetBufferOut = GraphBuilder.CreateUAV(InstanceIdOutOffsetBufferRDG);

	FRDGBufferRef DrawIndirectArgsRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(IndirectArgsNumWords * CullingCommands.Num()), TEXT("InstanceCulling.DrawIndirectArgsBuffer"));
	// not using structured buffer as we want/have toget at it as a vertex buffer 
	FRDGBufferRef InstanceIdOffsetBufferRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), CullingCommands.Num()), TEXT("InstanceCulling.InstanceIdOffsetBuffer"));

	PassParameters->ViewIds = GraphBuilder.CreateSRV(ViewIdsBuffer);
	PassParameters->NumViewIds = ViewIds.Num();
	PassParameters->bDrawOnlyVSMInvalidatingGeometry = bDrawOnlyVSMInvalidatingGeometry;

	PassParameters->InstanceIdsBufferOut = GraphBuilder.CreateUAV(InstanceIdsBuffer);
	PassParameters->DrawIndirectArgsBufferOut = GraphBuilder.CreateUAV(DrawIndirectArgsRDG, PF_R32_UINT);
	PassParameters->InstanceIdOffsetBufferOut = GraphBuilder.CreateUAV(InstanceIdOffsetBufferRDG, PF_R32_UINT);
	PassParameters->NumPrimitiveIds = PrimitiveIds.Num();
	PassParameters->NumInstanceRuns = InstanceRuns.Num();
	PassParameters->NumCommands = CullingCommands.Num();
	PassParameters->VisibleInstanceFlags = VisibleInstanceFlagsRDG ? GraphBuilder.CreateSRV(VisibleInstanceFlagsRDG) : nullptr;

	PassParameters->NumInstanceFlagWords = NumInstanceFlagWords;
	PassParameters->NumCulledInstances = NumCulledInstances;
	PassParameters->NumCulledViews = NumCulledViews;

	FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FPermutationDomain PermutationVector;
	PermutationVector.Set<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FOutputCommandIdDim>(0);
	PermutationVector.Set<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FCullInstancesDim>(bCullInstances);
	PermutationVector.Set<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FStereoModeDim>(InstanceCullingMode == EInstanceCullingMode::Stereo);
	PermutationVector.Set<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FDebugModeDim>(bUseDebugMode);
	PermutationVector.Set<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FBatchedDim>(false);

	auto ComputeShader = ShaderMap->GetShader<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs>(PermutationVector);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("BuildInstanceIdBufferAndCommandsFromPrimitiveIds"),
		ComputeShader,
		PassParameters,
		FIntVector(CullingCommands.Num(), 1, 1)
	);

	Results.DrawIndirectArgsBuffer = DrawIndirectArgsRDG;
	Results.InstanceIdOffsetBuffer = InstanceIdOffsetBufferRDG;

	FInstanceCullingGlobalUniforms* UniformParameters = GraphBuilder.AllocParameters<FInstanceCullingGlobalUniforms>();
	UniformParameters->InstanceIdsBuffer = GraphBuilder.CreateSRV(InstanceIdsBuffer);
	UniformParameters->PageInfoBuffer = GraphBuilder.CreateSRV(InstanceIdsBuffer);
	UniformParameters->BufferCapacity = InstanceIdBufferSize;
	Results.UniformBuffer = GraphBuilder.CreateUniformBuffer(UniformParameters);
}

void FInstanceCullingContext::BuildRenderingCommands(FRDGBuilder& GraphBuilder, FGPUScene& GPUScene, const TRange<int32>& DynamicPrimitiveIdRange, FInstanceCullingRdgParams& Params) const
{
	if (CullingCommands.Num() == 0)
	{
		return;
	}

	RDG_EVENT_SCOPE(GraphBuilder, "BuildRenderingCommands");

	const ERHIFeatureLevel::Type FeatureLevel = GPUScene.GetFeatureLevel();
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);

	const FInstanceCullingIntermediate& Intermediate = InstanceCullingManager->CullingIntermediate;

	FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FParameters* PassParameters = GraphBuilder.AllocParameters<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FParameters>();
	// Because the view uniforms are not set up by the time this runs
	// PassParameters->View = View.ViewUniformBuffer;
	// Set up global GPU-scene data instead...
	PassParameters->GPUSceneInstanceSceneData = GPUScene.InstanceDataBuffer.SRV;
	PassParameters->GPUScenePrimitiveSceneData = GPUScene.PrimitiveBuffer.SRV;
	PassParameters->InstanceDataSOAStride = GPUScene.InstanceDataSOAStride;
	PassParameters->GPUSceneFrameNumber = GPUScene.GetSceneFrameNumber();

	// Upload data etc
	Params.PrimitiveCullingCommands = CreateStructuredBuffer(GraphBuilder, TEXT("InstanceCulling.PrimitiveCullingCommands"), CullingCommands);
	PassParameters->PrimitiveCullingCommands = GraphBuilder.CreateSRV(Params.PrimitiveCullingCommands);

	PassParameters->PrimitiveIds = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("InstanceCulling.PrimitiveIds"), PrimitiveIds));
	PassParameters->DynamicPrimitiveIdOffset = DynamicPrimitiveIdRange.GetLowerBoundValue();
	PassParameters->DynamicPrimitiveIdMax = DynamicPrimitiveIdRange.GetUpperBoundValue();

	PassParameters->InstanceRuns = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("InstanceCulling.InstanceRuns"), InstanceRuns));


	FRDGBufferRef VisibleInstanceFlagsRDG = Intermediate.VisibleInstanceFlags;

	// Create and initialize if not allocated
	if (Params.InstanceIdWriteOffsetBuffer == nullptr)
	{
		TArray<uint32, TInlineAllocator<1> > NullArray;
		NullArray.AddZeroed(1);
		Params.InstanceIdWriteOffsetBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("InstanceCulling.InstanceIdWriteOffsetBuffer"), NullArray);
	}

	PassParameters->OutputOffsetBufferOut = GraphBuilder.CreateUAV(Params.InstanceIdWriteOffsetBuffer);

	Params.DrawIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(IndirectArgsNumWords * CullingCommands.Num()), TEXT("InstanceCulling.DrawIndirectArgsBuffer"));
	// not using structured buffer as we want/have to get at it as a vertex buffer 
	Params.InstanceIdStartOffsetBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), CullingCommands.Num()), TEXT("InstanceCulling.InstanceIdOffsetBuffer"));

	PassParameters->ViewIds = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("InstanceCulling.ViewIds"), ViewIds));
	PassParameters->NumViewIds = ViewIds.Num();
	PassParameters->bDrawOnlyVSMInvalidatingGeometry = 0;


	// TODO: Access resources through manager rather than global
	PassParameters->DrawIndirectArgsBufferOut = GraphBuilder.CreateUAV(Params.DrawIndirectArgs, PF_R32_UINT);
	PassParameters->InstanceIdOffsetBufferOut = GraphBuilder.CreateUAV(Params.InstanceIdStartOffsetBuffer, PF_R32_UINT);
	PassParameters->NumPrimitiveIds = PrimitiveIds.Num();
	PassParameters->NumInstanceRuns = InstanceRuns.Num();
	PassParameters->NumCommands = CullingCommands.Num();
	PassParameters->VisibleInstanceFlags = GraphBuilder.CreateSRV(VisibleInstanceFlagsRDG);

	if (Params.InstanceIdsBuffer == nullptr)
	{
		// TODO: we could compute the max instance count from the MDCs.
		const int32 InstanceIdBufferSize = TotalInstances * ViewIds.Num();
		Params.InstanceIdsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), InstanceIdBufferSize), TEXT("InstanceCulling.InstanceIdsBuffer"));
		Params.DrawCommandIdsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), InstanceIdBufferSize), TEXT("InstanceCulling.DrawCommandIdsBuffer"));

	}
	
	PassParameters->InstanceIdsBufferOut = GraphBuilder.CreateUAV(Params.InstanceIdsBuffer);
	PassParameters->DrawCommandIdsBufferOut = GraphBuilder.CreateUAV(Params.DrawCommandIdsBuffer);

	int32 NumInstanceFlagWords = FMath::DivideAndRoundUp(Intermediate.NumInstances, int32(sizeof(uint32) * 8));
	PassParameters->NumInstanceFlagWords = uint32(NumInstanceFlagWords);
	PassParameters->NumCulledInstances = uint32(Intermediate.NumInstances);
	PassParameters->NumCulledViews = uint32(Intermediate.NumViews);


	FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FPermutationDomain PermutationVector;
	PermutationVector.Set<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FOutputCommandIdDim>(1);
	PermutationVector.Set<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FCullInstancesDim>(false);
	PermutationVector.Set<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FStereoModeDim>(false);
	PermutationVector.Set<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FDebugModeDim>(false);

	auto ComputeShader = ShaderMap->GetShader<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs>(PermutationVector);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("BuildInstanceIdBufferAndCommandsFromPrimitiveIds"),
		ComputeShader,
		PassParameters,
		FIntVector(CullingCommands.Num(), 1, 1)
	);
}

/** Helper function to merge GPU instance culling input batches queued throughout a frame. */
static void MergeInstanceCullingContext(
	const TArray<FInstanceCullingContext::FBatchItem, SceneRenderingAllocator>& Batches,
	FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FParameters* PassParameters,
	FInstanceCullingContextMerged& MergedContext)
{
	if (!MergedContext.bInitialized)
	{
		MergedContext.bInitialized = true;

		// TODO: In the future, we should pre-upload the commands for static meshes when cached, only dynamic need uploading each frame, they should be kicked asynchronously or flushed in batches.
		//       Then this would only need indices.

		// 1. conglomerate and upload data for all batches - consider pre-allocating this in shared place directly to avoid copy.
		TArray<FInstanceCullingContext::FPrimCullingCommand, SceneRenderingAllocator>& CullingCommandsAll = MergedContext.CullingCommandsAll;
		TArray<int32, SceneRenderingAllocator>& PrimitiveIdsAll = MergedContext.PrimitiveIdsAll;
		TArray<FInstanceCullingContext::FInstanceRun, SceneRenderingAllocator>& InstanceRunsAll = MergedContext.InstanceRunsAll;
		TArray<int32, SceneRenderingAllocator>& ViewIdsAll = MergedContext.ViewIdsAll;
		TArray<FInstanceCullingContext::FBatchInfo, SceneRenderingAllocator>& BatchInfos = MergedContext.BatchInfos;
		BatchInfos.AddDefaulted(Batches.Num());
		MergedContext.InstanceIdBufferSize = 0U;

		// Index that maps from each command to the corresponding batch - maybe not the utmost efficiency
		TArray<int32, SceneRenderingAllocator>& BatchIndsAll = MergedContext.BatchIndsAll;
		for (int32 BatchIndex = 0; BatchIndex < Batches.Num(); ++BatchIndex)
		{
			const FInstanceCullingContext::FBatchItem& BatchItem = Batches[BatchIndex];
			const FInstanceCullingContext& InstanceCullingContext = *BatchItem.Context;

			FInstanceCullingContext::FBatchInfo& BatchInfo = BatchInfos[BatchIndex];

			BatchInfo.CullingCommandsOffset = CullingCommandsAll.Num();
			BatchInfo.NumCullingCommands = InstanceCullingContext.CullingCommands.Num();
			CullingCommandsAll.Append(InstanceCullingContext.CullingCommands);

			BatchInfo.PrimitiveIdsOffset = PrimitiveIdsAll.Num();
			BatchInfo.NumPrimitiveIds = InstanceCullingContext.PrimitiveIds.Num();
			PrimitiveIdsAll.Append(InstanceCullingContext.PrimitiveIds);

			BatchInfo.InstanceRunsOffset = InstanceRunsAll.Num();
			BatchInfo.NumInstanceRuns = InstanceCullingContext.InstanceRuns.Num();
			InstanceRunsAll.Append(InstanceCullingContext.InstanceRuns);

			BatchInfo.ViewIdsOffset = ViewIdsAll.Num();
			BatchInfo.NumViewIds = InstanceCullingContext.ViewIds.Num();
			ViewIdsAll.Append(InstanceCullingContext.ViewIds);

			BatchInfo.DynamicPrimitiveIdOffset = BatchItem.DynamicPrimitiveIdRange.GetLowerBoundValue();
			BatchInfo.DynamicPrimitiveIdMax = BatchItem.DynamicPrimitiveIdRange.GetUpperBoundValue();

			// Map to batch from index in command list
			// TODO: could be made more efficient than having an index back to the batch for each command
			BatchIndsAll.AddDefaulted(InstanceCullingContext.CullingCommands.Num());
			for (int32 Index = BatchInfo.CullingCommandsOffset; Index < BatchIndsAll.Num(); ++Index)
			{
				BatchIndsAll[Index] = BatchIndex;
			}

			MergedContext.InstanceIdBufferSize += InstanceCullingContext.TotalInstances * InstanceCullingContext.ViewIds.Num();

			FInstanceCullingDrawParams& Result = *BatchItem.Result;
			Result.DrawCommandDataOffset = BatchInfo.CullingCommandsOffset;
		}

		PassParameters->NumViewIds = ViewIdsAll.Num();
		PassParameters->NumPrimitiveIds = PrimitiveIdsAll.Num();
		PassParameters->NumInstanceRuns = InstanceRunsAll.Num();
		PassParameters->NumCommands = CullingCommandsAll.Num();
	}
}

void FInstanceCullingContext::BuildRenderingCommandsDeferred(
	FRDGBuilder& GraphBuilder,
	FGPUScene& GPUScene,
	FInstanceCullingManager& InstanceCullingManager)
{
#define INST_CULL_CALLBACK(CustomCode) \
	[&Batches, PassParameters, &MergedContext]() \
	{ \
		MergeInstanceCullingContext(Batches, PassParameters, MergedContext); \
		return CustomCode; \
	}
#define INST_CULL_CREATE_STRUCT_BUFF_ARGS(ArrayName) \
	GraphBuilder, \
	TEXT("InstanceCulling.") TEXT(#ArrayName), \
	MergedContext.ArrayName.GetTypeSize(), \
	INST_CULL_CALLBACK(MergedContext.ArrayName.Num()), \
	INST_CULL_CALLBACK(MergedContext.ArrayName.GetData()), \
	INST_CULL_CALLBACK(MergedContext.ArrayName.Num() * MergedContext.ArrayName.GetTypeSize())

	// Cannot defer pass execution in immediate mode.
	// TODO: support batching when r.RDG.Drain is enabled
	if (!AllowBatchedBuildRenderingCommands(GPUScene))
	{
		return;
	}

	// If there are no instances, there can be no work to perform later.
	if (InstanceCullingManager.CullingIntermediate.NumInstances == 0 || InstanceCullingManager.CullingIntermediate.NumViews == 0)
	{
		return;
	}

	FBatchedInstanceCullingScratchSpace& BatchedCullingScratch = InstanceCullingManager.BatchedCullingScratch;

	checkf(!BatchedCullingScratch.bBatchingActive, TEXT("Batched GPU instance culling has already been enabled."));
	BatchedCullingScratch.bBatchingActive = true;

	const TArray<FBatchItem, SceneRenderingAllocator>& Batches = BatchedCullingScratch.Batches;
	FInstanceCullingContextMerged& MergedContext = BatchedCullingScratch.MergedContext;

	FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FParameters* PassParameters = GraphBuilder.AllocParameters<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FParameters>();

	FRDGBufferRef CullingCommandsRDG = CreateStructuredBuffer(INST_CULL_CREATE_STRUCT_BUFF_ARGS(CullingCommandsAll));
	FRDGBufferRef PrimitiveIdsRDG = CreateStructuredBuffer(INST_CULL_CREATE_STRUCT_BUFF_ARGS(PrimitiveIdsAll));
	FRDGBufferRef InstanceRunsRDG = CreateStructuredBuffer(INST_CULL_CREATE_STRUCT_BUFF_ARGS(InstanceRunsAll));
	FRDGBufferRef ViewIdsRDG = CreateStructuredBuffer(INST_CULL_CREATE_STRUCT_BUFF_ARGS(ViewIdsAll));
	FRDGBufferRef BatchInfosRDG = CreateStructuredBuffer(INST_CULL_CREATE_STRUCT_BUFF_ARGS(BatchInfos));
	FRDGBufferRef BatchIndsRDG = CreateStructuredBuffer(INST_CULL_CREATE_STRUCT_BUFF_ARGS(BatchIndsAll));
	FRDGBufferRef InstanceIdsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1), TEXT("InstanceCulling.InstanceIdsBuffer"), INST_CULL_CALLBACK(MergedContext.InstanceIdBufferSize));

	// Note: use start at zero offset if there is no instance culling manager, this means each build rendering commands pass will overwrite the same ID range. Which is only ok assuming correct barriers (should be erring on this side by default).
	TArray<uint32, TInlineAllocator<1> > NullArray;
	NullArray.AddZeroed(1);
	FRDGBufferRef InstanceIdOutOffsetBufferRDG = CreateStructuredBuffer(GraphBuilder, TEXT("InstanceCulling.OutputOffsetBufferOut"), NullArray);
	// If there is no manager, then there is no data on culling, so set flag to skip that and ignore buffers.
	FRDGBufferRef VisibleInstanceFlagsRDG = InstanceCullingManager.CullingIntermediate.VisibleInstanceFlags;
	const int32 NumCulledInstances = InstanceCullingManager.CullingIntermediate.NumInstances;
	const uint32 NumCulledViews = uint32(InstanceCullingManager.CullingIntermediate.NumViews);
	int32 NumInstanceFlagWords = FMath::DivideAndRoundUp(NumCulledInstances, int32(sizeof(uint32) * 8));

	// Because the view uniforms are not set up by the time this runs
	// PassParameters->View = View.ViewUniformBuffer;
	// Set up global GPU-scene data instead...
	PassParameters->GPUSceneInstanceSceneData = GPUScene.InstanceDataBuffer.SRV;
	PassParameters->GPUScenePrimitiveSceneData = GPUScene.PrimitiveBuffer.SRV;
	PassParameters->InstanceDataSOAStride = GPUScene.InstanceDataSOAStride;
	// Upload data etc
	PassParameters->PrimitiveCullingCommands = GraphBuilder.CreateSRV(CullingCommandsRDG);

	PassParameters->PrimitiveIds = GraphBuilder.CreateSRV(PrimitiveIdsRDG);
	PassParameters->InstanceRuns = GraphBuilder.CreateSRV(InstanceRunsRDG);
	PassParameters->BatchInfos = GraphBuilder.CreateSRV(BatchInfosRDG);
	PassParameters->BatchInds = GraphBuilder.CreateSRV(BatchIndsRDG);

	PassParameters->OutputOffsetBufferOut = GraphBuilder.CreateUAV(InstanceIdOutOffsetBufferRDG);

	BatchedCullingScratch.DrawIndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(), TEXT("DrawIndirectArgsBuffer"), INST_CULL_CALLBACK(FInstanceCullingContext::IndirectArgsNumWords * MergedContext.CullingCommandsAll.Num()));
	// not using structured buffer as we want/have to get at it as a vertex buffer 
	//FRDGBufferRef InstanceIdsBufferRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), PrimitiveIds.Num() * FInstanceCullingManager::MaxAverageInstanceFactor), TEXT("InstanceIdsBuffer"));
	BatchedCullingScratch.InstanceIdOffsetBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("InstanceIdOffsetBuffer"), INST_CULL_CALLBACK(MergedContext.CullingCommandsAll.Num()));

	PassParameters->ViewIds = GraphBuilder.CreateSRV(ViewIdsRDG);

	PassParameters->InstanceIdsBufferOut = GraphBuilder.CreateUAV(InstanceIdsBuffer);
	PassParameters->DrawIndirectArgsBufferOut = GraphBuilder.CreateUAV(BatchedCullingScratch.DrawIndirectArgsBuffer, PF_R32_UINT);
	PassParameters->InstanceIdOffsetBufferOut = GraphBuilder.CreateUAV(BatchedCullingScratch.InstanceIdOffsetBuffer, PF_R32_UINT);
	PassParameters->VisibleInstanceFlags = VisibleInstanceFlagsRDG ? GraphBuilder.CreateSRV(VisibleInstanceFlagsRDG) : nullptr;

	PassParameters->NumInstanceFlagWords = uint32(NumInstanceFlagWords);
	PassParameters->NumCulledInstances = uint32(NumCulledInstances);
	PassParameters->NumCulledViews = NumCulledViews;

	FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FPermutationDomain PermutationVector;
	PermutationVector.Set<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FOutputCommandIdDim>(0);
	PermutationVector.Set<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FCullInstancesDim>(true);
	PermutationVector.Set<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FBatchedDim>(true);

	auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs>(PermutationVector);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("BuildInstanceIdBufferAndCommandsFromPrimitiveIds"),
		ComputeShader,
		PassParameters,
		INST_CULL_CALLBACK(FIntVector(MergedContext.CullingCommandsAll.Num(), 1, 1)));
	
	FInstanceCullingGlobalUniforms* UniformParameters = GraphBuilder.AllocParameters<FInstanceCullingGlobalUniforms>();
	UniformParameters->InstanceIdsBuffer = GraphBuilder.CreateSRV(InstanceIdsBuffer);
	UniformParameters->PageInfoBuffer = GraphBuilder.CreateSRV(InstanceIdsBuffer);
	UniformParameters->BufferCapacity = 0U; // TODO: this is not used at the moment, but is intended for range checks so would have been good.
	BatchedCullingScratch.UniformBuffer = GraphBuilder.CreateUniformBuffer(UniformParameters);


#undef INST_CULL_CREATE_STRUCT_BUFF_ARGS
#undef INST_CULL_CALLBACK
}

bool FInstanceCullingContext::AllowBatchedBuildRenderingCommands(const FGPUScene& GPUScene)
{
	static IConsoleVariable* CVarRDGDrain = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RDG.Drain"));
	check(CVarRDGDrain);
	return GPUScene.IsEnabled() && !!GAllowBatchedBuildRenderingCommands && !FRDGBuilder::IsImmediateMode() && !CVarRDGDrain->GetInt();
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


	QUICK_SCOPE_CYCLE_COUNTER(STAT_DynamicInstancingOfVisibleMeshDrawCommands);

	ResetCommands(VisibleMeshDrawCommandsInOut.Num());

	int32 CurrentStateBucketId = -1;
	MaxInstances = 1;
	// Only used to supply stats
	int32 CurrentAutoInstanceCount = 1;
	// Scan through and compact away all with consecutive statebucked ID, and record primitive IDs in GPU-scene culling command
	const int32 NumDrawCommandsIn = VisibleMeshDrawCommandsInOut.Num();
	int32 NumDrawCommandsOut = 0;
	// Allocate conservatively for all commands, may not use all.
	for (int32 DrawCommandIndex = 0; DrawCommandIndex < NumDrawCommandsIn; ++DrawCommandIndex)
	{
		const FVisibleMeshDrawCommand& RESTRICT VisibleMeshDrawCommand = PassVisibleMeshDrawCommands[DrawCommandIndex];

		const bool bSupportsGPUSceneInstancing = EnumHasAnyFlags(VisibleMeshDrawCommand.Flags, EFVisibleMeshDrawCommandFlags::HasPrimitiveIdStreamIndex);
		const bool bMaterialMayModifyPosition = EnumHasAnyFlags(VisibleMeshDrawCommand.Flags, EFVisibleMeshDrawCommandFlags::MaterialMayModifyPosition);

		if (bCompactIdenticalCommands && CurrentStateBucketId != -1 && VisibleMeshDrawCommand.StateBucketId == CurrentStateBucketId)
		{
			// Drop since previous covers for this

			// Update auto-instance count (only needed for logging)
			CurrentAutoInstanceCount++;
			MaxInstances = FMath::Max(CurrentAutoInstanceCount, MaxInstances);
		}
		else
		{
			// Reset auto-instance count (only needed for logging)
			CurrentAutoInstanceCount = 1;

			const FMeshDrawCommand* RESTRICT MeshDrawCommand = VisibleMeshDrawCommand.MeshDrawCommand;

			// GPUCULL_TODO: Always allocate command as otherwise the 1:1 mapping between mesh draw command index and culling command index is broken.
			// if (bSupportsGPUSceneInstancing)
			{
				// GPUCULL_TODO: Prepackage the culling command in the visible mesh draw command, or as a separate array and just index here, or even better - on the GPU (for cached CMDs at least).
				//               We don't really want to dereference the MeshDrawCommand here.
				BeginCullingCommand(MeshDrawCommand->PrimitiveType, MeshDrawCommand->VertexParams.BaseVertexIndex, MeshDrawCommand->FirstIndex, MeshDrawCommand->NumPrimitives, bMaterialMayModifyPosition);
			}
			// Record the last bucket ID (may be -1)
			CurrentStateBucketId = VisibleMeshDrawCommand.StateBucketId;

			// If we have dropped any we need to move up
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
				// GPUCULL_TODO: This complexity should be removed once the HISM culling & LOD selection is on the GPU side
				AddInstanceRunToCullingCommand(VisibleMeshDrawCommand.DrawPrimitiveId, VisibleMeshDrawCommand.RunArray, VisibleMeshDrawCommand.NumRuns);
			}
			else
			{
				AddPrimitiveToCullingCommand(VisibleMeshDrawCommand.DrawPrimitiveId, VisibleMeshDrawCommand.MeshDrawCommand->NumInstances);
			}
		}
	}
	ensure(bCompactIdenticalCommands || NumDrawCommandsIn == NumDrawCommandsOut);
	ensureMsgf(NumDrawCommandsOut == CullingCommands.Num(), TEXT("There must be a 1:1 mapping between culling commands and mesh draw commands, as this assumption is made in SubmitGPUInstancedMeshDrawCommandsRange."));
	// Setup instancing stats for logging.
	VisibleMeshDrawCommandsNum = VisibleMeshDrawCommandsInOut.Num();
	NewPassVisibleMeshDrawCommandsNum = NumDrawCommandsOut;

	// Resize array post-compaction of dynamic instances
	VisibleMeshDrawCommandsInOut.SetNum(NumDrawCommandsOut, false);
}

