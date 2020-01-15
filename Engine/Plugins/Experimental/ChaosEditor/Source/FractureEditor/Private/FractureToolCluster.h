// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FractureToolVoronoiBase.h"

#include "FractureToolCluster.generated.h"


UCLASS(config = EditorPerProjectUserSettings)
class UFractureClusterSettings
	: public UObject
{
	GENERATED_BODY()
public:

	UFractureClusterSettings()
	: NumberClustersMin(8)
	, NumberClustersMax(8)
	, SitesPerClusterMin(2)
	, SitesPerClusterMax(30)
	, ClusterRadiusPercentageMin(0.1)
	, ClusterRadiusPercentageMax(0.2)
	, ClusterRadius(0.0f)
	{}

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif

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

	UPROPERTY()
	UFractureTool *OwnerTool;
};

UCLASS(DisplayName="Cluster Voronoi", Category="FractureTools")
class UFractureToolCluster: public UFractureToolVoronoiBase
{
public:
	GENERATED_BODY()

	UFractureToolCluster(const FObjectInitializer& ObjInit);//  : Super(ObjInit) {}

	// UFractureTool Interface
	virtual FText GetDisplayText() const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetToolIcon() const override;
	virtual TArray<UObject*> GetSettingsObjects() const override;// { return TArray<UObject*>(); }

	virtual void RegisterUICommand( FFractureEditorCommands* BindingContext );

	// virtual void ExecuteFracture() {}
	// virtual bool CanExecuteFracture() { return true; }

	UPROPERTY(EditAnywhere, Category = Cluster)
	UFractureClusterSettings* Settings;

protected:

	void GenerateVoronoiSites(const FFractureContext &Context, TArray<FVector>& Sites) override;

};