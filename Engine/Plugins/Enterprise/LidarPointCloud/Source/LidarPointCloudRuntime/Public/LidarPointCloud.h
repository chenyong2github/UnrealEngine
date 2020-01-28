// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "LidarPointCloudShared.h"
#include "LidarPointCloudOctree.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "HAL/ThreadSafeBool.h"
#include "Engine/EngineTypes.h"
#include "LidarPointCloudSettings.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/LatentActionManager.h"
#include "ConvexVolume.h"
#include "Interfaces/Interface_CollisionDataProvider.h"
#include "LidarPointCloud.generated.h"

class ALidarPointCloudActor;
class ULidarPointCloudComponent;
class UBodySetup;
class FLidarPointCloudCollisionRendering;

/**
 * Used for ULidarPointCloud::CreateFromXXXX calls
 */
struct FLidarPointCloudAsyncParameters
{
	/** Should the creation use async operation */
	bool bUseAsync;

	/**
	 * Called every time at least 1% progress is generated.
	 * The parameter is within 0.0 - 1.0 range.
	 */
	TFunction<void(float)> ProgressCallback;

	/**
	 * Called once, when the operation completes.
	 * The parameter specifies whether it has been executed successfully.
	 */
	TFunction<void(bool)> CompletionCallback;

	FLidarPointCloudAsyncParameters(bool bUseAsync, TFunction<void(float)> ProgressCallback = nullptr, TFunction<void(bool)> CompletionCallback = nullptr)
		: bUseAsync(bUseAsync)
		, ProgressCallback(MoveTemp(ProgressCallback))
		, CompletionCallback(MoveTemp(CompletionCallback))
	{
	}
};

/** Wrapper around a NotificationItem to make the notification handling more centralized */
class FLidarPointCloudNotification
{
	/** Stores the pointer to the actual notification item */
	TSharedPtr<SNotificationItem> NotificationItem;

	/** Owner of this notification */
	UObject* Owner;

	FString CurrentText;
	int8 CurrentProgress;

public:
	FLidarPointCloudNotification() : FLidarPointCloudNotification(nullptr) {}
	FLidarPointCloudNotification(UObject* Owner);

	bool IsValid() const { return NotificationItem.IsValid(); }
	void Create(const FString& Text, FThreadSafeBool* bCancelPtr = nullptr, const FString& Icon = "ClassIcon32.LidarPointCloud");
	void SetText(const FString& Text);
	void SetProgress(int8 Progress);
	void SetTextWithProgress(const FString& Text, int8 Progress);
	void Close(bool bSuccess);

private:
	void UpdateStatus();
};

/**
 * Represents the Point Cloud asset
 */
UCLASS(BlueprintType, AutoExpandCategories=("Performance", "Rendering|Sprite"), AutoCollapseCategories=("Import Settings"))
class LIDARPOINTCLOUDRUNTIME_API ULidarPointCloud : public UObject, public IInterface_CollisionDataProvider
{
	GENERATED_BODY()

private:
	/** Stores the path to the original source file. Empty if dynamically created. */
	UPROPERTY(EditAnywhere, Category = "Import Settings", meta = (AllowPrivateAccess = "true"))
	FFilePath SourcePath;

public:
	/**
	 * Determines the accuracy (in cm) of the collision for this point cloud.
	 * NOTE: Lower values will require more time to build.
	 * Rebuild collision for the changes to take effect.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	float CollisionAccuracy;

	/** Holds pointer to the Import Settings used for the import. */
	TSharedPtr<struct FLidarPointCloudImportSettings> ImportSettings;

	FLidarPointCloudOctree Octree;
	FLidarPointCloudCollisionRendering* CollisionRendering;

	/** Stores the original offset as a double. */
	UPROPERTY()
	FDoubleVector OriginalCoordinates;

	/** Required for file versioning */
	static const FGuid PointCloudFileGUID;
	static const int32 PointCloudFileVersion;

private:
	/** Used for caching the asset registry tag data. */
	struct FLidarPointCloudAssetRegistryCache
	{
		FString PointCount;
		FString ApproxSize;
	} PointCloudAssetRegistryCache;

	/** Contains the list of imported classification IDs */
	UPROPERTY()
	TArray<uint8> ClassificationsImported;

	/** Used for async building */
	FThreadSafeBool bAsyncCancelled;
	FCriticalSection ImportLock;

	/** Notification we hold on to, that indicates status and progress. */
	FLidarPointCloudNotification Notification;

	/** Description of collision */
	UPROPERTY(transient, duplicatetransient)
	UBodySetup* BodySetup;

	/** Used for collision building */
	FThreadSafeBool bCollisionBuildInProgress;

public:
	ULidarPointCloud();

	/** Used to notify the component it should refresh its state. */
	DECLARE_EVENT(ULidarPointCloud, FOnPointCloudChanged);
	FOnPointCloudChanged& OnPointCloudRebuilt() { return OnPointCloudRebuiltEvent; }
	FOnPointCloudChanged& OnPointCloudCollisionUpdated() { return OnPointCloudUpdateCollisionEvent; }

