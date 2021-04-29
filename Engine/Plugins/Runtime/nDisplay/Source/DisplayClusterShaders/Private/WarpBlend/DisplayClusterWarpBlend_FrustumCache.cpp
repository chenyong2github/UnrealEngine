// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterWarpBlend_FrustumCache.h"

void FDisplayClusterWarpBlend_FrustumCache::SetFrustumCacheDepth(int InFrustumCacheDepth)
{
	check(IsInGameThread());

	FrustumCacheDepth = InFrustumCacheDepth;
}

void FDisplayClusterWarpBlend_FrustumCache::SetFrustumCachePrecision(float InFrustumCachePrecision)
{
	check(IsInGameThread());

	FrustumCachePrecision = InFrustumCachePrecision;
}

bool FDisplayClusterWarpBlend_FrustumCache::GetCachedFrustum(const FDisplayClusterWarpEye& InEye, FDisplayClusterWarpContext& OutContext)
{
	check(IsInGameThread());

	if (FrustumCacheDepth == 0)
	{
		// Cache disabled, clear old values
		ResetFrustumCache();
	}
	else
	{
		// Try to use old frustum values from cache (reduce CPU cost)
		if (FrustumCache.Num() > 0)
		{
			for (int i = 0; i < FrustumCache.Num(); i++)
			{
				if (FrustumCache[i].Eye.IsEqual(InEye, FrustumCachePrecision))
				{
					// Use cached value
					OutContext = FrustumCache[i].Context;

					// Move used value on a cache top
					AddFrustum(InEye, OutContext);
					return true;
				}
			}
		}
	}

	return false;
}

void FDisplayClusterWarpBlend_FrustumCache::AddFrustum(const FDisplayClusterWarpEye& InEye, const FDisplayClusterWarpContext& InContext)
{
	check(IsInGameThread());

	// Store current used frustum value to cache
	if (FrustumCacheDepth > 0)
	{
		FrustumCache.Add(FCacheItem(InEye, InContext));
	}

	// Remove too old cached values
	int TotalTooOldValuesCount = FrustumCache.Num() - FrustumCacheDepth;
	if (TotalTooOldValuesCount > 0)
	{
		FrustumCache.RemoveAt(0, TotalTooOldValuesCount);
	}
}

void FDisplayClusterWarpBlend_FrustumCache::ResetFrustumCache()
{
	check(IsInGameThread());

	FrustumCache.Empty();
}

