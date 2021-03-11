// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderResource.h"
#include "RendererInterface.h"
#include "PrimitiveUniformShaderParameters.h"

#include "GrowOnlySpanAllocator.h"

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
		ensure(GPUSceneDynamicContext != nullptr);
		ensure(!bCommitted);

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
		ensure(bCommitted);

		ensure(PrimitiveIdRange.GetLowerBoundValue() >= 0);
		ensure(PrimitiveIdRange.Size<int32>() >= LocalIndex);
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
	FORCEINLINE const TRange<int32> &GetPrimitiveIdRange() const
	{ 
		ensure(bCommitted || UploadData == nullptr);
		return PrimitiveIdRange; 
	}

	FORCEINLINE int32 GetInstanceDataOffset(int32 PrimitiveId) const
	{
		ensure(bCommitted);
		ensure(PrimitiveIdRange.Contains(PrimitiveId));
		ensure(UploadData->bIsUploaded);

		// Assume a 1:1 mapping between primitive ID and instance ID
		return UploadData->InstanceDataOffset + (PrimitiveId - PrimitiveIdRange.GetLowerBoundValue());
	}

	int32 Num() const {	return UploadData != nullptr ? UploadData->PrimitiveShaderData.Num() : 0; }
private:

	friend class FGPUScene;
	friend class FGPUSceneDynamicContext;

	struct FUploadData
	{
		TArray<FPrimitiveUniformShaderParameters, TInlineAllocator<8>> PrimitiveShaderData;
		int32 InstanceDataOffset = INDEX_NONE;
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

/**
 * Contains and controls the lifetime of any dynamic primitive data collected for the scene rendering.
 * Typically shares life-time with the SceneRenderer. 
 */
class FGPUSceneDynamicContext
{
public:
	FGPUSceneDynamicContext(FGPUScene& InGPUScene) : GPUScene(InGPUScene) {}
	~FGPUSceneDynamicContext();

private:
	friend class FGPUScene;
	friend class FGPUScenePrimitiveCollector;

	FGPUScenePrimitiveCollector::FUploadData* AllocateDynamicPrimitiveData();
	TArray<FGPUScenePrimitiveCollector::FUploadData*, TInlineAllocator<128> > DymamicPrimitiveUploadData;
	FGPUScene& GPUScene;
};


class FGPUScene
{
public:
	FGPUScene()
		: bUpdateAllPrimitives(false)
		, InstanceDataSOAStride(0)
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
	 * Allocates a range of space in the instance data buffer for the required number of instances, 
	 * returns the offset to the first instance or INDEX_NONE if either the allocation failed or NumInstanceDataEntries was zero.
	 * Marks the instances as requiring update (actual update is handled later).
	 */
	int32 AllocateInstanceSlots(int32 NumInstanceDataEntries);
	
	/**
	 * Free the instance data slots for reuse.
	 */
	void FreeInstanceSlots(int32 InstanceDataOffset, int32 NumInstanceDataEntries);

	/**
	 * Flag the primitive as added this frame (flags are consumed / reset when the GPU-Scene update is invoked).
	 */
	void MarkPrimitiveAdded(int32 PrimitiveId);

	/**
	 * Upload primitives from View.DynamicPrimitiveCollector.
	 */
	void UploadDynamicPrimitiveShaderDataForView(FRHICommandListImmediate& RHICmdList, FScene* Scene, FViewInfo& View);

	/**
	 * Pull all pending updates from Scene and upload primitive & instance data.
	 */
	void Update(FRDGBuilder& GraphBuilder, FScene& Scene);

	/**
	 * Mark the given primitive for upload to GPU at next call to Update.
	 * May be called multiple times.
	 */
	void RENDERER_API AddPrimitiveToUpdate(int32 PrimitiveId);

	uint32 GetSceneFrameNumber() const { return SceneFrameNumber; }

	bool bUpdateAllPrimitives;

	/** Indices of primitives that need to be updated in GPU Scene */
	TArray<int32>			PrimitivesToUpdate;

	/** Bit array of all scene primitives. Set bit means that current primitive is in PrimitivesToUpdate array. */
	TBitArray<>				PrimitivesMarkedToUpdate;

	/** GPU mirror of Primitives */
	FRWBufferStructured PrimitiveBuffer;
	FScatterUploadBuffer PrimitiveUploadBuffer;
	FScatterUploadBuffer PrimitiveUploadViewBuffer;

	/** GPU primitive instance list */
	TBitArray<>				InstanceDataToClear;
	FGrowOnlySpanAllocator	InstanceDataAllocator;
	FRWBufferStructured		InstanceDataBuffer;
	FScatterUploadBuffer	InstanceUploadBuffer;
	uint32					InstanceDataSOAStride;	// Distance between arrays in float4s
	TSet<uint32>			InstanceClearList;
	FRWBufferStructured		InstanceBVHBuffer;

	/** GPU light map data */
	FGrowOnlySpanAllocator	LightmapDataAllocator;
	FRWBufferStructured		LightmapDataBuffer;
	FScatterUploadBuffer	LightmapUploadBuffer;

	/** Flag array with 1 bit set for each newly added primitive. */
	TBitArray<>				AddedPrimitiveFlags;

private:

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

	/**
	 * Generalized upload that uses an adapter to abstract the data souce. Enables uploading scene primitives & dynamic primitives using a single path.
	 * @parameter Scene may be null, as it is only needed for the Nanite material table update (which is coupled to the Scene at the moment).
	 */
	template<typename FUploadDataSourceAdapter>
	void UploadGeneral(FRHICommandListImmediate& RHICmdList, FScene* Scene, FUploadDataSourceAdapter& UploadDataSourceAdapter);

	void UploadDynamicPrimitiveShaderDataForViewInternal(FRHICommandListImmediate& RHICmdList, FScene* Scene, FViewInfo& View);

	void UpdateInternal(FRHICommandListImmediate& RHICmdList, FScene& Scene);
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
