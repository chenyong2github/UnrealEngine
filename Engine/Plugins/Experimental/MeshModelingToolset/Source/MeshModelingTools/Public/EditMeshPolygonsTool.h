// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "BaseTools/MeshSurfacePointTool.h"
#include "SimpleDynamicMeshComponent.h"
#include "DynamicMeshAABBTree3.h"
#include "ToolDataVisualizer.h"
#include "Transforms/QuickAxisTranslater.h"
#include "Transforms/QuickAxisRotator.h"
#include "Changes/MeshVertexChange.h"
#include "GroupTopology.h"
#include "Spatial/GeometrySet3.h"
#include "Selection/GroupTopologySelector.h"
#include "Operations/GroupTopologyDeformer.h"
#include "ModelingOperators/Public/ModelingTaskTypes.h"
#include "Transforms/MultiTransformer.h"
#include "Selection/PolygonSelectionMechanic.h"
#include "Mechanics/PlaneDistanceFromHitMechanic.h"
#include "Mechanics/SpatialCurveDistanceMechanic.h"
#include "Mechanics/CollectSurfacePathMechanic.h"
#include "Drawing/PolyEditPreviewMesh.h"
#include "EditMeshPolygonsTool.generated.h"

class FMeshVertexChangeBuilder;


/**
 * ToolBuilder
 */
UCLASS()
class MESHMODELINGTOOLS_API UEditMeshPolygonsToolBuilder : public UMeshSurfacePointToolBuilder
{
	GENERATED_BODY()
public:
	bool bTriangleMode = false;

	virtual UMeshSurfacePointTool* CreateNewTool(const FToolBuilderState& SceneState) const override;
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

	UPROPERTY(EditAnywhere, Category = Gizmo)
	ELocalFrameMode LocalFrameMode = ELocalFrameMode::FromGeometry;

	UPROPERTY(EditAnywhere, Category = Gizmo)
	bool bLockRotation = false;

	UPROPERTY(EditAnywhere, Category = Gizmo)
	bool bSnapToWorldGrid = false;
};






UENUM()
enum class EEditMeshPolygonsToolActions
{
	NoAction,
	PlaneCut,
	Extrude,
	Offset,
	Inset,
	Outset,
	Merge,
	Delete,
	CutFaces,
	RecalculateNormals,
	FlipNormals,
	Retriangulate,
	Decompose,
	Disconnect,

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
class MESHMODELINGTOOLS_API UEditMeshPolygonsToolActionPropertySet : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<UEditMeshPolygonsTool> ParentTool;

	void Initialize(UEditMeshPolygonsTool* ParentToolIn) { ParentTool = ParentToolIn; }

	void PostAction(EEditMeshPolygonsToolActions Action);
};



UCLASS()
class MESHMODELINGTOOLS_API UEditMeshPolygonsToolActions : public UEditMeshPolygonsToolActionPropertySet
{
	GENERATED_BODY()
public:
	/** Extrude the current set of selected faces. Click in viewport to confirm extrude height. */
	UFUNCTION(CallInEditor, Category = FaceEdits, meta = (DisplayName = "Extrude", DisplayPriority = 1))
	void Extrude() { PostAction(EEditMeshPolygonsToolActions::Extrude); }

	/** Offset the current set of selected faces. Click in viewport to confirm offset distance. */
	UFUNCTION(CallInEditor, Category = FaceEdits, meta = (DisplayName = "Offset", DisplayPriority = 2))
	void Offset() { PostAction(EEditMeshPolygonsToolActions::Offset); }

	/** Inset the current set of selected faces. Click in viewport to confirm inset distance. */
	UFUNCTION(CallInEditor, Category = FaceEdits, meta = (DisplayName = "Inset", DisplayPriority = 3))
	void Inset() { PostAction(EEditMeshPolygonsToolActions::Inset);	}

	/** Outset the current set of selected faces. Click in viewport to confirm outset distance. */
	UFUNCTION(CallInEditor, Category = FaceEdits, meta = (DisplayName = "Outset", DisplayPriority = 3))
	void Outset() { PostAction(EEditMeshPolygonsToolActions::Outset); }

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
};



UCLASS()
class MESHMODELINGTOOLS_API UEditMeshPolygonsToolActions_Triangles : public UEditMeshPolygonsToolActionPropertySet
{
	GENERATED_BODY()
public:
	/** Extrude the current set of selected faces. Click in viewport to confirm extrude height. */
	UFUNCTION(CallInEditor, Category = TriangleEdits, meta = (DisplayName = "Extrude", DisplayPriority = 1))
	void Extrude() { PostAction(EEditMeshPolygonsToolActions::Extrude); }

