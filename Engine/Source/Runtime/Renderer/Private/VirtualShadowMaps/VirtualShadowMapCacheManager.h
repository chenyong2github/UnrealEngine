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
	// Key data (must be same to be valid)

	// True if the cache has been (re)populated, set to false on init and set to true once the cache update process has happened.
	// Also set to false whenever key data was not valid and all cached data is invalidated.
	bool IsValid() { return PrevVirtualShadowMapId != INDEX_NONE; }


	void Update(int32 VirtualShadowMapId, const FWholeSceneProjectedShadowInitializer &InCacheValidKey);

	void UpdateClipmap(int32 VirtualShadowMapId, const FMatrix &WorldToLight,
		FIntPoint PageSpaceLocation, float GlobalDepth);

	/**
	 * Returns the PrevVirtualShadowMapId if cached data is valid (bValidData), or INDEX_NONE otherwise. 
	 */
	uint32 GetValidPrevVirtualShadowMapId() { return PrevVirtualShadowMapId; }

	FIntPoint GetPageSpaceOffset() const { return PrevPageSpaceLocation - CurrentPageSpaceLocation; }

	/**
	 * Returns the depth offset to add to the depth of pages as they are copied.
	 */
	float GetDepthOffset() const { return PrevShadowMapGlobalDepth - CurrentShadowMapGlobalDepth; }

	// Previous frame data

	// Aligned location in pages previous frame
	//FIntRect PrevViewPort = FIntRect(0, 0, 0, 0);
	FIntPoint PrevPageSpaceLocation = FIntPoint(0,0);

	// Set to INDEX_NONE when cache data is invalidated by an external change, like movement.
	int32 PrevVirtualShadowMapId = INDEX_NONE;

	// Depth of the world-space origin of the shadow map in shadow maps space.
	// Used to offset the depth of pages as they are copied when the light moves
	float PrevShadowMapGlobalDepth = 0.0f;

	// Current frame data 
	
	// Aligned location in pages after update.
	FIntPoint CurrentPageSpaceLocation = FIntPoint(0, 0);
	int32 CurrentVirtualShadowMapId = INDEX_NONE;
	float CurrentShadowMapGlobalDepth = 0.0f;

	// TODO: Potentially refactor this to decouple the cache key details
	FWholeSceneProjectedShadowInitializer CacheValidKey;

	struct ClipmapCacheKey
	{
		FMatrix	WorldToLight;
	};
	ClipmapCacheKey ClipmapCacheValidKey;
};

// Persistent buffers that we ping pong frame by frame
struct FVirtualShadowMapArrayFrameData
{
	TRefCountPtr<FRDGPooledBuffer>		PageTable;
	TRefCountPtr<FRDGPooledBuffer>		PageFlags;
	TRefCountPtr<FRDGPooledBuffer>		HPageFlags;

	TRefCountPtr<FRDGPooledBuffer>		ShadowMapProjectionDataBuffer;
	TRefCountPtr<FRDGPooledBuffer>		PageRectBounds;

	TRefCountPtr<FRDGPooledBuffer>		DynamicCasterPageFlags;

	TRefCountPtr<IPooledRenderTarget>	PhysicalPagePool;
	TRefCountPtr<IPooledRenderTarget>	PhysicalPagePoolHw;
	TRefCountPtr<FRDGPooledBuffer>		PhysicalPageMetaData;

	TRefCountPtr<IPooledRenderTarget>			HZBPhysical;
	TMap<int32, FVirtualShadowMapHZBMetadata>	HZBMetadata;
};

class FVirtualShadowMapArrayCacheManager
{
public:
	// Align global coordinates to this mip-level, e.g., 3 and page size 128 => 512 texels 
	// This is only relevant for directional lights that support scrolling, spot lights are invalidated wholesale on movement
	static constexpr uint32 AlignmentLevel = 3U;
	static constexpr uint32 AlignmentPages = (1U << AlignmentLevel);
	static constexpr uint32 AlignmentTexels = AlignmentPages * FVirtualShadowMap::PageSize;
	static constexpr uint32 EffectiveCacheResolutionTexels = FVirtualShadowMap::VirtualMaxResolutionXY - AlignmentTexels;
	static constexpr uint32 EffectiveCacheResolutionPages = FVirtualShadowMap::Level0DimPagesXY - AlignmentPages;

	static constexpr float ClipSpaceScaleFactor = float(EffectiveCacheResolutionTexels) / float(FVirtualShadowMap::VirtualMaxResolutionXY);

	/**
	 * Call at end of frame to extract resouces from the virtual SM array to preserve to next frame.
	 * If bCachingEnabled is false, all previous frame data is dropped and cache (and HZB!) data will not be available for the next frame.
	 */ 
	void ExtractFrameData(bool bEnableCaching, FVirtualShadowMapArray &VirtualShadowMapArray, FRDGBuilder& GraphBuilder);

	/**
	 * Finds an existing cache entry and moves to the active set or creates a fresh one.
	 */
	TSharedPtr<FVirtualShadowMapCacheEntry> FindCreateCacheEntry(int32 LightSceneId, int32 CascadeIndex);

	/*
	 * Returns true if cached data is available.
	 */
	bool IsValid();

	/**
	 */
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

	// Index the Cache entries by the light ID and cascade index
	TMap< FIntPoint, TSharedPtr<FVirtualShadowMapCacheEntry> > CacheEntries;
	TMap< FIntPoint, TSharedPtr<FVirtualShadowMapCacheEntry> > PrevCacheEntries;

	FVirtualShadowMapArrayFrameData PrevBuffers;
	FVirtualShadowMapUniformParameters PrevUniformParameters;

	// Enough for er lots...
	static constexpr uint32 MaxStatFrames = 512*1024U;
	
	// Stores stats over frames when activated.
	TRefCountPtr<FRDGPooledBuffer>		AccumulatedStatsBuffer;
	bool bAccumulatingStats = false;
	FRHIGPUBufferReadback *GPUBufferReadback = nullptr;
	
protected:

	// Must match shader...
	struct FInstanceDataRange
	{
		int32 InstanceDataOffset;
		int32 NumInstanceDataEntries;
	};

	void ProcessInstanceRangeInvalidation(FRDGBuilder& GraphBuilder, const TArray<FInstanceDataRange>& InstanceRangesLarge, const TArray<FInstanceDataRange>& InstanceRangesSmall, const FGPUScene& GPUScene);
};
