// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InteractiveTool.h"
#include "SimpleDynamicMeshComponent.h"
#include "Selection/GroupTopologySelector.h"
#include "TransformTypes.h"
#include "ToolDataVisualizer.h"
#include "InteractionMechanic.h"
#include "PolygonSelectionMechanic.generated.h"

class FPolygonSelectionMechanicSelectionChange;

UCLASS()
class MODELINGCOMPONENTS_API UPolygonSelectionMechanicProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = SelectionFilter)
	bool bSelectFaces = true;

	UPROPERTY(EditAnywhere, Category = SelectionFilter)
	bool bSelectEdges = true;

	UPROPERTY(EditAnywhere, Category = SelectionFilter)
	bool bSelectVertices = true;
};



/**
 * UPolygonSelectionMechanic implements the interaction for selecting a set of faces/vertices/edges
 * from a FGroupTopology on a USimpleDynamicMeshComponent. 
 */
UCLASS()
class MODELINGCOMPONENTS_API UPolygonSelectionMechanic : public UInteractionMechanic
{
	GENERATED_BODY()
public:

	// configuration variables that must be set before bSetup is called
	bool bAddSelectionFilterPropertiesToParentTool = true;

	virtual void Setup(UInteractiveTool* ParentTool) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;


	void Initialize(const FDynamicMesh3* Mesh, 
		FTransform3d TargetTransform,
		const FGroupTopology* Topology,
		TFunction<FDynamicMeshAABBTree3*()> GetSpatialSourceFunc,
		TFunction<bool(void)> GetAddToSelectionModifierStateFunc = []() {return false; }
		);

	void Initialize(const USimpleDynamicMeshComponent* MeshComponent, const FGroupTopology* Topology,
		TFunction<FDynamicMeshAABBTree3 * ()> GetSpatialSourceFunc,
		TFunction<bool(void)> GetAddToSelectionModifierStateFunc = []() {return false; }
	);

	/**
	 * Notify internal data structures that the associated MeshComponent has been modified.
	 * @param bTopologyModified if true, the underlying mesh topology has been changed. This clears the current selection.
	 */
	void NotifyMeshChanged(bool bTopologyModified);

	bool TopologyHitTest(const FRay& WorldRay, FHitResult& OutHit, FGroupTopologySelection& OutSelection);
	bool TopologyHitTest(const FRay& WorldRay, FHitResult& OutHit);

	//
	// Hover API
	//

	/**
	 * Update the hover highlight based on the hit elements at the given World Ray
	 * @return true if something was hit and is now being hovered
	 */
	bool UpdateHighlight(const FRay& WorldRay);

	/**
	 * Clear current hover-highlight
	 */
	void ClearHighlight();


	//
	// Selection API
	//

	/** 
	 * Intersect the ray with the mesh and update the selection based on the hit element, modifier states, etc
	 * @return true if selection was modified
	 */
	bool UpdateSelection(const FRay& WorldRay, FVector3d& LocalHitPositionOut, FVector3d& LocalHitNormalOut);

	/**
	 * Replace the current selection with an external selection. 
	 * @warning does not check that the selection is valid!
	 */
	void SetSelection(const FGroupTopologySelection& Selection);

	/**
	 * Clear the current selection
	 */
	void ClearSelection();

	/** 
	 * @return true if the current selection is non-empty 
	 */
	bool HasSelection() const;

	/**
	 * @return the current selection
	 */
	const FGroupTopologySelection& GetActiveSelection() const { return PersistentSelection; }

	/**
	 * @return The best-guess 3D frame for the current select
	 * @param bWorld if true, local-to-world transform of the target MeshComponent is applied to the frame
	 */
	FFrame3d GetSelectionFrame(bool bWorld, FFrame3d* InitialLocalFrame = nullptr) const;


	//
	// Change Tracking
	//

	/**
	 * Begin a change record. Internally creates a FCommandChange and initializes it with current state
	 */
	void BeginChange();

	/**
	 * End the active change and return it. Returns empty change if the selection was not modified!
	 */
	TUniquePtr<FToolCommandChange> EndChange();

	/**
	 * Ends the active change and emits it via the parent tool, if the selection has been modified.
	 */
	bool EndChangeAndEmitIfModified();

	/** OnSelectionChanged is broadcast whenever the selection is modified (including by FChanges) */
	FSimpleMulticastDelegate OnSelectionChanged;

public:
	UPROPERTY()
	UPolygonSelectionMechanicProperties* Properties;

protected:
	const FDynamicMesh3* Mesh;
	const FGroupTopology* Topology;
	TFunction<FDynamicMeshAABBTree3*()> GetSpatialFunc;

	TFunction<bool(void)> GetAddToSelectionModifierStateFunc;

	FTransform3d TargetTransform;

	FGroupTopologySelector TopoSelector;
	void UpdateTopoSelector();

	FGroupTopologySelection HilightSelection;
	FGroupTopologySelection PersistentSelection;
	int32 SelectionTimestamp = 0;
	TUniquePtr<FPolygonSelectionMechanicSelectionChange> ActiveChange;

	FViewCameraState CameraState;
public:
	FToolDataVisualizer PolyEdgesRenderer;
	FToolDataVisualizer HilightRenderer;
	FToolDataVisualizer SelectionRenderer;

	friend class FPolygonSelectionMechanicSelectionChange;
};



class MODELINGCOMPONENTS_API FPolygonSelectionMechanicSelectionChange : public FToolCommandChange
{
public:
	FGroupTopologySelection Before;
	FGroupTopologySelection After;
	int32 Timestamp = 0;

	virtual void Apply(UObject* Object) override;
	virtual void Revert(UObject* Object) override;
	virtual FString ToString() const override;
};