// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LidarPointCloudShared.h"
#include "LidarPointCloudSettings.h"
#include "LidarPointCloudLODManager.h"
#include "HAL/ThreadSafeCounter64.h"
#include "Misc/ScopeLock.h"
#include "ConvexVolume.h"
#include "Interfaces/Interface_CollisionDataProvider.h"

class ULidarPointCloud;
class FLidarPointCloudOctree;
struct FLidarPointCloudTraversalOctree;
struct FLidarPointCloudTraversalOctreeNode;

/**
 * WARNING: Exercise caution when modifying the contents of the Octree, as it may be in use by the Rendering Thread via FPointCloudSceneProxy
 * Use the FLidarPointCloudOctree::DataLock prior to such attempt
 */

 /**
 Child ordering
 0	X- Y- Z-
 1	X- Y- Z+
 2	X- Y+ Z-
 3	X- Y+ Z+
 4	X+ Y- Z-
 5	X+ Y- Z+
 6	X+ Y+ Z-
 7	X+ Y+ Z+
 */

/**
 * Represents a single octant in the tree.
 */
struct FLidarPointCloudOctreeNode
{
private:
	/** Stores the time, at which the BulkData needs to be released */
	float BulkDataLifetime;

	/** Depth of this node */
	uint8 Depth;

	/** Location of this node inside the parent node - see the Child Ordering at the top of the file */
	uint8 LocationInParent;

	/** Center point of this node. */
	FVector Center;

	/** Stores the children array */
	// #todo: Change to TIndirectArray<> - investigate increased memory consumption, ~130 bytes / Node
	TArray<FLidarPointCloudOctreeNode*> Children;

	/** Marks the node for visibility recalculation next time it's necessary */
	bool bVisibilityDirty;

	/** Stores the number of visible points */
	int32 NumVisiblePoints;

	FCriticalSection MapLock;

	/** Used for streaming the data from disk */
	FLidarPointCloudBulkData BulkData;

	/** Used to keep track, which data is available for rendering */
	TAtomic<bool> bHasDataPending;

	/** This is used to prevent nodes with changed content from being overwritten by consecutive streaming. */
	TAtomic<bool> bCanReleaseData;

public:
	FORCEINLINE FLidarPointCloudOctreeNode() : FLidarPointCloudOctreeNode(nullptr, 0) {}
	FORCEINLINE FLidarPointCloudOctreeNode(FLidarPointCloudOctree* Tree, const uint8& Depth) : FLidarPointCloudOctreeNode(Tree, Depth, 0, FVector::ZeroVector) {}
	FLidarPointCloudOctreeNode(FLidarPointCloudOctree* Tree, const uint8& Depth, const uint8& LocationInParent, const FVector& Center);
	~FLidarPointCloudOctreeNode();
	FLidarPointCloudOctreeNode(const FLidarPointCloudOctreeNode&) = delete;
	FLidarPointCloudOctreeNode(FLidarPointCloudOctreeNode&&) = delete;
	FLidarPointCloudOctreeNode& operator=(const FLidarPointCloudOctreeNode&) = delete;
	FLidarPointCloudOctreeNode& operator=(FLidarPointCloudOctreeNode&&) = delete;

	/** Returns a pointer to the point data */
	FORCEINLINE FLidarPointCloudPoint* GetData() const { return BulkData.GetData(); }

	/** Returns a pointer to the point data and prevents it from being released */
	FLidarPointCloudPoint* GetPersistentData() const;

	/** Returns the sum of grid and padding points allocated to this node. */
	FORCEINLINE int32 GetNumPoints() const { return (int32)BulkData.GetElementCount(); }

	/** Returns the sum of visible grid and padding points allocated to this node. */
	int32 GetNumVisiblePoints() const { return NumVisiblePoints; }

	/** Calculates and returns the bounds of this node */
	FBox GetBounds(const FLidarPointCloudOctree* Tree) const;

	/** Calculates and returns the sphere bounds of this node */
	FSphere GetSphereBounds(const FLidarPointCloudOctree* Tree) const;

	/** Returns a pointer to the node at the given location, or null if one doesn't exist yet. */
	FLidarPointCloudOctreeNode* GetChildNodeAtLocation(const uint8& Location) const;

	void UpdateNumVisiblePoints();

	/** Attempts to insert given points to this node or passes it to the children, otherwise. */
	void InsertPoints(FLidarPointCloudOctree* Tree, const FLidarPointCloudPoint* Points, const int32& Count, ELidarPointCloudDuplicateHandling DuplicateHandling, const FVector& Translation);
	void InsertPoints(FLidarPointCloudOctree* Tree, FLidarPointCloudPoint** Points, const int32& Count, ELidarPointCloudDuplicateHandling DuplicateHandling, const FVector& Translation);

