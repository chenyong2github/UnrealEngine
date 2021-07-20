// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "ModelingOperators.h" //IDynamicMeshOperatorFactory
#include "InteractiveTool.h" //UInteractiveToolPropertySet
#include "InteractiveToolBuilder.h" //UInteractiveToolBuilder
#include "InteractiveToolChange.h" //FToolCommandChange
#include "MeshOpPreviewHelpers.h" //FDynamicMeshOpResult
#include "Selection/GroupTopologySelector.h"
#include "SingleSelectionTool.h"
#include "ToolDataVisualizer.h"
#include "BaseTools/SingleSelectionMeshEditingTool.h"

#include "EdgeLoopInsertionTool.generated.h"

PREDECLARE_USE_GEOMETRY_CLASS(FDynamicMeshChange);

UCLASS()
class MESHMODELINGTOOLS_API UEdgeLoopInsertionToolBuilder : public USingleSelectionMeshEditingToolBuilder
{
	GENERATED_BODY()

public:
	virtual USingleSelectionMeshEditingTool* CreateNewTool(const FToolBuilderState& SceneState) const override;
};

UENUM()
enum class EEdgeLoopPositioningMode
{
	/** Edge loops will be evenly centered within a group. Allows for multiple insertions at a time. */
	Even,

	/** Edge loops will fall at the same length proportion at each edge they intersect (e.g., a quarter way down). */
	ProportionOffset,

	/** Edge loops will fall a constant distance away from the start of each edge they intersect 
	 (e.g., 20 units down). Clamps to end if edge is too short. */
	DistanceOffset
};

UENUM()
enum class EEdgeLoopInsertionMode
{
	/** Existing groups will be deleted and new triangles will be created for the new groups. 
	 Keeps topology simple but breaks non-planar groups. */
	Retriangulate,

	/** Keeps existing triangles and cuts them to create a new path. May result in fragmented triangles over time.*/
	PlaneCut
};

UCLASS()
class MESHMODELINGTOOLS_API UEdgeLoopInsertionProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** Determines how edge loops position themselves vertically relative to loop direction. */
	UPROPERTY(EditAnywhere, Category = InsertEdgeLoop)
	EEdgeLoopPositioningMode PositionMode = EEdgeLoopPositioningMode::ProportionOffset;

	/** Determines how edge loops are added to the geometry */
	UPROPERTY(EditAnywhere, Category = InsertEdgeLoop)
	EEdgeLoopInsertionMode InsertionMode = EEdgeLoopInsertionMode::Retriangulate;

	/** How many loops to insert at a time. Only used with "even" positioning mode. */
	UPROPERTY(EditAnywhere, Category = InsertEdgeLoop, meta = (UIMin = "0", UIMax = "20", ClampMin = "0", ClampMax = "500", 
		EditCondition = "PositionMode == EEdgeLoopPositioningMode::Even", EditConditionHides))
	int32 NumLoops = 1;

	UPROPERTY(EditAnywhere, Category = InsertEdgeLoop, AdvancedDisplay, meta = (UIMin = "0", UIMax = "1.0", ClampMin = "0", ClampMax = "1.0",
		EditCondition = "PositionMode == EEdgeLoopPositioningMode::ProportionOffset && !bInteractive", EditConditionHides))
	double ProportionOffset = 0.5;

	UPROPERTY(EditAnywhere, Category = InsertEdgeLoop, AdvancedDisplay, meta = (UIMin = "0", ClampMin = "0",
		EditCondition = "PositionMode == EEdgeLoopPositioningMode::DistanceOffset && !bInteractive", EditConditionHides))
	double DistanceOffset = 10.0;

	/** When false, the distance/proportion offset is numerically specified, and mouse clicks just choose the edge. */
	UPROPERTY(EditAnywhere, Category = InsertEdgeLoop, AdvancedDisplay, meta = (
		EditCondition = "PositionMode != EEdgeLoopPositioningMode::Even", EditConditionHides))
	bool bInteractive = true;

	/** Measure the distance offset from the opposite side of the edges. */
	UPROPERTY(EditAnywhere, Category = InsertEdgeLoop, meta = (
		EditCondition = "PositionMode == EEdgeLoopPositioningMode::DistanceOffset", EditConditionHides))
	bool bFlipOffsetDirection = false;

	UPROPERTY(EditAnywhere, Category = InsertEdgeLoop)
	bool bWireframe = true;

	/** When true, non-quad-like groups that stop the loop will be highlighted, with X's marking the corners. */
	UPROPERTY(EditAnywhere, Category = InsertEdgeLoop)
	bool bHighlightProblemGroups = true;

	/** How close a new loop edge needs to pass next to an existing vertex to use that vertex rather than creating a new one. */
	UPROPERTY(EditAnywhere, Category = InsertEdgeLoop, AdvancedDisplay)
	double VertexTolerance = 0.001;
};

