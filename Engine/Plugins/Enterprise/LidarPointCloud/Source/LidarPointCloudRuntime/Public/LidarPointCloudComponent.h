// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LidarPointCloud.h"
#include "Components/MeshComponent.h"
#include "LidarPointCloudComponent.generated.h"

class UBodySetup;

UENUM(BlueprintType)
enum class ELidarPointCloudColorationMode : uint8
{
	/** Uses color tint only */
	None,
	/** Uses imported RGB / Intensity data */
	Data,
	/** The cloud's color will be overridden with elevation-based color */
	Elevation,
	/** The cloud's color will be overridden with relative position-based color */
	Position,
	/** Uses Classification ID of the point along with the component's Classification Colors property to sample the color */
	Classification
};

UENUM(BlueprintType)
enum class ELidarPointCloudSpriteShape : uint8
{
	Square,
	Circle,
};

/** Component that allows you to render specified point cloud section */
UCLASS(ClassGroup=Rendering, ShowCategories = (Rendering), HideCategories = (Object, LOD, Physics, Activation, Materials, Cooking, Input, HLOD, Mobile), meta = (BlueprintSpawnableComponent))
class LIDARPOINTCLOUDRUNTIME_API ULidarPointCloudComponent : public UMeshComponent
{
	GENERATED_BODY()
		
private:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lidar Point Cloud", meta = (AllowPrivateAccess = "true"))
	ULidarPointCloud *PointCloud;

