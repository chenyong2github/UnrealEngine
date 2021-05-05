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

#define ENABLE_DETERMINISTIC_INSTANCE_CULLING 0


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
}

void FInstanceCullingContext::BeginCullingCommand(EPrimitiveType BatchType, uint32 BaseVertexIndex, uint32 FirstIndex, uint32 NumPrimitives, bool bInMaterialMayModifyPosition)
{
#if GPUCULL_TODO
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
#endif
}

void FInstanceCullingContext::AddPrimitiveToCullingCommand(int32 ScenePrimitiveId)
{
#if GPUCULL_TODO
	PrimitiveIds.Add(ScenePrimitiveId);
#endif
}

void FInstanceCullingContext::AddInstanceRunToCullingCommand(int32 ScenePrimitiveId, const uint32* Runs, uint32 NumRuns)
{
#if GPUCULL_TODO
	//InstanceRuns.AddDefaulted(NumRuns);
	for (uint32 Index = 0; Index < NumRuns; ++Index)
	{
		InstanceRuns.Add(FInstanceRun{ Runs[Index * 2], Runs[Index * 2 + 1], ScenePrimitiveId });
	}
#endif
}

#if GPUCULL_TODO

#if ENABLE_DETERMINISTIC_INSTANCE_CULLING

class FComputeInstanceIdOutputSizeCs : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FComputeInstanceIdOutputSizeCs);
	SHADER_USE_PARAMETER_STRUCT(FComputeInstanceIdOutputSizeCs, FGlobalShader)
public:
	class FCullInstancesDim : SHADER_PERMUTATION_BOOL("CULL_INSTANCES");
	using FPermutationDomain = TShaderPermutationDomain<FCullInstancesDim>;

	static constexpr int32 NumThreadsPerGroup = 64;

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
		OutEnvironment.SetDefine(TEXT("ENABLE_DETERMINISTIC_INSTANCE_CULLING"), 1);
		OutEnvironment.SetDefine(TEXT("PRIM_ID_DYNAMIC_FLAG"), GPrimIDDynamicFlag);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUSceneInstanceSceneData)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUScenePrimitiveSceneData)
		SHADER_PARAMETER(uint32, InstanceDataSOAStride)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FInstanceCullingContext::FPrimCullingCommand >, PrimitiveCullingCommands)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< int32 >, PrimitiveIds)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FInstanceCullingContext::FInstanceRun >, InstanceRuns)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint32 >, VisibleInstanceFlags)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint32 >, ViewIds)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutputOffsetBufferOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint2>, InstanceCountsOut)

		SHADER_PARAMETER(int32, NumPrimitiveIds)
		SHADER_PARAMETER(int32, NumInstanceRuns)
		SHADER_PARAMETER(int32, NumCommands)
		SHADER_PARAMETER(uint32, NumInstanceFlagWords)
		SHADER_PARAMETER(uint32, NumCulledInstances)
		SHADER_PARAMETER(uint32, NumCulledViews)
		SHADER_PARAMETER(int32, NumViewIds)

		SHADER_PARAMETER(int32, DynamicPrimitiveIdOffset)
		SHADER_PARAMETER(int32, DynamicPrimitiveIdMax)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FComputeInstanceIdOutputSizeCs, "/Engine/Private/InstanceCulling/BuildInstanceDrawCommands.usf", "ComputeInstanceIdOutputSize", SF_Compute);

class FCalcOutputOffsetsCs : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCalcOutputOffsetsCs);
	SHADER_USE_PARAMETER_STRUCT(FCalcOutputOffsetsCs, FGlobalShader)
public:
	static constexpr int32 NumThreadsPerGroup = 64;

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
		OutEnvironment.SetDefine(TEXT("ENABLE_DETERMINISTIC_INSTANCE_CULLING"), 1);
		OutEnvironment.SetDefine(TEXT("PRIM_ID_DYNAMIC_FLAG"), GPrimIDDynamicFlag);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, InstanceIdOffsetBufferOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutputOffsetBufferOut)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint2>, InstanceCounts)
		SHADER_PARAMETER(int32, NumCommands)
		SHADER_PARAMETER(int32, NumViewIds)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FCalcOutputOffsetsCs, "/Engine/Private/InstanceCulling/BuildInstanceDrawCommands.usf", "CalcOutputOffsets", SF_Compute);