	/** Removes all points. */
	void Empty(bool bRecursive = true);

	/** Returns the maximum depth of any children of this node .*/
	uint32 GetMaxDepth() const;

	/** Returns the amount of memory used by this node */
	int64 GetAllocatedSize(bool bRecursive, bool bIncludeBulkData) const;

	/** Returns true, if the node has its data loaded */
	bool HasData() const { return BulkData.HasData(); }

	/**
	 * Releases the BulkData
	 * If forced, the node will be released even if persistent
	 */
	void ReleaseData(bool bForce = false);

	/**
	 * Convenience function, to add point statistics to the Tree table.
	 * If parameter set to negative value, GetNumPoints will be used
	 */
	void AddPointCount(FLidarPointCloudOctree* Tree, int32 PointCount = INT32_MIN);

	/** Sorts the points by visibility (visible first) to optimize data processing and rendering */
	void SortVisiblePoints();

	friend FLidarPointCloudOctree;
	friend FLidarPointCloudTraversalOctree;
	friend FLidarPointCloudTraversalOctreeNode;
};

/**
 * Used for efficient handling of point cloud data.
 */
class LIDARPOINTCLOUDRUNTIME_API FLidarPointCloudOctree
{
public:
	/** Stores shared per-LOD node data. */
	struct FSharedLODData
	{
		float Radius;
		float RadiusSq;
		FVector Extent;
		FVector GridSize;
		FVector NormalizationMultiplier;

		FSharedLODData() {}
		FSharedLODData(const FVector& InExtent);
	};

public:
	/** Maximum allowed depth for any node */
	static int32 MaxNodeDepth;

	/** Maximum number of unallocated points to keep inside the node before they need to be converted in to a full child node */
	static int32 MaxBucketSize;

	/** Virtual grid resolution to divide the node into */
	static int32 NodeGridResolution;

	/** Used for thread safety between rendering and asset operations. */
	mutable FCriticalSection DataLock;

private:
	FLidarPointCloudOctreeNode Root;
	
	/** Stores shared per-LOD node data. */
	TArray<FSharedLODData> SharedData;
	
	/** Stores number of points per each LOD. */
	TArray<FThreadSafeCounter64> PointCount;

	/** Stores number of nodes per each LOD. */
	TArray<FThreadSafeCounter> NodeCount;

	/** Extent of this Cloud. */
	FVector Extent;

	/** Used to cache the Allocated Size. */
	mutable int32 PreviousNodeCount;
	mutable int64 PreviousPointCount;
	mutable int64 PreviousAllocatedStructureSize;
	mutable int64 PreviousAllocatedSize;

	/** Used to notify any linked traversal octrees when they need to re-generate the data. */
	TArray<TWeakPtr<FLidarPointCloudTraversalOctree, ESPMode::ThreadSafe>> LinkedTraversalOctrees;

	/** Stores collision mesh data */
	FTriMeshCollisionData CollisionMesh;

	/** Pointer to the owner of this Octree */
	ULidarPointCloud* Owner;

	TQueue<FLidarPointCloudOctreeNode*> QueuedNodes;
	TArray<FLidarPointCloudOctreeNode*> NodesInUse;

	TAtomic<bool> bStreamingBusy;

	/** Set to true when the Octree is persistently force-loaded. */
	bool bIsFullyLoaded;

public:
	FLidarPointCloudOctree() : FLidarPointCloudOctree(nullptr) {}
	FLidarPointCloudOctree(ULidarPointCloud* Owner);
	~FLidarPointCloudOctree();
	FLidarPointCloudOctree(const FLidarPointCloudOctree&) = delete;
	FLidarPointCloudOctree(FLidarPointCloudOctree&&) = delete;
	FLidarPointCloudOctree& operator=(const FLidarPointCloudOctree&) = delete;
	FLidarPointCloudOctree& operator=(FLidarPointCloudOctree&&) = delete;

	/** Returns true if the Root node exists and has any data assigned. */
	bool HasData() const { return Root.GetNumPoints() > 0; }

	/** Returns the number of different LODs. */
	int32 GetNumLODs() const;

	/** Returns the Cloud bounds. */
	FBox GetBounds() const { return FBox(-Extent, Extent); }

	/** Returns the extent of the Cloud's bounds. */
	FORCEINLINE FVector GetExtent() const { return Extent; }

