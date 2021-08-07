// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderResource.h"
#include "RendererInterface.h"
#include "PrimitiveUniformShaderParameters.h"
#include "PrimitiveSceneInfo.h"
#include "GrowOnlySpanAllocator.h"
#include "InstanceCulling/InstanceCullingLoadBalancer.h"

class FRHICommandList;
class FScene;
class FViewInfo;

UE_DEPRECATED(5.0, "Use GPUScene::AddPrimitiveToUpdate instead.4")
extern RENDERER_API void AddPrimitiveToUpdateGPU(FScene& Scene, int32 PrimitiveId);


class FGPUScene;
class FGPUSceneDynamicContext;


/**
 * Used to manage dynamic primitives for a given view, during InitViews the data is collected and then can be committed to the GPU-Scene. 
 * Once committed the range of indices are valid and can be used to calculate the PrimitiveIds.
 */
class FGPUScenePrimitiveCollector
{
public:
	FGPUScenePrimitiveCollector(FGPUSceneDynamicContext* InGPUSceneDynamicContext = nullptr) :
		GPUSceneDynamicContext(InGPUSceneDynamicContext)
	{};

	/**
	 * Add data for a number of primitives, 
	 * May be called outside (before) a FGPUScene::Begin/EndRender block.
	 * @return The local index of the first primitive added - this needs to be offset after commiting to the GPU-Scene.
	 */
	FORCEINLINE int32 Add(const FPrimitiveUniformShaderParameters* Data, int32 Num)
	{
		check(GPUSceneDynamicContext != nullptr);
		check(!bCommitted);

		// Lazy allocation of the upload data to not waste space and processing if none was needed.
		if (UploadData == nullptr)
		{
			UploadData = AllocateUploadData();
		}

		const int32 LocalIndex = UploadData->PrimitiveShaderData.Num();
		UploadData->PrimitiveShaderData.Append(Data, Num);
		return LocalIndex;
	}

	/**
	 * Translates the LocalIndex returned when adding a dynamic primitive into the final GPU-Scene index,
	 * Only valid to call after Commit has been called.
	 */
	FORCEINLINE int32 GetFinalId(int32 LocalIndex) const
	{
		check(bCommitted);

		check(PrimitiveIdRange.GetLowerBoundValue() >= 0);
		check(PrimitiveIdRange.Size<int32>() >= LocalIndex);
		return PrimitiveIdRange.GetLowerBoundValue() + LocalIndex;
	}

	/**
	 * Allocates the range in GPUScene and queues the data for upload. 
	 * After this is called no more calls to Add are allowed.
	 * Only allowed inside a FGPUScene::Begin/EndRender block.
	 */
	RENDERER_API void Commit();

	/**
	 * Get the range of Primitive IDs in GPU-Scene for this batch of dynamic primitives, only valid to call after commit.
	 */
	FORCEINLINE const TRange<int32>& GetPrimitiveIdRange() const
	{
		check(bCommitted || UploadData == nullptr);
		return PrimitiveIdRange;
	}

	FORCEINLINE int32 GetInstanceSceneDataOffset(int32 PrimitiveId) const
	{
		check(bCommitted);
		check(PrimitiveIdRange.Contains(PrimitiveId));
		check(UploadData->bIsUploaded);

		// Assume a 1:1 mapping between primitive ID and instance ID
		return UploadData->InstanceSceneDataOffset + (PrimitiveId - PrimitiveIdRange.GetLowerBoundValue());
	}

	FORCEINLINE int32 GetInstanceSceneDataOffset() const
	{
		check(bCommitted || UploadData == nullptr);

		// Assume a 1:1 mapping between primitive ID and instance ID
		return UploadData != nullptr ? UploadData->InstanceSceneDataOffset : 0;
	}

	int32 Num() const {	return UploadData != nullptr ? UploadData->PrimitiveShaderData.Num() : 0; }
	int32 NumInstances() const { return Num(); }
private:

	friend class FGPUScene;
	friend class FGPUSceneDynamicContext;
	friend struct FGPUSceneCompactInstanceData;

	struct FUploadData
	{
		TArray<FPrimitiveUniformShaderParameters, TInlineAllocator<8>> PrimitiveShaderData;
		int32 InstanceSceneDataOffset = INDEX_NONE;
		bool bIsUploaded = false;
	};

