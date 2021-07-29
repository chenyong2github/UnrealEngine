// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Changes/MeshVertexChange.h" // IMeshVertexCommandChangeTarget
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "DynamicMesh/DynamicMeshChangeTracker.h" // FDynamicMeshChange for TUniquePtr
#include "InteractiveToolActivity.h" // IToolActivityHost
#include "InteractiveToolBuilder.h"
#include "InteractiveToolQueryInterfaces.h" // IInteractiveToolNestedAcceptCancelAPI
#include "Operations/GroupTopologyDeformer.h"
#include "SingleSelectionTool.h"

#include "GeometryBase.h"

#include "EditMeshPolygonsTool.generated.h"

PREDECLARE_GEOMETRY(class FGroupTopology);
PREDECLARE_GEOMETRY(struct FGroupTopologySelection);

class UDragAlignmentMechanic;
class UGroupTopologyStorableSelection;
class UMeshOpPreviewWithBackgroundCompute; 
class FMeshVertexChangeBuilder;
class UPolyEditInsertEdgeActivity;
class UPolyEditInsertEdgeLoopActivity;
class UPolyEditExtrudeActivity;
class UPolyEditInsetOutsetActivity;
class UPolyEditCutFacesActivity;
class UPolyEditPlanarProjectionUVActivity;
class UPolygonSelectionMechanic;
class UTransformGizmo;	
class UTransformProxy;


/**
 * ToolBuilder
 */
UCLASS()
class MESHMODELINGTOOLS_API UEditMeshPolygonsToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()
public:
	bool bTriangleMode = false;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;

protected:
	virtual const FToolTargetTypeRequirements& GetTargetRequirements() const override;
};


UENUM()
enum class ELocalFrameMode
{
	FromObject,
	FromGeometry
};


/** 
 * These are properties that do not get enabled/disabled based on the action 
 */
UCLASS()
class MESHMODELINGTOOLS_API UPolyEditCommonProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Options)
	bool bShowWireframe = false;

	/** Determines whether, on selection changes, the gizmo's rotation is taken from the object transform, or from the geometry
	 elements selected. Only relevant with a local coordinate system and when rotation is not locked. */
	UPROPERTY(EditAnywhere, Category = Gizmo, meta = (HideEditConditionToggle, EditCondition = "bLocalCoordSystem && !bLockRotation"))
	ELocalFrameMode LocalFrameMode = ELocalFrameMode::FromGeometry;

	/** When true, keeps rotation of gizmo constant through selection changes and manipulations 
	 (but not middle-click repositions). Only active with a local coordinate system.*/
	UPROPERTY(EditAnywhere, Category = Gizmo, meta = (HideEditConditionToggle, EditCondition = "bLocalCoordSystem"))
	bool bLockRotation = false;

	/** This gets updated internally so that properties can respond to whether the coordinate system is set to local or global */
	UPROPERTY()
	bool bLocalCoordSystem = true;
};


UENUM()
enum class EEditMeshPolygonsToolActions
{
	NoAction,
	CancelCurrent,
	Extrude,
	Inset,
	Outset,
	InsertEdge,
	InsertEdgeLoop,
	Complete,

	PlaneCut,
	Merge,
	Delete,
	CutFaces,
	RecalculateNormals,
	FlipNormals,
	Retriangulate,
	Decompose,
	Disconnect,
	Duplicate,

	CollapseEdge,
	WeldEdges,
	StraightenEdge,
	FillHole,

	PlanarProjectionUV,

	// triangle-specific edits
	PokeSingleFace,
	SplitSingleEdge,
	FlipSingleEdge,
	CollapseSingleEdge
};

UCLASS()
class MESHMODELINGTOOLS_API UEditMeshPolygonsActionModeToolBuilder : public UEditMeshPolygonsToolBuilder
{
	GENERATED_BODY()
public:
	EEditMeshPolygonsToolActions StartupAction = EEditMeshPolygonsToolActions::Extrude;

	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};

