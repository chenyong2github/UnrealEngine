// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tickable.h"
#include "PrimitiveSceneProxy.h"
#include "LidarPointCloudShared.h"
#include "Rendering/LidarPointCloudRendering.h"

class ULidarPointCloud;
class ULidarPointCloudComponent;
struct FLidarPointCloudTraversalOctree;
struct FLidarPointCloudTraversalOctreeNode;

/** Stores View data required to calculate LODs for Lidar Point Clouds */
struct FLidarPointCloudViewData
{
	bool bValid;
	FVector ViewOrigin;
	FVector ViewDirection;
	float ScreenSizeFactor;
	FConvexVolume ViewFrustum;
	bool bSkipMinScreenSize;
	bool bPIE;
	bool bHasFocus;

	FLidarPointCloudViewData(bool bCompute = false);

	void Compute();
	bool ComputeFromEditorViewportClient(class FViewportClient* ViewportClient);
};

/** Convenience struct to group all selection params into one */
struct FLidarPointCloudNodeSelectionParams
{
	float MinScreenSize;
	float ScreenCenterImportance;
	int32 MinDepth;
	int32 MaxDepth;
	float BoundsScale;
};

/**  
 * This class is responsible for selecting nodes for rendering among all instances of all LidarPointCloud assets.
 * 
 */
class FLidarPointCloudLODManager : public FTickableGameObject
{
	struct FRegisteredProxy
	{
		ULidarPointCloudComponent* Component;
		ULidarPointCloud* PointCloud;
		TWeakPtr<FLidarPointCloudSceneProxyWrapper, ESPMode::ThreadSafe> SceneProxyWrapper;
		TSharedPtr<FLidarPointCloudTraversalOctree, ESPMode::ThreadSafe> TraversalOctree;

		FRegisteredProxy(ULidarPointCloudComponent* Component, TWeakPtr<FLidarPointCloudSceneProxyWrapper, ESPMode::ThreadSafe> SceneProxyWrapper);

		/** Used to detect transform changes without the need of callbacks from the SceneProxy */
		FTransform LastComponentTransform;

		FLidarPointCloudViewData ViewData;

		/** If true, this proxy will be skipped. Used to avoid point duplication in PIE */
		bool bSkip;
	};

	/** This holds the list of currently registered proxies, which will be used for node selection */
	TArray<FRegisteredProxy> RegisteredProxies;

	/** Allows skipping processing, if another one is already in progress */
	TAtomic<bool> bProcessing;

	/** Stores cumulative time, elapsed from the creation of the manager. Used to determine nodes' lifetime. */
	float Time;

public:
	/** Used for node size sorting and node selection. */
	struct FNodeSizeData
	{
		FLidarPointCloudTraversalOctreeNode* Node;
		float Size;
		int32 ProxyIndex;
		FNodeSizeData(FLidarPointCloudTraversalOctreeNode* Node, const float& Size, const int32& ProxyIndex) : Node(Node), Size(Size), ProxyIndex(ProxyIndex) {}
	};

	virtual void Tick(float DeltaTime) override;

	virtual TStatId GetStatId() const override;

	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Always; }

	virtual bool IsTickableInEditor() const override { return true; }

	static void RegisterProxy(ULidarPointCloudComponent* Component, TWeakPtr<FLidarPointCloudSceneProxyWrapper, ESPMode::ThreadSafe> SceneProxyWrapper);

private:
	/**
	 * This function:
	 * - Resizes the global IndexBuffer and StructuredBuffer to fit the required GlobalPointBudget
	 * - Iterates over all registered proxies and selects the best set of nodes within the point budget
	 * - Generates the data for the StructuredBuffer and passes it to the Render Thread for an update
	 */
	void ProcessLOD(const TArray<FRegisteredProxy>& RegisteredProxies, const float CurrentTime);

	/** Called to prepare the proxies for processing */
	void PrepareProxies();
};