class FOutputInstanceIdsAtOffsetCs : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FOutputInstanceIdsAtOffsetCs);
	SHADER_USE_PARAMETER_STRUCT(FOutputInstanceIdsAtOffsetCs, FGlobalShader)
public:
	static constexpr int32 NumThreadsPerGroup = 64;

	// GPUCULL_TODO: remove once buffer is somehow unified
	class FOutputCommandIdDim : SHADER_PERMUTATION_BOOL("OUTPUT_COMMAND_IDS");
	class FCullInstancesDim : SHADER_PERMUTATION_BOOL("CULL_INSTANCES");
	using FPermutationDomain = TShaderPermutationDomain<FOutputCommandIdDim, FCullInstancesDim>;

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
		OutEnvironment.SetDefine(TEXT("ENABLE_DETERMINISTIC_INSTANCE_CULLING"), 1);
		OutEnvironment.SetDefine(TEXT("PRIM_ID_DYNAMIC_FLAG"), GPrimIDDynamicFlag);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, InstanceIdOffsetBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint2>, InstanceCounts)

		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUSceneInstanceSceneData)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUScenePrimitiveSceneData)
		SHADER_PARAMETER(uint32, InstanceDataSOAStride)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FInstanceCullingContext::FPrimCullingCommand >, PrimitiveCullingCommands)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< int32 >, PrimitiveIds)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FInstanceCullingContext::FInstanceRun >, InstanceRuns)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint32 >, VisibleInstanceFlags)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint32 >, ViewIds)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, InstanceIdsBufferOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, DrawCommandIdsBufferOut)
		// Using the wrong kind of buffer for RDG...
		SHADER_PARAMETER_UAV(RWBuffer<uint>, InstanceIdsBufferLegacyOut)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, DrawIndirectArgsBufferOut)
		SHADER_PARAMETER(int32, NumPrimitiveIds)
		SHADER_PARAMETER(int32, NumInstanceRuns)
		SHADER_PARAMETER(int32, NumCommands)
		SHADER_PARAMETER(uint32, NumInstanceFlagWords)
		SHADER_PARAMETER(uint32, NumCulledInstances)
		SHADER_PARAMETER(uint32, NumCulledViews)
		SHADER_PARAMETER(int32, NumViewIds)

		SHADER_PARAMETER(int32, DynamicPrimitiveIdOffset)
		SHADER_PARAMETER(int32, DynamicPrimitiveIdMax)

	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FOutputInstanceIdsAtOffsetCs, "/Engine/Private/InstanceCulling/BuildInstanceDrawCommands.usf", "OutputInstanceIdsAtOffset", SF_Compute);

#endif // ENABLE_DETERMINISTIC_INSTANCE_CULLING

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

	using FPermutationDomain = TShaderPermutationDomain<FOutputCommandIdDim, FCullInstancesDim, FStereoModeDim, FDebugModeDim>;

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

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutputOffsetBufferOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, InstanceIdOffsetBufferOut)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, InstanceIdsBufferOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, DrawCommandIdsBufferOut)
		// Using the wrong kind of buffer for RDG...
		SHADER_PARAMETER_UAV(RWBuffer<uint>, InstanceIdsBufferLegacyOut)

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