	/** Recalculates and updates points bounds. */
	void RefreshBounds();

	/** Returns the total number of points. */
	int64 GetNumPoints() const;

	/** Returns the total number of nodes. */
	int32 GetNumNodes() const;

	/** Returns a pointer to the Point Cloud asset, which owns this Octree. */
	ULidarPointCloud* GetOwner() const { return Owner; }

	/** Returns the amount of memory used by this Octree, including the BulkData */
	int64 GetAllocatedSize() const;

	/** Returns the amount of memory used by this Octree, excluding the BulkData */
	int64 GetAllocatedStructureSize() const;

	/** Returns the grid cell size at root level. */
	float GetRootCellSize() const { return SharedData[0].GridSize.GetMax(); }

	/** Returns an estimated spacing between points */
	float GetEstimatedPointSpacing() const;

	/** Returns true, if the Octree has collision built */
	bool HasCollisionData() const { return CollisionMesh.Vertices.Num() > 0; }

	/** Builds collision using the accuracy provided */
	void BuildCollision(const float& Accuracy, const bool& bVisibleOnly);

	/** Removes collision mesh data */
	void RemoveCollision();

	/** Returns pointer to the collision data */
	const FTriMeshCollisionData* GetCollisionData() const { return &CollisionMesh; }

	/** Populates the given array with points from the tree */
	void GetPoints(TArray<FLidarPointCloudPoint*>& Points, int64 StartIndex = 0, int64 Count = -1);

	/** Populates the array with the list of points within the given sphere. */
	void GetPointsInSphere(TArray<FLidarPointCloudPoint*>& SelectedPoints, const FSphere& Sphere, const bool& bVisibleOnly);

	/** Populates the array with the list of pointers to points within the given box. */
	void GetPointsInBox(TArray<FLidarPointCloudPoint*>& SelectedPoints, const FBox& Box, const bool& bVisibleOnly);

	/** Populates the array with the list of points within the given frustum. */
	void GetPointsInFrustum(TArray<FLidarPointCloudPoint*>& SelectedPoints, const FConvexVolume& Frustum, const bool& bVisibleOnly);

	/** Populates the given array with copies of points from the tree */
	void GetPointsAsCopies(TArray<FLidarPointCloudPoint>& Points, const FTransform* LocalToWorld, int64 StartIndex = 0, int64 Count = -1) const;

	/** Populates the array with the list of points within the given sphere. */
	void GetPointsInSphereAsCopies(TArray<FLidarPointCloudPoint>& SelectedPoints, const FSphere& Sphere, const bool& bVisibleOnly, const FTransform* LocalToWorld) const;

	/** Populates the array with the list of pointers to points within the given box. */
	void GetPointsInBoxAsCopies(TArray<FLidarPointCloudPoint>& SelectedPoints, const FBox& Box, const bool& bVisibleOnly, const FTransform* LocalToWorld) const;

	/** Performs a raycast test against the point cloud. Returns the pointer if hit or nullptr otherwise. */
	FLidarPointCloudPoint* RaycastSingle(const FLidarPointCloudRay& Ray, const float& Radius, const bool& bVisibleOnly);

	/**
	 * Performs a raycast test against the point cloud.
	 * Populates OutHits array with the results.
	 * Returns true it anything has been hit.
	 */
	bool RaycastMulti(const FLidarPointCloudRay& Ray, const float& Radius, const bool& bVisibleOnly, TArray<FLidarPointCloudPoint*>& OutHits);
	bool RaycastMulti(const FLidarPointCloudRay& Ray, const float& Radius, const bool& bVisibleOnly, const FTransform* LocalToWorld, TArray<FLidarPointCloudPoint>& OutHits);

	/** Returns true if there are any points within the given sphere. */
	bool HasPointsInSphere(const FSphere& Sphere, const bool& bVisibleOnly) const;

	/** Returns true if there are any points within the given box. */
	bool HasPointsInBox(const FBox& Box, const bool& bVisibleOnly) const;

	/** Sets visibility of points within the given sphere. */
	void SetVisibilityOfPointsInSphere(const bool& bNewVisibility, const FSphere& Sphere);

	/** Sets visibility of points within the given box. */
	void SetVisibilityOfPointsInBox(const bool& bNewVisibility, const FBox& Box);

	/** Sets visibility of the first point hit by the given ray. */
	void SetVisibilityOfFirstPointByRay(const bool& bNewVisibility, const FLidarPointCloudRay& Ray, const float& Radius);

	/** Sets visibility of points hit by the given ray. */
	void SetVisibilityOfPointsByRay(const bool& bNewVisibility, const FLidarPointCloudRay& Ray, const float& Radius);

