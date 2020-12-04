// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Platform.h"
#include "UObject/NoExportTypes.h"
#include "BaseTools/BaseBrushTool.h"
#include "SimpleDynamicMeshComponent.h"
#include "PropertySets/PolygroupLayersProperties.h"

#include "Sculpting/MeshSculptToolBase.h"
#include "Sculpting/MeshBrushOpBase.h"

#include "DynamicMeshAABBTree3.h"
#include "DynamicMeshOctree3.h"
#include "MeshNormals.h"
#include "TransformTypes.h"
#include "Changes/MeshPolygroupChange.h"
#include "Polygroups/PolygroupSet.h"

#include "MeshGroupPaintTool.generated.h"

class UMeshElementsVisualizer;
class UGroupEraseBrushOpProps;
class UGroupPaintBrushOpProps;

DECLARE_STATS_GROUP(TEXT("GroupPaintTool"), STATGROUP_GroupPaintTool, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("GroupPaintTool_UpdateROI"), GroupPaintTool_UpdateROI, STATGROUP_GroupPaintTool);
DECLARE_CYCLE_STAT(TEXT("GroupPaintTool_ApplyStamp"), GroupPaintToolApplyStamp, STATGROUP_GroupPaintTool );
DECLARE_CYCLE_STAT(TEXT("GroupPaintTool_Tick"), GroupPaintToolTick, STATGROUP_GroupPaintTool);
DECLARE_CYCLE_STAT(TEXT("GroupPaintTool_Tick_ApplyStampBlock"), GroupPaintTool_Tick_ApplyStampBlock, STATGROUP_GroupPaintTool);
DECLARE_CYCLE_STAT(TEXT("GroupPaintTool_Tick_ApplyStamp_Remove"), GroupPaintTool_Tick_ApplyStamp_Remove, STATGROUP_GroupPaintTool);
DECLARE_CYCLE_STAT(TEXT("GroupPaintTool_Tick_ApplyStamp_Insert"), GroupPaintTool_Tick_ApplyStamp_Insert, STATGROUP_GroupPaintTool);
DECLARE_CYCLE_STAT(TEXT("GroupPaintTool_Tick_NormalsBlock"), GroupPaintTool_Tick_NormalsBlock, STATGROUP_GroupPaintTool);
DECLARE_CYCLE_STAT(TEXT("GroupPaintTool_Tick_UpdateMeshBlock"), GroupPaintTool_Tick_UpdateMeshBlock, STATGROUP_GroupPaintTool);
DECLARE_CYCLE_STAT(TEXT("GroupPaintTool_Tick_UpdateTargetBlock"), GroupPaintTool_Tick_UpdateTargetBlock, STATGROUP_GroupPaintTool);
DECLARE_CYCLE_STAT(TEXT("GroupPaintTool_Normals_Collect"), GroupPaintTool_Normals_Collect, STATGROUP_GroupPaintTool);
DECLARE_CYCLE_STAT(TEXT("GroupPaintTool_Normals_Compute"), GroupPaintTool_Normals_Compute, STATGROUP_GroupPaintTool);





/**
 * Tool Builder
 */
UCLASS()
class MESHMODELINGTOOLS_API UMeshGroupPaintToolBuilder : public UMeshSurfacePointToolBuilder
{
	GENERATED_BODY()

public:
	virtual UMeshSurfacePointTool* CreateNewTool(const FToolBuilderState& SceneState) const override;
};




/** Mesh Sculpting Brush Types */
UENUM()
enum class EMeshGroupPaintInteractionType : uint8
{
	Brush,
	Fill,
	PolyLasso,

	LastValue UMETA(Hidden)
};






UCLASS()
class MESHMODELINGTOOLS_API UGroupPaintToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	//UPROPERTY(EditAnywhere, Category = "GroupPaint")
	UPROPERTY()
	EMeshGroupPaintInteractionType SubToolType = EMeshGroupPaintInteractionType::Brush;
};





/** Mesh Sculpting Brush Types */
UENUM()
enum class EMeshGroupPaintBrushType : uint8
{
	/** Paint active group */
	Paint UMETA(DisplayName = "Paint"),

