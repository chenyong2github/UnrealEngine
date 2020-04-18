// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Platform.h"
#include "UObject/NoExportTypes.h"
#include "SimpleDynamicMeshComponent.h"
#include "DynamicMeshAABBTree3.h"
#include "DynamicMeshOctree3.h"
#include "MeshNormals.h"
#include "BaseTools/BaseBrushTool.h"
#include "Changes/ValueWatcher.h"
#include "TransformTypes.h"
#include "Sculpting/MeshSculptToolBase.h"
#include "Sculpting/MeshBrushOpBase.h"
#include "MeshVertexSculptTool.generated.h"

class UTransformGizmo;
class UTransformProxy;
class UMaterialInstanceDynamic;

DECLARE_STATS_GROUP(TEXT("VertexSculptTool"), STATGROUP_VtxSculptTool, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("VertexSculptTool_UpdateROI"), VtxSculptTool_UpdateROI, STATGROUP_VtxSculptTool);
DECLARE_CYCLE_STAT(TEXT("VertexSculptTool_ApplyStamp"), VtxSculptToolApplyStamp, STATGROUP_VtxSculptTool );
DECLARE_CYCLE_STAT(TEXT("VertexSculptTool_Tick"), VtxSculptToolTick, STATGROUP_VtxSculptTool);
DECLARE_CYCLE_STAT(TEXT("VertexSculptTool_Tick_ApplyStampBlock"), VtxSculptTool_Tick_ApplyStampBlock, STATGROUP_VtxSculptTool);
DECLARE_CYCLE_STAT(TEXT("VertexSculptTool_Tick_ApplyStamp_Remove"), VtxSculptTool_Tick_ApplyStamp_Remove, STATGROUP_VtxSculptTool);
DECLARE_CYCLE_STAT(TEXT("VertexSculptTool_Tick_ApplyStamp_Insert"), VtxSculptTool_Tick_ApplyStamp_Insert, STATGROUP_VtxSculptTool);
DECLARE_CYCLE_STAT(TEXT("VertexSculptTool_Tick_NormalsBlock"), VtxSculptTool_Tick_NormalsBlock, STATGROUP_VtxSculptTool);
DECLARE_CYCLE_STAT(TEXT("VertexSculptTool_Tick_UpdateMeshBlock"), VtxSculptTool_Tick_UpdateMeshBlock, STATGROUP_VtxSculptTool);
DECLARE_CYCLE_STAT(TEXT("VertexSculptTool_Tick_UpdateTargetBlock"), VtxSculptTool_Tick_UpdateTargetBlock, STATGROUP_VtxSculptTool);
DECLARE_CYCLE_STAT(TEXT("VertexSculptTool_Normals_Collect"), VtxSculptTool_Normals_Collect, STATGROUP_VtxSculptTool);
DECLARE_CYCLE_STAT(TEXT("VertexSculptTool_Normals_Compute"), VtxSculptTool_Normals_Compute, STATGROUP_VtxSculptTool);

class FMeshVertexChangeBuilder;
class UPreviewMesh;




/**
 * Tool Builder
 */
UCLASS()
class MESHMODELINGTOOLS_API UMeshVertexSculptToolBuilder : public UMeshSurfacePointToolBuilder
{
	GENERATED_BODY()

public:
	virtual UMeshSurfacePointTool* CreateNewTool(const FToolBuilderState& SceneState) const override;
};





/** Mesh Sculpting Brush Types */
UENUM()
enum class EMeshVertexSculptBrushType : uint8
{
	/** Move vertices parallel to the view plane  */
	Move UMETA(DisplayName = "Move"),

	/** Smooth mesh vertices  */
	Smooth UMETA(DisplayName = "Smooth"),

	/** Displace vertices along the average surface normal (Ctrl to invert) */
	Offset UMETA(DisplayName = "Sculpt (Normal)"),

	/** Displace vertices towards the camera viewpoint (Ctrl to invert) */
	SculptView UMETA(DisplayName = "Sculpt (Viewpoint)"),

