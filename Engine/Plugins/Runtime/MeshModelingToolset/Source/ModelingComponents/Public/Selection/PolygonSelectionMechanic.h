// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MeshTopologySelectionMechanic.h"
#include "PolygonSelectionMechanic.generated.h"

// DEPRECATED: Use UMeshTopologySelectionMechanicProperties
UCLASS(Deprecated)
class MODELINGCOMPONENTS_API UDEPRECATED_PolygonSelectionMechanicProperties : public UMeshTopologySelectionMechanicProperties
{
	GENERATED_BODY()

public:

	void Initialize(UPolygonSelectionMechanic* MechanicIn)
	{
		Mechanic = MechanicIn;
	}
};


/**
 * UPolygonSelectionMechanic implements the interaction for selecting a set of faces/vertices/edges
 * from a FGroupTopology.
 */
UCLASS()
class MODELINGCOMPONENTS_API UPolygonSelectionMechanic : public UMeshTopologySelectionMechanic
{
	GENERATED_BODY()

public:

	void Initialize(const FDynamicMesh3* Mesh,
		FTransform3d TargetTransform,
		UWorld* World,
		const FGroupTopology* Topology,
		TFunction<FDynamicMeshAABBTree3* ()> GetSpatialSourceFunc
	);

	void Initialize(UDynamicMeshComponent* MeshComponent, const FGroupTopology* Topology,
		TFunction<FDynamicMeshAABBTree3* ()> GetSpatialSourceFunc
	);

	// UMeshTopologySelectionMechanic
	virtual void Setup(UInteractiveTool* ParentTool) override;
	virtual bool UpdateHighlight(const FRay& WorldRay) override;
	virtual bool UpdateSelection(const FRay& WorldRay, FVector3d& LocalHitPositionOut, FVector3d& LocalHitNormalOut) override;

	/**
	 * Gives the current selection as a storable selection object. Can optionally apply the passed-in
	 * compact maps to the object if the topology in the mechanic was not updated after compacting.
	 */
	void GetSelection(UPersistentMeshSelection& SelectionOut, const FCompactMaps* CompactMapsToApply = nullptr) const;

	/**
	 * Sets the current selection using the given storable selection object. The topology in the
	 * mechanic must already be initialized for this to work.
	 */
	void LoadSelection(const UPersistentMeshSelection& Selection);


	UE_DEPRECATED(5.2, "Use GetTopologySelector in base class")
	TSharedPtr<FGroupTopologySelector, ESPMode::ThreadSafe> GetTopologySelector() 
	{ 
		return StaticCastSharedPtr<FGroupTopologySelector>(TopoSelector); 
	}

	UE_DEPRECATED(5.2, "Use MechanicProperties in base class")
	UPROPERTY()
	TObjectPtr<UDEPRECATED_PolygonSelectionMechanicProperties> Properties_DEPRECATED;

private:

	// TODO: Would be nice to get rid of this and write everything in terms of TopologySelector and TopologyProvider
	const FGroupTopology* Topology;

};