	// Begin UObject Interface.
	virtual void Serialize(FArchive& Ar) override;
	virtual void PostLoad() override;
	virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;
	virtual void BeginDestroy() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent & PropertyChangedEvent) override;
#endif
	// End UObject Interface.

	UFUNCTION(BlueprintPure, Category = "Lidar Point Cloud")
	int32 GetNumLODs() const { return Octree.GetNumLODs(); }

	UFUNCTION(BlueprintPure, Category = "Lidar Point Cloud")
	int32 GetNumPoints() const { return Octree.GetNumPoints(); }

	UFUNCTION(BlueprintPure, Category = "Lidar Point Cloud")
	int32 GetNumNodes() const { return Octree.GetNumNodes(); }

	/** Returns the amount of memory in MB used to store the point cloud. */
	UFUNCTION(BlueprintPure, Category = "Lidar Point Cloud")
	int32 GetDataSize() const;

	UFUNCTION(BlueprintPure, Category = "Lidar Point Cloud")
	FString GetSourcePath() const { return SourcePath.FilePath; }

	UFUNCTION(BlueprintPure, Category = "Lidar Point Cloud")
	FBox GetBounds() const { return Octree.GetBounds(); }

	UFUNCTION(BlueprintPure, Category = "Lidar Point Cloud")
	FBox GetPointsBounds() const { return Octree.GetPointsBounds(); }

	/** Recalculates and updates points bounds. */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void RefreshPointsBounds() { Octree.RefreshPointsBounds(); }

	/** Returns true, if the Octree has collision built */
	UFUNCTION(BlueprintPure, Category = "Lidar Point Cloud")
	bool HasCollisionData() const;

	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void RefreshRendering() { OnPointCloudRebuiltEvent.Broadcast(); }

	TArray<uint8> GetClassificationsImported() { return ClassificationsImported; }

	/** Populates the given array with points from the tree */
	TArray<FLidarPointCloudPoint*> GetPoints(int64 StartIndex = 0, int64 Count = -1);
	void GetPoints(TArray<FLidarPointCloudPoint*>& Points, int64 StartIndex = 0, int64 Count = -1) { Octree.GetPoints(Points, StartIndex, Count); }

	/** Populates the array with the list of points within the given sphere. */
	void GetPointsInSphere(TArray<FLidarPointCloudPoint*>& SelectedPoints, const FVector& Center, const float& Radius, const bool& bVisibleOnly) { GetPointsInSphere(SelectedPoints, FSphere(Center, Radius), bVisibleOnly); }
	void GetPointsInSphere(TArray<FLidarPointCloudPoint*>& SelectedPoints, const FSphere& Sphere, const bool& bVisibleOnly) { Octree.GetPointsInSphere(SelectedPoints, Sphere, bVisibleOnly); }

	/** Populates the array with the list of points within the given box. */
	void GetPointsInBox(TArray<FLidarPointCloudPoint*>& SelectedPoints, const FVector& Center, const FVector& Extent, const bool& bVisibleOnly) { GetPointsInBox(SelectedPoints, FBox(Center - Extent, Center + Extent), bVisibleOnly); }
	void GetPointsInBox(TArray<FLidarPointCloudPoint*>& SelectedPoints, const FBox& Box, const bool& bVisibleOnly) { Octree.GetPointsInBox(SelectedPoints, Box, bVisibleOnly); }

	/** Populates the array with the list of points within the given frustum. */
	void GetPointsInFrustum(TArray<FLidarPointCloudPoint*>& SelectedPoints, const FConvexVolume& Frustum, const bool& bVisibleOnly) { Octree.GetPointsInFrustum(SelectedPoints, Frustum, bVisibleOnly); }

	/** Returns an array with copies of points from the tree */
	UFUNCTION(BlueprintPure, Category = "Lidar Point Cloud")
	TArray<FLidarPointCloudPoint> GetPointsAsCopies(int32 StartIndex = 0, int32 Count = -1) const;
	void GetPointsAsCopies(TArray<FLidarPointCloudPoint>& Points, int64 StartIndex = 0, int64 Count = -1) const { Octree.GetPointsAsCopies(Points, StartIndex, Count); }
	
	/** Returns an array with copies of points within the given sphere */
	UFUNCTION(BlueprintPure, Category = "Lidar Point Cloud")
	TArray<FLidarPointCloudPoint> GetPointsInSphereAsCopies(FVector Center, float Radius, bool bVisibleOnly);
	void GetPointsInSphereAsCopies(TArray<FLidarPointCloudPoint>& SelectedPoints, const FSphere& Sphere, const bool& bVisibleOnly) { Octree.GetPointsInSphereAsCopies(SelectedPoints, Sphere, bVisibleOnly); }

	/** Returns an array with copies of points within the given box */
	UFUNCTION(BlueprintPure, Category = "Lidar Point Cloud")
	TArray<FLidarPointCloudPoint> GetPointsInBoxAsCopies(FVector Center, FVector Extent, bool bVisibleOnly);
	void GetPointsInBoxAsCopies(TArray<FLidarPointCloudPoint>& SelectedPoints, const FBox& Box, const bool& bVisibleOnly) { Octree.GetPointsInBoxAsCopies(SelectedPoints, Box, bVisibleOnly); }

	/** Performs a raycast test against the point cloud. Returns the pointer if hit or nullptr otherwise. */
	UFUNCTION(BlueprintPure, Category = "Lidar Point Cloud", meta = (Keywords = "raycast"))
	bool LineTraceSingle(FVector Origin, FVector Direction, float Radius, bool bVisibleOnly, FLidarPointCloudPoint& PointHit);
	FLidarPointCloudPoint* LineTraceSingle(const FLidarPointCloudRay& Ray, const float& Radius, const bool& bVisibleOnly) { return Octree.RaycastSingle(Ray, Radius, bVisibleOnly); }

	/**
	 * Performs a raycast test against the point cloud.
	 * Populates OutHits array with the results.
	 * Returns true it anything has been hit.
	 */
	UFUNCTION(BlueprintPure, Category = "Lidar Point Cloud", meta = (Keywords = "raycast"))
	bool LineTraceMulti(FVector Origin, FVector Direction, float Radius, bool bVisibleOnly, TArray<FLidarPointCloudPoint>& OutHits) { return Octree.RaycastMulti(FLidarPointCloudRay(Origin, Direction), Radius, bVisibleOnly, OutHits); }
	bool LineTraceMulti(const FLidarPointCloudRay& Ray, const float& Radius, bool bVisibleOnly, TArray<FLidarPointCloudPoint>& OutHits) { return Octree.RaycastMulti(Ray, Radius, bVisibleOnly, OutHits); }
	bool LineTraceMulti(const FLidarPointCloudRay& Ray, const float& Radius, bool bVisibleOnly, TArray<FLidarPointCloudPoint*>& OutHits) { return Octree.RaycastMulti(Ray, Radius, bVisibleOnly, OutHits); }

	/** Sets visibility of points within the given sphere. */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void SetVisibilityOfPointsInSphere(bool bNewVisibility, FVector Center, float Radius) { SetVisibilityOfPointsInSphere(bNewVisibility, FSphere(Center, Radius)); }
	void SetVisibilityOfPointsInSphere(const bool& bNewVisibility, const FSphere& Sphere) { Octree.SetVisibilityOfPointsInSphere(bNewVisibility, Sphere); }

	/**
	 * Sets visibility of points within the given sphere.
	 * Async version - does not wait for completion before returning from the call.
	 */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void SetVisibilityOfPointsInSphereAsync(bool bNewVisibility, FVector Center, float Radius) { SetVisibilityOfPointsInSphereAsync(bNewVisibility, FSphere(Center, Radius)); }
	void SetVisibilityOfPointsInSphereAsync(const bool& bNewVisibility, const FSphere& Sphere, TFunction<void(void)> CompletionCallback = nullptr) { Octree.SetVisibilityOfPointsInSphereAsync(bNewVisibility, Sphere, MoveTemp(CompletionCallback)); }

	/** Sets visibility of points within the given box. */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void SetVisibilityOfPointsInBox(bool bNewVisibility, FVector Center, FVector Extent) { SetVisibilityOfPointsInBox(bNewVisibility, FBox(Center - Extent, Center + Extent)); }
	void SetVisibilityOfPointsInBox(const bool& bNewVisibility, const FBox& Box) { Octree.SetVisibilityOfPointsInBox(bNewVisibility, Box); }

	/**
	 * Sets visibility of points within the given box.
	 * Async version - does not wait for completion before returning from the call.
	 */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void SetVisibilityOfPointsInBoxAsync(bool bNewVisibility, FVector Center, FVector Extent) { SetVisibilityOfPointsInBoxAsync(bNewVisibility, FBox(Center - Extent, Center + Extent)); }
	void SetVisibilityOfPointsInBoxAsync(const bool& bNewVisibility, const FBox& Box, TFunction<void(void)> CompletionCallback = nullptr) { Octree.SetVisibilityOfPointsInBoxAsync(bNewVisibility, Box, MoveTemp(CompletionCallback)); }

	/** Sets visibility of points hit by the given ray. */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void SetVisibilityOfPointsByRay(bool bNewVisibility, FVector Origin, FVector Direction, float Radius) { SetVisibilityOfPointsByRay(bNewVisibility, FLidarPointCloudRay(Origin, Direction), Radius); }
	void SetVisibilityOfPointsByRay(const bool& bNewVisibility, const FLidarPointCloudRay& Ray, const float& Radius) { Octree.SetVisibilityOfPointsByRay(bNewVisibility, Ray, Radius); }

	/**
	 * Sets visibility of points hit by the given ray.
	 * Async version - does not wait for completion before returning from the call.
	 */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void SetVisibilityOfPointsByRayAsync(bool bNewVisibility, FVector Origin, FVector Direction, float Radius) { SetVisibilityOfPointsByRayAsync(bNewVisibility, FLidarPointCloudRay(Origin, Direction), Radius); }
	void SetVisibilityOfPointsByRayAsync(const bool& bNewVisibility, const FLidarPointCloudRay& Ray, const float& Radius, TFunction<void(void)> CompletionCallback = nullptr) { Octree.SetVisibilityOfPointsByRayAsync(bNewVisibility, Ray, Radius, MoveTemp(CompletionCallback)); }

	/** Marks all points hidden */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void HideAll() { Octree.HideAll(); }

	/** Marks all points visible */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void UnhideAll() { Octree.UnhideAll(); }

	/**
	 * Marks all points visible
	 * Async version - does not wait for completion before returning from the call.
	 */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void ResetVisibilityAsync() { Octree.ResetVisibilityAsync(); }
	void ResetVisibilityAsync(TFunction<void(void)> CompletionCallback) { Octree.ResetVisibilityAsync(MoveTemp(CompletionCallback)); }

	/** Executes the provided action on each of the points. */
	void ExecuteActionOnAllPoints(TFunction<void(FLidarPointCloudPoint*)> Action, const bool& bVisibleOnly) { Octree.ExecuteActionOnAllPoints(MoveTemp(Action), bVisibleOnly); }

	/**
	 * Executes the provided action on each of the points.
	 * Async version - does not wait for completion before returning from the call.
	 */
	void ExecuteActionOnAllPointsAsync(TFunction<void(FLidarPointCloudPoint*)> Action, const bool& bVisibleOnly, TFunction<void(void)> CompletionCallback = nullptr) { Octree.ExecuteActionOnAllPointsAsync(MoveTemp(Action), bVisibleOnly, MoveTemp(CompletionCallback)); }

	/** Executes the provided action on each of the points within the given sphere. */
	void ExecuteActionOnPointsInSphere(TFunction<void(FLidarPointCloudPoint*)> Action, const FVector& Center, const float& Radius, const bool& bVisibleOnly) { ExecuteActionOnPointsInSphere(MoveTemp(Action), FSphere(Center, Radius), bVisibleOnly); }
	void ExecuteActionOnPointsInSphere(TFunction<void(FLidarPointCloudPoint*)> Action, const FSphere& Sphere, const bool& bVisibleOnly) { Octree.ExecuteActionOnPointsInSphere(MoveTemp(Action), Sphere, bVisibleOnly); }

	/**
	 * Executes the provided action on each of the points within the given sphere.
	 * Async version - does not wait for completion before returning from the call.
	 */
	void ExecuteActionOnPointsInSphereAsync(TFunction<void(FLidarPointCloudPoint*)> Action, const FVector& Center, const float& Radius, const bool& bVisibleOnly) { ExecuteActionOnPointsInSphereAsync(MoveTemp(Action), FSphere(Center, Radius), bVisibleOnly); }
	void ExecuteActionOnPointsInSphereAsync(TFunction<void(FLidarPointCloudPoint*)> Action, const FSphere& Sphere, const bool& bVisibleOnly, TFunction<void(void)> CompletionCallback = nullptr) { Octree.ExecuteActionOnPointsInSphereAsync(MoveTemp(Action), Sphere, bVisibleOnly, MoveTemp(CompletionCallback)); }

	/** Executes the provided action on each of the points within the given box. */
	void ExecuteActionOnPointsInBox(TFunction<void(FLidarPointCloudPoint*)> Action, const FVector& Center, const FVector& Extent, const bool& bVisibleOnly) { ExecuteActionOnPointsInBox(MoveTemp(Action), FBox(Center - Extent, Center + Extent), bVisibleOnly); }
	void ExecuteActionOnPointsInBox(TFunction<void(FLidarPointCloudPoint*)> Action, const FBox& Box, const bool& bVisibleOnly) { Octree.ExecuteActionOnPointsInBox(MoveTemp(Action), Box, bVisibleOnly); }

	/**
	 * Executes the provided action on each of the points within the given box.
	 * Async version - does not wait for completion before returning from the call.
	 */
	void ExecuteActionOnPointsInBoxAsync(TFunction<void(FLidarPointCloudPoint*)> Action, const FVector& Center, const FVector& Extent, const bool& bVisibleOnly) { ExecuteActionOnPointsInBoxAsync(MoveTemp(Action), FBox(Center - Extent, Center + Extent), bVisibleOnly); }
	void ExecuteActionOnPointsInBoxAsync(TFunction<void(FLidarPointCloudPoint*)> Action, const FBox& Box, const bool& bVisibleOnly, TFunction<void(void)> CompletionCallback = nullptr) { Octree.ExecuteActionOnPointsInBoxAsync(MoveTemp(Action), Box, bVisibleOnly, MoveTemp(CompletionCallback)); }

	/** Executes the provided action on each of the points hit by the given ray. */
	void ExecuteActionOnPointsByRay(TFunction<void(FLidarPointCloudPoint*)> Action, const FLidarPointCloudRay& Ray, const float& Radius, bool bVisibleOnly) { Octree.ExecuteActionOnPointsByRay(MoveTemp(Action), Ray, Radius, bVisibleOnly); }

	/**
	 * Executes the provided action on each of the points hit by the given ray.
	 * Async version - does not wait for completion before returning from the call.
	 */
	void ExecuteActionOnPointsByRayAsync(TFunction<void(FLidarPointCloudPoint*)> Action, const FLidarPointCloudRay& Ray, const float& Radius, bool bVisibleOnly, TFunction<void(void)> CompletionCallback = nullptr) { Octree.ExecuteActionOnPointsByRayAsync(MoveTemp(Action), Ray, Radius, bVisibleOnly, MoveTemp(CompletionCallback)); }

	/** Applies the given color to all points */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void ApplyColorToAllPoints(const FColor& NewColor, const bool& bVisibleOnly) { Octree.ApplyColorToAllPoints(NewColor, bVisibleOnly); }

	/** Applies the given color to all points within the sphere */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void ApplyColorToPointsInSphere(FColor NewColor, FVector Center, float Radius, bool bVisibleOnly) { ApplyColorToPointsInSphere(NewColor, FSphere(Center, Radius), bVisibleOnly); }
	void ApplyColorToPointsInSphere(const FColor& NewColor, const FSphere& Sphere, const bool& bVisibleOnly) { Octree.ApplyColorToPointsInSphere(NewColor, Sphere, bVisibleOnly); }

	/** Applies the given color to all points within the box */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void ApplyColorToPointsInBox(FColor NewColor, FVector Center, FVector Extent, bool bVisibleOnly) { ApplyColorToPointsInBox(NewColor, FBox(Center - Extent, Center + Extent), bVisibleOnly); }
	void ApplyColorToPointsInBox(const FColor& NewColor, const FBox& Box, const bool& bVisibleOnly) { Octree.ApplyColorToPointsInBox(NewColor, Box, bVisibleOnly); }

	/** Applies the given color to all points hit by the given ray */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void ApplyColorToPointsByRay(FColor NewColor, FVector Origin, FVector Direction, float Radius, bool bVisibleOnly) { ApplyColorToPointsByRay(NewColor, FLidarPointCloudRay(Origin, Direction), Radius, bVisibleOnly); }
	void ApplyColorToPointsByRay(const FColor& NewColor, const FLidarPointCloudRay& Ray, const float& Radius, bool bVisibleOnly) { Octree.ApplyColorToPointsByRay(NewColor, Ray, Radius, bVisibleOnly); }

	/**
	 * This should to be called if any manual modification to individual points' visibility has been made.
	 * If not marked dirty, the rendering may work sub-optimally.
	 */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void MarkPointVisibilityDirty() { Octree.MarkPointVisibilityDirty(); }

	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void SetSourcePath(const FString& NewSourcePath);

	/**
	 * Re-initializes the asset with new bounds.
	 * Warning: Will erase all currently held data!
	 */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void Initialize(const FBox& NewBounds) { Octree.Initialize(NewBounds); }

	/** Builds collision mesh for the cloud, using current collision settings */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void BuildCollision();

	/** Removes collision mesh from the cloud. */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void RemoveCollision();

	/** Centers all contained points */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void CenterPoints();

	/** Restores original coordinates to all contained points */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void RestoreOriginalCoordinates();

	/**
	 * Applies given offset to all contained points.
	 * If bRefreshPointsBounds is set to false, make sure you call RefreshPointsBounds() manually or cloud centering may not work correctly.
	 */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void ShiftPointsBy(FVector Offset, bool bRefreshPointsBounds) { ShiftPointsBy(FDoubleVector(Offset), bRefreshPointsBounds); }
	void ShiftPointsBy(FDoubleVector Offset, bool bRefreshPointsBounds);

	/** Returns true, if the cloud has been centered. */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	bool IsCentered() const;

	/** Re-imports the cloud from it's original source file, overwriting any current point information. */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void Reimport(bool bUseAsync) { Reimport(FLidarPointCloudAsyncParameters(bUseAsync)); }
	void Reimport(const FLidarPointCloudAsyncParameters& AsyncParameters);

	/**
	 * Exports this Point Cloud to the given filename.
	 * Consult supported export formats.
	 * Returns true if successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	bool Export(const FString& Filename);

	/**
	 * Inserts the given point into the Octree structure.
	 * If bRefreshPointsBounds is set to false, make sure you call RefreshPointsBounds() manually or cloud centering may not work correctly.
	 */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void InsertPoint(const FLidarPointCloudPoint& Point, ELidarPointCloudDuplicateHandling DuplicateHandling, bool bRefreshPointsBounds);

	/**
	 * Inserts group of points into the Octree structure, multi-threaded.
	 * If bRefreshPointsBounds is set to false, make sure you call RefreshPointsBounds() manually or cloud centering may not work correctly.
	 */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void InsertPoints(const TArray<FLidarPointCloudPoint>& Points, ELidarPointCloudDuplicateHandling DuplicateHandling, bool bRefreshPointsBounds) { InsertPoints(Points.GetData(), Points.Num(), DuplicateHandling, bRefreshPointsBounds); }

	/**
	 * Inserts group of points into the Octree structure, multi-threaded.
	 * If bRefreshPointsBounds is set to false, make sure you call RefreshPointsBounds() manually or cloud centering may not work correctly.
	 * Can be optionally passed a cancellation pointer - if it ever becomes non-null with value of true, process will be canceled.
	 * May also provide progress callback, called approximately every 1% of progress.
	 * Returns false if canceled.
	 */
	template<typename T>
	bool InsertPoints(T InPoints, const int64& Count, ELidarPointCloudDuplicateHandling DuplicateHandling, bool bRefreshPointsBounds, FThreadSafeBool* bCanceled = nullptr, TFunction<void(float)> ProgressCallback = TFunction<void(float)>())
	{
		FScopeLock Lock(&Octree.DataLock);
		return InsertPoints_NoLock(InPoints, Count, DuplicateHandling, bRefreshPointsBounds, bCanceled, MoveTemp(ProgressCallback));
	}
	template<typename T>
	bool InsertPoints_NoLock(T InPoints, const int64& Count, ELidarPointCloudDuplicateHandling DuplicateHandling, bool bRefreshPointsBounds, FThreadSafeBool* bCanceled = nullptr, TFunction<void(float)> ProgressCallback = TFunction<void(float)>());

	/**
	 * Attempts to remove the given point.
	 * If bRefreshPointsBounds is set to false, make sure you call RefreshPointsBounds() manually or cloud centering may not work correctly.
	 */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void RemovePoint(FLidarPointCloudPoint Point, bool bRefreshPointsBounds)
	{
		FScopeLock Lock(&Octree.DataLock);
		Octree.RemovePoint(Point, bRefreshPointsBounds);
	}
	void RemovePoint_NoLock(FLidarPointCloudPoint Point, bool bRefreshPointsBounds)
	{
		Octree.RemovePoint(Point, bRefreshPointsBounds);
	}
	void RemovePoint(const FLidarPointCloudPoint* Point, bool bRefreshPointsBounds)
	{
		FScopeLock Lock(&Octree.DataLock);
		RemovePoint_NoLock(Point, bRefreshPointsBounds);
	}
	void RemovePoint_NoLock(const FLidarPointCloudPoint* Point, bool bRefreshPointsBounds)
	{
		Octree.RemovePoint(Point, bRefreshPointsBounds);
	}

	/**
	 * Removes points in bulk
	 * If bRefreshPointsBounds is set to false, make sure you call RefreshPointsBounds() manually or cloud centering may not work correctly.
	 */
	void RemovePoints(TArray<FLidarPointCloudPoint*>& Points, bool bRefreshPointsBounds)
	{
		FScopeLock Lock(&Octree.DataLock);
		RemovePoints_NoLock(Points, bRefreshPointsBounds);
	}
	void RemovePoints_NoLock(TArray<FLidarPointCloudPoint*>& Points, bool bRefreshPointsBounds)
	{
		Octree.RemovePoints(Points, bRefreshPointsBounds);
	}

	/**
	 * Removes all points within the given sphere
	 * If bRefreshPointsBounds is set to false, make sure you call RefreshPointsBounds() manually or cloud centering may not work correctly.
	 */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void RemovePointsInSphere(FVector Center, float Radius, bool bVisibleOnly, bool bRefreshPointsBounds) { RemovePointsInSphere(FSphere(Center, Radius), bVisibleOnly, bRefreshPointsBounds); }
	void RemovePointsInSphere(const FSphere& Sphere, const bool& bVisibleOnly, bool bRefreshPointsBounds) { Octree.RemovePointsInSphere(Sphere, bVisibleOnly, bRefreshPointsBounds); }

	/**
	 * Removes all points within the given box
	 * If bRefreshPointsBounds is set to false, make sure you call RefreshPointsBounds() manually or cloud centering may not work correctly.
	 */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void RemovePointsInBox(FVector Center, FVector Extent, bool bVisibleOnly, bool bRefreshPointsBounds) { RemovePointsInBox(FBox(Center - Extent, Center + Extent), bVisibleOnly, bRefreshPointsBounds); }
	void RemovePointsInBox(const FBox& Box, const bool& bVisibleOnly, bool bRefreshPointsBounds) { Octree.RemovePointsInBox(Box, bVisibleOnly, bRefreshPointsBounds); }

	/**
	 * Removes all points hit by the given ray
	 * If bRefreshPointsBounds is set to false, make sure you call RefreshPointsBounds() manually or cloud centering may not work correctly.
	 */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void RemovePointsByRay(FVector Origin, FVector Direction, float Radius, bool bVisibleOnly, bool bRefreshPointsBounds) { RemovePointsByRay(FLidarPointCloudRay(Origin, Direction), Radius, bVisibleOnly, bRefreshPointsBounds); }
	void RemovePointsByRay(const FLidarPointCloudRay& Ray, const float& Radius, const bool& bVisibleOnly, bool bRefreshPointsBounds) { Octree.RemovePointsByRay(Ray, Radius, bVisibleOnly, bRefreshPointsBounds); }

	/** Removes all hidden points */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void RemoveHiddenPoints(bool bRefreshPointsBounds) { Octree.RemoveHiddenPoints(bRefreshPointsBounds); }

	/** Reinitializes the cloud with the new set of data. */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	bool SetData(const TArray<FLidarPointCloudPoint>& Points) { return SetData(Points.GetData(), Points.Num()); }
	bool SetData(TArray<FLidarPointCloudPoint*>& Points) { return SetData(Points.GetData(), Points.Num()); }
	template<typename T>
	bool SetData(T Points, const int64& Count, TFunction<void(float)> ProgressCallback = TFunction<void(float)>());

	/** Merges this point cloud with the ones provided */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void Merge(TArray<ULidarPointCloud*> PointCloudsToMerge) { Merge(PointCloudsToMerge, nullptr); }
	void Merge(TArray<ULidarPointCloud*> PointCloudsToMerge, TFunction<void(float)> ProgressCallback);

	/** Merges this point cloud with the one provided */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void MergeSingle(ULidarPointCloud* PointCloudToMerge) { Merge(TArray<ULidarPointCloud*>({ PointCloudToMerge })); }

	//~ Begin Interface_CollisionDataProvider Interface
	virtual bool GetPhysicsTriMeshData(FTriMeshCollisionData* CollisionData, bool InUseAllTriData) override;
	virtual bool ContainsPhysicsTriMeshData(bool InUseAllTriData) const override { return HasCollisionData(); }
	virtual bool WantsNegXTriMesh() override { return false; }
	//~ End Interface_CollisionDataProvider Interface

	UBodySetup* GetBodySetup();