	/** Marks all points hidden */
	void HideAll();

	/** Marks all points visible */
	void UnhideAll();

	/** Executes the provided action on each of the points. */
	void ExecuteActionOnAllPoints(TFunction<void(FLidarPointCloudPoint*)> Action, const bool& bVisibleOnly);

	/** Executes the provided action on each of the points within the given sphere. */
	void ExecuteActionOnPointsInSphere(TFunction<void(FLidarPointCloudPoint*)> Action, const FSphere& Sphere, const bool& bVisibleOnly);

	/** Executes the provided action on each of the points within the given box. */
	void ExecuteActionOnPointsInBox(TFunction<void(FLidarPointCloudPoint*)> Action, const FBox& Box, const bool& bVisibleOnly);

	/** Executes the provided action on the first point hit by the given ray. */
	void ExecuteActionOnFirstPointByRay(TFunction<void(FLidarPointCloudPoint*)> Action, const FLidarPointCloudRay& Ray, const float& Radius, bool bVisibleOnly);

	/** Executes the provided action on each of the points hit by the given ray. */
	void ExecuteActionOnPointsByRay(TFunction<void(FLidarPointCloudPoint*)> Action, const FLidarPointCloudRay& Ray, const float& Radius, bool bVisibleOnly);

	/** Applies the given color to all points */
	void ApplyColorToAllPoints(const FColor& NewColor, const bool& bVisibleOnly);

	/** Applies the given color to all points within the sphere */
	void ApplyColorToPointsInSphere(const FColor& NewColor, const FSphere& Sphere, const bool& bVisibleOnly);

	/** Applies the given color to all points within the box */
	void ApplyColorToPointsInBox(const FColor& NewColor, const FBox& Box, const bool& bVisibleOnly);

	/** Applies the given color to the first point hit by the given ray */
	void ApplyColorToFirstPointByRay(const FColor& NewColor, const FLidarPointCloudRay& Ray, const float& Radius, bool bVisibleOnly);

	/** Applies the given color to all points hit by the given ray */
	void ApplyColorToPointsByRay(const FColor& NewColor, const FLidarPointCloudRay& Ray, const float& Radius, bool bVisibleOnly);

	/**
	 * This should to be called if any manual modification to individual points' visibility has been made.
	 * If not marked dirty, the rendering may work suboptimally.
	 */
	void MarkPointVisibilityDirty();

	/** Initializes the Octree properties. */
	void Initialize(const FVector& InExtent);

	/** Inserts the given point into the Octree structure, internally thread-safe. */
	FORCEINLINE void InsertPoint(const FLidarPointCloudPoint* Point, ELidarPointCloudDuplicateHandling DuplicateHandling, bool bRefreshPointsBounds, const FVector& Translation) { InsertPoints(Point, 1, DuplicateHandling, bRefreshPointsBounds, Translation); }

	/** Inserts group of points into the Octree structure, internally thread-safe. */
	template <typename T>
	void InsertPoints(T Points, const int32& Count, ELidarPointCloudDuplicateHandling DuplicateHandling, bool bRefreshPointsBounds, const FVector& Translation)
	{
		Root.InsertPoints(this, Points, Count, DuplicateHandling, Translation);
		MarkTraversalOctreesForInvalidation();
		if (bRefreshPointsBounds)
		{
			RefreshBounds();
		}
	}

	/** Attempts to remove the given point.  */
	void RemovePoint(const FLidarPointCloudPoint* Point);
	void RemovePoint(FLidarPointCloudPoint Point);

	/** Removes points in bulk */
	void RemovePoints(TArray<FLidarPointCloudPoint*>& Points);

	/** Removes all points within the given sphere */
	void RemovePointsInSphere(const FSphere& Sphere, const bool& bVisibleOnly);

	/** Removes all points within the given box */
	void RemovePointsInBox(const FBox& Box, const bool& bVisibleOnly);

	/** Removes all points hit by the given ray */
	void RemovePointsByRay(const FLidarPointCloudRay& Ray, const float& Radius, const bool& bVisibleOnly);

	/** Removes all hidden points */
	void RemoveHiddenPoints();

	/** Removes all points and, optionally, all nodes except for the root node. Retains the bounds. */
	void Empty(bool bDestroyNodes);

	/** Adds the given traversal octree to the list of linked octrees. */
	void RegisterTraversalOctree(TWeakPtr<FLidarPointCloudTraversalOctree, ESPMode::ThreadSafe> TraversalOctree)
	{
		if (TraversalOctree.IsValid())
		{
			LinkedTraversalOctrees.Add(TraversalOctree);
		}
	}

