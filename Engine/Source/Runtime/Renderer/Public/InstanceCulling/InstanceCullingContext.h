// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RenderGraphResources.h"
#include "MeshPassProcessor.h"

struct FInstanceCullingResult;
class FGPUScene;
class FInstanceCullingManager;
class FInstanceCullingDrawParams;
class FScene;
class FGPUScenePrimitiveCollector;


#define GPUCULL_ENABLE_CONCAT_ON_UPLOAD 0


BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FInstanceCullingGlobalUniforms, RENDERER_API)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, InstanceIdsBuffer)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, PageInfoBuffer)
	SHADER_PARAMETER(uint32, BufferCapacity)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FInstanceCullingDrawParams, )
	RDG_BUFFER_ACCESS(DrawIndirectArgsBuffer, ERHIAccess::IndirectArgs)
	RDG_BUFFER_ACCESS(InstanceIdOffsetBuffer, ERHIAccess::VertexOrIndexBuffer)
	SHADER_PARAMETER(uint32, DrawCommandDataOffset)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FInstanceCullingGlobalUniforms, InstanceCulling)	
END_SHADER_PARAMETER_STRUCT()


enum class EInstanceCullingMode
{
	Normal,
	Stereo,
};


// Enumeration of the specialized command processing variants
enum class EBatchProcessingMode : uint32
{
	// Generic processing mode, handles all the features.
	Generic,
	// General work batches that need load balancing, either instance runs or primitive id ranges (auto instanced) but culling is disabled
	// may have multi-view (but probably not used for that path)
	UnCulled,

	Num,
};

class FInstanceProcessingGPULoadBalancer;
/**
 * Thread-safe context for managing culling for a render pass.
 */
class FInstanceCullingContext
{
public:
	static constexpr uint32 IndirectArgsNumWords = 5;

	FInstanceCullingContext() {}

	RENDERER_API FInstanceCullingContext(
		FInstanceCullingManager* InInstanceCullingManager, 
		TArrayView<const int32> InViewIds, 
		EInstanceCullingMode InInstanceCullingMode = EInstanceCullingMode::Normal, 
		bool bInDrawOnlyVSMInvalidatingGeometry = false, 
		EBatchProcessingMode InSingleInstanceProcessingMode = EBatchProcessingMode::UnCulled
	);
	RENDERER_API ~FInstanceCullingContext();

	static RENDERER_API const TRDGUniformBufferRef<FInstanceCullingGlobalUniforms> CreateDummyInstanceCullingUniformBuffer(FRDGBuilder& GraphBuilder);

	struct FBatchItem
	{
		const FInstanceCullingContext* Context = nullptr;
		FInstanceCullingDrawParams* Result = nullptr;
		int32 DynamicInstanceIdOffset = 0;
		int32 DynamicInstanceIdNum = 0;
	};

	/**
	 * Call to empty out the culling commands & other culling data.
	 */
	void ResetCommands(int32 MaxNumCommands);

	bool IsEnabled() const { return bIsEnabled; }

	/**
	 * Add command to cull a range of instances for the given mesh draw command index.
	 * Multiple commands may add to the same slot, ordering is not preserved.
	 */
	void AddInstancesToDrawCommand(uint32 IndirectArgsOffset, int32 InstanceDataOffset, bool bDynamicInstanceDataOffset, uint32 NumInstances);

	/**
	 * Command that is executed in the per-view, post-cull pass to gather up the instances belonging to this primitive.
	 * Multiple commands may add to the same slot, ordering is not preserved.
	 */
	void AddInstanceRunsToDrawCommand(uint32 IndirectArgsOffset, int32 InstanceDataOffset, bool bDynamicInstanceDataOffset, const uint32* Runs, uint32 NumRuns);

	/*
	 * Allocate space for indirect draw call argumens for a given MeshDrawCommand and initialize with draw command data.
	 * TODO: support cached pre-allocated commands.
	 */
	uint32 AllocateIndirectArgs(const FMeshDrawCommand* MeshDrawCommand);

	/**
	 * If InstanceCullingDrawParams is not null, this BuildRenderingCommands operation may be deferred and merged into a global pass when possible.
	 */
	void BuildRenderingCommands(
		FRDGBuilder& GraphBuilder,
		const FGPUScene& GPUScene,
		int32 DynamicInstanceIdOffset,
		int32 DynamicInstanceIdNum,
		FInstanceCullingResult& Results,
		FInstanceCullingDrawParams* InstanceCullingDrawParams = nullptr) const;

	inline bool HasCullingCommands() const { return TotalInstances > 0; 	}

	EInstanceCullingMode GetInstanceCullingMode() const { return InstanceCullingMode; }

	/**
	 * Add a batched BuildRenderingCommands pass. Each batch represents a BuildRenderingCommands call from a mesh pass.
	 * Batches are gradually added as we walk through the main render function.
	 */
	static void BuildRenderingCommandsDeferred(
		FRDGBuilder& GraphBuilder,
		FGPUScene& GPUScene,
		FInstanceCullingManager& InstanceCullingManager);

	static bool AllowBatchedBuildRenderingCommands(const FGPUScene& GPUScene);

