// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseTools/SingleSelectionMeshEditingTool.h"
#include "MeshOpPreviewHelpers.h"
#include "ToolDataVisualizer.h"
#include "ParameterizationOps/UVProjectionOp.h"
#include "Properties/MeshMaterialProperties.h"
#include "Properties/MeshUVChannelProperties.h"
#include "Drawing/PreviewGeometryActor.h"
#include "Selection/SelectClickedAction.h"

#include "UVProjectionTool.generated.h"


// predeclarations
struct FMeshDescription;
class USimpleDynamicMeshComponent;
class UTransformGizmo;
class UTransformProxy;
class USingleClickInputBehavior;
class IAssetGenerationAPI;

/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UUVProjectionToolBuilder : public USingleSelectionMeshEditingToolBuilder
{
	GENERATED_BODY()

public:
	virtual USingleSelectionMeshEditingTool* CreateNewTool(const FToolBuilderState& SceneState) const override;

	virtual bool WantsInputSelectionIfAvailable() const override { return true; }
};




UENUM()
enum class EUVProjectionToolActions
{
	NoAction,
	AutoFit,
	AutoFitAlign,
	Reset
};


UCLASS()
class MESHMODELINGTOOLS_API UUVProjectionToolEditActions : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<UUVProjectionTool> ParentTool;

	void Initialize(UUVProjectionTool* ParentToolIn) { ParentTool = ParentToolIn; }
	void PostAction(EUVProjectionToolActions Action);

	/** Automatically fit the Projection Dimensions based on the current projection orientation */
	UFUNCTION(CallInEditor, Category = Actions, meta = (DisplayName = "AutoFit", DisplayPriority = 1))
	void AutoFit()
	{
		PostAction(EUVProjectionToolActions::AutoFit);
	}

	/** Automatically align the projection orientation and then automatically fit the Dimensions */
	UFUNCTION(CallInEditor, Category = Actions, meta = (DisplayName = "AutoFitAlign", DisplayPriority = 2))
	void AutoFitAlign()
	{
		PostAction(EUVProjectionToolActions::AutoFitAlign);
	}

	/** Re-initialize the Projection based on the Initialization setting */
	UFUNCTION(CallInEditor, Category = Actions, meta = (DisplayName = "Reset", DisplayPriority = 3))
	void Reset()
	{
		PostAction(EUVProjectionToolActions::Reset);
	}
};



UENUM()
enum class EUVProjectionToolInitializationMode
{
	/** Initialize projection to bounding box center */
	Default,
	/** Initialize projection based on previous usage of the UV Projection Tool */
	UsePrevious,
	/** Initialize projection using AutoFitting, for the initial projection type */
	AutoFit,
	/** Initialize projection using AutoFitting with Alignment, for the initial projection type */
	AutoFitAlign
};


UENUM()
enum class EUVProjectionToolDimensionMode
{
	/** All Dimensions of the projection are used */
	AllFree,
	/** The Dimensions.X value is used to define all projection Dimensions */
	UseFirst
};




/**
 * Standard properties
 */
UCLASS()
class MESHMODELINGTOOLS_API UUVProjectionToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** Choose the UV projection method (cube, cylinder, plane) */
	UPROPERTY(EditAnywhere, Category = ProjectionSettings)
	EUVProjectionMethod ProjectionType = EUVProjectionMethod::Plane;

	/** Per-axis Dimensions of the Projection. Z is height of box/cylinder. */
	UPROPERTY(EditAnywhere, Category = ProjectionSettings)
	FVector Dimensions = FVector(100.0f, 100.0f, 100.0f);

	/** How the Dimensions are interpreted for Box/Plane/ExpMap Projections */
	UPROPERTY(EditAnywhere, Category = ProjectionSettings)
	EUVProjectionToolDimensionMode DimensionMode = EUVProjectionToolDimensionMode::AllFree;

	/** Determines how projection settings will be initialized. Only affects Tool until changes are made to Dimensions/Position. */
	UPROPERTY(EditAnywhere, Category = ProjectionSettings)
	EUVProjectionToolInitializationMode Initialization = EUVProjectionToolInitializationMode::Default;

	//
	// UV-space transform options
	//

	/** UV-Space Rotation Angle in Degrees, applied after computing projected UVs */
	UPROPERTY(EditAnywhere, Category = "UV Transform", meta = (DisplayName = "UV Rotation") )
	float UVRotation = 0.0;

	/** UV-Space Scaling applied after computing projected UVs */
	UPROPERTY(EditAnywhere, Category = "UV Transform", meta = (DisplayName = "UV Scale"))
	FVector2D UVScale = FVector2D::UnitVector;

	/** UV-Space Translation applied after computing projected UVs */
	UPROPERTY(EditAnywhere, Category = "UV Transform", meta = (DisplayName = "UV Translation"))
	FVector2D UVTranslate = FVector2D::ZeroVector;


	//
	// Saved State. These are used internally to support UsePrevious initialization mode
	//
	UPROPERTY()
	FVector SavedDimensions = FVector::ZeroVector;

	UPROPERTY()
	FTransform SavedTransform;
};




/**
 * ExpMap Projection properties
 */