	/** Erase active group */
	Erase UMETA(DisplayName = "Erase"),

	LastValue UMETA(Hidden)
};


/** Mesh Sculpting Brush Types */
UENUM()
enum class EMeshGroupPaintVisibilityType : uint8
{
	None,
	FrontFacing,
	Unoccluded
};




UCLASS()
class MESHMODELINGTOOLS_API UGroupPaintBrushFilterProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** Primary Brush Mode */
	//UPROPERTY(EditAnywhere, Category = Brush2, meta = (DisplayName = "Brush Type"))
	UPROPERTY()
	EMeshGroupPaintBrushType PrimaryBrushType = EMeshGroupPaintBrushType::Paint;

	UPROPERTY(EditAnywhere, Category = BrushFilters, meta = (DisplayName = "Volumetric"))
	bool bVolumetric = false;

	UPROPERTY(EditAnywhere, Category = BrushFilters)
	EMeshGroupPaintVisibilityType VisibilityFilter = EMeshGroupPaintVisibilityType::None;

	UPROPERTY(EditAnywhere, Category = BrushFilters, meta = (UIMin = "0.0", UIMax = "180.0", EditCondition = "bVolumetric == false"))
	float AngleThreshold = 180.0f;

	UPROPERTY(EditAnywhere, Category = BrushFilters, meta = (UIMin = "0.0", UIMax = "180.0", EditCondition = "bVolumetric == false"))
	bool bUVSeams = false;

	UPROPERTY(EditAnywhere, Category = BrushFilters, meta = (UIMin = "0.0", UIMax = "180.0", EditCondition = "bVolumetric == false"))
	bool bNormalSeams = false;

};





UENUM()
enum class EMeshGroupPaintToolActions
{
	NoAction,

	ClearFrozen,
	FreezeCurrent,
	FreezeOthers,

	GrowCurrent,
	ShrinkCurrent
};


UCLASS()
class MESHMODELINGTOOLS_API UMeshGroupPaintToolActionPropertySet : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<UMeshGroupPaintTool> ParentTool;

	void Initialize(UMeshGroupPaintTool* ParentToolIn) { ParentTool = ParentToolIn; }

	void PostAction(EMeshGroupPaintToolActions Action);
};



UCLASS()
class MESHMODELINGTOOLS_API UMeshGroupPaintToolFreezeActions : public UMeshGroupPaintToolActionPropertySet
{
	GENERATED_BODY()

public:
	UFUNCTION(CallInEditor, Category = Freezing, meta = (DisplayPriority = 1))
	void UnfreezeAll()
	{
		PostAction(EMeshGroupPaintToolActions::ClearFrozen);
	}

	UFUNCTION(CallInEditor, Category = Freezing, meta = (DisplayPriority = 2))
	void FreezeCurrent()
	{
		PostAction(EMeshGroupPaintToolActions::FreezeCurrent);
	}

	UFUNCTION(CallInEditor, Category = Freezing, meta = (DisplayPriority = 3))
	void FreezeOthers()
	{
		PostAction(EMeshGroupPaintToolActions::FreezeOthers);
	}

	UFUNCTION(CallInEditor, Category = Operations, meta = (DisplayPriority = 4))
	void GrowCurrent()
	{
		PostAction(EMeshGroupPaintToolActions::GrowCurrent);
	}

	UFUNCTION(CallInEditor, Category = Operations, meta = (DisplayPriority = 5))
	void ShrinkCurrent()
	{
		PostAction(EMeshGroupPaintToolActions::ShrinkCurrent);
	}
};




/**
 * Mesh Element Paint Tool Class
 */
UCLASS()
class MESHMODELINGTOOLS_API UMeshGroupPaintTool : public UMeshSculptToolBase
{
	GENERATED_BODY()

public:
	virtual void RegisterActions(FInteractiveToolActionSet& ActionSet) override;

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void OnTick(float DeltaTime) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override { return true; }

	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;

public:

	UPROPERTY()
	UPolygroupLayersProperties* PolygroupLayerProperties;

	UPROPERTY()
	UGroupPaintToolProperties* ToolProperties;

