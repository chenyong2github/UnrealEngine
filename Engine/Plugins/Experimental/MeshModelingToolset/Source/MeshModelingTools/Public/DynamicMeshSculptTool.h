// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "BaseTools/MeshSurfacePointTool.h"
#include "OctreeDynamicMeshComponent.h"
#include "DynamicMeshAABBTree3.h"
#include "DynamicMeshOctree3.h"
#include "MeshNormals.h"
#include "BaseBrushTool.h"
#include "Drawing/ToolDataVisualizer.h"
#include "Changes/ValueWatcher.h"
#include "Gizmos/BrushStampIndicator.h"
#include "Properties/MeshMaterialProperties.h"
#include "TransformTypes.h"
#include "DynamicMeshSculptTool.generated.h"


DECLARE_STATS_GROUP(TEXT("SculptTool"), STATGROUP_SculptTool, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("SculptTool_UpdateROI"), SculptTool_UpdateROI, STATGROUP_SculptTool);
DECLARE_CYCLE_STAT(TEXT("SculptTool_ApplyStamp"), STAT_SculptToolApplyStamp, STATGROUP_SculptTool );
DECLARE_CYCLE_STAT(TEXT("SculptTool_Tick"), STAT_SculptToolTick, STATGROUP_SculptTool);
DECLARE_CYCLE_STAT(TEXT("SculptTool_Tick_ApplyStampBlock"), STAT_SculptTool_Tick_ApplyStampBlock, STATGROUP_SculptTool);
DECLARE_CYCLE_STAT(TEXT("SculptTool_Tick_ApplyStamp_Remove"), SculptTool_Tick_ApplyStamp_Remove, STATGROUP_SculptTool);
DECLARE_CYCLE_STAT(TEXT("SculptTool_Tick_ApplyStamp_Insert"), SculptTool_Tick_ApplyStamp_Insert, STATGROUP_SculptTool);
DECLARE_CYCLE_STAT(TEXT("SculptTool_Tick_RemeshBlock"), STAT_SculptTool_Tick_RemeshBlock, STATGROUP_SculptTool);
DECLARE_CYCLE_STAT(TEXT("SculptTool_Tick_NormalsBlock"), STAT_SculptTool_Tick_NormalsBlock, STATGROUP_SculptTool);
DECLARE_CYCLE_STAT(TEXT("SculptTool_Tick_UpdateMeshBlock"), STAT_SculptTool_Tick_UpdateMeshBlock, STATGROUP_SculptTool);
DECLARE_CYCLE_STAT(TEXT("SculptTool_Normals_Collect"), SculptTool_Normals_Collect, STATGROUP_SculptTool);
DECLARE_CYCLE_STAT(TEXT("SculptTool_Normals_Compute"), SculptTool_Normals_Compute, STATGROUP_SculptTool);
DECLARE_CYCLE_STAT(TEXT("SculptTool_Remesh_1Setup"), STAT_SculptTool_Remesh_Setup, STATGROUP_SculptTool);
DECLARE_CYCLE_STAT(TEXT("SculptTool_Remesh_2Constraints"), STAT_SculptTool_Remesh_Constraints, STATGROUP_SculptTool);
DECLARE_CYCLE_STAT(TEXT("SculptTool_Remesh_3RemeshROIUpdate"), STAT_SculptTool_Remesh_RemeshROIUpdate, STATGROUP_SculptTool);
DECLARE_CYCLE_STAT(TEXT("SculptTool_Remesh_4RemeshPass"), STAT_SculptTool_Remesh_RemeshPass, STATGROUP_SculptTool);
DECLARE_CYCLE_STAT(TEXT("SculptTool_Remesh_5PassOctreeUpdate"), STAT_SculptTool_Remesh_PassOctreeUpdate, STATGROUP_SculptTool);
DECLARE_CYCLE_STAT(TEXT("SculptTool_Remesh_6Finish"), STAT_SculptTool_Remesh_Finish, STATGROUP_SculptTool);


class FMeshVertexChangeBuilder;
class FDynamicMeshChangeTracker;
class UPreviewMesh;

/** Mesh Sculpting Brush Types */
UENUM()
enum class EDynamicMeshSculptBrushType : uint8
{
	/** Move Brush moves vertices parallel to the view plane  */
	Move UMETA(DisplayName = "Move"),

	/** Smooth brush smooths mesh vertices  */
	Smooth UMETA(DisplayName = "Smooth"),

	/** Sculpt Brush displaces vertices from the surface */
	Offset UMETA(DisplayName = "Sculpt"),

	/** SculptMax Brush displaces vertices to a maximum offset height */
	SculptMax UMETA(DisplayName = "Sculpt Max"),

	/** Inflate brush displaces vertices along their vertex normals */
	Inflate UMETA(DisplayName = "Inflate"),

	/** Pinch Brush pulls vertices towards the center of the brush*/
	Pinch UMETA(DisplayName = "Pinch"),

	/** Flatten Brush pulls vertices towards the average plane of the brush stamp */
	Flatten UMETA(DisplayName = "Flatten"),