	/**
	 * Helper function to add a pass to zero the instance count in the indirect args.
	 */
	static void AddClearIndirectArgInstanceCountPass(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, FRDGBufferRef DrawIndirectArgsBuffer);


	void SetupDrawCommands(
		FMeshCommandOneFrameArray& VisibleMeshDrawCommandsInOut,
		bool bCompactIdenticalCommands,
		// Stats
		int32& MaxInstancesOut,
		int32& VisibleMeshDrawCommandsNumOut,
		int32& NewPassVisibleMeshDrawCommandsNumOut);

	FInstanceCullingManager* InstanceCullingManager = nullptr;
	TArray<int32, TInlineAllocator<6>/*, SceneRenderingAllocator*/> ViewIds;
	bool bIsEnabled = false;
	EInstanceCullingMode InstanceCullingMode = EInstanceCullingMode::Normal;
	bool bDrawOnlyVSMInvalidatingGeometry = false;

	uint32 TotalInstances = 0U;

public:
	// Auxiliary info for each mesh draw command that needs submitting.
	struct FMeshDrawCommandInfo
	{
		// flag to indicate if using indirect or not.
		uint32 bUseIndirect : 1U;
		// stores either the offset to the indirect args or the number of instances
		uint32 IndirectArgsOffsetOrNumInstances : 31U;
		// Offset to write the instance data for the command to (either offset into the vertex array
		uint32 InstanceDataOffset;
	};

	// TODO: bit-pack
	struct FDrawCommandDesc
	{
		uint32 bMaterialMayModifyPosition;
	};

	// Info about a batch of culling work produced by a context, when part of a batched job
	// Store once per context, provides start offsets to commands/etc for the context.
	struct FContextBatchInfo
	{
		uint32 IndirectArgsOffset;
		uint32 InstanceDataWriteOffset;
		uint32 ViewIdsOffset;
		uint32 NumViewIds;
		uint32 DynamicInstanceIdOffset;
		uint32 DynamicInstanceIdMax;
		uint32 ItemDataOffset[uint32(EBatchProcessingMode::Num)];
	};

	//TArray<FMeshDrawCommandInfo> MeshDrawCommandInfos;
	TArray<FRHIDrawIndexedIndirectParameters> IndirectArgs;
	TArray<FDrawCommandDesc> DrawCommandDescs;
	TArray<uint32> InstanceIdOffsets;

	using LoadBalancerArray = TStaticArray<FInstanceProcessingGPULoadBalancer*, static_cast<uint32>(EBatchProcessingMode::Num)>;
	// Driver for collecting items using one mode of processing
	LoadBalancerArray LoadBalancers = LoadBalancerArray(InPlace, nullptr);

	// Set of specialized batches that collect items with different properties each context may have only a subset.
	//TStaticArray<FBatchProcessor, EBatchProcessingMode::Num> Batches;
	struct FMergedContext
	{
		bool bInitialized = false;
#if GPUCULL_ENABLE_CONCAT_ON_UPLOAD
		TArray<TArrayView<const int32>, SceneRenderingAllocator> ViewIds;
		TArray<TArrayView<const FRHIDrawIndexedIndirectParameters>, SceneRenderingAllocator> IndirectArgs;
		TArray<TArrayView<const FDrawCommandDesc>, SceneRenderingAllocator> DrawCommandDescs;
		TArray<TArrayView<const uint32>, SceneRenderingAllocator> InstanceIdOffsets;
#else // !GPUCULL_ENABLE_CONCAT_ON_UPLOAD
		TArray<int32, SceneRenderingAllocator> ViewIds;
		//TArray<FMeshDrawCommandInfo, SceneRenderingAllocator> MeshDrawCommandInfos;
		TArray<FRHIDrawIndexedIndirectParameters, SceneRenderingAllocator> IndirectArgs;
		TArray<FDrawCommandDesc, SceneRenderingAllocator> DrawCommandDescs;
		TArray<uint32, SceneRenderingAllocator> InstanceIdOffsets;
#endif // GPUCULL_ENABLE_CONCAT_ON_UPLOAD
		LoadBalancerArray LoadBalancers = LoadBalancerArray(InPlace, nullptr);
		TStaticArray<TArray<uint32, SceneRenderingAllocator>, static_cast<uint32>(EBatchProcessingMode::Num)> BatchInds;
		TArray<FInstanceCullingContext::FContextBatchInfo, SceneRenderingAllocator> BatchInfos;


		// Counters to sum up all sizes to facilitate pre-sizing
		uint32 InstanceIdBufferSize = 0U;
		TStaticArray<int32, uint32(EBatchProcessingMode::Num)> TotalBatches = TStaticArray<int32, uint32(EBatchProcessingMode::Num)>(InPlace, 0);
		TStaticArray<int32, uint32(EBatchProcessingMode::Num)> TotalItems = TStaticArray<int32, uint32(EBatchProcessingMode::Num)>(InPlace, 0);
		int32 TotalIndirectArgs = 0;
		int32 TotalViewIds = 0;
	};

	// Processing mode to use for single-instance primitives, default to skip culling, as this is already done on CPU. 
	EBatchProcessingMode SingleInstanceProcessingMode = EBatchProcessingMode::UnCulled;
};