public:
	/** Aligns provided clouds based on the relative offset between their Original Coordinates. Retains overall centering of the group. */
	static void AlignClouds(TArray<ULidarPointCloud*> PointCloudsToAlign);

	/**
	 * Returns new Point Cloud object imported using the settings provided.
	 * Use nullptr as ImportSettings parameter to use default set of settings instead.
	 */
	static ULidarPointCloud* CreateFromFile(const FString& Filename, TSharedPtr<struct FLidarPointCloudImportSettings> ImportSettings = nullptr, UObject* InParent = (UObject*)GetTransientPackage(), FName InName = NAME_None, EObjectFlags Flags = RF_NoFlags)
	{
		return CreateFromFile(Filename, FLidarPointCloudAsyncParameters(GetDefault<ULidarPointCloudSettings>()->bUseAsyncImport), ImportSettings, InParent, InName, Flags);
	}
	static ULidarPointCloud* CreateFromFile(const FString& Filename, const FLidarPointCloudAsyncParameters& AsyncParameters, TSharedPtr<struct FLidarPointCloudImportSettings> ImportSettings = nullptr, UObject* InParent = (UObject*)GetTransientPackage(), FName InName = NAME_None, EObjectFlags Flags = RF_NoFlags);

	/*
	 * Returns new Point Cloud object created from the data provided.
	 * Warning: If using Async, make sure the data does not get invalidated during processing!
	 */
	template<typename T>
	static ULidarPointCloud* CreateFromData(T Points, const int64& Count, const FLidarPointCloudAsyncParameters& AsyncParameters);
	static ULidarPointCloud* CreateFromData(const TArray<FLidarPointCloudPoint>& Points, const bool& bUseAsync) { return CreateFromData(Points.GetData(), Points.Num(), FLidarPointCloudAsyncParameters(bUseAsync)); }
	static ULidarPointCloud* CreateFromData(const TArray<FLidarPointCloudPoint>& Points, const FLidarPointCloudAsyncParameters& AsyncParameters) { return CreateFromData(Points.GetData(), Points.Num(), AsyncParameters); }
	static ULidarPointCloud* CreateFromData(TArray<FLidarPointCloudPoint*>& Points, const bool& bUseAsync) { return CreateFromData(Points.GetData(), Points.Num(), FLidarPointCloudAsyncParameters(bUseAsync)); }
	static ULidarPointCloud* CreateFromData(TArray<FLidarPointCloudPoint*>& Points, const FLidarPointCloudAsyncParameters& AsyncParameters) { return CreateFromData(Points.GetData(), Points.Num(), AsyncParameters); }

	/** Returns bounds fitting the given list of points */
	static FBox CalculateBoundsFromPoints(const FLidarPointCloudPoint* Points, const int64& Count);
	static FBox CalculateBoundsFromPoints(const TArray<FLidarPointCloudPoint>& Points) { return CalculateBoundsFromPoints(Points.GetData(), Points.Num()); }
	static FBox CalculateBoundsFromPoints(FLidarPointCloudPoint** Points, const int64& Count);