	// Note: needs to be virtual to prevent a liker error
	virtual FUploadData* AllocateUploadData();

	/**
	 * Range in GPU scene allocated to the dynamic primitives.
	 */
	TRange<int32> PrimitiveIdRange = TRange<int32>::Empty();
	FUploadData* UploadData = nullptr; // Owned by FGPUSceneDynamicContext
	bool bCommitted = false;
	FGPUSceneDynamicContext* GPUSceneDynamicContext = nullptr;
};

// TODO: move to own header
class FInstanceProcessingGPULoadBalancer;

/**
 * Contains and controls the lifetime of any dynamic primitive data collected for the scene rendering.
 * Typically shares life-time with the SceneRenderer. 
 */
class FGPUSceneDynamicContext
{
public:
	FGPUSceneDynamicContext(FGPUScene& InGPUScene) : GPUScene(InGPUScene) {}
	~FGPUSceneDynamicContext();

	void Release();

private:
	friend class FGPUScene;
	friend class FGPUScenePrimitiveCollector;

	FGPUScenePrimitiveCollector::FUploadData* AllocateDynamicPrimitiveData();
	TArray<FGPUScenePrimitiveCollector::FUploadData*, TInlineAllocator<128> > DymamicPrimitiveUploadData;
	FGPUScene& GPUScene;
};

// Buffers used by GPU-Scene, since they can be resized during updates AND the render passes must retain the 
// right copy (this is chiefly because the init of shadow views after pre-pass means we need to be able to set 
// up GPU-Scene before pre-pass, but then may discover new primitives etc. As there is no way to know how many
// dynamic primitives will turn up after Pre-pass, we can't guarantee a resize won't happen).
struct FGPUSceneBufferState
{
	FRWBufferStructured	PrimitiveBuffer;
	FRWBufferStructured	InstanceSceneDataBuffer;
	uint32				InstanceSceneDataSOAStride = 1; // Distance between arrays in float4s
	FRWBufferStructured	InstanceBVHBuffer;
	FRWBufferStructured	LightmapDataBuffer;
	uint32 LightMapDataBufferSize;

	bool bResizedPrimitiveData = false;
	bool bResizedInstanceSceneData = false;
	bool bResizedLightmapData = false;
};


// Include in shader that modifies GPU-Scene instances
BEGIN_SHADER_PARAMETER_STRUCT(FGPUSceneWriterParameters, )
	SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, GPUSceneInstanceSceneDataRW)
	SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, GPUScenePrimitiveSceneDataRW)
	SHADER_PARAMETER(uint32, GPUSceneInstanceSceneDataSOAStride)
	SHADER_PARAMETER(uint32, GPUSceneFrameNumber)
	SHADER_PARAMETER(uint32, GPUSceneNumAllocatedInstances)
	SHADER_PARAMETER(uint32, GPUSceneNumAllocatedPrimitives)
END_SHADER_PARAMETER_STRUCT()

class FGPUScene
{
public:
	FGPUScene()
		: bUpdateAllPrimitives(false)
		, InstanceSceneDataSOAStride(0)
	{
	}
	~FGPUScene();

	void SetEnabled(ERHIFeatureLevel::Type InFeatureLevel);
	bool IsEnabled() const { return bIsEnabled; }
	/**
	 * Call at start of rendering (but after scene primitives are updated) to let GPU-Scene record scene primitive count 
	 * and prepare for dynamic primitive allocations.
	 * Scene may be NULL which means there are zero scene primitives (but there may be dynamic ones added later).
	 */
	void BeginRender(const FScene* Scene, FGPUSceneDynamicContext &GPUSceneDynamicContext);
	inline bool IsRendering() const { return bInBeginEndBlock; }
	void EndRender();

	ERHIFeatureLevel::Type GetFeatureLevel() const { return FeatureLevel; }
	EShaderPlatform GetShaderPlatform() const { return GShaderPlatformForFeatureLevel[FeatureLevel]; }

	/**
	 * Allocates a range of space in the instance scene data buffer for the required number of instances, 
	 * returns the offset to the first instance or INDEX_NONE if either the allocation failed or NumInstanceSceneDataEntries was zero.
	 * Marks the instances as requiring update (actual update is handled later).
	 */
	int32 AllocateInstanceSceneDataSlots(int32 NumInstanceSceneDataEntries);
	