UCLASS()
class MESHMODELINGTOOLS_API UUVProjectionToolExpMapProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/** Blend between surface normals and projection normal - ExpMap becomes Planar Projection when value is 1 */
	UPROPERTY(EditAnywhere, Category = ExpMap, meta = (UIMin = "0", UIMax = "1"))
	float NormalBlending = 0.0;

	/** Rounds of Smoothing to apply to surface normals before computing ExpMap */
	UPROPERTY(EditAnywhere, Category = ExpMap, AdvancedDisplay, meta = (UIMin = "0", UIMax = "100"))
	int SmoothingRounds = 0;

	/** Smoothing Strength for ExpMap Smoothing */
	UPROPERTY(EditAnywhere, Category = ExpMap, AdvancedDisplay, meta = (UIMin = "0", UIMax = "1"))
	float SmoothingAlpha = 0.25f;
};



/**
 * Cylinder Projection properties
 */
UCLASS()
class MESHMODELINGTOOLS_API UUVProjectionToolCylinderProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/** Angle used to determine whether faces should be assigned to cylinder or flat endcaps */
	UPROPERTY(EditAnywhere, Category = Cylinder, meta = (UIMin = "0", UIMax = "90"))
	float SplitAngle = 45.0f;
};




/**
 * Factory with enough info to spawn the background-thread Operator to do a chunk of work for the tool
 *  stores a pointer to the tool and enough info to know which specific operator it should spawn
 */
UCLASS()
class MESHMODELINGTOOLS_API UUVProjectionOperatorFactory : public UObject, public UE::Geometry::IDynamicMeshOperatorFactory
{
	GENERATED_BODY()
public:
	// IDynamicMeshOperatorFactory API
	virtual TUniquePtr<UE::Geometry::FDynamicMeshOperator> MakeNewOperator() override;

	UPROPERTY()
	UUVProjectionTool *Tool;
};


/**
 * Simple Mesh Normal Updating Tool
 */
UCLASS()
class MESHMODELINGTOOLS_API UUVProjectionTool : public USingleSelectionMeshEditingTool
{
	GENERATED_BODY()

public:

	friend UUVProjectionOperatorFactory;

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void OnTick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override;

	virtual void RequestAction(EUVProjectionToolActions ActionType);

protected:

	UPROPERTY()
	UMeshUVChannelProperties* UVChannelProperties = nullptr;

	UPROPERTY()
	UUVProjectionToolProperties* BasicProperties = nullptr;

	UPROPERTY()
	UUVProjectionToolExpMapProperties* ExpMapProperties = nullptr;

	UPROPERTY()
	UUVProjectionToolCylinderProperties* CylinderProperties = nullptr;

	UPROPERTY()
	UUVProjectionToolEditActions* EditActions = nullptr;

	UPROPERTY()
	UExistingMeshMaterialProperties* MaterialSettings = nullptr;

	UPROPERTY()
	UMeshOpPreviewWithBackgroundCompute* Preview = nullptr;


	UPROPERTY()
	UMaterialInstanceDynamic* CheckerMaterial = nullptr;

	UPROPERTY()
	UTransformGizmo* TransformGizmo = nullptr;
	
	UPROPERTY()
	UTransformProxy* TransformProxy = nullptr;

	UPROPERTY()
	UUVProjectionOperatorFactory* OperatorFactory = nullptr;

	UPROPERTY()
	UPreviewGeometry* EdgeRenderer = nullptr;

protected:
	TSharedPtr<UE::Geometry::FDynamicMesh3, ESPMode::ThreadSafe> InputMesh;
	TSharedPtr<TArray<int32>, ESPMode::ThreadSafe> TriangleROI;
	TSharedPtr<TArray<int32>, ESPMode::ThreadSafe> VertexROI;
	TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3, ESPMode::ThreadSafe> InputMeshROISpatial;
	TSet<int32> TriangleROISet;

	UE::Geometry::FTransform3d WorldTransform;
	UE::Geometry::FAxisAlignedBox3d WorldBounds;

	FVector InitialDimensions;
	FTransform InitialTransform;
	int32 DimensionsWatcher = -1;
	bool bTransformModified = false;
	void OnInitializationModeChanged();
	void ApplyInitializationMode();

	FViewCameraState CameraState;

	FToolDataVisualizer ProjectionShapeVisualizer;

	void InitializeMesh();
	void UpdateNumPreviews();

	void TransformChanged(UTransformProxy* Proxy, FTransform Transform);

	void OnProjectionTypeChanged();
	void OnMaterialSettingsChanged();
	void OnMeshUpdated(UMeshOpPreviewWithBackgroundCompute* PreviewCompute);

	UE::Geometry::FOrientedBox3d GetProjectionBox();


	//
	// Support for ctrl+click to set plane from hit point
	//

	TUniquePtr<FSelectClickedAction> SetPlaneCtrlClickBehaviorTarget;

	UPROPERTY()
	USingleClickInputBehavior* ClickToSetPlaneBehavior;

	void UpdatePlaneFromClick(const FVector3d& Position, const FVector3d& Normal, bool bTransationOnly);

	//
	// Support for Action Buttons
	//

	bool bHavePendingAction = false;
	EUVProjectionToolActions PendingAction;
	virtual void ApplyAction(EUVProjectionToolActions ActionType);
	void ApplyAction_AutoFit(bool bAlign);
	void ApplyAction_Reset();

};