void FInstanceCullingContext::BuildRenderingCommands(FRDGBuilder& GraphBuilder, const FGPUScene& GPUScene, const TRange<int32>& DynamicPrimitiveIdRange, FInstanceCullingResult& Results) const
{
	Results = FInstanceCullingResult();
	if (CullingCommands.Num() == 0)
	{
		return;
	}

	ensure(InstanceCullingMode == EInstanceCullingMode::Normal || ViewIds.Num() == 2);

	RDG_EVENT_SCOPE(GraphBuilder, "BuildRenderingCommands");

	const ERHIFeatureLevel::Type FeatureLevel = GPUScene.GetFeatureLevel();
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);

	FRDGBufferUploader BufferUploader;

	// Note: use start at zero offset if there is no instance culling manager, this means each build rendering commands pass will overwrite the same ID range. Which is only ok assuming correct barriers (should be erring on this side by default).
	TArray<uint32> NullArray;
	NullArray.AddZeroed(1);
	FRDGBufferRef InstanceIdOutOffsetBufferRDG = InstanceCullingManager ? InstanceCullingManager->CullingIntermediate.InstanceIdOutOffsetBuffer : CreateStructuredBuffer(GraphBuilder, BufferUploader, TEXT("InstanceCulling.OutputOffsetBufferOutTransient"), NullArray);
	// If there is no manager, then there is no data on culling, so set flag to skip that and ignore buffers.
	FRDGBufferRef VisibleInstanceFlagsRDG = InstanceCullingManager != nullptr ? InstanceCullingManager->CullingIntermediate.VisibleInstanceFlags : nullptr;
	const bool bCullInstances = InstanceCullingManager != nullptr;
	const uint32 NumCulledInstances = InstanceCullingManager != nullptr ? uint32(InstanceCullingManager->CullingIntermediate.NumInstances) : 0U;
	const uint32 NumCulledViews = InstanceCullingManager != nullptr ? uint32(InstanceCullingManager->CullingIntermediate.NumViews) : 0U;
	uint32 NumInstanceFlagWords = FMath::DivideAndRoundUp(NumCulledInstances, uint32(sizeof(uint32) * 8));

	// Add any other conditions that needs debug code running here.
	const bool bUseDebugMode = bDrawOnlyVSMInvalidatingGeometry;

	FRDGBufferRef PrimitiveCullingCommandsBuffer = CreateStructuredBuffer(GraphBuilder, BufferUploader, TEXT("InstanceCulling.PrimitiveCullingCommands"), CullingCommands);
	FRDGBufferRef PrimitiveIdsBuffer = CreateStructuredBuffer(GraphBuilder, BufferUploader, TEXT("InstanceCulling.PrimitiveIds"), PrimitiveIds);
	FRDGBufferRef PrimitiveInstanceRunsBuffer = CreateStructuredBuffer(GraphBuilder, BufferUploader, TEXT("InstanceCulling.InstanceRuns"), InstanceRuns);
	FRDGBufferRef ViewIdsBuffer = CreateStructuredBuffer(GraphBuilder, BufferUploader, TEXT("InstanceCulling.ViewIds"), ViewIds);

	BufferUploader.Submit(GraphBuilder);

