// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "InteractiveGizmo.h"
#include "InteractiveToolBuilder.h"
#include "BaseTools/SingleClickTool.h"
#include "PreviewMesh.h"
#include "DynamicMeshAABBTree3.h"
#include "Snapping/PointPlanarSnapSolver.h"
#include "ToolSceneQueriesUtil.h"
#include "Properties/MeshMaterialProperties.h"
#include "Mechanics/PlaneDistanceFromHitMechanic.h"
#include "DrawPolygonTool.generated.h"


class UTransformGizmo;
class UTransformProxy;

/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UDrawPolygonToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IToolsContextAssetAPI* AssetAPI;

	UDrawPolygonToolBuilder() 
	{
		AssetAPI = nullptr;
	}

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};



/** Polygon Tool Draw Type */
UENUM()
enum class EDrawPolygonDrawMode : uint8
{
	/** Freehand Polygon Drawing */
	Freehand UMETA(DisplayName = "Freehand"),

	/** Circle */
	Circle UMETA(DisplayName = "Circle"),

	/** Square */
	Square UMETA(DisplayName = "Square"),

	/** Rectangle */
	Rectangle UMETA(DisplayName = "Rectangle"),

	/** Rounded Rectangle */
	RoundedRectangle UMETA(DisplayName = "Rounded Rectangle"),

	/** Circle w/ Hole */
	HoleyCircle UMETA(DisplayName = "Circle w/ Hole")

};


/** Output of Draw Polygon Tool */
UENUM()
enum class EDrawPolygonOutputMode : uint8
{
	/** Generate a meshed planar polygon */
	MeshedPolygon UMETA(DisplayName = "Flat Mesh"),

	/** Extrude closed polygon to constant height determined by Extrude Height Property */
	ExtrudedConstant UMETA(DisplayName = "Extrude To Height"),

	/** Extrusion height is set via additional mouse input after closing polygon */
	ExtrudedInteractive UMETA(DisplayName = "Interactive Extrude"),
};





UCLASS()
class MESHMODELINGTOOLS_API UDrawPolygonToolStandardProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UDrawPolygonToolStandardProperties();

	UPROPERTY(EditAnywhere, NonTransactional, Category = Polygon)
	EDrawPolygonDrawMode PolygonType = EDrawPolygonDrawMode::Freehand;

	UPROPERTY(EditAnywhere, NonTransactional, Category = Polygon)
	EDrawPolygonOutputMode OutputMode = EDrawPolygonOutputMode::ExtrudedInteractive;

	/** Feature size as fraction of overall shape size, for shapes with secondary features like the rounded corners of a Rounded Rectangle */
	UPROPERTY(EditAnywhere, NonTransactional, Category = Polygon, meta = (UIMin = "0.01", UIMax = "0.99", ClampMin = "0.01", ClampMax = "0.99",
									EditCondition = "PolygonType == EDrawPolygonDrawMode::RoundedRectangle || PolygonType == EDrawPolygonDrawMode::HoleyCircle", EditConditionHides))
	float FeatureSizeRatio = .25;

	/** Extrusion Distance in non-interactive mode */
	UPROPERTY(EditAnywhere, NonTransactional, Category = Polygon, meta = (UIMin = "-1000", UIMax = "1000", ClampMin = "-10000", ClampMax = "10000",
									EditCondition = "OutputMode == EDrawPolygonOutputMode::ExtrudedConstant", EditConditionHides))
	float ExtrudeHeight = 100.0f;

	/** Number of sections in round features */
	UPROPERTY(EditAnywhere, NonTransactional, Category = Polygon, meta = (UIMin = "3", UIMax = "100", ClampMin = "3", ClampMax = "10000",
				  EditCondition = "PolygonType == EDrawPolygonDrawMode::Circle || PolygonType == EDrawPolygonDrawMode::RoundedRectangle || PolygonType == EDrawPolygonDrawMode::HoleyCircle", EditConditionHides))
	int Steps = 16;

	UPROPERTY(EditAnywhere, NonTransactional, Category = Polygon, meta = (EditCondition = "PolygonType == EDrawPolygonDrawMode::Freehand", EditConditionHides))
	bool bAllowSelfIntersections = false;

	UPROPERTY(EditAnywhere, NonTransactional, Category = Polygon)
	bool bShowGizmo = true;
};