	/** Filters on paint brush */
	UPROPERTY()
	UGroupPaintBrushFilterProperties* FilterProperties;


private:
	// This will be of type UGroupPaintBrushOpProps, we keep a ref so we can change active group ID on pick
	UPROPERTY()
	UGroupPaintBrushOpProps* PaintBrushOpOperties;

	UPROPERTY()
	UGroupEraseBrushOpProps* EraseBrushOpOperties;

public:
	void AllocateNewGroupAndSetAsCurrentAction();
	void GrowCurrentGroupAction();
	void ShrinkCurrentGroupAction();

protected:
	// UMeshSculptToolBase API
	virtual UBaseDynamicMeshComponent* GetSculptMeshComponent() { return DynamicMeshComponent; }
	virtual FDynamicMesh3* GetBaseMesh() { check(false); return nullptr; }
	virtual const FDynamicMesh3* GetBaseMesh() const { check(false); return nullptr; }

	virtual int32 FindHitSculptMeshTriangle(const FRay3d& LocalRay) override;
	virtual int32 FindHitTargetMeshTriangle(const FRay3d& LocalRay) override;

	virtual void OnBeginStroke(const FRay& WorldRay) override;
	virtual void OnEndStroke() override;
	// end UMeshSculptToolBase API



	//
	// Action support
	//

public:
	virtual void RequestAction(EMeshGroupPaintToolActions ActionType);

	UPROPERTY()
	UMeshGroupPaintToolFreezeActions* FreezeActions;

protected:
	bool bHavePendingAction = false;
	EMeshGroupPaintToolActions PendingAction;
	virtual void ApplyAction(EMeshGroupPaintToolActions ActionType);


	//
	// Internals
	//

protected:
	UPROPERTY()
	USimpleDynamicMeshComponent* DynamicMeshComponent;

	UPROPERTY()
	UMeshElementsVisualizer* MeshElementsDisplay;

	// realtime visualization
	void OnDynamicMeshComponentChanged(USimpleDynamicMeshComponent* Component, const FMeshVertexChange* Change, bool bRevert);
	FDelegateHandle OnDynamicMeshComponentChangedHandle;

	TUniquePtr<UE::Geometry::FPolygroupSet> ActiveGroupSet;
	void OnSelectedGroupLayerChanged();
	void UpdateActiveGroupLayer();

	void UpdateSubToolType(EMeshGroupPaintInteractionType NewType);

	void UpdateBrushType(EMeshGroupPaintBrushType BrushType);

	TSet<int32> AccumulatedTriangleROI;
	bool bUndoUpdatePending = false;
	TArray<int> NormalsBuffer;
	void WaitForPendingUndoRedo();

	TArray<int> TempROIBuffer;
	TArray<int> VertexROI;
	TSet<int> VertexSetBuffer;
	TSet<int> TriangleROI;
	void UpdateROI(const FSculptBrushStamp& CurrentStamp);

	EMeshGroupPaintBrushType PendingStampType = EMeshGroupPaintBrushType::Paint;

	bool UpdateStampPosition(const FRay& WorldRay);
	void ApplyStamp();

	FDynamicMeshOctree3 Octree;

	bool UpdateBrushPosition(const FRay& WorldRay);


	bool bPendingPickGroup = false;
	bool bPendingToggleFreezeGroup = false;


	TArray<int32> ROITriangleBuffer;
	TArray<int32> ROIGroupBuffer;
	void SyncMeshWithGroupBuffer(FDynamicMesh3* Mesh);

	TUniquePtr<FDynamicMeshGroupEditBuilder> ActiveGroupEditBuilder;
	void BeginChange();
	void EndChange();

	TArray<int32> FrozenGroups;
	void ToggleFrozenGroup(int32 GroupID);
	void FreezeOtherGroups(int32 GroupID);
	void ClearAllFrozenGroups();
	void EmitFrozenGroupsChange(const TArray<int32>& FromGroups, const TArray<int32>& ToGroups, const FText& ChangeText);

	FColor GetColorForGroup(int32 GroupID);

	TArray<FVector3d> TriNormals;
	TArray<int32> UVSeamEdges;
	TArray<int32> NormalSeamEdges;
	void PrecomputeFilterData();


protected:
	virtual bool ShowWorkPlane() const override { return false; }
};