#if ENABLE_DETERMINISTIC_INSTANCE_CULLING
	// 1. Compute output sizes for all commands
	FRDGBufferRef InstanceCountsRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FIntVector2), CullingCommands.Num()), TEXT("InstanceCulling.InstanceCounts"));
	FRDGBufferRef InstanceIdOffsetBufferRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), CullingCommands.Num()), TEXT("InstanceCulling.InstanceIdOffsetBuffer"));
	FRDGBufferRef DrawIndirectArgsRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(IndirectArgsNumWords * CullingCommands.Num()), TEXT("InstanceCulling.DrawIndirectArgsBuffer"));


	{
		FComputeInstanceIdOutputSizeCs::FParameters* PassParameters = GraphBuilder.AllocParameters<FComputeInstanceIdOutputSizeCs::FParameters>();

		PassParameters->InstanceCountsOut = GraphBuilder.CreateUAV(InstanceCountsRDG);

		// Because the view uniforms are not set up by the time this runs
		// PassParameters->View = View.ViewUniformBuffer;
		// Set up global GPU-scene data instead...
		PassParameters->GPUSceneInstanceSceneData = GPUScene.InstanceDataBuffer.SRV;
		PassParameters->GPUScenePrimitiveSceneData = GPUScene.PrimitiveBuffer.SRV;
		PassParameters->InstanceDataSOAStride = GPUScene.InstanceDataSOAStride;
		// Upload data etc
		PassParameters->PrimitiveCullingCommands = GraphBuilder.CreateSRV(PrimitiveCullingCommandsBuffer, CullingCommands));

		PassParameters->PrimitiveIds = GraphBuilder.CreateSRV(PrimitiveIdsBuffer, PrimitiveIds));
		PassParameters->DynamicPrimitiveIdOffset = DynamicPrimitiveIdRange.GetLowerBoundValue();
		PassParameters->DynamicPrimitiveIdMax = DynamicPrimitiveIdRange.GetUpperBoundValue();

		PassParameters->InstanceRuns = GraphBuilder.CreateSRV(PrimitiveInstanceRunsBuffer);

		PassParameters->OutputOffsetBufferOut = GraphBuilder.CreateUAV(InstanceIdOutOffsetBufferRDG);

		PassParameters->ViewIds = GraphBuilder.CreateSRV(ViewIdsBuffer);
		PassParameters->NumViewIds = ViewIds.Num();
		PassParameters->NumPrimitiveIds = PrimitiveIds.Num();
		PassParameters->NumInstanceRuns = InstanceRuns.Num();
		PassParameters->NumCommands = CullingCommands.Num();
		PassParameters->VisibleInstanceFlags = VisibleInstanceFlagsRDG ? GraphBuilder.CreateSRV(VisibleInstanceFlagsRDG) : nullptr;

		PassParameters->NumInstanceFlagWords = NumInstanceFlagWords;
		PassParameters->NumCulledInstances = NumCulledInstances;
		PassParameters->NumCulledViews = NumCulledViews;

		FComputeInstanceIdOutputSizeCs::FPermutationDomain PermutationVector;
		PermutationVector.Set<FComputeInstanceIdOutputSizeCs::FCullInstancesDim>(bCullInstances);
		PermutationVector.Set<FComputeInstanceIdOutputSizeCs::FStereoModeDim>(InstanceCullingMode == EInstanceCullingMode::Stereo);
		auto ComputeShader = ShaderMap->GetShader<FComputeInstanceIdOutputSizeCs>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ComputeInstanceIdOutputSize"),
			ComputeShader,
			PassParameters,
			FIntVector(CullingCommands.Num(), 1, 1)
		);
	}
	// 2. Allocate output slots for each command
	{
		FCalcOutputOffsetsCs::FParameters* PassParameters = GraphBuilder.AllocParameters<FCalcOutputOffsetsCs::FParameters>();

		PassParameters->InstanceCounts = GraphBuilder.CreateSRV(InstanceCountsRDG);
		PassParameters->OutputOffsetBufferOut = GraphBuilder.CreateUAV(InstanceIdOutOffsetBufferRDG);
		PassParameters->InstanceIdOffsetBufferOut = GraphBuilder.CreateUAV(InstanceIdOffsetBufferRDG, PF_R32_UINT);
		PassParameters->NumViewIds = ViewIds.Num();
		PassParameters->NumCommands = CullingCommands.Num();

		auto ComputeShader = ShaderMap->GetShader<FCalcOutputOffsetsCs>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("CalcOutputOffsets"),
			ComputeShader,
			PassParameters,
			FIntVector(1, 1, 1)
		);
	}
	// 3. Populate the output buffers
	{
		FOutputInstanceIdsAtOffsetCs::FParameters* PassParameters = GraphBuilder.AllocParameters<FOutputInstanceIdsAtOffsetCs::FParameters>();

		PassParameters->InstanceCounts = GraphBuilder.CreateSRV(InstanceCountsRDG);
		PassParameters->InstanceIdOffsetBuffer = GraphBuilder.CreateSRV(InstanceIdOffsetBufferRDG, PF_R32_UINT);


		// Because the view uniforms are not set up by the time this runs
		// PassParameters->View = View.ViewUniformBuffer;
		// Set up global GPU-scene data instead...
		PassParameters->GPUSceneInstanceSceneData = GPUScene.InstanceDataBuffer.SRV;
		PassParameters->GPUScenePrimitiveSceneData = GPUScene.PrimitiveBuffer.SRV;
		PassParameters->InstanceDataSOAStride = GPUScene.InstanceDataSOAStride;
		// Upload data etc
		PassParameters->PrimitiveCullingCommands = GraphBuilder.CreateSRV(PrimitiveCullingCommandsBuffer);

		PassParameters->PrimitiveIds = GraphBuilder.CreateSRV(PrimitiveIdsBuffer);
		PassParameters->DynamicPrimitiveIdOffset = DynamicPrimitiveIdRange.GetLowerBoundValue();
		PassParameters->DynamicPrimitiveIdMax = DynamicPrimitiveIdRange.GetUpperBoundValue();

		PassParameters->InstanceRuns = GraphBuilder.CreateSRV(PrimitiveInstanceRunsBuffer);

		PassParameters->ViewIds = GraphBuilder.CreateSRV(ViewIdsBuffer);
		PassParameters->NumViewIds = ViewIds.Num();

		// TODO: Remove this when everything is properly RDG'd
		AddPass(GraphBuilder, [&GraphBuilder](FRHICommandList& RHICmdList)
		{
			RHICmdList.Transition(FRHITransitionInfo(GInstanceCullingManagerResources.GetInstancesIdBufferUav(), ERHIAccess::Unknown, ERHIAccess::UAVCompute));
		});

		//PassParameters->InstanceIdsBufferOut = GraphBuilder.CreateUAV(InstanceIdsBufferRDG, PF_R32_UINT);
		// TODO: Access resources through manager rather than global
		PassParameters->InstanceIdsBufferLegacyOut = GInstanceCullingManagerResources.GetInstancesIdBufferUav();
		PassParameters->DrawIndirectArgsBufferOut = GraphBuilder.CreateUAV(DrawIndirectArgsRDG, PF_R32_UINT);
		PassParameters->NumPrimitiveIds = PrimitiveIds.Num();
		PassParameters->NumInstanceRuns = InstanceRuns.Num();
		PassParameters->NumCommands = CullingCommands.Num();
		PassParameters->VisibleInstanceFlags = VisibleInstanceFlagsRDG ? GraphBuilder.CreateSRV(VisibleInstanceFlagsRDG) : nullptr;
		PassParameters->NumInstanceFlagWords = NumInstanceFlagWords;
		PassParameters->NumCulledInstances = NumCulledInstances;
		PassParameters->NumCulledViews = NumCulledViews;

		FOutputInstanceIdsAtOffsetCs::FPermutationDomain PermutationVector;
		// NOTE: this also switches between legacy buffer and RDG for Id output
		PermutationVector.Set<FOutputInstanceIdsAtOffsetCs::FOutputCommandIdDim>(0);
		PermutationVector.Set<FOutputInstanceIdsAtOffsetCs::FCullInstancesDim>(bCullInstances);
		PermutationVector.Set<FOutputInstanceIdsAtOffsetCs::FStereoModeDim>(InstanceCullingMode == EInstanceCullingMode::Stereo);
		auto ComputeShader = ShaderMap->GetShader<FOutputInstanceIdsAtOffsetCs>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("OutputInstanceIdsAtOffset"),
			ComputeShader,
			PassParameters,
			FIntVector(CullingCommands.Num(), 1, 1)
		);
	}

