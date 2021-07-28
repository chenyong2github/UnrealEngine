// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VirtualShadowMapArray.h:
=============================================================================*/
#pragma once

#include "CoreMinimal.h"
#include "VirtualShadowMapArray.h"
#include "SceneManagement.h"

class FRHIGPUBufferReadback;
class FGPUScene;

class FVirtualShadowMapCacheEntry
{
public:
	// True if the cache has been (re)populated, set to false on init and set to true once the cache update process has happened.
	// Also set to false whenever key data was not valid and all cached data is invalidated.
	bool IsValid() { return PrevVirtualShadowMapId != INDEX_NONE && bPrevRendered; }

	void UpdateLocal(int32 VirtualShadowMapId,
		const FWholeSceneProjectedShadowInitializer &InCacheValidKey);

	void UpdateClipmap(int32 VirtualShadowMapId,
		const FMatrix &WorldToLight,
		FIntPoint PageSpaceLocation,
		float LevelRadius,
		float ViewCenterZ,
		float ViewRadiusZ);

	void MarkRendered() { bCurrentRendered = true; }

	// Previous frame data
	FIntPoint PrevPageSpaceLocation = FIntPoint(0, 0);
	int32 PrevVirtualShadowMapId = INDEX_NONE;
	bool bPrevRendered = false;

	// Current frame data
	FIntPoint CurrentPageSpaceLocation = FIntPoint(0, 0);
	int32 CurrentVirtualShadowMapId = INDEX_NONE;
	bool bCurrentRendered = false;

	// TODO: Potentially refactor this to decouple the cache key details
	FWholeSceneProjectedShadowInitializer LocalCacheValidKey;

	struct FClipmapInfo
	{
		FMatrix	WorldToLight;
		float ViewCenterZ;
		float ViewRadiusZ;
	};
	FClipmapInfo Clipmap;
};

// Persistent buffers that we ping pong frame by frame
struct FVirtualShadowMapArrayFrameData
{
	TRefCountPtr<FRDGPooledBuffer>				PageTable;
	TRefCountPtr<FRDGPooledBuffer>				PageFlags;
	TRefCountPtr<FRDGPooledBuffer>				HPageFlags;

	TRefCountPtr<FRDGPooledBuffer>				ShadowMapProjectionDataBuffer;
	TRefCountPtr<FRDGPooledBuffer>				PageRectBounds;

	TRefCountPtr<FRDGPooledBuffer>				DynamicCasterPageFlags;

	TRefCountPtr<FRDGPooledBuffer>				PhysicalPageMetaData;

	TRefCountPtr<IPooledRenderTarget>			HZBPhysical;
	TMap<int32, FVirtualShadowMapHZBMetadata>	HZBMetadata;
};

class FVirtualShadowMapArrayCacheManager
{
public:
	// Enough for er lots...
	static constexpr uint32 MaxStatFrames = 512*1024U;

	// Called by VirtualShadowMapArray to potentially resize the physical pool
	// If the requested size is not already the size, all cache data is dropped and the pool is resized.
	TRefCountPtr<IPooledRenderTarget> SetPhysicalPoolSize(FRDGBuilder& GraphBuilder, FIntPoint RequestedSize);

	void FreePhysicalPool();

	// Invalidate the cache for all shadows, causing any pages to be rerendered
	void Invalidate();

	/**
	 * Call at end of frame to extract resouces from the virtual SM array to preserve to next frame.
	 * If bCachingEnabled is false, all previous frame data is dropped and cache (and HZB!) data will not be available for the next frame.
	 */ 
	void ExtractFrameData(FRDGBuilder& GraphBuilder,
		FVirtualShadowMapArray &VirtualShadowMapArray,
		bool bEnableCaching);

	/**
	 * Finds an existing cache entry and moves to the active set or creates a fresh one.
	 */
	TSharedPtr<FVirtualShadowMapCacheEntry> FindCreateCacheEntry(int32 LightSceneId, int32 Index = 0);

	/*
	 * Returns true if cached data is available.
	 */
	bool IsValid();

	bool IsAccumulatingStats();

	/**
	 * This must to be executed before the instances are actually removed / updated, otherwise the wrong position will be used. 
	 * In particular, it must be processed before the Scene primitive IDs are updated/compacted as part of the removal.
	 * Invalidate pages that are touched by (the instances of) the removed primitives. 
	 */
	void ProcessRemovedPrimives(FRDGBuilder& GraphBuilder, const FGPUScene& GPUScene, const TArray<FPrimitiveSceneInfo*>& RemovedPrimitiveSceneInfos);

	/**
	 * Invalidate pages that are touched by (the instances of) all primitive about to be removed. Must be called before the GPU-Scene is updated (but after all upates are registered).
	 */
	void ProcessPrimitivesToUpdate(FRDGBuilder& GraphBuilder, const FScene& Scene);

	TRDGUniformBufferRef<FVirtualShadowMapUniformParameters> GetPreviousUniformBuffer(FRDGBuilder& GraphBuilder) const;

	FVirtualShadowMapArrayFrameData PrevBuffers;
	FVirtualShadowMapUniformParameters PrevUniformParameters;
		
private:
	// Must match shader...
	struct FInstanceSceneDataRange
	{
		int32 InstanceSceneDataOffset;
		int32 NumInstanceSceneDataEntries;
	};

	void ProcessInstanceRangeInvalidation(
		FRDGBuilder& GraphBuilder,
		const TArray<FInstanceSceneDataRange,
		SceneRenderingAllocator>& InstanceRangesLarge,
		const TArray<FInstanceSceneDataRange,
		SceneRenderingAllocator>& InstanceRangesSmall,
		const FGPUScene& GPUScene);

	void ExtractStats(FRDGBuilder& GraphBuilder, FVirtualShadowMapArray &VirtualShadowMapArray);

	// The actual physical texture data is stored here rather than in VirtualShadowMapArray (which is recreated each frame)
	// This allows us to (optionally) persist cached pages between frames. Regardless of whether caching is enabled,
	// we store the physical pool here.
	TRefCountPtr<IPooledRenderTarget> PhysicalPagePool;

	// Index the Cache entries by the light ID and clipmap/cubemap face index (if applicable)
	TMap< FIntPoint, TSharedPtr<FVirtualShadowMapCacheEntry> > CacheEntries;
	TMap< FIntPoint, TSharedPtr<FVirtualShadowMapCacheEntry> > PrevCacheEntries;

	// Stores stats over frames when activated.
	TRefCountPtr<FRDGPooledBuffer> AccumulatedStatsBuffer;
	bool bAccumulatingStats = false;
	FRHIGPUBufferReadback* GPUBufferReadback = nullptr;
};