	/** Removes the given traversal octree from the list */
	void UnregisterTraversalOctree(FLidarPointCloudTraversalOctree* TraversalOctree);

	/** If bImmediate is true, the node will be loaded immediately, otherwise, it will be queued for async loading. */
	void QueueNode(FLidarPointCloudOctreeNode* Node, float Lifetime);

	/** Streams all requested nodes */
	void StreamQueuedNodes();

	void UnloadOldNodes(const float& CurrentTime);

	/** Returns true, if the cloud is fully and persistently loaded. */
	bool IsFullyLoaded() const { return bIsFullyLoaded; }

	/** Persistently loads all nodes. */
	void LoadAllNodes();

	/**
	 * Releases all nodes.
	 * Optionally, releases persistent nodes too.
	 */
	void ReleaseAllNodes(bool bIncludePersistent);

private:
	void RefreshAllocatedSize();

	void RemovePoint_Internal(FLidarPointCloudOctreeNode* Node, int32 Index);

	/** Notifies all linked traversal octrees that they should invalidate and regenerate the data. */
	void MarkTraversalOctreesForInvalidation();

	void Serialize(FArchive& Ar);

	friend FArchive& operator<<(FArchive& Ar, FLidarPointCloudOctree& O)
	{
		O.Serialize(Ar);
		return Ar;
	}

	friend FLidarPointCloudOctreeNode;
	friend FLidarPointCloudTraversalOctree;
};

/**
 * Represents a single octant in the traversal tree.
 */
struct FLidarPointCloudTraversalOctreeNode
{
	/** Pointer to the target node. */
	FLidarPointCloudOctreeNode* DataNode;

	/** Stores the center of the target node in World space. */
	FVector Center;

	/** Depth of this node */
	uint8 Depth;

	/** Calculated for use with adaptive sprite scaling */
	uint8 VirtualDepth;

	FLidarPointCloudTraversalOctreeNode* Parent;

	/** Stores the children array */
	TArray<FLidarPointCloudTraversalOctreeNode> Children;

	/** Holds true if the node has been selected for rendering. */
	bool bSelected;

	FLidarPointCloudTraversalOctreeNode();

	/** Builds the traversal version of the given node. */
	void Build(FLidarPointCloudOctreeNode* Node, const FTransform& LocalToWorld, const FVector& LocationOffset);

	// #refactor: This is ugly - refactor to avoid the const or call directly in the buffer update with no mutable
	/** Calculates virtual depth of this node, to be used to estimate the best sprite size */
	void CalculateVirtualDepth(const TArray<float>& LevelWeights, const float& VDMultiplier, const float& PointSizeBias);
};

/**
 * Used as a traversal tree for node selection during rendering
 */
struct FLidarPointCloudTraversalOctree
{
	FLidarPointCloudTraversalOctreeNode Root;

	/** Stores per-LOD bounds in World space. */
	TArray<float> RadiiSq;
	TArray<FVector> Extents;

	/** Stores the number of LODs. */
	uint8 NumLODs;

	/** Normalized histogram of level weights, one for each LOD. Used for point scaling */
	TArray<float> LevelWeights;

	float VirtualDepthMultiplier;
	float ReversedVirtualDepthMultiplier;

	/** Pointer to the source Octree */
	FLidarPointCloudOctree* Octree;

	bool bValid;

	/** Build the Traversal tree from the Octree provided */
	FLidarPointCloudTraversalOctree(FLidarPointCloudOctree* Octree, const FTransform& LocalToWorld);

	~FLidarPointCloudTraversalOctree();
	FLidarPointCloudTraversalOctree(const FLidarPointCloudTraversalOctree&) = delete;
	FLidarPointCloudTraversalOctree(FLidarPointCloudTraversalOctree&&) = delete;
	FLidarPointCloudTraversalOctree& operator=(const FLidarPointCloudTraversalOctree&) = delete;
	FLidarPointCloudTraversalOctree& operator=(FLidarPointCloudTraversalOctree&&) = delete;

	/** Selects and appends the subset of visible nodes for rendering. */
	void GetVisibleNodes(TArray<FLidarPointCloudLODManager::FNodeSizeData>& NodeSizeData, const FLidarPointCloudViewData& ViewData, const int32& ProxyIndex, const FLidarPointCloudNodeSelectionParams& SelectionParams, const float& CurrentTime);

	FVector GetCenter() const { return Root.Center; }
	FVector GetExtent() const { return Extents[0]; }
};