UENUM()
enum class EEditMeshPolygonsToolSelectionMode
{
	Faces,
	Edges,
	Vertices,
	Loops,
	Rings,
	FacesEdgesVertices
};

UCLASS()
class MESHMODELINGTOOLS_API UEditMeshPolygonsSelectionModeToolBuilder : public UEditMeshPolygonsToolBuilder
{
	GENERATED_BODY()
public:
	EEditMeshPolygonsToolSelectionMode SelectionMode = EEditMeshPolygonsToolSelectionMode::Faces;

	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};



UCLASS()
class MESHMODELINGTOOLS_API UEditMeshPolygonsToolActionPropertySet : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<UEditMeshPolygonsTool> ParentTool;

	void Initialize(UEditMeshPolygonsTool* ParentToolIn) { ParentTool = ParentToolIn; }

	void PostAction(EEditMeshPolygonsToolActions Action);
};


/** PolyEdit Actions */
UCLASS()
class MESHMODELINGTOOLS_API UEditMeshPolygonsToolActions : public UEditMeshPolygonsToolActionPropertySet
{
	GENERATED_BODY()
public:
	/** Extrude the current set of selected faces. Click in viewport to confirm extrude height. */
	UFUNCTION(CallInEditor, Category = FaceEdits, meta = (DisplayName = "Extrude", DisplayPriority = 1))
	void Extrude() { PostAction(EEditMeshPolygonsToolActions::Extrude); }

	/** Inset the current set of selected faces. Click in viewport to confirm inset distance. */
	UFUNCTION(CallInEditor, Category = FaceEdits, meta = (DisplayName = "Inset", DisplayPriority = 2))
	void Inset() { PostAction(EEditMeshPolygonsToolActions::Inset);	}

	/** Outset the current set of selected faces. Click in viewport to confirm outset distance. */
	UFUNCTION(CallInEditor, Category = FaceEdits, meta = (DisplayName = "Outset", DisplayPriority = 3))
	void Outset() { PostAction(EEditMeshPolygonsToolActions::Outset);	}

	/** Merge the current set of selected faces into a single face. */
	UFUNCTION(CallInEditor, Category = FaceEdits, meta = (DisplayName = "Merge", DisplayPriority = 4))
	void Merge() { PostAction(EEditMeshPolygonsToolActions::Merge);	}

	/** Delete the current set of selected faces */
	UFUNCTION(CallInEditor, Category = FaceEdits, meta = (DisplayName = "Delete", DisplayPriority = 4))
	void Delete() { PostAction(EEditMeshPolygonsToolActions::Delete); }

	/** Cut the current set of selected faces. Click twice in viewport to set cut line. */
	UFUNCTION(CallInEditor, Category = FaceEdits, meta = (DisplayName = "CutFaces", DisplayPriority = 5))
	void CutFaces() { PostAction(EEditMeshPolygonsToolActions::CutFaces); }

	/** Recalculate normals for the current set of selected faces */
	UFUNCTION(CallInEditor, Category = FaceEdits, meta = (DisplayName = "RecalcNormals", DisplayPriority = 6))
	void RecalcNormals() { PostAction(EEditMeshPolygonsToolActions::RecalculateNormals); }

	/** Flip normals and face orientation for the current set of selected faces */
	UFUNCTION(CallInEditor, Category = FaceEdits, meta = (DisplayName = "Flip", DisplayPriority = 7))
	void Flip() { PostAction(EEditMeshPolygonsToolActions::FlipNormals); }

	/** Retriangulate each of the selected faces */
	UFUNCTION(CallInEditor, Category = FaceEdits, meta = (DisplayName = "Retriangulate", DisplayPriority = 9))
	void Retriangulate() { PostAction(EEditMeshPolygonsToolActions::Retriangulate);	}

