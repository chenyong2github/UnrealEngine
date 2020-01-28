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
#include "Changes/ValueWatcher.h"
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
	virtual UMeshSurfacePointTool* CreateNewTool(const FToolBuilderState& SceneState) const override;
};

/** Modes for quick transformer */
UENUM()
enum class EQuickTransformerMode : uint8
{
	/** Translation along frame axes */
	AxisTranslation = 0 UMETA(DisplayName = "Translate"),

	/** Rotation around frame axes*/
	AxisRotation = 1 UMETA(DisplayName = "Rotate"),
};


UENUM()
enum class EPolygonGroupMode : uint8
{
	KeepInputPolygons						UMETA(DisplayName = "Edit Input Polygons"),
	RecomputePolygonsByAngleThreshold		UMETA(DisplayName = "Recompute Polygons Based on Angle Threshold"),
	PolygonsAreTriangles					UMETA(DisplayName = "Edit Triangles Directly")
};



UCLASS()
class MESHMODELINGTOOLS_API UPolyEditTransformProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPolyEditTransformProperties();

#if WITH_EDITOR
	// UObject interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	// End of UObject interface
#endif

	//Options
	UPROPERTY(EditAnywhere, Category = Options)
	EMultiTransformerMode TransformMode;

	UPROPERTY(EditAnywhere, Category = Options)
	bool bSelectFaces;

	UPROPERTY(EditAnywhere, Category = Options)
	bool bSelectEdges;

	UPROPERTY(EditAnywhere, Category = Options)
	bool bSelectVertices;

	UPROPERTY(EditAnywhere, Category = Options)
	bool bShowWireframe;

	UPROPERTY(EditAnywhere, Category = Options)
	bool bSnapToWorldGrid;

	UPROPERTY(EditAnywhere, Category = Options)
	EPolygonGroupMode PolygonMode;

	UPROPERTY(EditAnywhere, Category = Options, meta = (EditCondition="PolygonMode == EPolygonGroupMode::RecomputePolygonsByAngleThreshold"))
	float PolygonGroupingAngleThreshold;
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

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void Tick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override { return true; }


	// UMeshSurfacePointTool API
	virtual bool HitTest(const FRay& Ray, FHitResult& OutHit) override;
	virtual void OnBeginDrag(const FRay& Ray) override;
	virtual void OnUpdateDrag(const FRay& Ray) override;
	virtual void OnEndDrag(const FRay& Ray) override;
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;

	// IClickDragBehaviorTarget API
	virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& PressPos) override;

	// IClickBehaviorTarget API
	virtual FInputRayHit IsHitByClick(const FInputDeviceRay& ClickPos) override;
	virtual void OnClicked(const FInputDeviceRay& ClickPos) override;

public:
	virtual void NextTransformTypeAction();

	//
	float VisualAngleSnapThreshold = 0.5;

protected:
	UPROPERTY()
	USimpleDynamicMeshComponent* DynamicMeshComponent;

	UPROPERTY()
	UPolyEditTransformProperties* TransformProps;

	UPROPERTY()
	UMultiTransformer* MultiTransformer = nullptr;

	TValueWatcher<EMultiTransformerMode> TransformerModeWatcher;
	void UpdateTransformerMode(EMultiTransformerMode NewMode);

	void OnMultiTransformerTransformBegin();
	void OnMultiTransformerTransformUpdate();
	void OnMultiTransformerTransformEnd();

	// realtime visualization
	void OnDynamicMeshComponentChanged();
	FDelegateHandle OnDynamicMeshComponentChangedHandle;

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property);
	


	// camera state at last render
	FViewCameraState CameraState;

	FToolDataVisualizer PolyEdgesRenderer;

	// True for the duration of UI click+drag
	bool bInDrag;

	FPlane ActiveDragPlane;
	FVector StartHitPosWorld;
	FVector StartHitNormalWorld;
	FVector LastHitPosWorld;
	FVector LastBrushPosLocal;
	FVector StartBrushPosLocal;

	FFrame3d ActiveSurfaceFrame;
	FQuickTransformer* GetActiveQuickTransformer();
	void UpdateActiveSurfaceFrame(FGroupTopologySelection& Selection);
	void UpdateQuickTransformer();

	FRay UpdateRay;
	bool bUpdatePending = false;
	void ComputeUpdate();

	FVector3d LastMoveDelta;
	FQuickAxisTranslater QuickAxisTranslater;
	void ComputeUpdate_Translate();

	FQuickAxisRotator QuickAxisRotator;
	FVector3d RotationStartPointWorld;
	FFrame3d RotationStartFrame;
	void ComputeUpdate_Rotate();

	FFrame3d InitialGizmoFrame;
	void ComputeUpdate_Gizmo();

	FGroupTopology Topology;
	void PrecomputeTopology();
	void ComputePolygons(bool RecomputeTopology = true);

	FGroupTopologySelector TopoSelector;
	bool TopologyHitTest(const FRay& WorldRay, FHitResult& OutHit, FGroupTopologySelection& OutSelection);
	void UpdateTopoSelector();

	//
	// data for current drag
	//

	FGroupTopologySelection HilightSelection;
	FToolDataVisualizer HilightRenderer;

	FGroupTopologySelection PersistentSelection;
	FToolDataVisualizer SelectionRenderer;


	FDynamicMeshAABBTree3 MeshSpatial;
	FDynamicMeshAABBTree3& GetSpatial();

	FMeshVertexChangeBuilder* ActiveVertexChange;

	FGroupTopologyDeformer LinearDeformer;
	void UpdateDeformerFromSelection(const FGroupTopologySelection& Selection);




	// Initial polygon group and mesh info
	TDynamicVector<int> InitialTriangleGroups;
	TUniquePtr<FDynamicMesh3> InitialMesh;
	void BackupTriangleGroups();
	void SetTriangleGroups(const TDynamicVector<int>& Groups);
	
	// This is true when the spatial index needs to reflect a modification
	bool bSpatialDirty; 

	void BeginChange();
	void EndChange();
	void UpdateChangeFromROI(bool bFinal);

};

