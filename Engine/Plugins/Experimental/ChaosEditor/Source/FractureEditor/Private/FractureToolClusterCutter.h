// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FractureTool.h"
#include "FractureToolCutter.h"

#include "FractureToolClusterCutter.generated.h"

class FFractureToolContext;

UCLASS(config = EditorPerProjectUserSettings)
class UFractureClusterCutterSettings : public UFractureToolSettings
{
public:
	GENERATED_BODY()

	UFractureClusterCutterSettings(const FObjectInitializer& ObjInit)
		: Super(ObjInit)
		, NumberClustersMin(8)
		, NumberClustersMax(8)
		, SitesPerClusterMin(2)
		, SitesPerClusterMax(30)
		, ClusterRadiusPercentageMin(0.1)
		, ClusterRadiusPercentageMax(0.2)
		, ClusterRadius(0.0f)
	{}

	/** Number of Clusters - Cluster Voronoi Method */
	UPROPERTY(EditAnywhere, Category = ClusterVoronoi, meta = (DisplayName = "Minimum Cluster Sites", UIMin = "1", UIMax = "200", ClampMin = "1"))
	int32 NumberClustersMin;

	/** Number of Clusters - Cluster Voronoi Method */
	UPROPERTY(EditAnywhere, Category = ClusterVoronoi, meta = (DisplayName = "Maximum Cluster Sites", UIMin = "1", UIMax = "200", ClampMin = "1"))
	int32 NumberClustersMax;

	/** Sites per # of Clusters - Cluster Voronoi Method */
	UPROPERTY(EditAnywhere, Category = ClusterVoronoi, meta = (DisplayName = "Minimum Sites Per Cluster", UIMin = "0", UIMax = "200", ClampMin = "0"))
	int32 SitesPerClusterMin;

	UPROPERTY(EditAnywhere, Category = ClusterVoronoi, meta = (DisplayName = "Maximum Sites Per Cluster", UIMin = "0", UIMax = "200", ClampMin = "0"))
	int32 SitesPerClusterMax;

	/** Cluster's Radius - Cluster Voronoi Method */
	UPROPERTY(EditAnywhere, Category = ClusterVoronoi, meta = (DisplayName = "Minimum distance from center as part of bounds max extent", UIMin = "0.0", UIMax = "1.0"))
	float ClusterRadiusPercentageMin;

	/** Cluster's Radius - Cluster Voronoi Method */
	UPROPERTY(EditAnywhere, Category = ClusterVoronoi, meta = (DisplayName = "Maximum distance from center as part of bounds max extent", UIMin = "0.0", UIMax = "1.0"))
	float ClusterRadiusPercentageMax;

	/** Cluster's Radius - Cluster Voronoi Method */
	UPROPERTY(EditAnywhere, Category = ClusterVoronoi)
	float ClusterRadius;
};


UCLASS(DisplayName="Cluster Voronoi", Category="FractureTools")
class UFractureToolClusterCutter : public UFractureToolVoronoiCutterBase
{
public:
	GENERATED_BODY()

	UFractureToolClusterCutter(const FObjectInitializer& ObjInit);

	// UFractureTool Interface
	virtual FText GetDisplayText() const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetToolIcon() const override;
	virtual TArray<UObject*> GetSettingsObjects() const override;

	virtual void RegisterUICommand( FFractureEditorCommands* BindingContext ) override;

	
	UPROPERTY(EditAnywhere, Category = Cluster)
	UFractureClusterCutterSettings* ClusterSettings;

protected:
	void GenerateVoronoiSites(const FFractureToolContext& Context, TArray<FVector>& Sites) override;

};