	/** Offset the current set of selected faces. Click in viewport to confirm offset distance. */
	UFUNCTION(CallInEditor, Category = TriangleEdits, meta = (DisplayName = "Offset", DisplayPriority = 2))
	void Offset() { PostAction(EEditMeshPolygonsToolActions::Offset); }

	/** Inset the current set of selected faces. Click in viewport to confirm inset distance. */
	UFUNCTION(CallInEditor, Category = TriangleEdits, meta = (DisplayName = "Inset", DisplayPriority = 3))
	void Inset() { PostAction(EEditMeshPolygonsToolActions::Inset);	}

	/** Outset the current set of selected faces. Click in viewport to confirm outset distance. */
	UFUNCTION(CallInEditor, Category = TriangleEdits, meta = (DisplayName = "Outset", DisplayPriority = 3))
	void Outset() { PostAction(EEditMeshPolygonsToolActions::Outset); }

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

	/** Poke each face at its center point */
	UFUNCTION(CallInEditor, Category = TriangleEdits, meta = (DisplayName = "Poke", DisplayPriority = 12))
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
	UFUNCTION(CallInEditor, Category = EdgeEdits, meta = (DisplayName = "Weld", DisplayPriority = 1))
	void Weld() { PostAction(EEditMeshPolygonsToolActions::WeldEdges); }

	UFUNCTION(CallInEditor, Category = EdgeEdits, meta = (DisplayName = "Straighten", DisplayPriority = 1))
	void Straighten() { PostAction(EEditMeshPolygonsToolActions::StraightenEdge); }

	/** Fill the adjacent hole for any selected boundary edges */
	UFUNCTION(CallInEditor, Category = EdgeEdits, meta = (DisplayName = "Fill Hole", DisplayPriority = 1))
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




UENUM()
enum class EPolyEditExtrudeDirection
{
	SelectionNormal,
	WorldX,
	WorldY,
	WorldZ,
	LocalX,
	LocalY,
	LocalZ
};


UCLASS()
class MESHMODELINGTOOLS_API UPolyEditExtrudeProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Extrude)
	EPolyEditExtrudeDirection Direction = EPolyEditExtrudeDirection::SelectionNormal;

	/** Controls whether extruding an entire patch should create a solid or an open shell */
	UPROPERTY(EditAnywhere, Category = Extrude)
	bool bShellsToSolids = true;
};



UCLASS()
class MESHMODELINGTOOLS_API UPolyEditOffsetProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** Offset by averaged face normals instead of per-vertex normals */
	UPROPERTY(EditAnywhere, Category = Offset)
	bool bUseFaceNormals = false;
};



/**
 * Settings for Inset operation
 */
UCLASS()
class MESHMODELINGTOOLS_API UPolyEditInsetProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** Determines whether vertices in inset region should be projected back onto input surface */
	UPROPERTY(EditAnywhere, Category = Inset)
	bool bReproject = true;

	/** Amount of smoothing applied to inset boundary */
	UPROPERTY(EditAnywhere, Category = Inset, meta = (UIMin = "0.0", UIMax = "1.0", EditCondition = "bBoundaryOnly == false"))
	float Softness = 0.5;

	/** Controls whether inset operation will move interior vertices as well as border vertices */
	UPROPERTY(EditAnywhere, Category = Inset, AdvancedDisplay)
	bool bBoundaryOnly = false;

	/** Tweak area scaling when solving for interior vertices */
	UPROPERTY(EditAnywhere, Category = Inset, AdvancedDisplay, meta = (UIMin = "0.0", UIMax = "1.0", EditCondition = "bBoundaryOnly == false"))
	float AreaScale = true;
};



UCLASS()
class MESHMODELINGTOOLS_API UPolyEditOutsetProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** Amount of smoothing applied to outset boundary */
	UPROPERTY(EditAnywhere, Category = Inset, meta = (UIMin = "0.0", UIMax = "1.0", EditCondition = "bBoundaryOnly == false"))
	float Softness = 0.5;

	/** Controls whether outset operation will move interior vertices as well as border vertices */
	UPROPERTY(EditAnywhere, Category = Inset, AdvancedDisplay)
	bool bBoundaryOnly = false;

	/** Tweak area scaling when solving for interior vertices */
	UPROPERTY(EditAnywhere, Category = Inset, AdvancedDisplay, meta = (UIMin = "0.0", UIMax = "1.0", EditCondition = "bBoundaryOnly == false"))
	float AreaScale = true;
};






UENUM()
enum class EPolyEditCutPlaneOrientation
{
	FaceNormals,
	ViewDirection
};



