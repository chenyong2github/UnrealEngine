// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FractureTool.h"

#include "FractureToolClusterMagnet.generated.h"

namespace Chaos
{
	template<class T, int d>
	class TParticles;
}


struct FClusterMagnet
{
	TSet<int32> ClusteredNodes;
	TSet<int32> Connections;
};


UCLASS(DisplayName = "Cluster Magnet", Category = "FractureTools")
class UFractureClusterMagnetSettings : public UFractureToolSettings
{
public:
	GENERATED_BODY()

		UFractureClusterMagnetSettings(const FObjectInitializer& ObjInit)
		: Super(ObjInit)
		, Iterations(1)
		, MassPercentile(0.9)
	{}

	UPROPERTY(EditAnywhere, Category = ClusterMagnet, meta = (DisplayName = "Iterations", UIMin = "1", UIMax = "100", ClampMin = "1", ClampMax = "500"))
	uint32 Iterations;

	UPROPERTY(EditAnywhere, Category = ClusterMagnet, meta = (DisplayName = "Mass percentile to form magnets", UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float MassPercentile;

};


UCLASS(DisplayName = "ClusterMagnet", Category = "FractureTools")
class UFractureToolClusterMagnet : public UFractureModalTool
{
public:
	GENERATED_BODY()

	using FGeometryCollectionPtr = TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe>;
		
	UFractureToolClusterMagnet(const FObjectInitializer& ObjInit);

	// UFractureTool Interface
	virtual FText GetDisplayText() const override;
	virtual FText GetTooltipText() const override;
	virtual FText GetApplyText() const override;
	virtual FSlateIcon GetToolIcon() const override;
	virtual TArray<UObject*> GetSettingsObjects() const override;
	virtual void RegisterUICommand(FFractureEditorCommands* BindingContext);

	virtual void Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit) override;

	UPROPERTY(EditAnywhere, Category = AutoCluster)
	UFractureClusterMagnetSettings* ClusterMagnetSettings;

private:
	bool CheckPresenceOfNecessaryAttributes(const FGeometryCollectionPtr GeometryCollection) const;
	//TArray<int32> GatherTopNodes(const FGeometryCollectionPtr GeometryCollection) const;
	float FindCutoffMass(float Percentile, const FGeometryCollectionPtr GeometryCollection, const TSet<int32>& TopNodes) const;
	void UpdateMasses(FGeometryCollectionPtr GeometryCollection, const TSet<int32>& TopNodes) const;
	void UpdateMasses(FGeometryCollectionPtr GeometryCollection, const Chaos::TParticles<float, 3>& MassSpaceParticles, int32 TransformIndex) const;
	TMap<int32, TSet<int32>> InitializeConnectivity(const TSet<int32>& TopNodes, FGeometryCollectionPtr GeometryCollection, int32 OperatingLevel) const;
	void CollectTopNodeConnections(FGeometryCollectionPtr GeometryCollection, int32 Index, int32 OperatingLevel, TSet<int32>& OutConnections) const;
	void SeparateClusterMagnets(const FGeometryCollectionPtr GeometryCollection, const TSet<int32>& TopNodes, float CutoffMass, const TMap<int32, TSet<int32>>& TopNodeConnectivity, TArray<FClusterMagnet>& OutClusterMagnets, TSet<int32>& OutRemainingPool) const;
	bool AbsorbClusterNeighbors(const TMap<int32, TSet<int32>> TopNodeConnectivity, FClusterMagnet& OutClusterMagnets, TSet<int32>& OutRemainingPool) const;

};
