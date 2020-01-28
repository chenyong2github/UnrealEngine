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
#include "Changes/ValueWatcher.h"
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
	UPROPERTY(EditAnywhere, NonTransactional, Category = Polygon, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1", 
									EditCondition = "PolygonType == EDrawPolygonDrawMode::RoundedRectangle || PolygonType == EDrawPolygonDrawMode::HoleyCircle"))
	float FeatureSizeRatio = .25;

	/** Extrusion Distance in non-interactive mode */
	UPROPERTY(EditAnywhere, NonTransactional, Category = Polygon, meta = (UIMin = "-1000", UIMax = "1000", ClampMin = "-10000", ClampMax = "10000"))
	float ExtrudeHeight = 100.0f;

	/** Extrusion Distance in non-interactive mode */
	UPROPERTY(EditAnywhere, NonTransactional, Category = Polygon, meta = (UIMin = "3", UIMax = "100", ClampMin = "3", ClampMax = "10000"))
	int Steps = 16;

	UPROPERTY(EditAnywhere, NonTransactional, Category = Polygon)
	bool bAllowSelfIntersections = false;

	UPROPERTY(EditAnywhere, NonTransactional, Category = Polygon)
	bool bShowGizmo = true;

	//
	// save/restore support
	//
	virtual void SaveProperties(UInteractiveTool* SaveFromTool) override;
	virtual void RestoreProperties(UInteractiveTool* RestoreToTool) override;
};




UCLASS()
class MESHMODELINGTOOLS_API UDrawPolygonToolSnapProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, NonTransactional, Category = Snapping)
	bool bEnableSnapping = true;

	UPROPERTY(EditAnywhere, NonTransactional, Category = Snapping, Meta = (EditCondition = "bEnableSnapping"))
	bool bSnapToWorldGrid = false;

	UPROPERTY(EditAnywhere, NonTransactional, Category = Snapping, Meta = (EditCondition = "bEnableSnapping"))
	bool bSnapToVertices = true;

	UPROPERTY(EditAnywhere, NonTransactional, Category = Snapping, Meta = (EditCondition = "bEnableSnapping"))
	bool bSnapToEdges = false;

	UPROPERTY(EditAnywhere, NonTransactional, Category = Snapping, Meta = (EditCondition = "bEnableSnapping"))
	bool bSnapToAngles = true;

	UPROPERTY(EditAnywhere, NonTransactional, Category = Snapping, Meta = (EditCondition = "bEnableSnapping"))
	bool bSnapToLengths = true;

	UPROPERTY(VisibleAnywhere, NonTransactional, Category = Snapping)
	float SegmentLength = 0.0f;

	UPROPERTY(EditAnywhere, NonTransactional, Category = Snapping)
	bool bHitSceneObjects = false;

	UPROPERTY(EditAnywhere, NonTransactional, Category = Snapping, Meta = (EditCondition = "bHitSceneObjects"))
	float HitNormalOffset = 0.0f;

	//
	// save/restore support
	//
	virtual void SaveProperties(UInteractiveTool* SaveFromTool) override;
	virtual void RestoreProperties(UInteractiveTool* RestoreToTool) override;
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

	virtual void Tick(float DeltaTime) override;
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
	virtual void UpdatePreviewVertex(const FVector& PreviewVertex);
	virtual void AppendVertex(const FVector& Vertex);
	virtual bool FindDrawPlaneHitPoint(const FInputDeviceRay& ClickPos, FVector& HitPosOut);
	virtual void EmitCurrentPolygon();

	virtual void BeginInteractiveExtrude();
	virtual void EndInteractiveExtrude();
	virtual float FindInteractiveHeightDistance(const FInputDeviceRay& ClickPos);


public:
	virtual void PopLastVertexAction();


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
	UPROPERTY()
	FVector DrawPlaneOrigin;

	/** Orientation of plane we will draw polygon on */
	UPROPERTY()
	FQuat DrawPlaneOrientation;
	
	/** Vertices of current preview polygon */
	UPROPERTY()
	TArray<FVector> PolygonVertices;

	/** Vertices of holes in current preview polygon */
	TArray<TArray<FVector>> PolygonHolesVertices;

	/** last vertex of polygon that is actively being updated as input device is moved */
	UPROPERTY()
	FVector PreviewVertex;

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
	virtual void SetDrawPlaneFromWorldPos(const FVector& Position, const FVector& Normal);

	TValueWatcher<bool> ShowGizmoWatcher;
	void UpdateShowGizmoState(bool bNewVisibility);


	// polygon drawing

	bool bAbortActivePolygonDraw;

	bool bInFixedPolygonMode = false;
	TArray<FVector> FixedPolygonClickPoints;

	// can close poly if current segment intersects existing segment
	bool UpdateSelfIntersection();
	bool bHaveSelfIntersection;
	int SelfIntersectSegmentIdx;
	FVector3f SelfIntersectionPoint;

	// only used when SnapSettings.bHitSceneObjects = true
	bool bHaveSurfaceHit;
	FVector3f SurfaceHitPoint;
	FVector3f SurfaceOffsetPoint;

	bool bIgnoreSnappingToggle = false;		// toggled by hotkey (shift)
	FPointPlanarSnapSolver SnapEngine;
	ToolSceneQueriesUtil::FSnapGeometry LastSnapGeometry;
	FVector3d LastGridSnapPoint;

	void GetPolygonParametersFromFixedPoints(const TArray<FVector>& FixedPoints, FVector2f& FirstReferencePt, FVector2f& BoxSize, float& YSign, float& AngleRad);
	void GenerateFixedPolygon(const TArray<FVector>& FixedPoints, TArray<FVector>& VerticesOut, TArray<TArray<FVector>>& HolesVerticesOut);


	// extrusion control

	bool bInInteractiveExtrude = false;

	void UpdateLivePreview();
	bool bPreviewUpdatePending;

	FDynamicMesh3 PreviewHeightTarget;
	FDynamicMeshAABBTree3 PreviewHeightTargetAABB;
	FFrame3d PreviewHeightFrame;
	void GeneratePreviewHeightTarget();


	FFrame3f HitPosFrameWorld;

	/** Generate extruded meshes.  Returns true on success. */
	bool GeneratePolygonMesh(const TArray<FVector>& Polygon, const TArray<TArray<FVector>>& PolygonHoles, FDynamicMesh3* ResultMeshOut, FFrame3d& WorldFrameOut, bool bIncludePreviewVtx, double ExtrudeDistance, bool bExtrudeSymmetric);


	// user feedback messages
	void ShowStartupMessage();
	void ShowExtrudeMessage();
};





