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
#include "TransformTypes.h"
#include "Sculpting/MeshSculptToolBase.h"
#include "Sculpting/MeshBrushOpBase.h"
#include "Image/ImageBuilder.h"
#include "Util/UniqueIndexSet.h"
#include "Polygroups/PolygroupSet.h"
#include "MeshVertexSculptTool.generated.h"

class UTransformGizmo;
class UTransformProxy;
class UMaterialInstanceDynamic;
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

	/** Grab Brush, fall-off alters the influence of the grab */
	PullKelvin UMETA(DisplayName = "Kelvin Grab"),

	/** Grab Brush that may generate cusps, fall-off alters the influence of the grab */
	PullSharpKelvin UMETA(DisplayName = "Sharp Kelvin Grab"),

	/** Smooth mesh vertices  */
	Smooth UMETA(DisplayName = "Smooth"),

	/** Smooth mesh vertices but only in direction of normal (Ctrl to invert) */
	SmoothFill UMETA(DisplayName = "SmoothFill"),

	/** Displace vertices along the average surface normal (Ctrl to invert) */
	Offset UMETA(DisplayName = "Sculpt (Normal)"),

	/** Displace vertices towards the camera viewpoint (Ctrl to invert) */
	SculptView UMETA(DisplayName = "Sculpt (Viewpoint)"),

	/** Displaces vertices along the average surface normal to a maximum height based on the brush size (Ctrl to invert) */
	SculptMax UMETA(DisplayName = "Sculpt Max"),

	/** Displace vertices along their vertex normals */
	Inflate UMETA(DisplayName = "Inflate"),

	/** Scale Brush will inflate or pinch radially from the center of the brush */
	ScaleKelvin UMETA(DisplayName = "Kelvin Scale"),

	/** Move vertices towards the center of the brush (Ctrl to push away)*/
	Pinch UMETA(DisplayName = "Pinch"),

	/** Twist Brush moves vertices in the plane perpendicular to the local mesh normal */
	TwistKelvin UMETA(DisplayName = "Kelvin Twist"),

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



/** Brush Triangle Filter Type */
UENUM()
enum class EMeshVertexSculptBrushFilterType : uint8
{
	/** Do not filter brush area */
	None,
	/** Only apply brush to triangles in the same connected mesh component/island */
	Component,
	/** Only apply brush to triangles with the same PolyGroup */
	PolyGroup
};



UCLASS()
class MESHMODELINGTOOLS_API UVertexBrushSculptProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** Primary Brush Mode */
	UPROPERTY(EditAnywhere, Category = Sculpting, meta = (DisplayName = "Brush Type"))
	EMeshVertexSculptBrushType PrimaryBrushType = EMeshVertexSculptBrushType::Offset;

	/** Primary Brush Falloff Type, multiplied by Alpha Mask where applicable */
	UPROPERTY(EditAnywhere, Category = Sculpting, meta = (DisplayName = "Falloff Shape"))
	EMeshSculptFalloffType PrimaryFalloffType = EMeshSculptFalloffType::Smooth;

	/** Filter applied to Stamp Region Triangles, based on first Stroke Stamp */
	UPROPERTY(EditAnywhere, Category = Sculpting, meta = (DisplayName = "Region Filter"))
	EMeshVertexSculptBrushFilterType BrushFilter = EMeshVertexSculptBrushFilterType::None;

	/** When Freeze Target is toggled on, the Brush Target Surface will be Frozen in its current state, until toggled off. Brush strokes will be applied relative to the Target Surface, for applicable Brushes */
	UPROPERTY(EditAnywhere, Category = Sculpting, meta = (EditCondition = "PrimaryBrushType == EMeshVertexSculptBrushType::Offset || PrimaryBrushType == EMeshVertexSculptBrushType::SculptMax || PrimaryBrushType == EMeshVertexSculptBrushType::SculptView || PrimaryBrushType == EMeshVertexSculptBrushType::Pinch || PrimaryBrushType == EMeshVertexSculptBrushType::Resample" ))
	bool bFreezeTarget = false;

	/** When enabled, instead of Mesh Smoothing, the Shift-Smooth modifier will "erase" the displacement relative to the Brush Target Surface */
	//UPROPERTY(EditAnywhere, Category = Sculpting, meta = (DisplayName = "Shift-Smooth Erases", EditCondition = "bFreezeTarget == true"))
	UPROPERTY()
	bool bSmoothErases = false;
};



/**
 * Tool Properties for a brush alpha mask
 */
UCLASS()
class MESHMODELINGTOOLS_API UVertexBrushAlphaProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** Alpha mask applied to brush stamp. Red channel is used. */
	UPROPERTY(EditAnywhere, Category = Alpha, meta = (DisplayName = "Alpha Mask"))
	UTexture2D* Alpha = nullptr;

	/** Alpha is rotated by this angle, inside the brush stamp frame (vertically aligned) */
	UPROPERTY(EditAnywhere, Category = Alpha, meta = (DisplayName = "Rotation", UIMin = "-180.0", UIMax = "180.0", ClampMin = "-360.0", ClampMax = "360.0"))
	float RotationAngle = 0.0;

	/** If true, a random angle in +/- RandomRange is added to Rotation angle for each stamp */
	UPROPERTY(EditAnywhere, Category = Alpha, AdvancedDisplay)
	bool bRandomize = false;

	/** Bounds of random generation (positive and negative) for randomized stamps */
	UPROPERTY(EditAnywhere, Category = Alpha, AdvancedDisplay, meta = (UIMin = "0.0", UIMax = "180.0"))
	float RandomRange = 180.0;
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

	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;

