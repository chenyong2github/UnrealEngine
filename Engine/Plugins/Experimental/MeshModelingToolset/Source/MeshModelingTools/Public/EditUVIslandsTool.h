// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseTools/MeshSurfacePointTool.h"
#include "Properties/MeshMaterialProperties.h"
#include "SimpleDynamicMeshComponent.h"
#include "DynamicMeshAABBTree3.h"
#include "ToolDataVisualizer.h"
#include "Changes/MeshVertexChange.h"
#include "GroupTopology.h"
#include "Selection/GroupTopologySelector.h"
#include "ModelingOperators/Public/ModelingTaskTypes.h"
#include "Transforms/MultiTransformer.h"
#include "Selection/PolygonSelectionMechanic.h"
#include "EditUVIslandsTool.generated.h"

class FMeshVertexChangeBuilder;
using UE::Geometry::FDynamicMeshUVOverlay;

/**
 * ToolBuilder
 */
UCLASS()
class MESHMODELINGTOOLS_API UEditUVIslandsToolBuilder : public UMeshSurfacePointToolBuilder
{
	GENERATED_BODY()
public:
	virtual UMeshSurfacePointTool* CreateNewTool(const FToolBuilderState& SceneState) const override;
};




class MESHMODELINGTOOLS_API FUVGroupTopology : public FGroupTopology
{
public:
	TArray<int32> TriIslandGroups;
	const FDynamicMeshUVOverlay* UVOverlay;

	FUVGroupTopology() {}
	FUVGroupTopology(const UE::Geometry::FDynamicMesh3* Mesh, uint32 UVLayerIndex, bool bAutoBuild = false);

	void CalculateIslandGroups();

	virtual int GetGroupID(int32 TriangleID) const override
	{
		return TriIslandGroups[TriangleID];
	}

	UE::Geometry::FFrame3d GetIslandFrame(int32 GroupID, UE::Geometry::FDynamicMeshAABBTree3& AABBTree);
};


/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UEditUVIslandsTool : public UMeshSurfacePointTool
{
	GENERATED_BODY()

public:
	UEditUVIslandsTool();

	virtual void RegisterActions(FInteractiveToolActionSet& ActionSet) override;

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


public:
	UPROPERTY()
	UExistingMeshMaterialProperties* MaterialSettings = nullptr;

	UPROPERTY()
	UMaterialInstanceDynamic* CheckerMaterial = nullptr;

protected:
	UPROPERTY()
	USimpleDynamicMeshComponent* DynamicMeshComponent;

	UPROPERTY()
	UPolygonSelectionMechanic* SelectionMechanic;

	bool bSelectionStateDirty = false;
	void OnSelectionModifiedEvent();

	UPROPERTY()
	UMultiTransformer* MultiTransformer = nullptr;

	void OnMultiTransformerTransformBegin();
	void OnMultiTransformerTransformUpdate();
	void OnMultiTransformerTransformEnd();

	// realtime visualization
	void OnDynamicMeshComponentChanged();
	FDelegateHandle OnDynamicMeshComponentChangedHandle;

	// camera state at last render
	UE::Geometry::FTransform3d WorldTransform;
	FViewCameraState CameraState;

	// True for the duration of UI click+drag
	bool bInDrag;

	double UVTranslateScale;
	UE::Geometry::FFrame3d InitialGizmoFrame;
	UE::Geometry::FVector3d InitialGizmoScale;
	void ComputeUpdate_Gizmo();

	FUVGroupTopology Topology;
	void PrecomputeTopology();

	FDynamicMeshAABBTree3 MeshSpatial;
	FDynamicMeshAABBTree3& GetSpatial();
	bool bSpatialDirty;


	//
	// data for current drag
	//
	struct FEditIsland
	{
		UE::Geometry::FFrame3d LocalFrame;
		TArray<int32> Triangles;
		TArray<int32> UVs;
		UE::Geometry::FAxisAlignedBox2d UVBounds;
		UE::Geometry::FVector2d UVOrigin;
		TArray<UE::Geometry::FVector2f> InitialPositions;
	};
	TArray<FEditIsland> ActiveIslands;
	void UpdateUVTransformFromSelection(const FGroupTopologySelection& Selection);

	FMeshVertexChangeBuilder* ActiveVertexChange;
	void BeginChange();
	void EndChange();
	void UpdateChangeFromROI(bool bFinal);

	void OnMaterialSettingsChanged();
};

