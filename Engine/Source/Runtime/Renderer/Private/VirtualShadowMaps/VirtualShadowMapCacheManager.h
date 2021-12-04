// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VirtualShadowMapArray.h:
=============================================================================*/
#pragma once

#include "CoreMinimal.h"
#include "VirtualShadowMapArray.h"
#include "SceneManagement.h"
#include "InstanceCulling/InstanceCullingLoadBalancer.h"
#include "GPUScene.h"
#include "GPUMessaging.h"

class FRHIGPUBufferReadback;
class FGPUScene;

#define VSM_LOG_INVALIDATIONS 0

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

	TRefCountPtr<FRDGPooledBuffer>				InvalidatingInstancesBuffer;
	int32										NumInvalidatingInstanceSlots = 0;
};

class FVirtualShadowMapArrayCacheManager
{
public:
	FVirtualShadowMapArrayCacheManager(FScene *InScene);
	~FVirtualShadowMapArrayCacheManager();

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

	using FInstanceGPULoadBalancer = TInstanceCullingLoadBalancer<SceneRenderingAllocator>;

	/**
	 * Helper to collect primitives that need invalidation, filters out redundant adds and also those that are not yet known to the GPU
	 */
	class FInvalidatingPrimitiveCollector
	{
	public:
		FInvalidatingPrimitiveCollector(int32 MaxPrimitiveID, const FGPUScene& InGPUScene)
			: AlreadyAddedPrimitives(false, MaxPrimitiveID)
			, GPUScene(InGPUScene)
		{
		}

		/**
		 * Add a primitive to invalidate the instances for, the function filters redundant primitive adds, and thus expects valid IDs (so can't be called for primitives that have not yet been added)
		 * and unchanging IDs (so can't be used over a span that include any scene mutation).
		 */
		void Add(const FPrimitiveSceneInfo* PrimitiveSceneInfo)
		{
			int32 PrimitiveID = PrimitiveSceneInfo->GetIndex();
			if (PrimitiveID >= 0
				&& !AlreadyAddedPrimitives[PrimitiveID]
				&& PrimitiveSceneInfo->GetInstanceSceneDataOffset() != INDEX_NONE
				// Don't process primitives that are still in the 'added' state because this means that they
				// have not been uploaded to the GPU yet and may be pending from a previous call to update primitive scene infos.
				&& !EnumHasAnyFlags(GPUScene.GetPrimitiveDirtyState(PrimitiveID), EPrimitiveDirtyState::Added))
			{
				AlreadyAddedPrimitives[PrimitiveID] = true;
				const int32 NumInstanceSceneDataEntries = PrimitiveSceneInfo->GetNumInstanceSceneDataEntries();
				LoadBalancer.Add(PrimitiveSceneInfo->GetInstanceSceneDataOffset(), NumInstanceSceneDataEntries, 0U);
#if VSM_LOG_INVALIDATIONS
				RangesStr.Appendf(TEXT("[%6d, %6d), "), PrimitiveSceneInfo->GetInstanceSceneDataOffset(), PrimitiveSceneInfo->GetInstanceSceneDataOffset() + NumInstanceSceneDataEntries);
#endif
				TotalInstanceCount += NumInstanceSceneDataEntries;
			}
		}

		bool IsEmpty() const { return LoadBalancer.IsEmpty(); }

		TBitArray<SceneRenderingAllocator> AlreadyAddedPrimitives;
		FInstanceGPULoadBalancer LoadBalancer;
		int32 TotalInstanceCount = 0;
#if VSM_LOG_INVALIDATIONS
		FString RangesStr;
#endif
		const FGPUScene& GPUScene;
	};
	/**
	 * This must to be executed before the instances are actually removed / updated, otherwise the wrong position will be used. 
	 * In particular, it must be processed before the Scene primitive IDs are updated/compacted as part of the removal.
	 * Invalidate pages that are touched by (the instances of) the removed primitives. 
	 */
	void ProcessRemovedOrUpdatedPrimitives(FRDGBuilder& GraphBuilder, const FGPUScene& GPUScene, FInvalidatingPrimitiveCollector& InvalidatingPrimitiveCollector);

	TRDGUniformBufferRef<FVirtualShadowMapUniformParameters> GetPreviousUniformBuffer(FRDGBuilder& GraphBuilder) const;

	FVirtualShadowMapArrayFrameData PrevBuffers;
	FVirtualShadowMapUniformParameters PrevUniformParameters;
		
	void SetHZBViewParams(int32 HZBKey, Nanite::FPackedViewParams& OutParams);


	GPUMessage::FSocket StatusFeedbackSocket;

private:
	void ProcessInvalidations(FRDGBuilder& GraphBuilder, FInstanceGPULoadBalancer& Instances, int32 TotalInstanceCount, const FGPUScene& GPUScene);

	void ProcessGPUInstanceInvalidations(FRDGBuilder& GraphBuilder, const FGPUScene& GPUScene);

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
#if !UE_BUILD_SHIPPING
	FDelegateHandle ScreenMessageDelegate;
	int32 LastOverflowFrame = -1;
	bool bLoggedPageOverflow = false;
#endif
	FScene* Scene;
};