	/**
	 * Free the instance data slots for reuse.
	 */
	void FreeInstanceSceneDataSlots(int32 InstanceSceneDataOffset, int32 NumInstanceSceneDataEntries);

	int32 AllocateInstancePayloadDataSlots(int32 NumInstancePayloadFloat4Entries);
	void FreeInstancePayloadDataSlots(int32 InstancePayloadDataOffset, int32 NumInstancePayloadFloat4Entries);

	/**
	 * Upload primitives from View.DynamicPrimitiveCollector.
	 */
	void UploadDynamicPrimitiveShaderDataForView(FRDGBuilder& GraphBuilder, FScene* Scene, FViewInfo& View);

	/**
	 * Pull all pending updates from Scene and upload primitive & instance data.
	 */
	void Update(FRDGBuilder& GraphBuilder, FScene& Scene);

	/**
	 * Queue the given primitive for upload to GPU at next call to Update.
	 * May be called multiple times, dirty-flags are cumulative.
	 */
	void RENDERER_API AddPrimitiveToUpdate(int32 PrimitiveId, EPrimitiveDirtyState DirtyState = EPrimitiveDirtyState::ChangedAll);

	/**
	 * Let GPU-Scene know that two primitive IDs swapped location, such that dirty-state can be tracked.
	 * Marks both as having changed ID. 
	 */
	FORCEINLINE void RecordPrimitiveIdSwap(int32 PrimitiveIdA, int32 PrimitiveIdB)
	{
		if (IsEnabled())
		{
			// We should never call this on a non-existent primitive, so no need to resize
			checkSlow(PrimitiveIdA < PrimitiveDirtyState.Num());
			checkSlow(PrimitiveIdB < PrimitiveDirtyState.Num());

			if (PrimitiveDirtyState[PrimitiveIdA] == EPrimitiveDirtyState::None)
			{
				PrimitivesToUpdate.Add(PrimitiveIdA);
			}
			PrimitiveDirtyState[PrimitiveIdA] |= EPrimitiveDirtyState::ChangedId;
			if (PrimitiveDirtyState[PrimitiveIdB] == EPrimitiveDirtyState::None)
			{
				PrimitivesToUpdate.Add(PrimitiveIdB);
			}
			PrimitiveDirtyState[PrimitiveIdB] |= EPrimitiveDirtyState::ChangedId;

			Swap(PrimitiveDirtyState[PrimitiveIdA], PrimitiveDirtyState[PrimitiveIdB]);
		}
	}

	FORCEINLINE EPrimitiveDirtyState GetPrimitiveDirtyState(int32 PrimitiveId) const { return PrimitiveDirtyState[PrimitiveId]; }

	FORCEINLINE void ResizeDirtyState(int32 NewSizeIn)
	{
		if (IsEnabled() && NewSizeIn > PrimitiveDirtyState.Num())
		{
			const int32 NewSize = Align(NewSizeIn, 64);
			static_assert(static_cast<uint32>(EPrimitiveDirtyState::None) == 0U, "Using AddZeroed to ensure efficent add, requires None == 0");
			PrimitiveDirtyState.AddZeroed(NewSize - PrimitiveDirtyState.Num());
		}
	}

	/**
	 * Call before accessing the GPU scene in a read/write pass, returns false if read/write acces is not supported on the platform or the GPU scene is not enabled.
	 */
	bool BeginReadWriteAccess(FRDGBuilder& GraphBuilder);

	/**
	 * Fills in the FGPUSceneWriterParameters to use for read/write access to the GPU Scene.
	 */
	void GetWriteParameters(FGPUSceneWriterParameters& GPUSceneWriterParametersOut);

	/**
	 * Call after accessing the GPU scene in a read/write pass. Ensures barriers are done.
	 */
	void EndReadWriteAccess(FRDGBuilder& GraphBuilder, ERHIAccess FinalAccessState = ERHIAccess::SRVGraphics);

	uint32 GetSceneFrameNumber() const { return SceneFrameNumber; }

	int32 GetNumInstances() const { return InstanceSceneDataAllocator.GetMaxSize(); }
	int32 GetNumPrimitives() const { return DynamicPrimitivesOffset; }

	bool bUpdateAllPrimitives;

	/** Indices of primitives that need to be updated in GPU Scene */
	TArray<int32>			PrimitivesToUpdate;

	/** GPU mirror of Primitives */
	FRWBufferStructured PrimitiveBuffer;
	FScatterUploadBuffer PrimitiveUploadBuffer;

