// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "InteractiveGizmo.h"
#include "SingleSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "MeshOpPreviewHelpers.h"
#include "CuttingOps/EmbedPolygonsOp.h"
#include "DynamicMesh3.h"
#include "BaseTools/SingleClickTool.h"
#include "Mechanics/ConstructionPlaneMechanic.h"
#include "Mechanics/CollectSurfacePathMechanic.h"
#include "Polygon2.h"
#include "PolygonOnMeshTool.generated.h"


// predeclarations
struct FMeshDescription;
class USimpleDynamicMeshComponent;
class UTransformGizmo;
class UTransformProxy;
class ULineSetComponent;




/**
 *
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UPolygonOnMeshToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};



UENUM()
enum class EPolygonType
{
	Circle,
	Square,
	Rectangle,
	RoundRect,
	Custom
};




/**
 * Standard properties of the polygon-on-mesh operations
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UPolygonOnMeshToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** What operation to apply using the Polygon */
	UPROPERTY(EditAnywhere, Category = Operation)
	EEmbeddedPolygonOpMethod Operation = EEmbeddedPolygonOpMethod::CutThrough;

	/** Polygon Shape to use in this Operation */
	UPROPERTY(EditAnywhere, Category = Shape)
	EPolygonType Shape = EPolygonType::Circle;

	// TODO: re-add if/when extrude is added as a supported operation
	///** Amount to extrude, if extrude is enabled */
	//UPROPERTY(EditAnywhere, Category = Options)
	//float ExtrudeDistance;

	/** Scale of polygon to embed */
	UPROPERTY(EditAnywhere, Category = Shape, meta = (UIMin = "0.01", UIMax = "10.0", ClampMin = "0.00001", ClampMax = "10000"))
	float PolygonScale = 1.0f;

	/** Width of Polygon */
	UPROPERTY(EditAnywhere, Category = Shape, meta = (UIMin = "0.001", UIMax = "1000.0", ClampMin = "0.00001", ClampMax = "10000"))
	float Width = 100.0f;
		
	/** Height of Polygon */
	UPROPERTY(EditAnywhere, Category = Shape, meta = (UIMin = "0.001", UIMax = "1000.0", ClampMin = "0.00001", ClampMax = "10000", EditCondition = "Shape == EPolygonType::Rectangle || Shape == EPolygonType::RoundRect"))
	float Height = 50.0f;

	/** Corner Ratio of RoundRect Polygon */
	UPROPERTY(EditAnywhere, Category = Shape, meta = (UIMin = "0.0", UIMax = "1.0", EditCondition = "Shape == EPolygonType::RoundRect"))
	float CornerRatio = 0.5f;

	/** Number of sides in Circle or RoundRect Corner */
	UPROPERTY(EditAnywhere, Category = Shape, meta = (UIMin = "3", UIMax = "20", ClampMin = "3", ClampMax = "10000", EditCondition = "Shape == EPolygonType::Circle || Shape == EPolygonType::RoundRect"))
	int32 Subdivisions = 12;
};




UENUM()
enum class EPolygonOnMeshToolActions
{
	NoAction,
	DrawPolygon
};


UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UPolygonOnMeshToolActionPropertySet : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<UPolygonOnMeshTool> ParentTool;

	void Initialize(UPolygonOnMeshTool* ParentToolIn) { ParentTool = ParentToolIn; }

	void PostAction(EPolygonOnMeshToolActions Action);

	/** Extrude the current set of selected faces. Click in viewport to confirm extrude height. */
	UFUNCTION(CallInEditor, Category = Actions, meta = (DisplayName = "Draw Polygon", DisplayPriority = 1))
	void DrawPolygon()
	{
		PostAction(EPolygonOnMeshToolActions::DrawPolygon);
	}
};




/**
 * Simple Mesh Plane Cutting Tool
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UPolygonOnMeshTool : public USingleSelectionTool, public IDynamicMeshOperatorFactory, public IClickBehaviorTarget, public IHoverBehaviorTarget
{
	GENERATED_BODY()

public:

	UPolygonOnMeshTool();

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void SetWorld(UWorld* World);

	virtual void OnTick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override;

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;

	// IDynamicMeshOperatorFactory API
	virtual TUniquePtr<FDynamicMeshOperator> MakeNewOperator() override;


	virtual void RequestAction(EPolygonOnMeshToolActions ActionType);



public:
	// support for hover and click, for drawing polygon. This should be in the UCollectSurfacePathMechanic,
	// but we don't support dynamically changing input behavior set yet

	virtual bool HitTest(const FRay& Ray, FHitResult& OutHit);

	// IClickBehaviorTarget API
	virtual FInputRayHit IsHitByClick(const FInputDeviceRay& ClickPos) override;
	virtual void OnClicked(const FInputDeviceRay& ClickPos) override;

	// IHoverBehaviorTarget API
	virtual FInputRayHit BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos) override;
	virtual void OnBeginHover(const FInputDeviceRay& DevicePos) override {}
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;
	virtual void OnEndHover() override {}


protected:

	UPROPERTY()
	UPolygonOnMeshToolProperties* BasicProperties;

	UPROPERTY()
	UPolygonOnMeshToolActionPropertySet* ActionProperties;

	UPROPERTY()
	UMeshOpPreviewWithBackgroundCompute* Preview;

	UPROPERTY()
	ULineSetComponent* DrawnLineSet;

	TArray<int> EmbeddedEdges;
	bool bEmbedSucceeded;

protected:
	UWorld* TargetWorld;

	FTransform3d WorldTransform;
	FViewCameraState CameraState;

	TSharedPtr<FDynamicMesh3> OriginalDynamicMesh;

	UPROPERTY()
	UConstructionPlaneMechanic* PlaneMechanic = nullptr;

	UPROPERTY()
	UCollectSurfacePathMechanic* DrawPolygonMechanic = nullptr;

	EPolygonOnMeshToolActions PendingAction = EPolygonOnMeshToolActions::NoAction;

	FFrame3d DrawPlaneWorld;

	FPolygon2d LastDrawnPolygon;
	FPolygon2d ActivePolygon;
	void UpdatePolygonType();

	void SetupPreview();
	void UpdateDrawPlane();

	void BeginDrawPolygon();
	void CompleteDrawPolygon();

	void UpdateVisualization();

	void GenerateAsset(const TArray<FDynamicMeshOpResult>& Results);
};