UCLASS()
class MESHMODELINGTOOLS_API UPolyEditCutProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Cut)
	EPolyEditCutPlaneOrientation Orientation = EPolyEditCutPlaneOrientation::FaceNormals;

	UPROPERTY(EditAnywhere, Category = Cut)
	bool bSnapToVertices = true;
};




UCLASS()
class MESHMODELINGTOOLS_API UPolyEditSetUVProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = PlanarProjectUV)
	bool bShowMaterial = false;
};




/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UEditMeshPolygonsTool : public UMeshSurfacePointTool, public IClickBehaviorTarget
{
	GENERATED_BODY()

public:
	UEditMeshPolygonsTool();

	virtual void RegisterActions(FInteractiveToolActionSet& ActionSet) override;
	void EnableTriangleMode();

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void OnTick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }

	// UMeshSurfacePointTool API
	virtual bool HitTest(const FRay& Ray, FHitResult& OutHit) override;
	virtual void OnBeginDrag(const FRay& Ray) override;
	virtual void OnUpdateDrag(const FRay& Ray) override;
	virtual void OnEndDrag(const FRay& Ray) override;
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;
	virtual void OnEndHover() override;

	// IClickDragBehaviorTarget API
	virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& PressPos) override;

	// IClickBehaviorTarget API
	virtual FInputRayHit IsHitByClick(const FInputDeviceRay& ClickPos) override;
	virtual void OnClicked(const FInputDeviceRay& ClickPos) override;

public:

	virtual void RequestAction(EEditMeshPolygonsToolActions ActionType);