	/** Split each of the selected faces into a separate polygon for each triangle */
	UFUNCTION(CallInEditor, Category = FaceEdits, meta = (DisplayName = "Decompose", DisplayPriority = 10))
	void Decompose() { PostAction(EEditMeshPolygonsToolActions::Decompose);	}

	/** Separate the selected faces at their borders */
	UFUNCTION(CallInEditor, Category = FaceEdits, meta = (DisplayName = "Disconnect", DisplayPriority = 11))
	void Disconnect() { PostAction(EEditMeshPolygonsToolActions::Disconnect); }

	/** Duplicate the selected faces at their borders */
	UFUNCTION(CallInEditor, Category = FaceEdits, meta = (DisplayName = "Duplicate", DisplayPriority = 12))
	void Duplicate() { PostAction(EEditMeshPolygonsToolActions::Duplicate); }

};



UCLASS()
class MESHMODELINGTOOLS_API UEditMeshPolygonsToolActions_Triangles : public UEditMeshPolygonsToolActionPropertySet
{
	GENERATED_BODY()
public:
	/** Extrude the current set of selected faces. Click in viewport to confirm extrude height. */
	UFUNCTION(CallInEditor, Category = TriangleEdits, meta = (DisplayName = "Extrude", DisplayPriority = 1))
	void Extrude() { PostAction(EEditMeshPolygonsToolActions::Extrude); }

	/** Inset the current set of selected faces. Click in viewport to confirm inset distance. */
	UFUNCTION(CallInEditor, Category = FaceEdits, meta = (DisplayName = "Inset", DisplayPriority = 2))
	void Inset() { PostAction(EEditMeshPolygonsToolActions::Inset);	}

	/** Outset the current set of selected faces. Click in viewport to confirm outset distance. */
	UFUNCTION(CallInEditor, Category = FaceEdits, meta = (DisplayName = "Outset", DisplayPriority = 3))
	void Outset() { PostAction(EEditMeshPolygonsToolActions::Outset);	}

	/** Delete the current set of selected faces */
	UFUNCTION(CallInEditor, Category = TriangleEdits, meta = (DisplayName = "Delete", DisplayPriority = 4))
	void Delete() { PostAction(EEditMeshPolygonsToolActions::Delete); }

	/** Cut the current set of selected faces. Click twice in viewport to set cut line. */
	UFUNCTION(CallInEditor, Category = TriangleEdits, meta = (DisplayName = "CutFaces", DisplayPriority = 5))
	void CutFaces() { PostAction(EEditMeshPolygonsToolActions::CutFaces); }

	/** Recalculate normals for the current set of selected faces */
	UFUNCTION(CallInEditor, Category = TriangleEdits, meta = (DisplayName = "RecalcNormals", DisplayPriority = 6))
	void RecalcNormals() { PostAction(EEditMeshPolygonsToolActions::RecalculateNormals); }

	/** Flip normals and face orientation for the current set of selected faces */
	UFUNCTION(CallInEditor, Category = TriangleEdits, meta = (DisplayName = "Flip", DisplayPriority = 7))
	void Flip() { PostAction(EEditMeshPolygonsToolActions::FlipNormals); }

	/** Separate the selected faces at their borders */
	UFUNCTION(CallInEditor, Category = TriangleEdits, meta = (DisplayName = "Disconnect", DisplayPriority = 11))
	void Disconnect() { PostAction(EEditMeshPolygonsToolActions::Disconnect); }

	/** Duplicate the selected faces */
	UFUNCTION(CallInEditor, Category = TriangleEdits, meta = (DisplayName = "Duplicate", DisplayPriority = 12))
	void Duplicate() { PostAction(EEditMeshPolygonsToolActions::Duplicate); }

	/** Poke each face at its center point */
	UFUNCTION(CallInEditor, Category = TriangleEdits, meta = (DisplayName = "Poke", DisplayPriority = 13))
	void Poke() { PostAction(EEditMeshPolygonsToolActions::PokeSingleFace); }
};