UCLASS()
class MESHMODELINGTOOLS_API UEdgeLoopInsertionOperatorFactory : public UObject, public UE::Geometry::IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:
	// IDynamicMeshOperatorFactory API
	virtual TUniquePtr<UE::Geometry::FDynamicMeshOperator> MakeNewOperator() override;

	UPROPERTY()
	TObjectPtr<UEdgeLoopInsertionTool> Tool;
};

/** Tool for inserting (group) edge loops into a mesh. */
UCLASS()
class MESHMODELINGTOOLS_API UEdgeLoopInsertionTool : public USingleSelectionMeshEditingTool, public IHoverBehaviorTarget, public IClickBehaviorTarget
{
	GENERATED_BODY()
public:

	friend class UEdgeLoopInsertionOperatorFactory;
	friend class FEdgeLoopInsertionChange;

	UEdgeLoopInsertionTool() {};

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void OnTick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override;

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;

	// IHoverBehaviorTarget
	virtual FInputRayHit BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos) override;
	virtual void OnBeginHover(const FInputDeviceRay& DevicePos) override {}
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;
	virtual void OnEndHover() override;

	// IClickBehaviorTarget
	virtual FInputRayHit IsHitByClick(const FInputDeviceRay& ClickPos) override;
	virtual void OnClicked(const FInputDeviceRay& ClickPos) override;

protected:

	UPROPERTY()
	TObjectPtr<UEdgeLoopInsertionProperties> Settings = nullptr;

	TSharedPtr<FDynamicMesh3, ESPMode::ThreadSafe> CurrentMesh;
	TSharedPtr<FGroupTopology, ESPMode::ThreadSafe> CurrentTopology;
	UE::Geometry::FDynamicMeshAABBTree3 MeshSpatial;
	FGroupTopologySelector TopologySelector;

	TArray<TPair<FVector3d, FVector3d>> PreviewEdges;

	// Used to highlight problematic topology (non-quad groups) when it stops a loop.
	TArray<TPair<FVector3d, FVector3d>> ProblemTopologyEdges;
	TArray<FVector3d> ProblemTopologyVerts;

	FViewCameraState CameraState;

	UPROPERTY()
	TObjectPtr<UMeshOpPreviewWithBackgroundCompute> Preview;

	FToolDataVisualizer ExistingEdgesRenderer;
	FToolDataVisualizer PreviewEdgeRenderer;
	FToolDataVisualizer ProblemTopologyRenderer;
	FGroupTopologySelector::FSelectionSettings TopologySelectorSettings;
	float ProblemVertTickWidth = 8;

	void SetupPreview();

	FInputRayHit HitTest(const FRay& WorldRay);
	bool UpdateHoveredItem(const FRay& WorldRay);

	void ConditionallyUpdatePreview(int32 NewGroupID, double* NewInputLength = nullptr);
	void ClearPreview();

	// Taken from user interaction, read as inputs by the op factory
	int32 InputGroupEdgeID = FDynamicMesh3::InvalidID;
	double InteractiveInputLength = 0;

	// Lets us reset the preview to the original mesh using the op
	bool bShowingBaseMesh = false;

	// On valid clicks, we wait to finish the background op and apply it before taking more input.
	// Gets reset OnTick when the result is ready.
	bool bWaitingForInsertionCompletion = false;

	// Copied over on op completion
	bool bLastComputeSucceeded = false;
	TSharedPtr<FGroupTopology, ESPMode::ThreadSafe> LatestOpTopologyResult;
	TSharedPtr<TSet<int32>, ESPMode::ThreadSafe> LatestOpChangedTids;

	// Used to expire undo/redo changes on op shutdown.
	int32 CurrentChangeStamp = 0;

	/** 
	 * Expires the tool-associated changes in the undo/redo stack. The ComponentTarget
	 * changes will stay (we want this).
	 */
	inline void ExpireChanges()
	{
		++CurrentChangeStamp;
	}


};

/**
 * Wraps a FDynamicMeshChange so that it can be expired and so that other data
 * structures in the tool can be updated.
 */
class MESHMODELINGTOOLS_API FEdgeLoopInsertionChange : public FToolCommandChange
{
public:
	FEdgeLoopInsertionChange(TUniquePtr<UE::Geometry::FDynamicMeshChange> MeshChangeIn, int32 CurrentChangeStamp)
		: MeshChange(MoveTemp(MeshChangeIn))
		, ChangeStamp(CurrentChangeStamp)
	{};

	virtual void Apply(UObject* Object) override;
	virtual void Revert(UObject* Object) override;
	virtual bool HasExpired(UObject* Object) const override
	{
		return Cast<UEdgeLoopInsertionTool>(Object)->CurrentChangeStamp != ChangeStamp;
	}
	virtual FString ToString() const override
	{
		return TEXT("FEdgeLoopInsertionChange");
	}

protected:
	TUniquePtr<UE::Geometry::FDynamicMeshChange> MeshChange;
	int32 ChangeStamp;
};