UCLASS()
class MESHMODELINGTOOLS_API UDrawPolygonToolSnapProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, NonTransactional, Category = Snapping)
	bool bEnableSnapping = true;

	UPROPERTY(EditAnywhere, NonTransactional, Category = Snapping, Meta = (EditCondition = "bEnableSnapping", EditConditionHides))
	bool bSnapToWorldGrid = false;

	UPROPERTY(EditAnywhere, NonTransactional, Category = Snapping, Meta = (EditCondition = "bEnableSnapping", EditConditionHides))
	bool bSnapToVertices = true;

	UPROPERTY(EditAnywhere, NonTransactional, Category = Snapping, Meta = (EditCondition = "bEnableSnapping", EditConditionHides))
	bool bSnapToEdges = false;

	UPROPERTY(EditAnywhere, NonTransactional, Category = Snapping, Meta = (EditCondition = "bEnableSnapping", EditConditionHides))
	bool bSnapToAngles = true;

	UPROPERTY(EditAnywhere, NonTransactional, Category = Snapping, Meta = (EditCondition = "bEnableSnapping", EditConditionHides))
	bool bSnapToLengths = true;

	UPROPERTY(VisibleAnywhere, NonTransactional, Category = Snapping, meta = (TransientToolProperty))
	float SegmentLength = 0.0f;

	UPROPERTY(EditAnywhere, NonTransactional, Category = Snapping)
	bool bHitSceneObjects = false;

	UPROPERTY(EditAnywhere, NonTransactional, Category = Snapping, Meta = (EditCondition = "bHitSceneObjects", EditConditionHides))
	float HitNormalOffset = 0.0f;
};





/**
 * This tool allows the user to draw and extrude 2D polygons
 */
UCLASS()
class MESHMODELINGTOOLS_API UDrawPolygonTool : public UInteractiveTool, public IClickSequenceBehaviorTarget
{
	GENERATED_BODY()

public:
	UDrawPolygonTool();

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

	// IClickSequenceBehaviorTarget implementation

	virtual void OnBeginSequencePreview(const FInputDeviceRay& ClickPos) override;
	virtual bool CanBeginClickSequence(const FInputDeviceRay& ClickPos) override;
	virtual void OnBeginClickSequence(const FInputDeviceRay& ClickPos) override;
	virtual void OnNextSequencePreview(const FInputDeviceRay& ClickPos) override;
	virtual bool OnNextSequenceClick(const FInputDeviceRay& ClickPos) override;
	virtual void OnTerminateClickSequence() override;
	virtual bool RequestAbortClickSequence() override;
	virtual void OnUpdateModifierState(int ModifierID, bool bIsOn) override;

	// polygon drawing functions
	virtual void ResetPolygon();
	virtual void UpdatePreviewVertex(const FVector3d& PreviewVertex);
	virtual void AppendVertex(const FVector3d& Vertex);
	virtual bool FindDrawPlaneHitPoint(const FInputDeviceRay& ClickPos, FVector3d& HitPosOut);
	virtual void EmitCurrentPolygon();

	virtual void BeginInteractiveExtrude();
	virtual void EndInteractiveExtrude();


public:
	virtual void ApplyUndoPoints(const TArray<FVector3d>& ClickPointsIn, const TArray<FVector3d>& PolygonVerticesIn);


protected:
	// flags used to identify modifier keys/buttons
	static const int IgnoreSnappingModifier = 1;
	static const int AngleSnapModifier = 2;

protected:

	/** Properties that control polygon generation exposed to user via detailsview */
	UPROPERTY()
	UDrawPolygonToolStandardProperties* PolygonProperties;

	UPROPERTY()
	UDrawPolygonToolSnapProperties* SnapProperties;

	UPROPERTY()
	UNewMeshMaterialProperties* MaterialProperties;
	

	/** Origin of plane we will draw polygon on */
	FVector3d DrawPlaneOrigin;

	/** Orientation of plane we will draw polygon on */
	FQuaterniond DrawPlaneOrientation;
	
	/** Vertices of current preview polygon */
	TArray<FVector3d> PolygonVertices;

	/** Vertices of holes in current preview polygon */
	TArray<TArray<FVector3d>> PolygonHolesVertices;