UCLASS()
class MESHMODELINGTOOLS_API UEditMeshPolygonsToolUVActions : public UEditMeshPolygonsToolActionPropertySet
{
	GENERATED_BODY()

public:

	/** Assign planar-projection UVs to mesh */
	UFUNCTION(CallInEditor, Category = UVs, meta = (DisplayName = "PlanarProjection", DisplayPriority = 11))
	void PlanarProjection()
	{
		PostAction(EEditMeshPolygonsToolActions::PlanarProjectionUV);
	}
};





UCLASS()
class MESHMODELINGTOOLS_API UEditMeshPolygonsToolEdgeActions : public UEditMeshPolygonsToolActionPropertySet
{
	GENERATED_BODY()
public:
	UFUNCTION(CallInEditor, Category = ShapeEdits, meta = (DisplayName = "InsertEdgeLoop", DisplayPriority = 1))
	void InsertEdgeLoop() { PostAction(EEditMeshPolygonsToolActions::InsertEdgeLoop); }

	UFUNCTION(CallInEditor, Category = ShapeEdits, meta = (DisplayName = "Insert Edge", DisplayPriority = 2))
	void InsertEdge() { PostAction(EEditMeshPolygonsToolActions::InsertEdge); }

	UFUNCTION(CallInEditor, Category = EdgeEdits, meta = (DisplayName = "Weld", DisplayPriority = 3))
	void Weld() { PostAction(EEditMeshPolygonsToolActions::WeldEdges); }

	UFUNCTION(CallInEditor, Category = EdgeEdits, meta = (DisplayName = "Straighten", DisplayPriority = 4))
	void Straighten() { PostAction(EEditMeshPolygonsToolActions::StraightenEdge); }

	/** Fill the adjacent hole for any selected boundary edges */
	UFUNCTION(CallInEditor, Category = EdgeEdits, meta = (DisplayName = "Fill Hole", DisplayPriority = 5))
	void FillHole()	{ PostAction(EEditMeshPolygonsToolActions::FillHole); }

	
};


UCLASS()
class MESHMODELINGTOOLS_API UEditMeshPolygonsToolEdgeActions_Triangles : public UEditMeshPolygonsToolActionPropertySet
{
	GENERATED_BODY()
public:
	UFUNCTION(CallInEditor, Category = EdgeEdits, meta = (DisplayName = "Weld", DisplayPriority = 1))
	void Weld() { PostAction(EEditMeshPolygonsToolActions::WeldEdges); }

	/** Fill the adjacent hole for any selected boundary edges */
	UFUNCTION(CallInEditor, Category = EdgeEdits, meta = (DisplayName = "Fill Hole", DisplayPriority = 1))
	void FillHole() { PostAction(EEditMeshPolygonsToolActions::FillHole); }

	UFUNCTION(CallInEditor, Category = EdgeEdits, meta = (DisplayName = "Collapse", DisplayPriority = 1))
	void Collapse() { PostAction(EEditMeshPolygonsToolActions::CollapseSingleEdge); }

	UFUNCTION(CallInEditor, Category = EdgeEdits, meta = (DisplayName = "Flip", DisplayPriority = 1))
	void Flip() { PostAction(EEditMeshPolygonsToolActions::FlipSingleEdge); }

	UFUNCTION(CallInEditor, Category = EdgeEdits, meta = (DisplayName = "Split", DisplayPriority = 1))
	void Split() { PostAction(EEditMeshPolygonsToolActions::SplitSingleEdge); }

};


/**
 * TODO: This is currently a separate action set so that we can show/hide it depending on whether
 * we have an activity running. We should have a cleaner alternative.
 */
UCLASS()
class MESHMODELINGTOOLS_API UEditMeshPolygonsToolCancelAction : public UEditMeshPolygonsToolActionPropertySet
{
	GENERATED_BODY()
public:
	UFUNCTION(CallInEditor, Category = CurrentOperation, meta = (DisplayName = "Cancel", DisplayPriority = 1))
	void Done() { PostAction(EEditMeshPolygonsToolActions::CancelCurrent); }
};



