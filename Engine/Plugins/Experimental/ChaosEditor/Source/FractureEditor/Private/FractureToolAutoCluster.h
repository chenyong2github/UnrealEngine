// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FractureTool.h"

#include "FractureToolAutoCluster.generated.h"

// Note: Only Voronoi-based auto-clustering is currently supported
UENUM()
enum class EFractureAutoClusterMode : uint8
{
	/** Overlapping bounding box*/
	BoundingBox UMETA(DisplayName = "Bounding Box"),

	/** GC connectivity */
	Proximity UMETA(DisplayName = "Proximity"),

	/** Distance */
	Distance UMETA(DisplayName = "Distance"),

	Voronoi UMETA(DisplayName = "Voronoi"),
};


UCLASS(DisplayName = "Auto Cluster", Category = "FractureTools")
class UFractureAutoClusterSettings : public UFractureToolSettings
{
public:
	GENERATED_BODY()

	UFractureAutoClusterSettings(const FObjectInitializer& ObjInit)
		: Super(ObjInit)
		, SiteCount(10)
	{}

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Simplified interface now only supports Voronoi clustering."))
	EFractureAutoClusterMode AutoClusterMode_DEPRECATED;

	/** Use a Voronoi diagram with this many Voronoi sites as a guide for deciding cluster boundaries */
	UPROPERTY(EditAnywhere, Category = AutoCluster, meta = (DisplayName = "Cluster Sites", UIMin = "1", UIMax = "5000", ClampMin = "1"))
	uint32 SiteCount=10;

	/** If true, bones will only be added to the same cluster if they are physically connected (either directly, or via other bones in the same cluster) */
	UPROPERTY(EditAnywhere, Category = AutoCluster, meta = (DisplayName = "Enforce Cluster Connectivity"))
	bool bEnforceConnectivity=true;
};


UCLASS(DisplayName="AutoCluster", Category="FractureTools")
class UFractureToolAutoCluster: public UFractureModalTool
{
public:
	GENERATED_BODY()

	UFractureToolAutoCluster(const FObjectInitializer& ObjInit);

	// UFractureTool Interface
	virtual FText GetDisplayText() const override;
	virtual FText GetTooltipText() const override;
	virtual FText GetApplyText() const override; 
	virtual FSlateIcon GetToolIcon() const override;
	virtual TArray<UObject*> GetSettingsObjects() const override;
	virtual void RegisterUICommand( FFractureEditorCommands* BindingContext );

	virtual void Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit) override;

	UPROPERTY(EditAnywhere, Category = AutoCluster)
	TObjectPtr<UFractureAutoClusterSettings> AutoClusterSettings;
};


class FVoronoiPartitioner
{
public:
	FVoronoiPartitioner(const FGeometryCollection* GeometryCollection, int32 ClusterIndex);

	/** Cluster bodies into k partitions using K-Means. Connectivity is ignored: only spatial proximity is considered. */
	void KMeansPartition(int32 InPartitionCount);

	/** Split any partition islands into their own partition. This will possbily increase number of partitions to exceed desired count. */
	void SplitDisconnectedPartitions(FGeometryCollection* GeometryCollection);

	int32 GetPartitionCount() const { return PartitionCount; }

	/** return the GeometryCollection TranformIndices within the partition. */
	TArray<int32> GetPartition(int32 PartitionIndex) const;

private:
	void GenerateConnectivity(const FGeometryCollection* GeometryCollection);
	void CollectConnections(const FGeometryCollection* GeometryCollection, int32 Index, int32 OperatingLevel, TSet<int32>& OutConnections) const;
	void GenerateCentroids(const FGeometryCollection* GeometryCollection);
	FVector GenerateCentroid(const FGeometryCollection* GeometryCollection, int32 TransformIndex) const;
	FBox GenerateBounds(const FGeometryCollection* GeometryCollection, int32 TransformIndex) const;
	void InitializePartitions();
	bool Refine();
	int32 FindClosestPartitionCenter(const FVector& Location) const;
	void MarkVisited(int32 Index, int32 PartitionIndex);

private:
	TArray<int32> TransformIndices;
	TArray<FVector> Centroids;
	TArray<int32> Partitions;
	int32 PartitionCount;
	TArray<int32> PartitionSize;
	TArray<FVector> PartitionCenters;
	TArray<TSet<int32>> Connectivity;
	TArray<bool> Visited;


	// Not generally necessary but this is a safety measure to prevent oscillating solves that never converge.
	const int32 MaxKMeansIterations = 500;
};