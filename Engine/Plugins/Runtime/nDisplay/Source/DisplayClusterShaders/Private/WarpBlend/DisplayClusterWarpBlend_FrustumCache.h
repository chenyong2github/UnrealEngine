// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WarpBlend/DisplayClusterWarpContext.h"

class FDisplayClusterWarpBlend_FrustumCache
{
public:
	FDisplayClusterWarpBlend_FrustumCache()
	{ }

	~FDisplayClusterWarpBlend_FrustumCache()
	{ }

public:
	void SetFrustumCacheDepth(int InFrustumCacheDepth);
	void SetFrustumCachePrecision(float InFrustumCachePrecision);

	bool GetCachedFrustum(const FDisplayClusterWarpEye& InEye, FDisplayClusterWarpContext& OutContext);
	void AddFrustum(const FDisplayClusterWarpEye& InEye, const FDisplayClusterWarpContext& InContext);

	void ResetFrustumCache();

private:
	struct FCacheItem
	{
		FDisplayClusterWarpEye     Eye;
		FDisplayClusterWarpContext Context;

		FCacheItem(const FDisplayClusterWarpEye& InEye, const FDisplayClusterWarpContext& InContext)
			: Eye(InEye)
			, Context(InContext)
		{}
	};

	TArray<FCacheItem> FrustumCache;

	int FrustumCacheDepth = 0;
	float FrustumCachePrecision = 0;
};