	/** GPU primitive instance list */
	FGrowOnlySpanAllocator	InstanceSceneDataAllocator;
	FRWBufferStructured		InstanceSceneDataBuffer;
	FScatterUploadBuffer	InstanceSceneUploadBuffer;
	uint32					InstanceSceneDataSOAStride;	// Distance between arrays in float4s

	FGrowOnlySpanAllocator	InstancePayloadDataAllocator;
	//FRWBufferStructured		InstancePayloadDataBuffer;
	//FScatterUploadBuffer	InstancePayloadUploadBuffer;

	FRWBufferStructured		InstanceBVHBuffer;

	/** GPU light map data */
	FGrowOnlySpanAllocator	LightmapDataAllocator;
	FRWBufferStructured		LightmapDataBuffer;
	FScatterUploadBuffer	LightmapUploadBuffer;

	using FInstanceGPULoadBalancer = TInstanceCullingLoadBalancer<SceneRenderingAllocator>;
private:
	TArray<EPrimitiveDirtyState> PrimitiveDirtyState;

	TBitArray<>				InstanceSceneDataToClear;
	TSet<uint32>			InstanceSceneDataClearList;

	TRange<int32> CommitPrimitiveCollector(FGPUScenePrimitiveCollector& PrimitiveCollector);


	friend class FGPUScenePrimitiveCollector;

	/** 
	 * Stores a copy of the Scene.GetFrameNumber() when updated. Used to track which primitives/instances are updated.
	 * When using GPU-Scene for rendering it should ought to be the same as that stored in the Scene (otherwise they are not in sync).
	 */
	uint32 SceneFrameNumber = 0xFFFFFFFF;

	int32 DynamicPrimitivesOffset = 0;

	bool bIsEnabled = false;
	bool bInBeginEndBlock = false;
	FGPUSceneDynamicContext* CurrentDynamicContext = nullptr;
	int32 NumScenePrimitives = 0;

	ERHIFeatureLevel::Type FeatureLevel;

	template<typename FUploadDataSourceAdapter>
	FGPUSceneBufferState UpdateBufferState(FRDGBuilder& GraphBuilder, FScene* Scene, const FUploadDataSourceAdapter& UploadDataSourceAdapter);

	/**
	 * Generalized upload that uses an adapter to abstract the data souce. Enables uploading scene primitives & dynamic primitives using a single path.
	 * @parameter Scene may be null, as it is only needed for the Nanite material table update (which is coupled to the Scene at the moment).
	 */
	template<typename FUploadDataSourceAdapter>
	void UploadGeneral(FRHICommandListImmediate& RHICmdList, FScene* Scene, const FUploadDataSourceAdapter& UploadDataSourceAdapter, const FGPUSceneBufferState &BufferState);

	void UploadDynamicPrimitiveShaderDataForViewInternal(FRDGBuilder& GraphBuilder, FScene* Scene, FViewInfo& View);

	void UpdateInternal(FRDGBuilder& GraphBuilder, FScene& Scene);

	void AddUpdatePrimitiveIdsPass(FRDGBuilder& GraphBuilder, FInstanceGPULoadBalancer& IdOnlyUpdateItems);
};

class FGPUSceneScopeBeginEndHelper
{
public:
	FGPUSceneScopeBeginEndHelper(FGPUScene& InGPUScene, FGPUSceneDynamicContext &GPUSceneDynamicContext, const FScene* Scene) :
		GPUScene(InGPUScene)
	{
		GPUScene.BeginRender(Scene, GPUSceneDynamicContext);
	}

	~FGPUSceneScopeBeginEndHelper()
	{
		GPUScene.EndRender();
	}

private:
	FGPUSceneScopeBeginEndHelper() = delete;
	FGPUSceneScopeBeginEndHelper(const FGPUSceneScopeBeginEndHelper&) = delete;
	FGPUScene& GPUScene;
};

struct FGPUSceneCompactInstanceData
{
	FVector4 InstanceOriginAndId;
	FVector4 InstanceTransform1;
	FVector4 InstanceTransform2;
	FVector4 InstanceTransform3;
	FVector4 PackedLightmapData;

	void Init(const FGPUScenePrimitiveCollector* PrimitiveCollector, int32 PrimitiveId);
	void Init(const FScene* Scene, int32 PrimitiveId);
};