	/** Displaces vertices along the average surface normal to a maximum height based on the brush size (Ctrl to invert) */
	SculptMax UMETA(DisplayName = "Sculpt Max"),

	/** Displace vertices along their vertex normals */
	Inflate UMETA(DisplayName = "Inflate"),

	/** Move vertices towards the center of the brush (Ctrl to push away)*/
	Pinch UMETA(DisplayName = "Pinch"),

	/** Move vertices towards the average plane of the brush stamp region */
	Flatten UMETA(DisplayName = "Flatten"),

	/** Move vertices towards a plane defined by the initial brush position  */
	Plane UMETA(DisplayName = "Plane (Normal)"),

	/** Move vertices towards a view-facing plane defined at the initial brush position */
	PlaneViewAligned UMETA(DisplayName = "Plane (Viewpoint)"),

	/** Move vertices towards a fixed plane in world space, positioned with a 3D gizmo */
	FixedPlane UMETA(DisplayName = "FixedPlane"),

	LastValue UMETA(Hidden)

};






UCLASS()
class MESHMODELINGTOOLS_API UVertexBrushSculptProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** Primary Brush Mode */
	UPROPERTY(EditAnywhere, Category = Sculpting, meta = (DisplayName = "Brush Type"))
	EMeshVertexSculptBrushType PrimaryBrushType = EMeshVertexSculptBrushType::Move;

	/** Strength of the Primary Brush */
	UPROPERTY(EditAnywhere, Category = Sculpting, meta = (DisplayName = "Strength", UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0", EditCondition = "PrimaryBrushType != EMeshVertexSculptBrushType::Pull"))
	float PrimaryBrushSpeed = 0.5;

	/** If true, try to preserve the shape of the UV/3D mapping. This will limit Smoothing and Remeshing in some cases. */
	UPROPERTY(EditAnywhere, Category = Sculpting)
	bool bPreserveUVFlow = false;

	/** When Freeze Target is toggled on, the Brush Target Surface will be Frozen in its current state, until toggled off. Brush strokes will be applied relative to the Target Surface, for applicable Brushes */
	UPROPERTY(EditAnywhere, Category = Sculpting, meta = (EditCondition = "PrimaryBrushType == EMeshVertexSculptBrushType::Sculpt || PrimaryBrushType == EMeshVertexSculptBrushType::SculptMax || PrimaryBrushType == EMeshVertexSculptBrushType::SculptView || PrimaryBrushType == EMeshVertexSculptBrushType::Pinch || PrimaryBrushType == EMeshVertexSculptBrushType::Resample" ))
	bool bFreezeTarget = false;

	/** Strength of Shift-to-Smooth Brushing and Smoothing Brush */
	UPROPERTY(EditAnywhere, Category = Smoothing, meta = (DisplayName = "Smoothing Strength", UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float SmoothBrushSpeed = 0.25;
};





/**
 * Mesh Vertex Sculpt Tool Class
 */
UCLASS()
class MESHMODELINGTOOLS_API UMeshVertexSculptTool : public UMeshSculptToolBase
{
	GENERATED_BODY()

public:
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void OnTick(float DeltaTime) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override { return true; }

	virtual bool HitTest(const FRay& Ray, FHitResult& OutHit) override;

	virtual void OnBeginDrag(const FRay& Ray) override;
	virtual void OnUpdateDrag(const FRay& Ray) override;
	virtual void OnEndDrag(const FRay& Ray) override;

	virtual FInputRayHit BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos) override;
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;

public:

	/** Properties that control sculpting*/
	UPROPERTY()
	UVertexBrushSculptProperties* SculptProperties;

	UPROPERTY()
	UPlaneBrushProperties* PlaneBrushProperties;

	UPROPERTY()
	USculptMaxBrushProperties* SculptMaxBrushProperties;
	
	//UPROPERTY()
	//UKelvinBrushProperties* KelvinBrushProperties;


public:
	virtual void IncreaseBrushSpeedAction() override;
	virtual void DecreaseBrushSpeedAction() override;
	virtual void NextBrushModeAction() override;
	virtual void PreviousBrushModeAction() override;

protected:
	UPROPERTY()
	USimpleDynamicMeshComponent* DynamicMeshComponent;

	virtual UBaseDynamicMeshComponent* GetSculptMeshComponent() { return DynamicMeshComponent; }

	FTransform3d InitialTargetTransform;
	FTransform3d CurTargetTransform;

	// realtime visualization
	void OnDynamicMeshComponentChanged(USimpleDynamicMeshComponent* Component, const FMeshVertexChange* Change, bool bRevert);
	FDelegateHandle OnDynamicMeshComponentChangedHandle;

	TValueWatcher<EMeshVertexSculptBrushType> BrushTypeWatcher;
	void UpdateBrushType(EMeshVertexSculptBrushType BrushType);


	bool bInDrag;

	FFrame3d ActiveDragPlane;
	FVector3d LastHitPosWorld;
	FVector3d LastBrushPosLocal;
	FVector3d LastBrushPosWorld;
	FVector3d LastBrushPosNormalWorld;

	TSet<int32> AccumulatedTriangleROI;
	bool bUndoUpdatePending = false;
	TFuture<bool> UndoNormalsFuture;
	TFuture<bool> UndoUpdateOctreeFuture;
	TFuture<bool> UndoUpdateBaseMeshFuture;
	TArray<int> NormalsBuffer;
	void WaitForPendingUndoRedo();

	TArray<int> VertexROI;
	TSet<int> VertexSetBuffer;
	TSet<int> TriangleROI;
	void UpdateROI(const FVector3d& BrushPos);

	bool bTargetDirty;

	bool bSmoothing;
	bool bInvert;

	bool bStampPending;
	FRay PendingStampRay;
	int StampTimestamp = 0;
	EMeshVertexSculptBrushType PendingStampType = EMeshVertexSculptBrushType::Smooth;

	TUniquePtr<FMeshSculptBrushOp> PrimaryBrushOp;
	TUniquePtr<FMeshSculptBrushOp> SecondaryBrushOp;
	TSharedPtr<FMeshSculptFallofFunc> ActiveFalloff;

	bool UpdateStampPosition(const FRay& WorldRay);
	void ApplyStamp();


	FDynamicMesh3 BaseMesh;
	FDynamicMeshOctree3 BaseMeshSpatial;
	TArray<int32> BaseMeshIndexBuffer;
	bool bCachedFreezeTarget = false;
	void UpdateBaseMesh(const TSet<int32>* TriangleROI = nullptr);
	bool GetBaseMeshNearest(int32 VertexID, const FVector3d& Position, double SearchRadius, FVector3d& TargetPosOut, FVector3d& TargetNormalOut);

	FDynamicMeshOctree3 Octree;

	int FindHitSculptMeshTriangle(const FRay3d& LocalRay);
	int FindHitTargetMeshTriangle(const FRay3d& LocalRay);

	bool UpdateBrushPosition(const FRay& WorldRay);
	bool UpdateBrushPositionOnActivePlane(const FRay& WorldRay);
	bool UpdateBrushPositionOnTargetMesh(const FRay& WorldRay, bool bFallbackToViewPlane);
	bool UpdateBrushPositionOnSculptMesh(const FRay& WorldRay, bool bFallbackToViewPlane);
	void AlignBrushToView();

	double SculptMaxFixedHeight = -1.0;

	double CalculateBrushFalloff(double Distance);
	TArray<FVector3d> ROIPositionBuffer;
	void SyncMeshWithPositionBuffer(FDynamicMesh3* Mesh);

	FFrame3d ActiveFixedBrushPlane;
	FFrame3d ComputeROIBrushPlane(const FVector3d& BrushCenter, bool bIgnoreDepth, bool bViewAligned);

	FMeshVertexChangeBuilder* ActiveVertexChange = nullptr;
	void BeginChange();
	void EndChange();

	FCriticalSection UpdateSavedVertexLock;



protected:
	virtual bool ShowWorkPlane() const override { return SculptProperties->PrimaryBrushType == EMeshVertexSculptBrushType::FixedPlane; }
};



