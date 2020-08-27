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

#include "EdgeLoopInsertionTool.generated.h"

class UDynamicMeshReplacementChangeTarget;

UCLASS()
class MESHMODELINGTOOLS_API UEdgeLoopInsertionToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IToolsContextAssetAPI* AssetAPI = nullptr;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
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
	 Keeps topology simple but breaks non-planar groups and loses the UV's. */
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

	/** How close a new loop edge needs to pass next to an existing vertex to use that vertex rather than creating a new one. */
	UPROPERTY(EditAnywhere, Category = InsertEdgeLoop, AdvancedDisplay)
	double VertexTolerance = 0.001;
};

UCLASS()
class MESHMODELINGTOOLS_API UEdgeLoopInsertionOperatorFactory : public UObject, public IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:
	// IDynamicMeshOperatorFactory API
	virtual TUniquePtr<FDynamicMeshOperator> MakeNewOperator() override;

	UPROPERTY()
	UEdgeLoopInsertionTool* Tool;
};

/** Tool for inserting (group) edge loops into a mesh. */
UCLASS()
class MESHMODELINGTOOLS_API UEdgeLoopInsertionTool : public USingleSelectionTool, public IHoverBehaviorTarget, public IClickBehaviorTarget
{
	GENERATED_BODY()

public:

	friend class UEdgeLoopInsertionOperatorFactory;
	friend class FEdgeLoopInsertionChangeBookend;

	UEdgeLoopInsertionTool() {};

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void SetWorld(UWorld* World) { TargetWorld = World; }
	virtual void SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn) { AssetAPI = AssetAPIIn; }

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
	UEdgeLoopInsertionProperties* Settings = nullptr;

	TSharedPtr<FDynamicMesh3> CurrentMesh;
	TSharedPtr<FGroupTopology> CurrentTopology;
	FDynamicMeshAABBTree3 MeshSpatial;
	FGroupTopologySelector TopologySelector;

	TArray<TPair<FVector3d, FVector3d>> PreviewEdges;

	FViewCameraState CameraState;

	UPROPERTY()
	UMeshOpPreviewWithBackgroundCompute* Preview;

	UWorld* TargetWorld;
	IToolsContextAssetAPI* AssetAPI;

	FToolDataVisualizer ExistingEdgesRenderer;
	FToolDataVisualizer PreviewEdgeRenderer;
	FGroupTopologySelector::FSelectionSettings TopologySelectorSettings;

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
	TSharedPtr<FGroupTopology> LatestOpTopologyResult;

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
 * This change object is a bit of a hack: if it is emitted on both sides of the associated
 * ComponentTarget change, it will reload the current mesh and topology from the target
 * on Undo/Redo, thereby propagating it to the tool.
 */
class MESHMODELINGTOOLS_API FEdgeLoopInsertionChangeBookend : public FToolCommandChange
{
public:
	FEdgeLoopInsertionChangeBookend(int32 CurrentChangeStamp, bool bBeforeChangeIn)
		: ChangeStamp(CurrentChangeStamp) 
		, bBeforeChange(bBeforeChangeIn)
	{};

	virtual void Apply(UObject* Object) override;
	virtual void Revert(UObject* Object) override;
	virtual bool HasExpired(UObject* Object) const override
	{
		return Cast<UEdgeLoopInsertionTool>(Object)->CurrentChangeStamp != ChangeStamp;
	}
	virtual FString ToString() const override
	{
		return TEXT("FEdgeLoopInsertionChangeBookend");
	}

protected:
	int32 ChangeStamp;
	bool bBeforeChange;
};