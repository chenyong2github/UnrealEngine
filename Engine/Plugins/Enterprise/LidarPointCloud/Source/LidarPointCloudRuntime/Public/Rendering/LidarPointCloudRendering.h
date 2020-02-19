// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class ILidarPointCloudSceneProxy;

/** This allows the LOD Manager to observe the SceneProxy via weak pointer */
struct FLidarPointCloudSceneProxyWrapper
{
	ILidarPointCloudSceneProxy* Proxy;
	FLidarPointCloudSceneProxyWrapper(ILidarPointCloudSceneProxy* Proxy) : Proxy(Proxy) {}
};

/** Used to pass data to RT to update the proxy's render data */
struct FLidarPointCloudProxyUpdateData
{
	TWeakPtr<FLidarPointCloudSceneProxyWrapper, ESPMode::ThreadSafe> SceneProxyWrapper;

	/** Index of the first element within the structured buffer */
	int32 FirstElementIndex;

	/** Number of elements within the structured buffer related to this proxy */
	int32 NumElements;

	/** Contains the current global point budget */
	int32 PointBudget;

	float VDMultiplier;
	float RootCellSize;

#if !(UE_BUILD_SHIPPING)
	/** Stores bounds of selected nodes, used for debugging */
	TArray<FBox> Bounds;
#endif

	FLidarPointCloudProxyUpdateData();
};

/** Used for communication between LOD Manager and SceneProxy */
class ILidarPointCloudSceneProxy
{
public:
	/** Updates necessary render data for the proxy. Initiated via LOD Manager's Tick */
	virtual void UpdateRenderData(FLidarPointCloudProxyUpdateData InRenderData) = 0;
};