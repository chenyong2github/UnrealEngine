// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FractureTool.h"
#include "AutoClusterFracture.h"

#include "FractureToolAutoCluster.generated.h"


UCLASS(DisplayName = "Auto Cluster", Category = "FractureTools")
class UFractureAutoClusterSettings : public UFractureToolSettings
{
public:
	GENERATED_BODY()

	UFractureAutoClusterSettings(const FObjectInitializer& ObjInit)
		: Super(ObjInit)
		, AutoClusterMode(EFractureAutoClusterMode::BoundingBox)
		, SiteCount(10)
	{}

	UPROPERTY(EditAnywhere, Category = AutoCluster, meta = (DisplayName = "Mode"))
		EFractureAutoClusterMode AutoClusterMode;

	UPROPERTY(EditAnywhere, Category = AutoCluster, meta = (DisplayName = "Cluster Sites", UIMin = "1", UIMax = "5000", ClampMin = "1"))
		uint32 SiteCount = 1;

	UPROPERTY(EditAnywhere, Category = AutoCluster, meta = (DisplayName = "Enforce Cluster Connectivity"))
		bool bEnforceConnectivity = true;
};


UCLASS(DisplayName = "AutoCluster", Category = "FractureTools")
class UFractureToolAutoCluster : public UFractureModalTool
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
	virtual void RegisterUICommand(FFractureEditorCommands* BindingContext);

	virtual void Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit) override;

	UPROPERTY(EditAnywhere, Category = AutoCluster)
	UFractureAutoClusterSettings* AutoClusterSettings;
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