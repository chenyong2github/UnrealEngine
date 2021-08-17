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
class FInstanceCullingDeferredContext;


BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FInstanceCullingGlobalUniforms, RENDERER_API)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, InstanceIdsBuffer)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, PageInfoBuffer)
	SHADER_PARAMETER(uint32, BufferCapacity)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FInstanceCullingDrawParams, )
	RDG_BUFFER_ACCESS(DrawIndirectArgsBuffer, ERHIAccess::IndirectArgs)
	RDG_BUFFER_ACCESS(InstanceIdOffsetBuffer, ERHIAccess::VertexOrIndexBuffer)
	SHADER_PARAMETER(uint32, InstanceDataByteOffset) // offset into per-instance buffer
	SHADER_PARAMETER(uint32, IndirectArgsByteOffset) // offset into indirect args buffer
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FInstanceCullingGlobalUniforms, InstanceCulling)	
END_SHADER_PARAMETER_STRUCT()

FMeshDrawCommandOverrideArgs GetMeshDrawCommandOverrideArgs(const FInstanceCullingDrawParams& InstanceCullingDrawParams);

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
	RENDERER_API static uint32 GetInstanceIdBufferStride(ERHIFeatureLevel::Type FeatureLevel);

	FInstanceCullingContext() {}

	/**
	 * Create an instance culling context to process draw commands that can be culled using GPU-Scene.
	 * @param InPrevHZB if non-null enables HZB-occlusion culling for the context (if r.InstanceCulling.OcclusionCull is enabled),
	 *                  NOTE: only one PrevHZB target is allowed accross all passes currently, so either must be atlased or otherwise the same.
	 */
	RENDERER_API FInstanceCullingContext(
		ERHIFeatureLevel::Type FeatureLevel,
		FInstanceCullingManager* InInstanceCullingManager, 
		TArrayView<const int32> InViewIds, 
		const TRefCountPtr<IPooledRenderTarget>& InPrevHZB,
		EInstanceCullingMode InInstanceCullingMode = EInstanceCullingMode::Normal, 
		bool bInDrawOnlyVSMInvalidatingGeometry = false, 
		EBatchProcessingMode InSingleInstanceProcessingMode = EBatchProcessingMode::UnCulled
	);
	RENDERER_API ~FInstanceCullingContext();

	static RENDERER_API const TRDGUniformBufferRef<FInstanceCullingGlobalUniforms> CreateDummyInstanceCullingUniformBuffer(FRDGBuilder& GraphBuilder);

	static bool IsOcclusionCullingEnabled();

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
	 * Batches are collected as we walk through the main render setup and are executed when RDG Execute or Drain is called.
	 * This implicitly ends the deferred context, so if Drain is used, it should be paired with a new call to BeginDeferredCulling.
	 */
	static FInstanceCullingDeferredContext *CreateDeferredContext(
		FRDGBuilder& GraphBuilder,
		FGPUScene& GPUScene,
		FInstanceCullingManager* InstanceCullingManager);

	/**
	 * Helper function to add a pass to zero the instance count in the indirect args.
	 */
	static void AddClearIndirectArgInstanceCountPass(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, FRDGBufferRef DrawIndirectArgsBuffer, TFunction<int32()> NumIndirectArgsCallback = TFunction<int32()>());


	void SetupDrawCommands(
		FMeshCommandOneFrameArray& VisibleMeshDrawCommandsInOut,
		bool bCompactIdenticalCommands,
		// Stats
		int32& MaxInstancesOut,
		int32& VisibleMeshDrawCommandsNumOut,
		int32& NewPassVisibleMeshDrawCommandsNumOut);

	void SubmitDrawCommands(
		const FMeshCommandOneFrameArray& VisibleMeshDrawCommands,
		const FGraphicsMinimalPipelineStateSet& GraphicsMinimalPipelineStateSet,
		const FMeshDrawCommandOverrideArgs& OverrideArgs,
		int32 StartIndex,
		int32 NumMeshDrawCommands,
		uint32 InstanceFactor,
		FRHICommandList& RHICmdList) const;

	FInstanceCullingManager* InstanceCullingManager = nullptr;
	ERHIFeatureLevel::Type FeatureLevel = ERHIFeatureLevel::Num;
	TArray<int32, TInlineAllocator<6>/*, SceneRenderingAllocator*/> ViewIds;
	TRefCountPtr<IPooledRenderTarget> PrevHZB = nullptr;
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
		// stores either the offset (in bytes) to the indirect args or the number of instances
		uint32 IndirectArgsOffsetOrNumInstances : 31U;
		// offset into per-instance buffer
		uint32 InstanceDataByteOffset;
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
	
	TArray<FMeshDrawCommandInfo> MeshDrawCommandInfos;
	TArray<FRHIDrawIndexedIndirectParameters> IndirectArgs;
	TArray<FDrawCommandDesc> DrawCommandDescs;
	TArray<uint32> InstanceIdOffsets;

	using LoadBalancerArray = TStaticArray<FInstanceProcessingGPULoadBalancer*, static_cast<uint32>(EBatchProcessingMode::Num)>;
	// Driver for collecting items using one mode of processing
	LoadBalancerArray LoadBalancers = LoadBalancerArray(InPlace, nullptr);

	// Set of specialized batches that collect items with different properties each context may have only a subset.
	//TStaticArray<FBatchProcessor, EBatchProcessingMode::Num> Batches;

	// Processing mode to use for single-instance primitives, default to skip culling, as this is already done on CPU. 
	EBatchProcessingMode SingleInstanceProcessingMode = EBatchProcessingMode::UnCulled;
};