#else // !ENABLE_DETERMINISTIC_INSTANCE_CULLING

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
	//FRDGBufferRef InstanceIdsBufferRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), PrimitiveIds.Num() * FInstanceCullingManager::MaxAverageInstanceFactor), TEXT("InstanceCulling.InstanceIdsBuffer"));
	FRDGBufferRef InstanceIdOffsetBufferRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), CullingCommands.Num()), TEXT("InstanceCulling.InstanceIdOffsetBuffer"));

	PassParameters->ViewIds = GraphBuilder.CreateSRV(ViewIdsBuffer);
	PassParameters->NumViewIds = ViewIds.Num();
	PassParameters->bDrawOnlyVSMInvalidatingGeometry = bDrawOnlyVSMInvalidatingGeometry;

	// TODO: Remove this when everything is properly RDG'd
	AddPass(GraphBuilder, [&GraphBuilder](FRHICommandList& RHICmdList)
	{
		RHICmdList.Transition(FRHITransitionInfo(GInstanceCullingManagerResources.GetInstancesIdBufferUav(), ERHIAccess::Unknown, ERHIAccess::UAVCompute));
	});

	//PassParameters->InstanceIdsBufferOut = GraphBuilder.CreateUAV(InstanceIdsBufferRDG, PF_R32_UINT);
	// TODO: Access resources through manager rather than global
	PassParameters->InstanceIdsBufferLegacyOut = GInstanceCullingManagerResources.GetInstancesIdBufferUav();
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
	// NOTE: this also switches between legacy buffer and RDG for Id output
	PermutationVector.Set<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FOutputCommandIdDim>(0);
	PermutationVector.Set<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FCullInstancesDim>(bCullInstances);
	PermutationVector.Set<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FStereoModeDim>(InstanceCullingMode == EInstanceCullingMode::Stereo);
	PermutationVector.Set<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FDebugModeDim>(bUseDebugMode);

	auto ComputeShader = ShaderMap->GetShader<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs>(PermutationVector);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("BuildInstanceIdBufferAndCommandsFromPrimitiveIds"),
		ComputeShader,
		PassParameters,
		FIntVector(CullingCommands.Num(), 1, 1)
	);