/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UEditMeshPolygonsTool : public USingleSelectionTool, 
	public IToolActivityHost, 
	public IMeshVertexCommandChangeTarget,
	public IInteractiveToolNestedAcceptCancelAPI
{
	GENERATED_BODY()
	using FFrame3d = UE::Geometry::FFrame3d;

public:
	UEditMeshPolygonsTool();

	virtual void RegisterActions(FInteractiveToolActionSet& ActionSet) override;
	void EnableTriangleMode();

	/**
	 * This should be set before tool Setup() is called to allow the tool to load
	 * the passed-in selection.
	 */
	void SetStoredToolSelection(const UGroupTopologyStorableSelection *StoredToolSelectionIn) 
	{ 
		StoredToolSelection = StoredToolSelectionIn; 
	}

	virtual void SetWorld(UWorld* World) { this->TargetWorld = World; }

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void OnTick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual void DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }

	// IInteractiveToolCameraFocusAPI implementation
	virtual FBox GetWorldSpaceFocusBox() override;
	virtual bool GetWorldSpaceFocusPoint(const FRay& WorldRay, FVector& PointOut) override;

	// IToolActivityHost
	virtual void NotifyActivitySelfEnded(UInteractiveToolActivity* Activity) override;

	// IMeshVertexCommandChangeTarget
	virtual void ApplyChange(const FMeshVertexChange* Change, bool bRevert) override;

	// IInteractiveToolNestedAcceptCancelAPI
	virtual bool SupportsNestedCancelCommand() override { return true; }
	virtual bool CanCurrentlyNestedCancel() override;
	virtual bool ExecuteNestedCancelCommand() override;
	virtual bool SupportsNestedAcceptCommand() override { return true; }
	virtual bool CanCurrentlyNestedAccept() override;
	virtual bool ExecuteNestedAcceptCommand() override;

public:

	virtual void RequestAction(EEditMeshPolygonsToolActions ActionType);

	void SetActionButtonsVisibility(bool bVisible);

