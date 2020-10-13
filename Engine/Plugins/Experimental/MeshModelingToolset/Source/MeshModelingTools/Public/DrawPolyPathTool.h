// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "BaseTools/MeshSurfacePointTool.h"
#include "Mechanics/PlaneDistanceFromHitMechanic.h"
#include "Mechanics/SpatialCurveDistanceMechanic.h"
#include "Mechanics/CollectSurfacePathMechanic.h"
#include "Mechanics/ConstructionPlaneMechanic.h"
#include "Drawing/PolyEditPreviewMesh.h"
#include "Properties/MeshMaterialProperties.h"
#include "DrawPolyPathTool.generated.h"

class FMeshVertexChangeBuilder;
class UTransformGizmo;
class UTransformProxy;

/**
 * ToolBuilder
 */
UCLASS()
class MESHMODELINGTOOLS_API UDrawPolyPathToolBuilder : public UMeshSurfacePointToolBuilder
{
	GENERATED_BODY()
public:
	IToolsContextAssetAPI* AssetAPI = nullptr;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;

	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};


UENUM()
enum class EDrawPolyPathOutputMode
{
	Ribbon,
	Extrusion,
	Ramp
};



UENUM()
enum class EDrawPolyPathWidthMode
{
	Interactive,
	Constant
};

UENUM()
enum class EDrawPolyPathHeightMode
{
	Interactive,
	Constant
};



UCLASS()
class MESHMODELINGTOOLS_API UDrawPolyPathProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Shape)
	EDrawPolyPathOutputMode OutputType = EDrawPolyPathOutputMode::Extrusion;

	UPROPERTY(EditAnywhere, Category = Shape)
	EDrawPolyPathWidthMode WidthMode = EDrawPolyPathWidthMode::Interactive;

	UPROPERTY(EditAnywhere, Category = Shape, meta = (UIMin = "0.0001", UIMax = "1000", ClampMin = "0", ClampMax = "999999"))
	float Width = 10.0f;

	UPROPERTY(EditAnywhere, Category = Shape, meta = (
		EditCondition = "OutputType != EDrawPolyPathOutputMode::Ribbon", EditConditionHides))
	EDrawPolyPathHeightMode HeightMode = EDrawPolyPathHeightMode::Interactive;

	UPROPERTY(EditAnywhere, Category = Shape, meta = (
		EditCondition = "OutputType != EDrawPolyPathOutputMode::Ribbon", 
		EditConditionHides, UIMin = "-1000", UIMax = "1000", ClampMin = "-10000", ClampMax = "10000"))
	float Height = 10.0f;

	UPROPERTY(EditAnywhere, Category = Shape, meta = (
		EditCondition = "OutputType == EDrawPolyPathOutputMode::Ramp", EditConditionHides,
		UIMin = "0.01", UIMax = "1.0", ClampMin = "0", ClampMax = "100.0"))
	float RampStartRatio = 0.05f;


	UPROPERTY(EditAnywhere, Category = Snapping)
	bool bSnapToWorldGrid = true;
};



UENUM()
enum class EDrawPolyPathExtrudeDirection
{
	SelectionNormal,
	WorldX,
	WorldY,
	WorldZ,
	LocalX,
	LocalY,
	LocalZ
};


UCLASS()
class MESHMODELINGTOOLS_API UDrawPolyPathExtrudeProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Extrude)
	EDrawPolyPathExtrudeDirection Direction = EDrawPolyPathExtrudeDirection::SelectionNormal;


};



/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UDrawPolyPathTool : public UInteractiveTool, public IClickBehaviorTarget, public IHoverBehaviorTarget
{
	GENERATED_BODY()

public:
	virtual void RegisterActions(FInteractiveToolActionSet& ActionSet) override;

	virtual void SetWorld(UWorld* World);
	virtual void SetAssetAPI(IToolsContextAssetAPI* AssetAPI);

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void OnTick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return false; }
	virtual bool HasAccept() const override { return false; }
	virtual bool CanAccept() const override { return false; }

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
	UWorld* TargetWorld;
	IToolsContextAssetAPI* AssetAPI;

	UPROPERTY()
	UDrawPolyPathProperties* TransformProps;

	UPROPERTY()
	UDrawPolyPathExtrudeProperties* ExtrudeProperties;

	UPROPERTY()
	UNewMeshMaterialProperties* MaterialProperties;

protected:
	// camera state at last render
	FTransform3d WorldTransform;
	FViewCameraState CameraState;

	// drawing plane and gizmo

	UPROPERTY()
	UConstructionPlaneMechanic* PlaneMechanic = nullptr;

	FFrame3d DrawPlaneWorld;

	bool CanUpdateDrawPlane() const;

	// UV Scale factor to apply to texturing on any new geometry (e.g. new faces added by extrude)
	float UVScaleFactor = 1.0f;

	TArray<FFrame3d> CurPathPoints;
	TArray<double> OffsetScaleFactors;
	TArray<double> ArcLengths;
	TArray<FVector3d> CurPolyLine;
	double CurPathLength;
	double CurOffsetDistance;
	double CurHeight;
	bool bPathIsClosed = false;		// If true, CurPathPoints are assumed to define a closed path

	UPROPERTY()
	UPolyEditPreviewMesh* EditPreview;

	UPROPERTY()
	UPlaneDistanceFromHitMechanic* ExtrudeHeightMechanic = nullptr;
	UPROPERTY()
	USpatialCurveDistanceMechanic* CurveDistMechanic = nullptr;
	UPROPERTY()
	UCollectSurfacePathMechanic* SurfacePathMechanic = nullptr;

	void InitializeNewSurfacePath();
	void UpdateSurfacePathPlane();
	void OnCompleteSurfacePath();
	void OnCompleteOffsetDistance();
	void OnCompleteExtrudeHeight();

	void BeginInteractiveOffsetDistance();
	void BeginConstantOffsetDistance();
	void UpdatePathPreview();
	void BeginInteractiveExtrudeHeight();
	void UpdateExtrudePreview();
	void InitializePreviewMesh();
	void ClearPreview();
	void GeneratePathMesh(FDynamicMesh3& Mesh);
	void GenerateExtrudeMesh(FDynamicMesh3& PathMesh);
	void GenerateRampMesh(FDynamicMesh3& PathMesh);
	void EmitNewObject(EDrawPolyPathOutputMode OutputMode);

	// user feedback messages
	void ShowStartupMessage();
	void ShowExtrudeMessage();
	void ShowOffsetMessage();

	friend class FDrawPolyPathStateChange;
	int32 CurrentCurveTimestamp = 1;
	void UndoCurrentOperation();
	bool CheckInCurve(int32 Timestamp) const { return CurrentCurveTimestamp == Timestamp; }
};




class MESHMODELINGTOOLS_API FDrawPolyPathStateChange : public FToolCommandChange
{
public:
	bool bHaveDoneUndo = false;
	int32 CurveTimestamp = 0;
	FDrawPolyPathStateChange(int32 CurveTimestampIn)
	{
		CurveTimestamp = CurveTimestampIn;
	}
	virtual void Apply(UObject* Object) override {}
	virtual void Revert(UObject* Object) override;
	virtual bool HasExpired(UObject* Object) const override;
	virtual FString ToString() const override;
};