	/**
	 * Allows using custom-built material for the point cloud.
	 * Set to None to use the default one instead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Material", meta = (AllowPrivateAccess = "true"))
	UMaterialInterface* CustomMaterial;

public:
	/**
	 * Determines the maximum number of points to be visible on the screen.
	 * Higher values will produce better image quality, but will require faster hardware.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance", meta = (ClampMin = "0"))
	int32 PointBudget;

	/**
	 * Determines the preference towards selecting nodes closer to screen center.
	 * Larger values assign more priority towards screen center.
	 * 0 to disable.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Performance", meta = (ClampMin = "0.0"))
	float ScreenCenterImportance;

	/**
	 * Determines the minimum screen size for the node to be rendered.
	 * Lower values will produce farther view distance, at the cost of higher CPU usage.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Performance", meta = (ClampMin = "0"))
	float MinScreenSize;

	/**
	 * Use to tweak the size of the points.
	 * Set to 0 to switch to 1 pixel points.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (ClampMin = "0.0"))
	float PointSize;

	/** Specifies which source to use for point colors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	ELidarPointCloudColorationMode ColorSource;

	/** Affects the shape of points. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	ELidarPointCloudSpriteShape PointShape;

	/**
	 * Used with the Classification source.
	 * Maps the given classification ID to a color.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	TMap<int32, FLinearColor> ClassificationColors;

	/** Specifies the bottom color of the elevation-based gradient. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Appearance")
	FLinearColor ElevationColorBottom;

	/** Specifies the top color of the elevation-based gradient. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Appearance")
	FLinearColor ElevationColorTop;

	/**
	 * Larger values will help mask LOD transition areas, but too large values will lead to loss of detail.
	 * Values in range 0.035 - 0.05 seem to produce best overall results.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Appearance", meta = (ClampMin = "0.0", ClampMax = "0.15"))
	float PointSizeBias;

	UPROPERTY(interp, EditAnywhere, BlueprintReadWrite, Category = "Color Adjustment", meta = (UIMin = "0.0", UIMax = "2.0", Delta = "0.01", ColorGradingMode = "saturation", ShiftMouseMovePixelPerDelta = "10"))
	FVector4 Saturation;

	UPROPERTY(interp, EditAnywhere, BlueprintReadWrite, Category = "Color Adjustment", meta = (UIMin = "0.0", UIMax = "2.0", Delta = "0.01", ColorGradingMode = "contrast", ShiftMouseMovePixelPerDelta = "10"))
	FVector4 Contrast;

	UPROPERTY(interp, EditAnywhere, BlueprintReadWrite, Category = "Color Adjustment", meta = (UIMin = "0.0", UIMax = "2.0", Delta = "0.01", ColorGradingMode = "gamma", ShiftMouseMovePixelPerDelta = "10"))
	FVector4 Gamma;

	/** Affects the emissive strength of the color. Useful to create Bloom and light bleed effects. */
	UPROPERTY(interp, EditAnywhere, BlueprintReadWrite, Category = "Color Adjustment", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01", ColorGradingMode = "gain", ShiftMouseMovePixelPerDelta = "10"))
	FVector4 Gain;

	/** Applied additively, 0 being neutral. */
	UPROPERTY(interp, EditAnywhere, BlueprintReadWrite, Category = "Color Adjustment", meta = (UIMin = "-1.0", UIMax = "1.0", Delta = "0.001", ColorGradingMode = "offset", ShiftMouseMovePixelPerDelta = "20", SupportDynamicSliderMaxValue = "true", SupportDynamicSliderMinValue = "true"))
	FVector4 Offset;

	/** Specifies the tint to apply to the points. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Adjustment")
	FLinearColor ColorTint;

	/** Specifies the influence of Intensity data, if available, on the overall color. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Adjustment", meta = (ClampMin = "0", ClampMax = "1"))
	float IntensityInfluence;

	/**
	 * Minimum Depth from which the nodes should be rendered.
	 * 0 to disable.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Rendering", meta = (ClampMin = "0"))
	int32 MinDepth;

	/**
	 * Maximum Depth to which the nodes should be rendered.
	 * -1 to disable.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Rendering")
	int32 MaxDepth;

	/** Enabling this will cause the visible nodes to render their bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Rendering")
	bool bDrawNodeBounds;

private:
	UPROPERTY(Transient)
	UMaterialInterface* Material;
	UPROPERTY(Transient)
	UMaterialInterface *MasterMaterial;
	UPROPERTY(Transient)
	UMaterialInterface* MasterMaterialMasked;

	/** For asset editor use only */
	bool bOwnedByEditor;

public:
	ULidarPointCloudComponent();

	UFUNCTION(BlueprintPure, Category = "Components|LidarPointCloud")
	ULidarPointCloud* GetPointCloud() const { return PointCloud; }

	FORCEINLINE bool IsOwnedByEditor() const { return bOwnedByEditor; }

	/** Populates the array with the list of points within the given sphere. */
	void GetPointsInSphere(TArray<FLidarPointCloudPoint*>& SelectedPoints, const FVector& Center, const float& Radius, const bool& bVisibleOnly) { GetPointsInSphere(SelectedPoints, FSphere(Center, Radius), bVisibleOnly); }
	void GetPointsInSphere(TArray<FLidarPointCloudPoint*>& SelectedPoints, const FSphere& Sphere, const bool& bVisibleOnly)
	{
		if (PointCloud)
		{
			PointCloud->GetPointsInSphere(SelectedPoints, Sphere.TransformBy(GetComponentTransform().Inverse()), bVisibleOnly);
		}
	}

	/** Populates the array with the list of points within the given box. */
	void GetPointsInBox(TArray<FLidarPointCloudPoint*>& SelectedPoints, const FVector& Center, const FVector& Extent, const bool& bVisibleOnly) { GetPointsInBox(SelectedPoints, FBox(Center - Extent, Center + Extent), bVisibleOnly); }
	void GetPointsInBox(TArray<FLidarPointCloudPoint*>& SelectedPoints, const FBox& Box, const bool& bVisibleOnly)
	{
		if (PointCloud)
		{
			PointCloud->GetPointsInBox(SelectedPoints, Box.TransformBy(GetComponentTransform().Inverse()), bVisibleOnly);
		}
	}

	/** Populates the array with copies of points within the given sphere. */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	TArray<FLidarPointCloudPoint> GetPointsInSphereAsCopies(const FVector& Center, const float& Radius, const bool& bVisibleOnly)
	{
		TArray<FLidarPointCloudPoint> Points;
		GetPointsInSphereAsCopies(Points, FSphere(Center, Radius), bVisibleOnly);
		return Points;
	}
	void GetPointsInSphereAsCopies(TArray<FLidarPointCloudPoint>& SelectedPoints, const FSphere& Sphere, const bool& bVisibleOnly)
	{
		if (PointCloud)
		{
			PointCloud->GetPointsInSphereAsCopies(SelectedPoints, Sphere.TransformBy(GetComponentTransform().Inverse()), bVisibleOnly);
		}
	}

	/** Populates the array with copies of points within the given box. */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	TArray<FLidarPointCloudPoint> GetPointsInBoxAsCopies(FVector Center, FVector Extent, bool bVisibleOnly)
	{
		TArray<FLidarPointCloudPoint> Points;
		GetPointsInBoxAsCopies(Points, FBox(Center - Extent, Center + Extent), bVisibleOnly);
		return Points;
	}
	void GetPointsInBoxAsCopies(TArray<FLidarPointCloudPoint>& SelectedPoints, const FBox& Box, const bool& bVisibleOnly)
	{
		if (PointCloud)
		{
			PointCloud->GetPointsInBoxAsCopies(SelectedPoints, Box.TransformBy(GetComponentTransform().Inverse()), bVisibleOnly);
		}
	}

	/** Performs a raycast test against the point cloud. Returns the pointer if hit or nullptr otherwise. */
	UFUNCTION(BlueprintPure, Category = "Lidar Point Cloud", meta = (Keywords = "raycast"))
	bool LineTraceSingle(FVector Origin, FVector Direction, float Radius, bool bVisibleOnly, FLidarPointCloudPoint& PointHit)
	{
		FLidarPointCloudPoint* Point = LineTraceSingle(FLidarPointCloudRay(Origin, Direction), Radius, bVisibleOnly);
		if (Point)
		{
			PointHit = *Point;
			return true;
		}

		return false;
	}
	FLidarPointCloudPoint* LineTraceSingle(FLidarPointCloudRay Ray, float Radius, bool bVisibleOnly)
	{
		return PointCloud ? PointCloud->LineTraceSingle(Ray.TransformBy(GetComponentTransform().Inverse()), Radius, bVisibleOnly) : nullptr;
	}

	/**
	 * Performs a raycast test against the point cloud.
	 * Populates OutHits array with the results.
	 * Returns true it anything has been hit.
	 */
	UFUNCTION(BlueprintPure, Category = "Lidar Point Cloud", meta = (Keywords = "raycast"))
	bool LineTraceMulti(FVector Origin, FVector Direction, float Radius, bool bVisibleOnly, TArray<FLidarPointCloudPoint>& OutHits)
	{
		return PointCloud ? PointCloud->LineTraceMulti(FLidarPointCloudRay(Origin, Direction).TransformBy(GetComponentTransform().Inverse()), Radius, bVisibleOnly, OutHits) : false;
	}
	bool LineTraceMulti(FLidarPointCloudRay Ray, float Radius, bool bVisibleOnly, TArray<FLidarPointCloudPoint>& OutHits)
	{
		return PointCloud ? PointCloud->LineTraceMulti(Ray.TransformBy(GetComponentTransform().Inverse()), Radius, bVisibleOnly, OutHits) : false;
	}
	bool LineTraceMulti(FLidarPointCloudRay Ray, float Radius, bool bVisibleOnly, TArray<FLidarPointCloudPoint*>& OutHits)
	{
		return PointCloud ? PointCloud->LineTraceMulti(Ray.TransformBy(GetComponentTransform().Inverse()), Radius, bVisibleOnly, OutHits) : false;
	}

	/** Sets visibility of points within the given sphere. */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void SetVisibilityOfPointsInSphere(bool bNewVisibility, FVector Center, float Radius) { SetVisibilityOfPointsInSphere(bNewVisibility, FSphere(Center, Radius)); }
	void SetVisibilityOfPointsInSphere(const bool& bNewVisibility, const FSphere& Sphere)
	{
		if (PointCloud)
		{
			PointCloud->SetVisibilityOfPointsInSphere(bNewVisibility, Sphere.TransformBy(GetComponentTransform().Inverse()));
		}
	}

	/**
	 * Sets visibility of points within the given sphere.
	 * Async version - does not wait for completion before returning from the call.
	 */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void SetVisibilityOfPointsInSphereAsync(bool bNewVisibility, FVector Center, float Radius) { SetVisibilityOfPointsInSphereAsync(bNewVisibility, FSphere(Center, Radius)); }
	void SetVisibilityOfPointsInSphereAsync(const bool& bNewVisibility, const FSphere& Sphere)
	{
		if (PointCloud)
		{
			PointCloud->SetVisibilityOfPointsInSphereAsync(bNewVisibility, Sphere.TransformBy(GetComponentTransform().Inverse()));
		}
	}

	/** Sets visibility of points within the given box. */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void SetVisibilityOfPointsInBox(bool bNewVisibility, FVector Center, FVector Extent) { SetVisibilityOfPointsInBox(bNewVisibility, FBox(Center - Extent, Center + Extent)); }
	void SetVisibilityOfPointsInBox(const bool& bNewVisibility, const FBox& Box)
	{
		if (PointCloud)
		{
			PointCloud->SetVisibilityOfPointsInBox(bNewVisibility, Box.TransformBy(GetComponentTransform().Inverse()));
		}
	}

	/**
	 * Sets visibility of points within the given box.
	 * Async version - does not wait for completion before returning from the call.
	 */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void SetVisibilityOfPointsInBoxAsync(bool bNewVisibility, FVector Center, FVector Extent) { SetVisibilityOfPointsInBoxAsync(bNewVisibility, FBox(Center - Extent, Center + Extent)); }
	void SetVisibilityOfPointsInBoxAsync(const bool& bNewVisibility, const FBox& Box)
	{
		if (PointCloud)
		{
			PointCloud->SetVisibilityOfPointsInBoxAsync(bNewVisibility, Box.TransformBy(GetComponentTransform().Inverse()));
		}
	}

	/** Sets visibility of points hit by the given ray. */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void SetVisibilityOfPointsByRay(bool bNewVisibility, FVector Origin, FVector Direction, float Radius) { SetVisibilityOfPointsByRay(bNewVisibility, FLidarPointCloudRay(Origin, Direction), Radius); }
	void SetVisibilityOfPointsByRay(bool bNewVisibility, FLidarPointCloudRay Ray, float Radius)
	{
		if (PointCloud)
		{
			PointCloud->SetVisibilityOfPointsByRay(bNewVisibility, Ray.TransformBy(GetComponentTransform().Inverse()), Radius);
		}
	}

	/**
	 * Sets visibility of points hit by the given ray.
	 * Async version - does not wait for completion before returning from the call.
	 */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void SetVisibilityOfPointsByRayAsync(bool bNewVisibility, FVector Origin, FVector Direction, float Radius) { SetVisibilityOfPointsByRayAsync(bNewVisibility, FLidarPointCloudRay(Origin, Direction), Radius); }
	void SetVisibilityOfPointsByRayAsync(bool bNewVisibility, FLidarPointCloudRay Ray, float Radius, TFunction<void(void)> CompletionCallback = nullptr)
	{
		if (PointCloud)
		{
			PointCloud->SetVisibilityOfPointsByRayAsync(bNewVisibility, Ray.TransformBy(GetComponentTransform().Inverse()), Radius, MoveTemp(CompletionCallback));
		}
	}

	/** Executes the provided action on each of the points within the given sphere. */
	void ExecuteActionOnPointsInSphere(TFunction<void(FLidarPointCloudPoint*)> Action, const FVector& Center, const float& Radius, const bool& bVisibleOnly) { ExecuteActionOnPointsInSphere(MoveTemp(Action), FSphere(Center, Radius), bVisibleOnly); }
	void ExecuteActionOnPointsInSphere(TFunction<void(FLidarPointCloudPoint*)> Action, const FSphere& Sphere, const bool& bVisibleOnly)
	{
		if (PointCloud)
		{
			PointCloud->ExecuteActionOnPointsInSphere(MoveTemp(Action), Sphere.TransformBy(GetComponentTransform().Inverse()), bVisibleOnly);
		}
	}

	/**
	 * Executes the provided action on each of the points within the given sphere.
	 * Async version - does not wait for completion before returning from the call.
	 */
	void ExecuteActionOnPointsInSphereAsync(TFunction<void(FLidarPointCloudPoint*)> Action, const FVector& Center, const float& Radius, const bool& bVisibleOnly) { ExecuteActionOnPointsInSphereAsync(MoveTemp(Action), FSphere(Center, Radius), bVisibleOnly); }
	void ExecuteActionOnPointsInSphereAsync(TFunction<void(FLidarPointCloudPoint*)> Action, const FSphere& Sphere, const bool& bVisibleOnly)
	{
		if (PointCloud)
		{
			PointCloud->ExecuteActionOnPointsInSphereAsync(MoveTemp(Action), Sphere.TransformBy(GetComponentTransform().Inverse()), bVisibleOnly);
		}
	}

	/** Executes the provided action on each of the points within the given box. */
	void ExecuteActionOnPointsInBox(TFunction<void(FLidarPointCloudPoint*)> Action, const FVector& Center, const FVector& Extent, const bool& bVisibleOnly) { ExecuteActionOnPointsInBox(MoveTemp(Action), FBox(Center - Extent, Center + Extent), bVisibleOnly); }
	void ExecuteActionOnPointsInBox(TFunction<void(FLidarPointCloudPoint*)> Action, const FBox& Box, const bool& bVisibleOnly)
	{
		if (PointCloud)
		{
			PointCloud->ExecuteActionOnPointsInBox(MoveTemp(Action), Box.TransformBy(GetComponentTransform().Inverse()), bVisibleOnly);
		}
	}

	/**
	 * Executes the provided action on each of the points within the given box.
	 * Async version - does not wait for completion before returning from the call.
	 */
	void ExecuteActionOnPointsInBoxAsync(TFunction<void(FLidarPointCloudPoint*)> Action, const FVector& Center, const FVector& Extent, const bool& bVisibleOnly) { ExecuteActionOnPointsInBoxAsync(MoveTemp(Action), FBox(Center - Extent, Center + Extent), bVisibleOnly); }
	void ExecuteActionOnPointsInBoxAsync(TFunction<void(FLidarPointCloudPoint*)> Action, const FBox& Box, const bool& bVisibleOnly)
	{
		if (PointCloud)
		{
			PointCloud->ExecuteActionOnPointsInBoxAsync(MoveTemp(Action), Box.TransformBy(GetComponentTransform().Inverse()), bVisibleOnly);
		}
	}

	/** Executes the provided action on each of the points hit by the given ray. */
	void ExecuteActionOnPointsByRay(TFunction<void(FLidarPointCloudPoint*)> Action, FLidarPointCloudRay Ray, float Radius, bool bVisibleOnly)
	{
		if (PointCloud)
		{
			PointCloud->ExecuteActionOnPointsByRay(MoveTemp(Action), Ray.TransformBy(GetComponentTransform().Inverse()), Radius, bVisibleOnly);
		}
	}

	/**
	 * Executes the provided action on each of the points hit by the given ray.
	 * Async version - does not wait for completion before returning from the call.
	 */
	void ExecuteActionOnPointsByRayAsync(TFunction<void(FLidarPointCloudPoint*)> Action, FLidarPointCloudRay Ray, float Radius, bool bVisibleOnly, TFunction<void(void)> CompletionCallback = nullptr)
	{
		if (PointCloud)
		{
			PointCloud->ExecuteActionOnPointsByRayAsync(MoveTemp(Action), Ray.TransformBy(GetComponentTransform().Inverse()), Radius, bVisibleOnly, MoveTemp(CompletionCallback));
		}
	}

	/** Applies the given color to all points within the sphere */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void ApplyColorToPointsInSphere(FColor NewColor, FVector Center, float Radius, bool bVisibleOnly) { ApplyColorToPointsInSphere(NewColor, FSphere(Center, Radius), bVisibleOnly); }
	void ApplyColorToPointsInSphere(const FColor& NewColor, const FSphere& Sphere, const bool& bVisibleOnly)
	{
		if (PointCloud)
		{
			PointCloud->ApplyColorToPointsInSphere(NewColor, Sphere.TransformBy(GetComponentTransform().Inverse()), bVisibleOnly);
		}
	}

	/** Applies the given color to all points within the box */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void ApplyColorToPointsInBox(FColor NewColor, FVector Center, FVector Extent, bool bVisibleOnly) { ApplyColorToPointsInBox(NewColor, FBox(Center - Extent, Center + Extent), bVisibleOnly); }
	void ApplyColorToPointsInBox(const FColor& NewColor, const FBox& Box, const bool& bVisibleOnly)
	{
		if (PointCloud)
		{
			PointCloud->ApplyColorToPointsInBox(NewColor, Box.TransformBy(GetComponentTransform().Inverse()), bVisibleOnly);
		}
	}

	/** Applies the given color to all points hit by the given ray */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void ApplyColorToPointsByRay(FColor NewColor, FVector Origin, FVector Direction, float Radius, bool bVisibleOnly) { ApplyColorToPointsByRay(NewColor, FLidarPointCloudRay(Origin, Direction), Radius, bVisibleOnly); }
	void ApplyColorToPointsByRay(const FColor& NewColor, FLidarPointCloudRay Ray, float Radius, bool bVisibleOnly)
	{
		if (PointCloud)
		{
			PointCloud->ApplyColorToPointsByRay(NewColor, Ray.TransformBy(GetComponentTransform().Inverse()), Radius, bVisibleOnly);
		}
	}
	
	/**
	 * Removes all points within the given sphere
	 * If bRefreshPointsBounds is set to false, make sure you call RefreshPointsBounds() manually or cloud centering may not work correctly.
	 */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void RemovePointsInSphere(FVector Center, float Radius, bool bVisibleOnly, bool bRefreshPointsBounds) { RemovePointsInSphere(FSphere(Center, Radius), bVisibleOnly, bRefreshPointsBounds); }
	void RemovePointsInSphere(const FSphere& Sphere, const bool& bVisibleOnly, bool bRefreshPointsBounds)
	{
		if (PointCloud)
		{
			PointCloud->RemovePointsInSphere(Sphere.TransformBy(GetComponentTransform().Inverse()), bVisibleOnly, bRefreshPointsBounds);
		}
	}

	/**
	 * Removes all points within the given box
	 * If bRefreshPointsBounds is set to false, make sure you call RefreshPointsBounds() manually or cloud centering may not work correctly.
	 */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void RemovePointsInBox(FVector Center, FVector Extent, bool bVisibleOnly, bool bRefreshPointsBounds) { RemovePointsInBox(FBox(Center - Extent, Center + Extent), bVisibleOnly, bRefreshPointsBounds); }
	void RemovePointsInBox(const FBox& Box, const bool& bVisibleOnly, bool bRefreshPointsBounds)
	{
		if (PointCloud)
		{
			PointCloud->RemovePointsInBox(Box.TransformBy(GetComponentTransform().Inverse()), bVisibleOnly, bRefreshPointsBounds);
		}
	}

	/**
	 * Removes all points hit by the given ray
	 * If bRefreshPointsBounds is set to false, make sure you call RefreshPointsBounds() manually or cloud centering may not work correctly.
	 */
	UFUNCTION(BlueprintCallable, Category = "Lidar Point Cloud")
	void RemovePointsByRay(FVector Origin, FVector Direction, float Radius, bool bVisibleOnly, bool bRefreshPointsBounds) { RemovePointsByRay(FLidarPointCloudRay(Origin, Direction), Radius, bVisibleOnly, bRefreshPointsBounds); }
	void RemovePointsByRay(FLidarPointCloudRay Ray, float Radius, const bool& bVisibleOnly, bool bRefreshPointsBounds)
	{
		if (PointCloud)
		{
			PointCloud->RemovePointsByRay(Ray.TransformBy(GetComponentTransform().Inverse()), Radius, bVisibleOnly, bRefreshPointsBounds);
		}
	}

public:
	UFUNCTION(BlueprintCallable, Category = "Components|LidarPointCloud")
	void SetPointCloud(ULidarPointCloud *InPointCloud);

	/** Applies specified rendering parameters (Brightness, Saturation, etc) to the selected material */
	UFUNCTION(BlueprintCallable, Category = "Components|LidarPointCloud|Rendering")
	void ApplyRenderingParameters();

public:
	// Begin UObject Interface.
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
	virtual void PostLoad() override;
	// End UObject Interface.

	// End UMeshComponent Interface
	virtual int32 GetNumMaterials() const override { return 1; }
	virtual UMaterialInterface* GetMaterial(int32 ElementIndex) const override { return Material; }
	virtual void SetMaterial(int32 ElementIndex, UMaterialInterface* InMaterial) override;
	// End UMeshComponent Interface

#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyThatWillChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditImport() override
	{
		Super::PostEditImport();

		// Make sure to update the material after duplicating this component
		UpdateMaterial();
	}
#endif

	virtual UBodySetup* GetBodySetup() override;

private:
	// Begin UPrimitiveComponent Interface
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	// End UMeshComponent Interface

	// Begin USceneComponent Interface
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	// End USceneComponent Interface

	void UpdateMaterial();

	void AttachPointCloudListener();
	void RemovePointCloudListener();
	void OnPointCloudRebuilt();
	void OnPointCloudCollisionUpdated();

	void PostPointCloudSet();

	friend class FPointCloudSceneProxy;
#if WITH_EDITOR
	friend class FLidarPointCloudEditorViewportClient;
#endif
};
