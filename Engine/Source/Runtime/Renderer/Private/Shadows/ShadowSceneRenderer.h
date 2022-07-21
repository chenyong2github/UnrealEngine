// Copyright Epic Games, Inc. All Rights Reserved.
/*=============================================================================
	ShadowSceneRenderer.h:
=============================================================================*/
#pragma once

#include "CoreMinimal.h"
#include "Containers/SparseArray.h"
#include "Containers/ArrayView.h"
#include "Containers/BinaryHeap.h"
#include "Containers/Array.h"

#include "../VirtualShadowMaps/VirtualShadowMapArray.h"

class FProjectedShadowInfo;
class FDeferredShadingSceneRenderer;
class FWholeSceneProjectedShadowInitializer;
class FRDGBuilder;
class FVirtualShadowMapPerLightCacheEntry;

/**
 * Transient scope for per-frame rendering resources for the shadow rendering.
 */
class FShadowSceneRenderer
{
public:
	FShadowSceneRenderer(FDeferredShadingSceneRenderer& InSceneRenderer);

	/**
	 * Add a cube/spot light for processing this frame.
	 * TODO: Don't use legacy FProjectedShadowInfo or other params, instead info should flow from persistent setup & update.
	 * TODO: Return reference to FLocalLightShadowFrameSetup ?
	 */
	TSharedPtr<FVirtualShadowMapPerLightCacheEntry> AddLocalLightShadow(const FWholeSceneProjectedShadowInitializer& Initializer, FProjectedShadowInfo* ProjectedShadowInfo, FLightSceneInfo* LightSceneInfo, float MaxScreenRadius);

	/**
	 * Call after view-dependent setup has been processed (InitView etc) but before any rendering activity has been kicked off.
	 */
	void PostInitDynamicShadowsSetup();

private:
	/**
	 * Select the budgeted set of distant lights to update this frame.
	 */
	void UpdateDistantLightPriorityRender();

	void PostSetupDebugRender();

	// TODO: void RenderVirtualShadowMaps(FRDGBuilder& GraphBuilder, bool bNaniteEnabled);

	struct FLocalLightShadowFrameSetup
	{
		TArray<FVirtualShadowMap*, TInlineAllocator<6>> VirtualShadowMaps;
		// link to legacy system stuff, to be removed in due time
		FProjectedShadowInfo* ProjectedShadowInfo = nullptr;
		FLightSceneInfo* LightSceneInfo = nullptr;
		TSharedPtr<FVirtualShadowMapPerLightCacheEntry> PerLightCacheEntry;
	};

	// TODO: maybe we want to keep these in a 1:1 sparse array wrt the light scene infos, for easy crossreference & GPU access (maybe)?
	//       tradeoff is easy to look up (given light ID) but not compact, but OTOH can keep compact lists of indices for various purposes
	TArray<FLocalLightShadowFrameSetup, SceneRenderingAllocator> LocalLights;

	// Priority queue of distant lights to update.
	FBinaryHeap<int32, uint32> DistantLightUpdateQueue;

	// Links to other systems etc.
	FDeferredShadingSceneRenderer& SceneRenderer;
	FScene& Scene;
	FVirtualShadowMapArray& VirtualShadowMapArray;
};
