// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "BaseTools/MeshSurfacePointTool.h"
#include "OctreeDynamicMeshComponent.h"
#include "DynamicMeshAABBTree3.h"
#include "DynamicMeshOctree3.h"
#include "BrushToolIndicator.h"
#include "MeshNormals.h"
#include "BaseBrushTool.h"
#include "Drawing/ToolDataVisualizer.h"
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


/** Mesh sculpting brush types */
UENUM()
enum class EDynamicMeshSculptBrushType : uint8
{
	/** Move brush */
	Pull UMETA(DisplayName = "Pull"),

	/** Offset brush */
	Offset UMETA(DisplayName = "Offset"),

	/** Pinch brush */
	Pinch UMETA(DisplayName = "Pinch"),

	/** Flatten brush */
	Flatten UMETA(DisplayName = "Flatten"),
};



/** Smooth type for sculpting brush */
UENUM()
enum class EMeshSculptToolSmoothType : uint8
{
	/** Uniform smoothing */
	Uniform UMETA(DisplayName = "Uniform"),

	/** Texture-preserving smoothing */
	TexturePreserving UMETA(DisplayName = "Texture Preserving"),
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

	/** primary brush mode */
	UPROPERTY(EditAnywhere, Category = Options)
	EDynamicMeshSculptBrushType PrimaryBrushType;

	/** smoothing type */
	UPROPERTY(EditAnywhere, Category = Options)
	EMeshSculptToolSmoothType SmoothingType;

	/** Smoothing speed of brush */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float SmoothPower;

	/** Offset speed of brush */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0", EditCondition = "PrimaryBrushType != EDynamicMeshSculptBrushType::Pull"))
	float OffsetPower;

	/** Depth of Brush into surface along ray */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "-0.5", UIMax = "0.5", ClampMin = "-1.0", ClampMax = "1.0"))
	float Depth;
};





UCLASS()
class MESHMODELINGTOOLS_API UBrushRemeshProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UBrushRemeshProperties();

	/** Target Relative Triangle Sizefor Dynamic Meshing */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "0.5", UIMax = "2.0", ClampMin = "0.1", ClampMax = "100.0"))
	float RelativeSize;

	/** Smoothing speed for dynamic meshing */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float Smoothing;


	/** Enable edge flips */
	UPROPERTY(EditAnywhere, Category = Options, AdvancedDisplay)
	bool bFlips = true;

	/** Enable edge splits */
	UPROPERTY(EditAnywhere, Category = Options, AdvancedDisplay)
	bool bSplits = true;

	/** Enable edge collapses */
	UPROPERTY(EditAnywhere, Category = Options, AdvancedDisplay)
	bool bCollapses = true;

	/** Prevent normal flips */
	UPROPERTY(EditAnywhere, Category = Options, AdvancedDisplay)
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
	virtual void OnUpdateHover(const FInputDeviceRay& DevicePos) override;

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



public:

	virtual void IncreaseBrushRadiusAction();
	virtual void DecreaseBrushRadiusAction();


protected:

	UPROPERTY()
	UToolIndicatorSet* Indicators;

	UPROPERTY()
	UOctreeDynamicMeshComponent* DynamicMeshComponent;

	// realtime visualization
	void OnDynamicMeshComponentChanged();
	FDelegateHandle OnDynamicMeshComponentChangedHandle;

	FInterval1d BrushRelativeSizeRange;
	double CurrentBrushRadius;
	void CalculateBrushRadius();

	bool bEnableRemeshing;
	double InitialEdgeLength;

	bool bInDrag;

	FPlane ActiveDragPlane;
	FVector LastHitPosWorld;
	FVector BrushStartCenterWorld;
	FVector BrushStartNormalWorld;
	FVector LastBrushPosLocal;
	FVector LastBrushPosWorld;
	FVector LastBrushPosNormalWorld;
	

	TArray<int> VertexROI;
	TSet<int> VertexSetBuffer;
	TSet<int> TriangleROI;
	void UpdateROI(const FVector& BrushPos);

	bool bRemeshPending;
	bool bNormalUpdatePending;
	bool bTargetDirty;

	bool bSmoothing;

	bool bHaveRemeshed;

	bool bStampPending;
	FRay PendingStampRay;
	void ApplyStamp(const FRay& WorldRay);

	FDynamicMesh3 BrushTargetMesh;
	FDynamicMeshAABBTree3 BrushTargetMeshSpatial;
	FMeshNormals BrushTargetNormals;
	void UpdateTarget();
	bool GetTargetMeshNearest(const FVector3d& Position, double SearchRadius, FVector3d& TargetPosOut, FVector3d& TargetNormalOut);

	bool UpdateBrushPositionOnActivePlane(const FRay& WorldRay);
	bool UpdateBrushPositionOnTargetMesh(const FRay& WorldRay);
	void ApplySmoothBrush(const FRay& WorldRay);
	void ApplyMoveBrush(const FRay& WorldRay);
	void ApplyOffsetBrush(const FRay& WorldRay);
	void ApplyPinchBrush(const FRay& WorldRay);
	void ApplyFlattenBrush(const FRay& WorldRay);

	FFrame3d ActiveFlattenFrame;
	void ComputeFlattenFrame();

	TArray<int> TrianglesBuffer;
	TArray<int> NormalsBuffer;
	TArray<bool> NormalsVertexFlags;
	void RecalculateNormals_PerVertex();
	void RecalculateNormals_Overlay();

	void RemeshROIPass();

	FMeshVertexChangeBuilder* ActiveVertexChange = nullptr;
	FDynamicMeshChangeTracker* ActiveMeshChange = nullptr;
	void BeginChange(bool bIsVertexChange);
	void EndChange();
	void SaveActiveROI();
	void UpdateSavedVertex(int vid, const FVector3d& OldPosition, const FVector3d& NewPosition);

	double EstimateIntialSafeTargetLength(const FDynamicMesh3& Mesh, int MinTargetTriCount);
};