protected:
	// If bTriangleMode = true, then we use a per-triangle FTriangleGroupTopology instead of polygroup topology.
	// This allows low-level mesh editing with mainly the same code, at a significant cost in overhead.
	// This is a fundamental mode switch, must be set before ::Setup() is called!
	bool bTriangleMode;		

	TObjectPtr<UWorld> TargetWorld = nullptr;

	UPROPERTY()
	TObjectPtr<UMeshOpPreviewWithBackgroundCompute> Preview = nullptr;

	UPROPERTY()
	TObjectPtr<UPolyEditCommonProperties> CommonProps = nullptr;

	UPROPERTY()
	TObjectPtr<UEditMeshPolygonsToolActions> EditActions = nullptr;
	UPROPERTY()
	TObjectPtr<UEditMeshPolygonsToolActions_Triangles> EditActions_Triangles = nullptr;

	UPROPERTY()
	TObjectPtr<UEditMeshPolygonsToolEdgeActions> EditEdgeActions = nullptr;
	UPROPERTY()
	TObjectPtr<UEditMeshPolygonsToolEdgeActions_Triangles> EditEdgeActions_Triangles = nullptr;

	UPROPERTY()
	TObjectPtr<UEditMeshPolygonsToolUVActions> EditUVActions = nullptr;

	UPROPERTY()
	TObjectPtr<UEditMeshPolygonsToolCancelAction> CancelAction = nullptr;

	/**
	 * Activity objects that handle multi-interaction operations
	 */
	UPROPERTY()
	TObjectPtr<UPolyEditExtrudeActivity> ExtrudeActivity = nullptr;
	UPROPERTY()
	TObjectPtr<UPolyEditInsetOutsetActivity> InsetOutsetActivity = nullptr;
	UPROPERTY()
	TObjectPtr<UPolyEditCutFacesActivity> CutFacesActivity = nullptr;
	UPROPERTY()
	TObjectPtr<UPolyEditPlanarProjectionUVActivity> PlanarProjectionUVActivity = nullptr;
	UPROPERTY()
	TObjectPtr<UPolyEditInsertEdgeActivity> InsertEdgeActivity = nullptr;
	UPROPERTY()
	TObjectPtr<UPolyEditInsertEdgeLoopActivity> InsertEdgeLoopActivity = nullptr;

	/**
	 * Points to one of the activities when it is active
	 */
	TObjectPtr<UInteractiveToolActivity> CurrentActivity = nullptr;

	TSharedPtr<UE::Geometry::FDynamicMesh3> CurrentMesh;
	TSharedPtr<UE::Geometry::FGroupTopology> Topology;
	TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3> MeshSpatial;

	UPROPERTY()
	TObjectPtr<UPolygonSelectionMechanic> SelectionMechanic;

	UPROPERTY()
	TObjectPtr<UDragAlignmentMechanic> DragAlignmentMechanic = nullptr;

	UPROPERTY()
	TObjectPtr<const UGroupTopologyStorableSelection> StoredToolSelection = nullptr;

	UPROPERTY()
	TObjectPtr<UTransformGizmo> TransformGizmo = nullptr;

	UPROPERTY()
	TObjectPtr<UTransformProxy> TransformProxy = nullptr;

	FText DefaultMessage;

	bool IsStoredToolSelectionUsable(const UGroupTopologyStorableSelection* StoredSelection);
	bool bSelectionStateDirty = false;
	void OnSelectionModifiedEvent();

	void OnBeginGizmoTransform(UTransformProxy* Proxy);
	void OnEndGizmoTransform(UTransformProxy* Proxy);
	void OnGizmoTransformChanged(UTransformProxy* Proxy, FTransform Transform);
	void UpdateGizmoFrame(const FFrame3d* UseFrame = nullptr);
	FFrame3d LastGeometryFrame;
	FFrame3d LastTransformerFrame;
	FFrame3d LockedTransfomerFrame;
	bool bInGizmoDrag = false;

	UE::Geometry::FTransform3d WorldTransform;

	FFrame3d InitialGizmoFrame;
	FVector3d InitialGizmoScale;
	bool bGizmoUpdatePending = false;
	FFrame3d LastUpdateGizmoFrame;
	FVector3d LastUpdateGizmoScale;
	bool bLastUpdateUsedWorldFrame = false;
	void ComputeUpdate_Gizmo();

	UE::Geometry::FDynamicMeshAABBTree3& GetSpatial();
	bool bSpatialDirty;

	// UV Scale factor to apply to texturing on any new geometry (e.g. new faces added by extrude)
	float UVScaleFactor = 1.0f;

	EEditMeshPolygonsToolActions PendingAction = EEditMeshPolygonsToolActions::NoAction;

	int32 ActivityTimestamp = 1;

	void StartActivity(TObjectPtr<UInteractiveToolActivity> Activity);
	void EndCurrentActivity(EToolShutdownType ShutdownType = EToolShutdownType::Cancel);
	void SetActionButtonPanelsVisible(bool bVisible);

	// Emit an undoable change to CurrentMesh and update related structures (preview, spatial, etc)
	void EmitCurrentMeshChangeAndUpdate(const FText& TransactionLabel,
		TUniquePtr<UE::Geometry::FDynamicMeshChange> MeshChangeIn,
		const UE::Geometry::FGroupTopologySelection& OutputSelection,
		bool bTopologyChanged);

	// Emit an undoable start of an activity
	void EmitActivityStart(const FText& TransactionLabel);

	void UpdateGizmoVisibility();

	void ApplyMerge();
	void ApplyDelete();
	void ApplyRecalcNormals();
	void ApplyFlipNormals();
	void ApplyRetriangulate();
	void ApplyDecompose();
	void ApplyDisconnect();
	void ApplyDuplicate();
	void ApplyPokeSingleFace();

	void ApplyCollapseEdge();
	void ApplyWeldEdges();
	void ApplyStraightenEdges();
	void ApplyFillHole();

	void ApplyFlipSingleEdge();
	void ApplyCollapseSingleEdge();
	void ApplySplitSingleEdge();

	FFrame3d ActiveSelectionFrameLocal;
	FFrame3d ActiveSelectionFrameWorld;
	TArray<int32> ActiveTriangleSelection;
	UE::Geometry::FAxisAlignedBox3d ActiveSelectionBounds;

	struct FSelectedEdge
	{
		int32 EdgeTopoID;
		TArray<int32> EdgeIDs;
	};
	TArray<FSelectedEdge> ActiveEdgeSelection;

	enum class EPreviewMaterialType
	{
		SourceMaterials, PreviewMaterial, UVMaterial
	};
	void UpdateEditPreviewMaterials(EPreviewMaterialType MaterialType);
	EPreviewMaterialType CurrentPreviewMaterial;


	//
	// data for current drag
	//
	UE::Geometry::FGroupTopologyDeformer LinearDeformer;
	void UpdateDeformerFromSelection(const UE::Geometry::FGroupTopologySelection& Selection);

	FMeshVertexChangeBuilder* ActiveVertexChange;
	void UpdateDeformerChangeFromROI(bool bFinal);
	void BeginDeformerChange();
	void EndDeformerChange();

	bool BeginMeshFaceEditChange();

	bool BeginMeshEdgeEditChange();
	bool BeginMeshBoundaryEdgeEditChange(bool bOnlySimple);
	bool BeginMeshEdgeEditChange(TFunctionRef<bool(int32)> GroupEdgeIDFilterFunc);

	void UpdateFromCurrentMesh(bool bGroupTopologyModified);
	int32 ModifiedTopologyCounter = 0;
	bool bWasTopologyEdited = false;

	friend class FEditMeshPolygonsToolMeshChange;
	friend class FPolyEditActivityStartChange;

	// custom setup support
	friend class UEditMeshPolygonsSelectionModeToolBuilder;
	friend class UEditMeshPolygonsActionModeToolBuilder;
	TUniqueFunction<void(UEditMeshPolygonsTool*)> PostSetupFunction;
	void SetToSelectionModeInterface();
};


