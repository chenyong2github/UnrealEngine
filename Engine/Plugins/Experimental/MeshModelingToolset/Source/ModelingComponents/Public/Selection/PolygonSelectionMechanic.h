// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Drawing/PreviewGeometryActor.h"
#include "Drawing/TriangleSetComponent.h"
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

	/** When true, will select edge loops. Edge loops are paths along a string of valence-4 vertices. */
	UPROPERTY(EditAnywhere, Category = SelectionFilter, meta = (EditCondition = "bSelectEdges"))
	bool bSelectEdgeLoops = false;

	/** When true, will select rings of edges that are opposite each other across a quad face. */
	UPROPERTY(EditAnywhere, Category = SelectionFilter, meta = (EditCondition = "bSelectEdges"))
	bool bSelectEdgeRings = false;


	// The following were originally in their own category, all marked as AdvancedDisplay. However, since there wasn't a non-AdvancedDisplay
	// property in the category, they started out as expanded and could not be collapsed.
	// The alternative approach, used below, is to have them in a nested category, which starts out as collapsed. This works nicely.

	/** Prefer to select an edge projected to a point rather than the point, or a face projected to an edge rather than the edge. */
	UPROPERTY(EditAnywhere, Category = "SelectionFilter|Ortho Viewport Behavior")
	bool bPreferProjectedElement = true;

	/** If the closest element is valid, select other elements behind it that are aligned with it. */
	UPROPERTY(EditAnywhere, Category = "SelectionFilter|Ortho Viewport Behavior")
	bool bSelectDownRay = true;

	/** Do not check whether the closest element is occluded from the current view. */
	UPROPERTY(EditAnywhere, Category = "SelectionFilter|Ortho Viewport Behavior")
	bool bIgnoreOcclusion = false;
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

	virtual ~UPolygonSelectionMechanic();

	// configuration variables that must be set before bSetup is called
	bool bAddSelectionFilterPropertiesToParentTool = true;

	virtual void Setup(UInteractiveTool* ParentTool) override;
	virtual void Shutdown() override;

	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	/**
	 * Initializes the mechanic.
	 *
	 * @param Mesh Mesh that we are operating on.
	 * @param TargetTransform Transform of the mesh.
	 * @param World World in which we are operating, used to add drawing components that draw highlighted edges.
	 * @param Topology Group topology of the mesh.
	 * @param GetSpatialSourceFunc Function that returns an AABB tree for the mesh.
	 * @param GetAddToSelectionModifierStateFunc Functions that returns whether new selection should be trying to append to an existing 
	     selection, usually by checking whether a particular modifier key is currently pressed.
	 */
	void Initialize(const FDynamicMesh3* Mesh,
		FTransform TargetTransform,
		UWorld* World,
		const FGroupTopology* Topology,
		TFunction<FDynamicMeshAABBTree3*()> GetSpatialSourceFunc,
		TFunction<bool(void)> GetAddToSelectionModifierStateFunc = []() {return false; }
		);

	void Initialize(USimpleDynamicMeshComponent* MeshComponent, const FGroupTopology* Topology,
		TFunction<FDynamicMeshAABBTree3 * ()> GetSpatialSourceFunc,
		TFunction<bool(void)> GetAddToSelectionModifierStateFunc = []() {return false; }
	);

	void SetShouldSelectEdgeLoopsFunc(TFunction<bool(void)> Func)
	{
		ShouldSelectEdgeLoopsFunc = Func;
	}

	void SetShouldSelectEdgeRingsFunc(TFunction<bool(void)> Func)
	{
		ShouldSelectEdgeRingsFunc = Func;
	}

	/**
	 * Notify internal data structures that the associated MeshComponent has been modified.
	 * @param bTopologyModified if true, the underlying mesh topology has been changed. This clears the current selection.
	 */
	void NotifyMeshChanged(bool bTopologyModified);

	/**
	 * Perform a hit test on the topology using the current selection settings. In cases of hitting edges and
	 * corners, OutHit contains the following:
	 *   OutHit.FaceIndex: edge or corner id in the topology
	 *   OutHit.ImpactPoint: closest point on the ray to the hit element (Note: not a point on the element!)
	 *   OutHit.Distance: distance along the ray to ImpactPoint
	 *   OutHit.Item: if hit item was an edge, index of the segment within the edge polyline. Otherwise undefined.
	 *
	 * @param bUseOrthoSettings If true, the ortho-relevant settings for selection are used (selecting down the view ray, etc)
	 */
	bool TopologyHitTest(const FRay& WorldRay, FHitResult& OutHit, FGroupTopologySelection& OutSelection, bool bUseOrthoSettings = false);
	bool TopologyHitTest(const FRay& WorldRay, FHitResult& OutHit, bool bUseOrthoSettings = false);

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

	// When bSelectEdgeLoops is true, this function is tested to see if we should select edge loops,
	// to allow edge loop selection to be toggled with some key (setting bSelectEdgeLoops to
	// false overrides this function).
	TFunction<bool(void)> ShouldSelectEdgeLoopsFunc = []() {return true; };

	// When bSelectEdgeRings is true, this function is tested to see if we should select edge rings,
	// to allow edge ring selection to be toggled with some key (setting bSelectEdgeRings to
	// false overrides this function).
	TFunction<bool(void)> ShouldSelectEdgeRingsFunc = []() {return true; };

	FTransform3d TargetTransform;

	FGroupTopologySelector TopoSelector;

	/**
	 * Get the topology selector settings to use given the current selection settings.
	 * 
	 * @param bUseOrthoSettings If true, the topology selector will be configured to use ortho settings,
	 *  which are generally different to allow for selection of projected elements, etc.
	 */
	FGroupTopologySelector::FSelectionSettings GetTopoSelectorSettings(bool bUseOrthoSettings = false);

	FGroupTopologySelection HilightSelection;
	FGroupTopologySelection PersistentSelection;
	int32 SelectionTimestamp = 0;
	TUniquePtr<FPolygonSelectionMechanicSelectionChange> ActiveChange;

	/** The actor we create internally to own the DrawnTriangleSetComponent */
	UPROPERTY()
	APreviewGeometryActor* PreviewGeometryActor;

	UPROPERTY()
	UTriangleSetComponent* DrawnTriangleSetComponent;

	TSet<int> CurrentlyHighlightedGroups;

	UPROPERTY()
	UMaterialInterface* HighlightedFaceMaterial;

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