protected:
	// If bTriangleMode = true, then we use a per-triangle FTriangleGroupTopology instead of polygroup topology.
	// This allows low-level mesh editing with mainly the same code, at a significant cost in overhead.
	// This is a fundamental mode switch, must be set before ::Setup() is called!
	bool bTriangleMode;		

	UPROPERTY()
	USimpleDynamicMeshComponent* DynamicMeshComponent = nullptr;

	UPROPERTY()
	UPolyEditCommonProperties* CommonProps;

	UPROPERTY()
	UEditMeshPolygonsToolActions* EditActions;
	UPROPERTY()
	UEditMeshPolygonsToolActions_Triangles* EditActions_Triangles;

	UPROPERTY()
	UEditMeshPolygonsToolEdgeActions* EditEdgeActions;
	UPROPERTY()
	UEditMeshPolygonsToolEdgeActions_Triangles* EditEdgeActions_Triangles;

	UPROPERTY()
	UEditMeshPolygonsToolUVActions* EditUVActions;

	UPROPERTY()
	UPolyEditExtrudeProperties* ExtrudeProperties;

	UPROPERTY()
	UPolyEditOffsetProperties* OffsetProperties;

	UPROPERTY()
	UPolyEditInsetProperties* InsetProperties;

	UPROPERTY()
	UPolyEditOutsetProperties* OutsetProperties;

	UPROPERTY()
	UPolyEditCutProperties* CutProperties;

	UPROPERTY()
	UPolyEditSetUVProperties* SetUVProperties;


	UPROPERTY()
	UPolygonSelectionMechanic* SelectionMechanic;


	bool bSelectionStateDirty = false;
	void OnSelectionModifiedEvent();

	UPROPERTY()
	UMultiTransformer* MultiTransformer = nullptr;

	void OnMultiTransformerTransformBegin();
	void OnMultiTransformerTransformUpdate();
	void OnMultiTransformerTransformEnd();
	void UpdateMultiTransformerFrame(const FFrame3d* UseFrame = nullptr);
	FFrame3d LastGeometryFrame;
	FFrame3d LastTransformerFrame;
	FFrame3d LockedTransfomerFrame;

	// realtime visualization
	void OnDynamicMeshComponentChanged();
	FDelegateHandle OnDynamicMeshComponentChangedHandle;


	// camera state at last render
	FTransform3d WorldTransform;
	FViewCameraState CameraState;

	// True for the duration of UI click+drag
	bool bInDrag;

	FFrame3d InitialGizmoFrame;
	FVector3d InitialGizmoScale;
	void CacheUpdate_Gizmo();
	bool bGizmoUpdatePending = false;
	FFrame3d LastUpdateGizmoFrame;
	FVector3d LastUpdateGizmoScale;
	void ComputeUpdate_Gizmo();

	TUniquePtr<FGroupTopology> Topology;
	void PrecomputeTopology();

	FDynamicMeshAABBTree3 MeshSpatial;
	FDynamicMeshAABBTree3& GetSpatial();
	bool bSpatialDirty;

	// UV Scale factor to apply to texturing on any new geometry (e.g. new faces added by extrude)
	float UVScaleFactor = 1.0f;

	EEditMeshPolygonsToolActions PendingAction = EEditMeshPolygonsToolActions::NoAction;

	enum class ECurrentToolMode
	{
		TransformSelection,
		ExtrudeSelection,
		OffsetSelection,
		InsetSelection,
		OutsetSelection,
		CutSelection,
		SetUVs
	};
	ECurrentToolMode CurrentToolMode = ECurrentToolMode::TransformSelection;
	int32 CurrentOperationTimestamp = 1;
	bool CheckInOperation(int32 Timestamp) const { return CurrentOperationTimestamp == Timestamp; }

	void BeginExtrude(bool bIsNormalOffset);
	void ApplyExtrude(bool bIsOffset);
	void RestartExtrude();
	FVector3d GetExtrudeDirection() const;

	void BeginInset(bool bOutset);
	void ApplyInset(bool bOutset);

	void BeginCutFaces();
	void ApplyCutFaces();

	void BeginSetUVs();
	void UpdateSetUVS();
	void ApplySetUVs();

	void ApplyPlaneCut();
	void ApplyMerge();
	void ApplyDelete();
	void ApplyRecalcNormals();
	void ApplyFlipNormals();
	void ApplyRetriangulate();
	void ApplyDecompose();
	void ApplyDisconnect();
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
	FAxisAlignedBox3d ActiveSelectionBounds;

	struct FSelectedEdge
	{
		int32 EdgeTopoID;
		TArray<int32> EdgeIDs;
	};
	TArray<FSelectedEdge> ActiveEdgeSelection;

	bool bPreviewUpdatePending = false;

	UPROPERTY()
	UPolyEditPreviewMesh* EditPreview;

	enum class EPreviewMaterialType
	{
		SourceMaterials, PreviewMaterial, UVMaterial
	};
	void UpdateEditPreviewMaterials(EPreviewMaterialType MaterialType);
	EPreviewMaterialType CurrentPreviewMaterial;

	UPROPERTY()
	UPlaneDistanceFromHitMechanic* ExtrudeHeightMechanic = nullptr;
	UPROPERTY()
	USpatialCurveDistanceMechanic* CurveDistMechanic = nullptr;
	UPROPERTY()
	UCollectSurfacePathMechanic* SurfacePathMechanic = nullptr;

	//
	// data for current drag
	//

	FGroupTopologyDeformer LinearDeformer;
	void UpdateDeformerFromSelection(const FGroupTopologySelection& Selection);



	FMeshVertexChangeBuilder* ActiveVertexChange;
	void BeginChange();
	void EndChange();
	void UpdateChangeFromROI(bool bFinal);

	bool BeginMeshFaceEditChange();
	bool BeginMeshFaceEditChangeWithPreview();
	void CompleteMeshEditChange(const FText& TransactionLabel, TUniquePtr<FToolCommandChange> EditChange, const FGroupTopologySelection& OutputSelection);
	void CancelMeshEditChange();

	bool BeginMeshEdgeEditChange();
	bool BeginMeshBoundaryEdgeEditChange(bool bOnlySimple);
	bool BeginMeshEdgeEditChange(TFunctionRef<bool(int32)> GroupEdgeIDFilterFunc);

	void AfterTopologyEdit();
	int32 ModifiedTopologyCounter = 0;
	bool bWasTopologyEdited = false;

	void SetActionButtonPanelsVisible(bool bVisible);

	friend class FEditPolygonsTopologyPreEditChange;
	friend class FEditPolygonsTopologyPostEditChange;
	friend class FBeginInteractivePolyEditChange;
};





class MESHMODELINGTOOLS_API FEditPolygonsTopologyPreEditChange : public FToolCommandChange
{
public:
	virtual void Apply(UObject* Object) override;
	virtual void Revert(UObject* Object) override;
	virtual FString ToString() const override;
};

class MESHMODELINGTOOLS_API FEditPolygonsTopologyPostEditChange : public FToolCommandChange
{
public:
	virtual void Apply(UObject* Object) override;
	virtual void Revert(UObject* Object) override;
	virtual FString ToString() const override;
};


/**
 * FBeginInteractivePolyEditChange is used to cancel out of an active action on Undo. No action is taken on Redo
 * No action is taken on Redo, ie we do not re-start the Tool on Redo.
 */
class MESHMODELINGTOOLS_API FBeginInteractivePolyEditChange : public FToolCommandChange
{
public:
	bool bHaveDoneUndo = false;
	int32 OperationTimestamp = 0;
	FBeginInteractivePolyEditChange(int32 CurrentTimestamp)
	{
		OperationTimestamp = CurrentTimestamp;
	}
	virtual void Apply(UObject* Object) override {}
	virtual void Revert(UObject* Object) override;
	virtual bool HasExpired(UObject* Object) const override;
	virtual FString ToString() const override;
};