#endif // ENABLE_DETERMINISTIC_INSTANCE_CULLING

	Results.DrawIndirectArgsBuffer = DrawIndirectArgsRDG;
	//ConvertToExternalBuffer(GraphBuilder, DrawIndirectArgsRDG, Results.DrawIndirectArgsBuffer);
	//GraphBuilder.QueueBufferExtraction(InstanceIdsBufferRDG, &Results.InstanceIdsBuffer);
	Results.InstanceIdOffsetBuffer = InstanceIdOffsetBufferRDG;
	//ConvertToExternalBuffer(GraphBuilder, InstanceIdOffsetBufferRDG, Results.InstanceIdOffsetBuffer);
	//GraphBuilder.Transition(FRHITransitionInfo(GInstanceCullingManagerResources.GetInstancesIdBufferUav(), ERHIAccess::Unknown, ERHIAccess::SRVGraphics));

	// TODO: Remove this when everything is properly RDG'd
	AddPass(GraphBuilder, [&GraphBuilder](FRHICommandList& RHICmdList)
	{
		RHICmdList.Transition(FRHITransitionInfo(GInstanceCullingManagerResources.GetInstancesIdBufferUav(), ERHIAccess::UAVCompute, ERHIAccess::SRVGraphics));
		RHICmdList.Transition(FRHITransitionInfo(GInstanceCullingManagerResources.GetPageInfoBufferUav(), ERHIAccess::Unknown, ERHIAccess::SRVGraphics));
	});
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

	FRDGBufferUploader BufferUploader;

	FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FParameters* PassParameters = GraphBuilder.AllocParameters<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FParameters>();
	// Because the view uniforms are not set up by the time this runs
	// PassParameters->View = View.ViewUniformBuffer;
	// Set up global GPU-scene data instead...
	PassParameters->GPUSceneInstanceSceneData = GPUScene.InstanceDataBuffer.SRV;
	PassParameters->GPUScenePrimitiveSceneData = GPUScene.PrimitiveBuffer.SRV;
	PassParameters->InstanceDataSOAStride = GPUScene.InstanceDataSOAStride;
	PassParameters->GPUSceneFrameNumber = GPUScene.GetSceneFrameNumber();

	// Upload data etc
	Params.PrimitiveCullingCommands = CreateStructuredBuffer(GraphBuilder, BufferUploader, TEXT("InstanceCulling.PrimitiveCullingCommands"), CullingCommands);
	PassParameters->PrimitiveCullingCommands = GraphBuilder.CreateSRV(Params.PrimitiveCullingCommands);

	PassParameters->PrimitiveIds = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, BufferUploader, TEXT("InstanceCulling.PrimitiveIds"), PrimitiveIds));
	PassParameters->DynamicPrimitiveIdOffset = DynamicPrimitiveIdRange.GetLowerBoundValue();
	PassParameters->DynamicPrimitiveIdMax = DynamicPrimitiveIdRange.GetUpperBoundValue();

	PassParameters->InstanceRuns = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, BufferUploader, TEXT("InstanceCulling.InstanceRuns"), InstanceRuns));


	FRDGBufferRef InstanceIdOutOffsetBufferRDG = Intermediate.InstanceIdOutOffsetBuffer;
	FRDGBufferRef VisibleInstanceFlagsRDG = Intermediate.VisibleInstanceFlags;

	// Create and initialize if not allocated
	if (Params.InstanceIdWriteOffsetBuffer == nullptr)
	{
		TArray<uint32> NullArray;
		NullArray.AddZeroed(1);
		Params.InstanceIdWriteOffsetBuffer = CreateStructuredBuffer(GraphBuilder, BufferUploader, TEXT("InstanceCulling.InstanceIdWriteOffsetBuffer"), NullArray);
	}

	PassParameters->OutputOffsetBufferOut = GraphBuilder.CreateUAV(Params.InstanceIdWriteOffsetBuffer);

	Params.DrawIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(IndirectArgsNumWords * CullingCommands.Num()), TEXT("InstanceCulling.DrawIndirectArgsBuffer"));
	// not using structured buffer as we want/have to get at it as a vertex buffer 
	//FRDGBufferRef InstanceIdsBufferRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), PrimitiveIds.Num() * FInstanceCullingManager::MaxAverageInstanceFactor), TEXT("InstanceIdsBuffer"));
	Params.InstanceIdStartOffsetBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), CullingCommands.Num()), TEXT("InstanceCulling.InstanceIdOffsetBuffer"));

	PassParameters->ViewIds = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, BufferUploader, TEXT("InstanceCulling.ViewIds"), ViewIds));
	PassParameters->NumViewIds = ViewIds.Num();
	PassParameters->bDrawOnlyVSMInvalidatingGeometry = 0;


	//PassParameters->InstanceIdsBufferOut = GraphBuilder.CreateUAV(InstanceIdsBufferRDG, PF_R32_UINT);
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
		const int32 InstanceIdBufferSize = CullingCommands.Num() * FInstanceCullingManager::MaxAverageInstanceFactor * 64;
		Params.InstanceIdsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), InstanceIdBufferSize), TEXT("InstanceCulling.InstanceIdsBuffer"));
		Params.DrawCommandIdsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), InstanceIdBufferSize), TEXT("InstanceCulling.DrawCommandIdsBuffer"));

	}
	
	PassParameters->InstanceIdsBufferOut = GraphBuilder.CreateUAV(Params.InstanceIdsBuffer, PF_R32_UINT);
	PassParameters->DrawCommandIdsBufferOut = GraphBuilder.CreateUAV(Params.DrawCommandIdsBuffer, PF_R32_UINT);

	int32 NumInstanceFlagWords = FMath::DivideAndRoundUp(Intermediate.NumInstances, int32(sizeof(uint32) * 8));
	PassParameters->NumInstanceFlagWords = uint32(NumInstanceFlagWords);
	PassParameters->NumCulledInstances = uint32(Intermediate.NumInstances);
	PassParameters->NumCulledViews = uint32(Intermediate.NumViews);


	FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FPermutationDomain PermutationVector;
	// NOTE: this also switches between legacy buffer and RDG for Id output
	PermutationVector.Set<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FOutputCommandIdDim>(1);
	PermutationVector.Set<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FCullInstancesDim>(false);
	PermutationVector.Set<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FStereoModeDim>(false);
	PermutationVector.Set<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs::FDebugModeDim>(false);

	auto ComputeShader = ShaderMap->GetShader<FBuildInstanceIdBufferAndCommandsFromPrimitiveIdsCs>(PermutationVector);

	BufferUploader.Submit(GraphBuilder);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("BuildInstanceIdBufferAndCommandsFromPrimitiveIds"),
		ComputeShader,
		PassParameters,
		FIntVector(CullingCommands.Num(), 1, 1)
	);
}

#else // GPUCULL_TODO

void FInstanceCullingContext::BuildRenderingCommands(FRDGBuilder& GraphBuilder, const FGPUScene& GPUScene, const TRange<int32>& DynamicPrimitiveIdRange, FInstanceCullingResult& Results) const
{
}

void FInstanceCullingContext::BuildRenderingCommands(FRDGBuilder& GraphBuilder, FGPUScene& GPUScene, const TRange<int32>& DynamicPrimitiveIdRange, FInstanceCullingRdgParams& Params) const
{
}

#endif // GPUCULL_TODO