public:

	/** Properties that control sculpting*/
	UPROPERTY()
	UVertexBrushSculptProperties* SculptProperties;

	UPROPERTY()
	UVertexBrushAlphaProperties* AlphaProperties;

	UPROPERTY()
	UTexture2D* BrushAlpha;

public:
	virtual void IncreaseBrushSpeedAction() override;
	virtual void DecreaseBrushSpeedAction() override;

	virtual void UpdateBrushAlpha(UTexture2D* NewAlpha);

protected:
	// UMeshSculptToolBase API
	virtual UBaseDynamicMeshComponent* GetSculptMeshComponent() { return DynamicMeshComponent; }
	virtual FDynamicMesh3* GetBaseMesh() { return &BaseMesh; }
	virtual const FDynamicMesh3* GetBaseMesh() const { return &BaseMesh; }

	virtual int32 FindHitSculptMeshTriangle(const FRay3d& LocalRay) override;
	virtual int32 FindHitTargetMeshTriangle(const FRay3d& LocalRay) override;
	bool IsHitTriangleBackFacing(int32 TriangleID, const FDynamicMesh3* QueryMesh);

	virtual void OnBeginStroke(const FRay& WorldRay) override;
	virtual void OnEndStroke() override;
	// end UMeshSculptToolBase API

protected:
	UPROPERTY()
	USimpleDynamicMeshComponent* DynamicMeshComponent;

	// realtime visualization
	void OnDynamicMeshComponentChanged(USimpleDynamicMeshComponent* Component, const FMeshVertexChange* Change, bool bRevert);
	FDelegateHandle OnDynamicMeshComponentChangedHandle;

	void UpdateBrushType(EMeshVertexSculptBrushType BrushType);

	TUniquePtr<UE::Geometry::FPolygroupSet> ActiveGroupSet;
	TArray<int32> TriangleComponentIDs;

	int32 InitialStrokeTriangleID = -1;

	TSet<int32> AccumulatedTriangleROI;
	bool bUndoUpdatePending = false;
	TFuture<bool> UndoNormalsFuture;
	TFuture<bool> UndoUpdateOctreeFuture;
	TFuture<bool> UndoUpdateBaseMeshFuture;
	TArray<int> NormalsBuffer;
	void WaitForPendingUndoRedo();

	TArray<uint32> OctreeUpdateTempBuffer;
	TArray<bool> OctreeUpdateTempFlagBuffer;
	TFuture<void> StampUpdateOctreeFuture;
	bool bStampUpdatePending = false;
	void WaitForPendingStampUpdate();

	TArray<int> RangeQueryTriBuffer;
	UE::Geometry::FUniqueIndexSet VertexROIBuilder;
	UE::Geometry::FUniqueIndexSet TriangleROIBuilder;
	TArray<UE::Geometry::FIndex3i> TriangleROIInBuf;
	TArray<int> VertexROI;
	TArray<int> TriangleROIArray;
	void UpdateROI(const FVector3d& BrushPos);

	UE::Geometry::FUniqueIndexSet NormalsROIBuilder;
	TArray<std::atomic<bool>> NormalsFlags;		// set of per-vertex or per-element-id flags that indicate
												// whether normal needs recompute. Fast to do it this way
												// than to use a TSet or UniqueIndexSet...

	bool bTargetDirty;

	EMeshVertexSculptBrushType PendingStampType = EMeshVertexSculptBrushType::Smooth;

	bool UpdateStampPosition(const FRay& WorldRay);
	TFuture<void> ApplyStamp();

	FRandomStream StampRandomStream;

	FDynamicMesh3 BaseMesh;
	UE::Geometry::FDynamicMeshOctree3 BaseMeshSpatial;
	TArray<int32> BaseMeshIndexBuffer;
	bool bCachedFreezeTarget = false;
	void UpdateBaseMesh(const TSet<int32>* TriangleROI = nullptr);
	bool GetBaseMeshNearest(int32 VertexID, const FVector3d& Position, double SearchRadius, FVector3d& TargetPosOut, FVector3d& TargetNormalOut);
	TFunction<bool(int32, const FVector3d&, double MaxDist, FVector3d&, FVector3d&)> BaseMeshQueryFunc;

	UE::Geometry::FDynamicMeshOctree3 Octree;

	bool UpdateBrushPosition(const FRay& WorldRay);

	double SculptMaxFixedHeight = -1.0;

	bool bHaveBrushAlpha = false;
	UE::Geometry::TImageBuilder<FVector4f> BrushAlphaValues;
	UE::Geometry::FImageDimensions BrushAlphaDimensions;
	double SampleBrushAlpha(const FSculptBrushStamp& Stamp, const FVector3d& Position) const;

	TArray<FVector3d> ROIPositionBuffer;
	TArray<FVector3d> ROIPrevPositionBuffer;

	FMeshVertexChangeBuilder* ActiveVertexChange = nullptr;
	void BeginChange();
	void EndChange();


protected:
	virtual bool ShowWorkPlane() const override { return SculptProperties->PrimaryBrushType == EMeshVertexSculptBrushType::FixedPlane; }
};