private:
	/** Once async physics cook is done, create needed state */
	void FinishPhysicsAsyncCook(UBodySetup* NewBodySetup) { FinishPhysicsAsyncCook(true, NewBodySetup); }
	void FinishPhysicsAsyncCook(bool bSuccess, UBodySetup* NewBodySetup);

	void InitializeCollisionRendering();
	void ReleaseCollisionRendering();

	FOnPointCloudChanged OnPointCloudRebuiltEvent;
	FOnPointCloudChanged OnPointCloudUpdateCollisionEvent;
};

USTRUCT(BlueprintType)
struct FLidarPointCloudTraceHit
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	ALidarPointCloudActor* Actor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	ULidarPointCloudComponent* Component;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	TArray<FLidarPointCloudPoint> Points;

	FLidarPointCloudTraceHit() : FLidarPointCloudTraceHit(nullptr, nullptr) {}
	FLidarPointCloudTraceHit(ALidarPointCloudActor* Actor, ULidarPointCloudComponent* Component)
		: Actor(Actor)
		, Component(Component)
	{
	}
};

/**
 * Blueprint library for the Point Cloud assets
 */
UCLASS(BlueprintType)
class LIDARPOINTCLOUDRUNTIME_API ULidarPointCloudBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Returns new, empty Point Cloud object. */
	UFUNCTION(BlueprintPure, Category = "Lidar Point Cloud", meta = (DisplayName = "Create Empty Lidar Point Cloud"))
	static ULidarPointCloud* CreatePointCloudEmpty() { return NewObject<ULidarPointCloud>(); }

	/**
	 * Returns new Point Cloud object imported using default settings.
	 * If using Async, the process runs in the background without blocking the game thread.
	 */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud", meta = (Latent, WorldContext = "WorldContextObject", LatentInfo = "LatentInfo", ExpandEnumAsExecs = "AsyncMode", DisplayName = "Create Lidar Point Cloud From File"))
	static void CreatePointCloudFromFile(UObject* WorldContextObject, const FString& Filename, bool bUseAsync, FLatentActionInfo LatentInfo, ELidarPointCloudAsyncMode& AsyncMode, float& Progress, ULidarPointCloud*& PointCloud);
	static void CreatePointCloudFromFile(UObject* WorldContextObject, const FString& Filename, bool bUseAsync, FLatentActionInfo LatentInfo, TSharedPtr<struct FLidarPointCloudImportSettings> ImportSettings, ELidarPointCloudAsyncMode& AsyncMode, float& Progress, ULidarPointCloud*& PointCloud);

	/*
	 * Returns new Point Cloud object created from the data provided.
	 * Warning: If using Async, make sure the data does not get invalidated during processing!
	 */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud", meta = (Latent, WorldContext = "WorldContextObject", LatentInfo = "LatentInfo", ExpandEnumAsExecs = "AsyncMode", DisplayName = "Create Lidar Point Cloud From Data"))
	static void CreatePointCloudFromData(UObject* WorldContextObject, const TArray<FLidarPointCloudPoint>& Points, bool bUseAsync, FLatentActionInfo LatentInfo, ELidarPointCloudAsyncMode& AsyncMode, float& Progress, ULidarPointCloud*& PointCloud);

	/**
	 * Exports the Point Cloud to the given filename.
	 * Consult supported export formats.
	 * Returns true if successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	static bool ExportPointCloudToFile(ULidarPointCloud* PointCloud, const FString& Filename) { return PointCloud && PointCloud->Export(Filename); }

	/** Does a collision trace along the given line and returns the first blocking hit encountered. */
	UFUNCTION(BlueprintPure, Category = "Lidar Point Cloud", meta = (WorldContext = "WorldContextObject", DisplayName = "LineTraceForLidarPointCloud", Keywords = "raycast"))
	static bool LineTraceSingle(UObject* WorldContextObject, FVector Origin, FVector Direction, float Radius, bool bVisibleOnly, FLidarPointCloudTraceHit& Hit);

	/** Does a collision trace along the given line and returns all hits encountered up to and including the first blocking hit. */
	UFUNCTION(BlueprintPure, Category = "Lidar Point Cloud", meta = (WorldContext = "WorldContextObject", DisplayName = "LineTraceMultiForLidarPointCloud", Keywords = "raycast"))
	static bool LineTraceMulti(UObject* WorldContextObject, FVector Origin, FVector Direction, float Radius, bool bVisibleOnly, TArray<FLidarPointCloudTraceHit>& Hits);
};