	/** Plane Brush pulls vertices to a plane defined by the initial brush position  */
	Plane UMETA(DisplayName = "Plane"),


	LastValue UMETA(Hidden)

};




/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UDynamicMeshSculptToolBuilder : public UMeshSurfacePointToolBuilder
{
	GENERATED_BODY()

public:
	bool bEnableRemeshing;

	UDynamicMeshSculptToolBuilder()
	{
		bEnableRemeshing = false;
	}

	virtual UMeshSurfacePointTool* CreateNewTool(const FToolBuilderState& SceneState) const override;
};









UCLASS()
class MESHMODELINGTOOLS_API UBrushSculptProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UBrushSculptProperties();

	/** Primary Brush Mode */
	UPROPERTY(EditAnywhere, Category = Sculpting)
	EDynamicMeshSculptBrushType PrimaryBrushType;

	/** Power/Speed of Brush */
	UPROPERTY(EditAnywhere, Category = Sculpting, meta = (UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0", EditCondition = "PrimaryBrushType != EDynamicMeshSculptBrushType::Pull"))
	float BrushSpeed;

	/** Smoothing Speed of Smoothing brush */
	UPROPERTY(EditAnywhere, Category = Sculpting, meta = (UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float SmoothSpeed;

	/** If true, try to preserve the shape of the UV/3D mapping. This will prevent smoothing in some cases */
	UPROPERTY(EditAnywhere, Category = Sculpting)
	bool bPreserveUVFlow;


	/** Depth of Brush into surface along view ray or surface normal, depending on brush */
	UPROPERTY(EditAnywhere, Category = Sculpting, meta = (UIMin = "-0.5", UIMax = "0.5", ClampMin = "-1.0", ClampMax = "1.0"))
	float BrushDepth;

	/** Disable updating of the brush Target Surface after each stroke */
	UPROPERTY(EditAnywhere, Category = Sculpting, meta = (EditCondition = "PrimaryBrushType == EDynamicMeshSculptBrushType::Sculpt || PrimaryBrushType == EDynamicMeshSculptBrushType::SculptMax || PrimaryBrushType == EDynamicMeshSculptBrushType::Pinch" ))
	bool bFreezeTarget;

	virtual void SaveProperties(UInteractiveTool* SaveFromTool) override;
	virtual void RestoreProperties(UInteractiveTool* RestoreToTool) override;

};




UCLASS()
class MESHMODELINGTOOLS_API UBrushRemeshProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UBrushRemeshProperties();

	/** Target Relative Triangle Sizefor Dynamic Meshing */
	UPROPERTY(EditAnywhere, Category = Remeshing, meta = (UIMin = "0.5", UIMax = "2.0", ClampMin = "0.1", ClampMax = "100.0"))
	float RelativeSize;

	/** Smoothing speed for dynamic meshing */
	UPROPERTY(EditAnywhere, Category = Remeshing, meta = (UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float Smoothing;

	/** If enabled, Full Remeshing is applied during smoothing, which will wipe out fine details */
	UPROPERTY(EditAnywhere, Category = Remeshing)
	bool bRemeshSmooth = false;


	/** Enable edge flips */
	UPROPERTY(EditAnywhere, Category = Remeshing, AdvancedDisplay)
	bool bFlips = false;

	/** Enable edge splits */
	UPROPERTY(EditAnywhere, Category = Remeshing, AdvancedDisplay)
	bool bSplits = true;

	/** Enable edge collapses */
	UPROPERTY(EditAnywhere, Category = Remeshing, AdvancedDisplay)
	bool bCollapses = true;

	/** Prevent normal flips */
	UPROPERTY(EditAnywhere, Category = Remeshing, AdvancedDisplay)
	bool bPreventNormalFlips = false;

};



/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UDynamicMeshSculptTool : public UMeshSurfacePointTool
{
	GENERATED_BODY()

public:
	UDynamicMeshSculptTool();

	virtual void SetWorld(UWorld* World);
	virtual void RegisterActions(FInteractiveToolActionSet& ActionSet) override;

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void Tick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override { return true; }

	virtual bool HitTest(const FRay& Ray, FHitResult& OutHit) override;

	virtual void OnBeginDrag(const FRay& Ray) override;
	virtual void OnUpdateDrag(const FRay& Ray) override;
	virtual void OnEndDrag(const FRay& Ray) override;
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;

	virtual void SetEnableRemeshing(bool bEnable) { bEnableRemeshing = bEnable; }
	virtual bool GetEnableRemeshing() const { return bEnableRemeshing; }


	virtual void OnPropertyModified(UObject* PropertySet, UProperty* Property) override;

public:

	/** Properties that control sculpting*/
	UPROPERTY()
	UBrushSculptProperties* SculptProperties;

	/** Properties that control brush size/etc*/
	UPROPERTY()
	UBrushBaseProperties* BrushProperties;

	/** Properties that control dynamic remeshing */
	UPROPERTY()
	UBrushRemeshProperties* RemeshProperties;

	UPROPERTY()
	UMeshEditingViewProperties* ViewProperties;


public:

	virtual void IncreaseBrushRadiusAction();
	virtual void DecreaseBrushRadiusAction();
	virtual void IncreaseBrushRadiusSmallStepAction();
	virtual void DecreaseBrushRadiusSmallStepAction();

	virtual void IncreaseBrushSpeedAction();
	virtual void DecreaseBrushSpeedAction();


	virtual void NextBrushModeAction();
	virtual void PreviousBrushModeAction();

	virtual void NextHistoryBrushModeAction();
	virtual void PreviousHistoryBrushModeAction();


protected:

	UWorld* TargetWorld;		// required to spawn UPreviewMesh/etc

	UPROPERTY()
	UBrushStampIndicator* BrushIndicator;

	UPROPERTY()
	UPreviewMesh* BrushIndicatorMesh;

	UPROPERTY()
	UOctreeDynamicMeshComponent* DynamicMeshComponent;

	FTransform3d InitialTargetTransform;
	FTransform3d CurTargetTransform;

	// realtime visualization
	void OnDynamicMeshComponentChanged();
	FDelegateHandle OnDynamicMeshComponentChangedHandle;

	TValueWatcher<bool> ShowWireframeWatcher;
	TValueWatcher<EMeshEditingMaterialModes> MaterialModeWatcher;
	void UpdateMaterialMode(EMeshEditingMaterialModes NewMode);


	FInterval1d BrushRelativeSizeRange;
	double CurrentBrushRadius;
	void CalculateBrushRadius();

	bool bEnableRemeshing;
	double InitialEdgeLength;

	bool bInDrag;

	FFrame3d ActiveDragPlane;
	FVector3d LastHitPosWorld;
	FVector3d BrushStartCenterWorld;
	FVector3d BrushStartNormalWorld;
	FVector3d LastBrushPosLocal;
	FVector3d LastBrushPosWorld;
	FVector3d LastBrushPosNormalWorld;
	FVector3d LastSmoothBrushPosLocal;
	

	TArray<int> VertexROI;
	TSet<int> VertexSetBuffer;
	TSet<int> TriangleROI;
	void UpdateROI(const FVector& BrushPos);

	bool bRemeshPending;
	bool bNormalUpdatePending;
	bool bTargetDirty;

	bool bSmoothing;
	bool bInvert;

	bool bHaveRemeshed;

	bool bStampPending;
	FRay PendingStampRay;
	int StampTimestamp = 0;
	EDynamicMeshSculptBrushType LastStampType = EDynamicMeshSculptBrushType::LastValue;
	EDynamicMeshSculptBrushType PendingStampType = LastStampType;
	void ApplyStamp(const FRay& WorldRay);

	FDynamicMesh3 BrushTargetMesh;
	FDynamicMeshAABBTree3 BrushTargetMeshSpatial;
	FMeshNormals BrushTargetNormals;
	bool bCachedFreezeTarget = false;
	void UpdateTarget();
	bool GetTargetMeshNearest(const FVector3d& Position, double SearchRadius, FVector3d& TargetPosOut, FVector3d& TargetNormalOut);

	bool UpdateBrushPositionOnActivePlane(const FRay& WorldRay);
	bool UpdateBrushPositionOnTargetMesh(const FRay& WorldRay);
	bool UpdateBrushPositionOnSculptMesh(const FRay& WorldRay);

	void ApplySmoothBrush(const FRay& WorldRay);
	void ApplyMoveBrush(const FRay& WorldRay);
	void ApplyOffsetBrush(const FRay& WorldRay);
	void ApplySculptMaxBrush(const FRay& WorldRay);
	void ApplyPinchBrush(const FRay& WorldRay);
	void ApplyInflateBrush(const FRay& WorldRay);
	void ApplyPlaneBrush(const FRay& WorldRay);
	void ApplyFlattenBrush(const FRay& WorldRay);

	double CalculateBrushFalloff(double Distance);
	TArray<FVector3d> ROIPositionBuffer;

	FFrame3d ActiveFixedBrushPlane;
	FFrame3d ComputeROIBrushPlane(const FVector3d& BrushCenter, bool bIgnoreDepth);

	TArray<int> TrianglesBuffer;
	TArray<int> NormalsBuffer;
	TArray<bool> NormalsVertexFlags;
	void RecalculateNormals_PerVertex();
	void RecalculateNormals_Overlay();

	bool bHaveMeshBoundaries;
	bool bHaveUVSeams;
	bool bHaveNormalSeams;
	void PrecomputeRemeshInfo();
	void RemeshROIPass();

	FMeshVertexChangeBuilder* ActiveVertexChange = nullptr;
	FDynamicMeshChangeTracker* ActiveMeshChange = nullptr;
	void BeginChange(bool bIsVertexChange);
	void EndChange();
	void SaveActiveROI();

	void UpdateSavedVertex(int vid, const FVector3d& OldPosition, const FVector3d& NewPosition);
	FCriticalSection UpdateSavedVertexLock;

	double EstimateIntialSafeTargetLength(const FDynamicMesh3& Mesh, int MinTargetTriCount);

	TArray<EDynamicMeshSculptBrushType> BrushTypeHistory;
	int BrushTypeHistoryIndex = 0;

};



