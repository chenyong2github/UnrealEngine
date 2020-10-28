// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/PimplPtr.h"
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
#include "Solvers/MeshLaplacian.h"
#include "Curves/CurveFloat.h"
#include "DeformMeshPolygonsTool.generated.h"

class FMeshVertexChangeBuilder;
class FGroupTopologyLaplacianDeformer;
struct FDeformerVertexConstraintData;

/**
 * ToolBuilder
 */
UCLASS()
class MESHMODELINGTOOLS_API UDeformMeshPolygonsToolBuilder : public UMeshSurfacePointToolBuilder
{
	GENERATED_BODY()

public:
	UDeformMeshPolygonsToolBuilder()
	{
	}

	virtual UMeshSurfacePointTool* CreateNewTool(const FToolBuilderState& SceneState) const override;
};

/** Deformation strategies */
UENUM()
enum class EGroupTopologyDeformationStrategy : uint8
{
	/** Deforms the mesh using linear translations*/
	Linear UMETA(DisplayName = "Linear"),

	/** Deforms the mesh using laplacian deformation*/
	Laplacian UMETA(DisplayName = "Smooth")
};

/** Laplacian weight schemes determine how we will look at the curvature at a given vertex in relation to its neighborhood*/
UENUM()
enum class EWeightScheme 
{
	Uniform				UMETA(DisplayName = "Uniform"),
	Umbrella			UMETA(DisplayName = "Umbrella"),
	Valence				UMETA(DisplayName = "Valence"),
	MeanValue			UMETA(DisplayName = "MeanValue"),
	Cotangent			UMETA(DisplayName = "Cotangent"),
	ClampedCotangent	UMETA(DisplayName = "ClampedCotangent")
};

/** The ELaplacianWeightScheme enum is the same..*/
static ELaplacianWeightScheme ConvertToLaplacianWeightScheme(const EWeightScheme WeightScheme)
{
	return static_cast<ELaplacianWeightScheme>(WeightScheme);
}


/** Modes for quick transformer */
UENUM()
enum class EQuickTransformerMode : uint8
{
	/** Translation along frame axes */
	AxisTranslation = 0 UMETA(DisplayName = "Translate"),

	/** Rotation around frame axes*/
	AxisRotation = 1 UMETA(DisplayName = "Rotate"),
};




UCLASS()
class MESHMODELINGTOOLS_API UDeformMeshPolygonsTransformProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UDeformMeshPolygonsTransformProperties();

	//Options

	UPROPERTY(EditAnywhere, Category = Options, meta = (DisplayName = "Deformation Type", ToolTip = "Select the type of deformation you wish to employ on a polygroup."))
	EGroupTopologyDeformationStrategy DeformationStrategy;

	UPROPERTY(EditAnywhere, Category = Options)
	EQuickTransformerMode TransformMode;

	UPROPERTY(EditAnywhere, Category = SelectionFilter)
	bool bSelectFaces;

	UPROPERTY(EditAnywhere, Category = SelectionFilter)
	bool bSelectEdges;

	UPROPERTY(EditAnywhere, Category = SelectionFilter)
	bool bSelectVertices;

	UPROPERTY(EditAnywhere, Category = Snapping)
	bool bSnapToWorldGrid;

	UPROPERTY(EditAnywhere, Category = Display)
	bool bShowWireframe;

	//Laplacian Deformation Options, currently not exposed.

	UPROPERTY() 
	EWeightScheme SelectedWeightScheme = EWeightScheme::ClampedCotangent;

	UPROPERTY() 
	double HandleWeight = 1000.0;

	UPROPERTY()
	bool bPostFixHandles = false;
/**
	// How to add a weight curve
	UPROPERTY(EditAnywhere, Category = LaplacianOptions, meta = (EditCondition="DeformationStrategy == EGroupTopologyDeformationStrategy::Laplacian", DisplayName = "Localize Deformation", ToolTip = "When enabled, only the vertices in the polygroups immediately adjacent to the selected group will be affected by the deformation.\nWhen disabled, the deformer will solve for the curvature of the entire mesh (slower)"))
	bool bLocalizeDeformation{ true};

	UPROPERTY(EditAnywhere, Category = LaplacianOptions, meta = (EditCondition="DeformationStrategy == EGroupTopologyDeformationStrategy::Laplacian", DisplayName = "Apply Weight Attenuation Curve", ToolTip = "When enabled, the curve below will be used to calculate the weight at a given vertex based on distance from the handles"))
	bool bApplyAttenuationCurve{ false };
	
	FRichCurve DefaultFalloffCurve;

	UPROPERTY(EditAnywhere,  Category = LaplacianOptions, meta = ( EditCondition="DeformationStrategy == EGroupTopologyDeformationStrategy::Laplacian && bApplyAttenuationCurve", DisplayName = "Distance-Weight Attenuation Curve",UIMin = "1.0", UIMax = "1.0", ClampMin = "1.0", ClampMax = "1.0", ToolTip = "This curve determines the weight attenuation over the distance of the mesh.\nThe selected polygroup handle is t=0.0, and t=1.0 is roughly the farthest vertices from the handles.\nThe value of the curve at each time interval represents the weight of the vertices at that distance from the selection."))
	FRuntimeFloatCurve WeightAttenuationCurve;
*/ 
};


/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UDeformMeshPolygonsTool : public UMeshSurfacePointTool
{
	GENERATED_BODY()

public:
	UDeformMeshPolygonsTool();

	virtual void RegisterActions(FInteractiveToolActionSet& ActionSet) override;

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void OnTick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }

	virtual bool HitTest(const FRay& Ray, FHitResult& OutHit) override;
	
	virtual void OnBeginDrag(const FRay& Ray) override;
	virtual void OnUpdateDrag(const FRay& Ray) override;
	virtual void OnEndDrag(const FRay& Ray) override;
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;

public:
	virtual void NextTransformTypeAction();

	//
	float VisualAngleSnapThreshold = 0.5;

protected:
	UPROPERTY()
	USimpleDynamicMeshComponent* DynamicMeshComponent;

	UPROPERTY()
	UDeformMeshPolygonsTransformProperties* TransformProps;

	// realtime visualization
	void OnDynamicMeshComponentChanged();
	FDelegateHandle OnDynamicMeshComponentChangedHandle;

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;
	
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

	FGroupTopology Topology;
	void PrecomputeTopology();

	FGroupTopologySelector TopoSelector;
	FGroupTopologySelector::FSelectionSettings GetTopoSelectorSettings();
	
	//
	// data for current drag
	//

	FGroupTopologySelection HilightSelection;
	FToolDataVisualizer HilightRenderer;

	FDynamicMeshAABBTree3 MeshSpatial;
	FDynamicMeshAABBTree3& GetSpatial();

	FMeshVertexChangeBuilder* ActiveVertexChange;

	EGroupTopologyDeformationStrategy DeformationStrategy;

	// The two deformer type options.
	FGroupTopologyDeformer LinearDeformer;
	TPimplPtr<FGroupTopologyLaplacianDeformer> LaplacianDeformer;



	// This is true when the spatial index needs to reflect a modification
	bool bSpatialDirty; 

	void BeginChange();
	void EndChange();
	void UpdateChangeFromROI(bool bFinal);

};