/**
 * Wraps a FDynamicMeshChange so that it can be expired and so that other data
 * structures in the tool can be updated.
 */
class MESHMODELINGTOOLS_API FEditMeshPolygonsToolMeshChange : public FToolCommandChange
{
public:
	FEditMeshPolygonsToolMeshChange(TUniquePtr<UE::Geometry::FDynamicMeshChange> MeshChangeIn, bool bGroupTopologyModified)
		: MeshChange(MoveTemp(MeshChangeIn))
		, bGroupTopologyModified(bGroupTopologyModified)
	{};

	virtual void Apply(UObject* Object) override;
	virtual void Revert(UObject* Object) override;
	virtual FString ToString() const override;

protected:
	TUniquePtr<UE::Geometry::FDynamicMeshChange> MeshChange;
	bool bGroupTopologyModified;
};



/**
 * FPolyEditActivityStartChange is used to cancel out of an active action on Undo. 
 * No action is taken on Redo, ie we do not re-start the Tool on Redo.
 */
class MESHMODELINGTOOLS_API FPolyEditActivityStartChange : public FToolCommandChange
{
public:
	FPolyEditActivityStartChange(int32 ActivityTimestampIn)
	{
		ActivityTimestamp = ActivityTimestampIn;
	}
	virtual void Apply(UObject* Object) override {}
	virtual void Revert(UObject* Object) override;
	virtual bool HasExpired(UObject* Object) const override;
	virtual FString ToString() const override;

protected:
	bool bHaveDoneUndo = false;
	int32 ActivityTimestamp = 0;
};