	/** last vertex of polygon that is actively being updated as input device is moved */
	FVector3d PreviewVertex;

protected:
	UWorld* TargetWorld;
	IToolsContextAssetAPI* AssetAPI;

	FViewCameraState CameraState;

	UPROPERTY()
	UPreviewMesh* PreviewMesh;

	
	// drawing plane gizmo

	UPROPERTY()
	UTransformGizmo* PlaneTransformGizmo;

	UPROPERTY()
	UTransformProxy* PlaneTransformProxy;

	// called on PlaneTransformProxy.OnTransformChanged
	void PlaneTransformChanged(UTransformProxy* Proxy, FTransform Transform);

	// calls SetDrawPlaneFromWorldPos when user ctrl+clicks on scene
	IClickBehaviorTarget* SetPointInWorldConnector = nullptr;

	// updates plane and gizmo position
	virtual void SetDrawPlaneFromWorldPos(const FVector3d& Position, const FVector3d& Normal);

	void UpdateShowGizmoState(bool bNewVisibility);


	// polygon drawing

	bool bAbortActivePolygonDraw;

	bool bInFixedPolygonMode = false;
	TArray<FVector3d> FixedPolygonClickPoints;

	// can close poly if current segment intersects existing segment
	bool UpdateSelfIntersection();
	bool bHaveSelfIntersection;
	int SelfIntersectSegmentIdx;
	FVector3d SelfIntersectionPoint;

	// only used when SnapSettings.bHitSceneObjects = true
	bool bHaveSurfaceHit;
	FVector3d SurfaceHitPoint;
	FVector3d SurfaceOffsetPoint;

	bool bIgnoreSnappingToggle = false;		// toggled by hotkey (shift)
	FPointPlanarSnapSolver SnapEngine;
	ToolSceneQueriesUtil::FSnapGeometry LastSnapGeometry;
	FVector3d LastGridSnapPoint;

	void GetPolygonParametersFromFixedPoints(const TArray<FVector3d>& FixedPoints, FVector2d& FirstReferencePt, FVector2d& BoxSize, double& YSign, double& AngleRad);
	void GenerateFixedPolygon(const TArray<FVector3d>& FixedPoints, TArray<FVector3d>& VerticesOut, TArray<TArray<FVector3d>>& HolesVerticesOut);


	// extrusion control

	bool bInInteractiveExtrude = false;

	void UpdateLivePreview();
	bool bPreviewUpdatePending;

	UPROPERTY()
	UPlaneDistanceFromHitMechanic* HeightMechanic;


	FFrame3f HitPosFrameWorld;

	/** Generate extruded meshes.  Returns true on success. */
	bool GeneratePolygonMesh(const TArray<FVector3d>& Polygon, const TArray<TArray<FVector3d>>& PolygonHoles, FDynamicMesh3* ResultMeshOut, FFrame3d& WorldFrameOut, bool bIncludePreviewVtx, double ExtrudeDistance, bool bExtrudeSymmetric);


	// user feedback messages
	void ShowStartupMessage();
	void ShowExtrudeMessage();


	friend class FDrawPolygonStateChange;
	int32 CurrentCurveTimestamp = 1;
	void UndoCurrentOperation(const TArray<FVector3d>& ClickPointsIn, const TArray<FVector3d>& PolygonVerticesIn);
	bool CheckInCurve(int32 Timestamp) const { return CurrentCurveTimestamp == Timestamp; }
};



// Change event used by DrawPolygonTool to undo draw state.
// Currently does not redo.
class MESHMODELINGTOOLS_API FDrawPolygonStateChange : public FToolCommandChange
{
public:
	using Points = TArray<FVector3d>;
	bool bHaveDoneUndo = false;
	int32 CurveTimestamp = 0;
	const Points FixedVertexPoints;
	const Points PolyPoints;

	FDrawPolygonStateChange(int32 CurveTimestampIn,
							const Points& FixedVertexPointsIn,
							const Points& PolyPointsIn)
		: CurveTimestamp(CurveTimestampIn),
		FixedVertexPoints(FixedVertexPointsIn),
		PolyPoints(PolyPointsIn)
	{
	}
	virtual void Apply(UObject* Object) override {}
	virtual void Revert(UObject* Object) override;
	virtual bool HasExpired(UObject* Object) const override;
	virtual FString ToString() const override;
};
