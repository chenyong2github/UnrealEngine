// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/PimplPtr.h"
#include "BaseTools/MeshSurfacePointTool.h"
#include "ToolDataVisualizer.h"
#include "SpaceDeformerOps/MeshSpaceDeformerOp.h"
#include "SpaceDeformerOps/BendMeshOp.h"
#include "SpaceDeformerOps/TwistMeshOp.h"
#include "SpaceDeformerOps/FlareMeshOp.h"
#include "SimpleDynamicMeshComponent.h"
#include "BaseGizmos/GizmoInterfaces.h"

#include "MeshSpaceDeformerTool.generated.h"

class UPreviewMesh;
class UTransformGizmo;
class UTransformProxy;
class UIntervalGizmo;
class UMeshOpPreviewWithBackgroundCompute;
class UGizmoLocalFloatParameterSource;
class UGizmoTransformChangeStateTarget;
class FSelectClickedAction;

/**
 * ToolBuilder
 */
UCLASS()
class MESHMODELINGTOOLS_API UMeshSpaceDeformerToolBuilder : public UMeshSurfacePointToolBuilder
{
	GENERATED_BODY()

public:
	UMeshSpaceDeformerToolBuilder()
	{
	}

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;

	virtual UMeshSurfacePointTool* CreateNewTool(const FToolBuilderState& SceneState) const override;
};

/** ENonlinearOperation determines which type of nonlinear deformation will be applied*/
UENUM()
enum class  ENonlinearOperationType : int8
{
	Bend		UMETA(DisplayName = "Bend"),
	Flare		UMETA(DisplayName = "Flare"),
	Twist		UMETA(DisplayName = "Twist"),
	//Sinusoid	UMETA(DisplayName = "Sinusoid"),
	//Wave		UMETA(DisplayName = "Wave"),
	//Squish	UMETA(DisplayName = "Squish")
};



UCLASS()
class MESHMODELINGTOOLS_API USpaceDeformerOperatorFactory : public UObject, public IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:
	// IDynamicMeshOperatorFactory API
	virtual TUniquePtr<FDynamicMeshOperator> MakeNewOperator() override;

	UPROPERTY()
	UMeshSpaceDeformerTool* SpaceDeformerTool;  // back pointer
};


/**
 * Applies non-linear deformations to a mesh 
 */
UCLASS()
class MESHMODELINGTOOLS_API UMeshSpaceDeformerTool : public UMeshSurfacePointTool
{
	GENERATED_BODY()

public:
	UMeshSpaceDeformerTool();


	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void SetWorld(UWorld* World);
	virtual void SetAssetAPI(IToolsContextAssetAPI* AssetAPI);


	virtual void OnTick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override {};

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override;


	// disable UMeshSurfacePointTool hits as tool is not using that interaction (subclass should be changed)
	virtual bool HitTest(const FRay& Ray, FHitResult& OutHit) override { return false; }


#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;

	// sync the parameters owned by the MeshSpaceDeformerOp 
	void UpdateOpParameters(FMeshSpaceDeformerOp& MeshSpaceDeformerOp) const;

public:

	UPROPERTY(EditAnywhere, Category = Options, meta = (DisplayName = "Operation Type"))
	ENonlinearOperationType SelectedOperationType = ENonlinearOperationType::Bend;

	/** The upper bounds interval corresponds to the region of space which the selected operator will affect. A setting of 1.0 should envelope all points in the "upper" half of the mesh given the axis has been auto-detected. The corresponding lower value of -1 will cover the entire mesh. */
	UPROPERTY(EditAnywhere, Category = Options, meta = (DisplayName = "Upper Bound", UIMin = "0.0", ClampMin = "0.0"))
	float UpperBoundsInterval = 10.0;

	/** The upper bounds interval corresponds to the region of space which the selected operator will affect. A setting of -1.0 should envelope all points in the "lower" half of the mesh given the axis has been auto-detected. The corresponding upper value of 1 will cover the entire mesh. */
	UPROPERTY(EditAnywhere, Category = Options, meta = (DisplayName = "Lower Bound", UIMax = "0", ClampMax = "0"))
	float LowerBoundsInterval = -10.0;

	/** As each operator has a range of values (i.e. curvature, angle of twist, scale), this represents the percentage passed to the operator as a parameter. In the future, for more control, this should be separated into individual settings for each operator for more precise control */
	UPROPERTY(EditAnywhere, Category = Options, meta = (DisplayName = "Modifier Percent"))
	float ModifierPercent = 20.0;

	/** Snap the deformer gizmo to the world grid */
	UPROPERTY(EditAnywhere, Category = Snapping)
	bool bSnapToWorldGrid = false;


	//UPROPERTY()
	//UMeshSpaceDeformerToolStandardProperties* SpaceDeformerProperties;

protected:

	UPROPERTY()
	UGizmoTransformChangeStateTarget* StateTarget = nullptr;
	
	// used to coordinate undo for the detail panel. 

	bool bHasBegin = false;
protected:

	UPROPERTY()
	UMeshOpPreviewWithBackgroundCompute* Preview = nullptr;   


protected:	

	TSharedPtr<FDynamicMesh3> OriginalDynamicMesh;

	UWorld* TargetWorld;
	IToolsContextAssetAPI* AssetAPI;


	// drawing plane control

	/** offset to center of gizmo*/
	UPROPERTY()
	FVector GizmoCenter;

	/** Gizmo Plane Orientation */
	UPROPERTY()
	FQuat GizmoOrientation;


	UPROPERTY()
	UIntervalGizmo* IntervalGizmo;

	UPROPERTY()
	UTransformGizmo* TransformGizmo;

	UPROPERTY()
	UTransformProxy* TransformProxy;

	/** Interval Parameter sources that reflect UI settings. */

	UPROPERTY()
	UGizmoLocalFloatParameterSource* UpIntervalSource;

	UPROPERTY()
	UGizmoLocalFloatParameterSource* DownIntervalSource;

	UPROPERTY()
	UGizmoLocalFloatParameterSource* ForwardIntervalSource;

	FFrame3d GizmoFrame;

	TPimplPtr<FSelectClickedAction> SetPointInWorldConnector;

	FVector3d AABBHalfExtents; // 1/2 the extents of the bbox

	void TransformProxyChanged(UTransformProxy* Proxy, FTransform Transform);
	void SetGizmoPlaneFromWorldPos(const FVector& Position, const FVector& Normal, bool bIsInitializing);

	/** Compute the axis aligned abounding box for the source mesh */
	void ComputeAABB(const FDynamicMesh3& MeshIn, const FTransform& XFormIn, FVector& BBoxMin, FVector& BBoxMax) const;
	
	
};

