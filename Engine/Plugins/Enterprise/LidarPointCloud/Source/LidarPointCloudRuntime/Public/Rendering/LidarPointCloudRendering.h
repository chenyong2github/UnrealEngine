// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LidarPointCloudShared.h"

class ILidarPointCloudSceneProxy;

/** This allows the LOD Manager to observe the SceneProxy via weak pointer */
struct FLidarPointCloudSceneProxyWrapper
{
	ILidarPointCloudSceneProxy* Proxy;
	FLidarPointCloudSceneProxyWrapper(ILidarPointCloudSceneProxy* Proxy) : Proxy(Proxy) {}
};

struct FLidarPointCloudProxyUpdateDataNode
{
	uint8 VirtualDepth;
	int64 NumVisiblePoints;
	struct FLidarPointCloudOctreeNode* DataNode;

	FLidarPointCloudProxyUpdateDataNode() : FLidarPointCloudProxyUpdateDataNode(0, 0, nullptr) {}
	FLidarPointCloudProxyUpdateDataNode(uint8 VirtualDepth, int64 NumVisiblePoints, FLidarPointCloudOctreeNode* DataNode)
		: VirtualDepth(VirtualDepth)
		, NumVisiblePoints(NumVisiblePoints)
		, DataNode(DataNode)
	{
	}
};

/** Used to pass data to RT to update the proxy's render data */
struct FLidarPointCloudProxyUpdateData
{
	TWeakPtr<FLidarPointCloudSceneProxyWrapper, ESPMode::ThreadSafe> SceneProxyWrapper;

	/** Number of elements within the structured buffer related to this proxy */
	int32 NumElements;

	TArray<FLidarPointCloudProxyUpdateDataNode> SelectedNodes;

	float VDMultiplier;
	float RootCellSize;
	
	bool bUseStaticBuffers;

	TArray<uint32> TreeStructure;

#if !(UE_BUILD_SHIPPING)
	/** Stores bounds of selected nodes, used for debugging */
	TArray<FBox> Bounds;
#endif

	TArray<FLidarPointCloudClippingVolumeParams> ClippingVolumes;

	FLidarPointCloudComponentRenderParams RenderParams;

	FLidarPointCloudProxyUpdateData();
};

/** Used for communication between LOD Manager and SceneProxy */
class ILidarPointCloudSceneProxy
{
public:
	/** Updates necessary render data for the proxy. Initiated via LOD Manager's Tick */
	virtual void UpdateRenderData(const FLidarPointCloudProxyUpdateData& InRenderData) = 0;
};