// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RenderGraphResources.h"

struct FInstanceCullingResult;
class FGPUScene;
class FInstanceCullingManager;
class FInstanceCullingDrawParams;
class FScene;
class FGPUScenePrimitiveCollector;

struct FInstanceCullingRdgParams
{
	/**
	 * In/Out parameter, in non-null it contains the start location of the output to write instance IDs for the command buffer
	 * will be incremented atomically to allocate ID ranges for each of the commands (does not require exclusive access).
	 * If null, a new single-entry int32 buffer with initial value of zero is created and returned.
	 */
	FRDGBufferRef InstanceIdWriteOffsetBuffer = nullptr;

	/**
	 * Out parameter, populated with draw commands with instance counts set to draw the instances that survive culling.
	 * The number of draw args is allocated to match the number of mesh draw commands in the context.
	 */
	FRDGBufferRef DrawIndirectArgs = nullptr;
	/**
	 * Out parameter, populated with draw start offsets in the instance ID lists (to be fetched by the vertex shader).
	 * The number of elements is allocated to match the number of mesh draw commands in the context.
	 */
	FRDGBufferRef InstanceIdStartOffsetBuffer = nullptr;

	/**
	 * In/Out parameter, buffer to write instance ID's to, each draw command will have a consecutive range in this buffer containing 
	 * IDs of surviving instances. The range is allocated atomically InstanceIdWriteOffsetBuffer 
	 * If null, a new buffer is created and returned with a size of some pre-set [GPUCULL_TODO] hardcoded size.
	 */
	FRDGBufferRef InstanceIdsBuffer = nullptr;
	/**
	 * [GPUCULL_TODO: Optional] In/Out parameter, buffer to write draw command ID's to, for each instance ID. 
	 * Used to map each instance ID back to the corresponding draw command.
	 */
	FRDGBufferRef DrawCommandIdsBuffer = nullptr;

	/**
	 * Out parameter, GPU representation of the FInstanceCullingContext::CullingCommands.
	 */
	FRDGBufferRef PrimitiveCullingCommands = nullptr;
};

BEGIN_SHADER_PARAMETER_STRUCT(FInstanceCullingDrawParams, )
	RDG_BUFFER_ACCESS(DrawIndirectArgsBuffer, ERHIAccess::IndirectArgs)
	RDG_BUFFER_ACCESS(InstanceIdOffsetBuffer, ERHIAccess::VertexOrIndexBuffer)
END_SHADER_PARAMETER_STRUCT()


enum class EInstanceCullingMode
{
	Normal,
	Stereo,
};

/**
 * Thread-safe context for managing culling for a render pass.
 */
class FInstanceCullingContext
{
public:
	static constexpr uint32 IndirectArgsNumWords = 5;

	FInstanceCullingContext() {}

	FInstanceCullingContext(FInstanceCullingManager* InInstanceCullingManager, TArrayView<const int32> InViewIds, EInstanceCullingMode InInstanceCullingMode = EInstanceCullingMode::Normal, bool bInDrawOnlyVSMInvalidatingGeometry = false);

	struct FPrimCullingCommand
	{
		uint32 BaseVertexIndex;
		uint32 FirstIndex;
		uint32 NumVerticesOrIndices;
		uint32 FirstPrimitiveIdOffset;
		uint32 FirstInstanceRunOffset;
		uint32 bMaterialMayModifyPosition;
	};

	/**
	 * Call to empty out the culling commands & other culling data.
	 */
	void ResetCommands(int32 MaxNumCommands);

	bool IsEnabled() const { return bIsEnabled; }

	/**
	 * Begin a new command. Allocates a slot and is referenced by subsequent AddPrimitiveToCullingCommand and AddInstanceRunToCullingCommand.
	 */
	void BeginCullingCommand(EPrimitiveType BatchType, uint32 BaseVertexIndex, uint32 FirstIndex, uint32 NumPrimitives, bool bInMaterialMayModifyPosition);

	/**
	 * Command that is executed in the per-view, post-cull pass to gather up the instances belonging to this primitive.
	 * Multiple commands may add to the same slot, ordering is not preserved.
	 */
	void AddPrimitiveToCullingCommand(int32 ScenePrimitiveId);

	/**
	 * Command that is executed in the per-view, post-cull pass to gather up the instances belonging to this primitive.
	 * Multiple commands may add to the same slot, ordering is not preserved.
	 */
	void AddInstanceRunToCullingCommand(int32 ScenePrimitiveId, const uint32* Runs, uint32 NumRuns);


	void BuildRenderingCommands(FRDGBuilder& GraphBuilder, const FGPUScene& GPUScene, const TRange<int32> &DynamicPrimitiveIdRange, FInstanceCullingResult& Results) const;

	/**
	 * Build instance ID lists & render commands, but without extracting results and also accepting RGD resources for output buffers etc (rather than registering global resources)
	 */
	void BuildRenderingCommands(FRDGBuilder& GraphBuilder, FGPUScene& GPUScene, const TRange<int32>& DynamicPrimitiveIdRange, FInstanceCullingRdgParams& Params) const;

	inline bool HasCullingCommands() const { return CullingCommands.Num() > 0; 	}

	EInstanceCullingMode GetInstanceCullingMode() const { return InstanceCullingMode; }

	// GPUCULL_TODO: These should not be dynamically heap-allocated, all except instance runs are easy to pre-size on memstack.
	//           Must be presized as populated from task threads.
	TArray<FPrimCullingCommand/*, SceneRenderingAllocator*/> CullingCommands;
	TArray<int32/*, SceneRenderingAllocator*/> PrimitiveIds;

	struct FInstanceRun
	{
		uint32 Start;
		uint32 EndInclusive;
		int32 PrimitiveId;
	};

	FInstanceCullingManager* InstanceCullingManager = nullptr;
	TArray<FInstanceRun/*, SceneRenderingAllocator*/> InstanceRuns;
	TArray<int32/*, SceneRenderingAllocator*/> ViewIds;
	bool bIsEnabled = false;
	EInstanceCullingMode InstanceCullingMode = EInstanceCullingMode::Normal;
	bool bDrawOnlyVSMInvalidatingGeometry = false;
};

