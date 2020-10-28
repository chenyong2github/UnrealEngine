// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "ModelingOperators.h" //IDynamicMeshOperatorFactory
#include "InteractiveTool.h" //UInteractiveToolPropertySet
#include "InteractiveToolBuilder.h" //UInteractiveToolBuilder
#include "InteractiveToolChange.h" //FToolCommandChange
#include "MeshOpPreviewHelpers.h" //FDynamicMeshOpResult
#include "Operations/GroupEdgeInserter.h"
#include "Selection/GroupTopologySelector.h"
#include "SingleSelectionTool.h"
#include "ToolDataVisualizer.h"

#include "GroupEdgeInsertionTool.generated.h"

class UDynamicMeshReplacementChangeTarget;

UCLASS()
class MESHMODELINGTOOLS_API UGroupEdgeInsertionToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IToolsContextAssetAPI* AssetAPI = nullptr;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};

UENUM()
enum class EGroupEdgeInsertionMode
{
	/** Existing groups will be deleted and new triangles will be created for the new groups.
	 Keeps topology simple but breaks non-planar groups and loses the UV's. */
	Retriangulate,

	/** Keeps existing triangles and cuts them to create a new path. May result in fragmented triangles over time.*/
	PlaneCut
};

UCLASS()
class MESHMODELINGTOOLS_API UGroupEdgeInsertionProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** Determines how group edges are added to the geometry */
	UPROPERTY(EditAnywhere, Category = InsertEdge)
	EGroupEdgeInsertionMode InsertionMode = EGroupEdgeInsertionMode::PlaneCut;

	UPROPERTY(EditAnywhere, Category = InsertEdge)
	bool bWireframe = true;

	/** How close a new loop edge needs to pass next to an existing vertex to use that vertex rather than creating a new one (used for plane cut). */
	UPROPERTY(EditAnywhere, Category = InsertEdgeLoop, AdvancedDisplay)
	double VertexTolerance = 0.001;
};

UCLASS()
class MESHMODELINGTOOLS_API UGroupEdgeInsertionOperatorFactory : public UObject, public IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:
	// IDynamicMeshOperatorFactory API
	virtual TUniquePtr<FDynamicMeshOperator> MakeNewOperator() override;

	UPROPERTY()
	UGroupEdgeInsertionTool* Tool;
};

/** Tool for inserting group edges into polygons of the mesh. */
UCLASS()
class MESHMODELINGTOOLS_API UGroupEdgeInsertionTool : public USingleSelectionTool, public IHoverBehaviorTarget, public IClickBehaviorTarget
{
	GENERATED_BODY()

	enum class EToolState
	{
		GettingStart,
		GettingEnd,
		WaitingForInsertComplete
	};

public:

	friend class UGroupEdgeInsertionOperatorFactory;
	friend class FGroupEdgeInsertionFirstPointChange;
	friend class FGroupEdgeInsertionChangeBookend;

	UGroupEdgeInsertionTool() {};

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void SetWorld(UWorld* World) { TargetWorld = World; }
	virtual void SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn) { AssetAPI = AssetAPIIn; }

	virtual void OnTick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }

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
	UGroupEdgeInsertionProperties* Settings = nullptr;

	UPROPERTY()
	UMeshOpPreviewWithBackgroundCompute* Preview;

	UWorld* TargetWorld;
	IToolsContextAssetAPI* AssetAPI;

	TSharedPtr<FDynamicMesh3> CurrentMesh;
	TSharedPtr<FGroupTopology> CurrentTopology;
	FDynamicMeshAABBTree3 MeshSpatial;
	FGroupTopologySelector TopologySelector;

	TArray<TPair<FVector3d, FVector3d>> PreviewEdges;
	TArray<FVector3d> PreviewPoints;

	FViewCameraState CameraState;

	FToolDataVisualizer ExistingEdgesRenderer;
	FToolDataVisualizer PreviewEdgeRenderer;
	FGroupTopologySelector::FSelectionSettings TopologySelectorSettings;


	// Inputs from user interaction:
	FGroupEdgeInserter::FGroupEdgeSplitPoint StartPoint;
	int32 StartTopologyID = FDynamicMesh3::InvalidID;
	bool bStartIsCorner = false;

	FGroupEdgeInserter::FGroupEdgeSplitPoint EndPoint;
	int32 EndTopologyID = FDynamicMesh3::InvalidID;
	bool bEndIsCorner = false;

	int32 CommonGroupID = FDynamicMesh3::InvalidID;
	int32 CommonBoundaryIndex = FDynamicMesh3::InvalidID;


	// State control:
	EToolState ToolState = EToolState::GettingStart;
	bool bShowingBaseMesh = false;
	bool bLastComputeSucceeded = false;
	TSharedPtr<FGroupTopology> LatestOpTopologyResult;

	int32 CurrentChangeStamp = 0;

	void SetupPreview();

	bool TopologyHitTest(const FRay& WorldRay, FVector3d& RayPositionOut, FRay3d* LocalRayOut = nullptr);
	bool GetHoveredItem(const FRay& WorldRay, FGroupEdgeInserter::FGroupEdgeSplitPoint& PointOut,
		int32& TopologyElementIDOut, bool& bIsCornerOut, FVector3d& PositionOut, FRay3d* LocalRayOut = nullptr);

	void ConditionallyUpdatePreview(const FGroupEdgeInserter::FGroupEdgeSplitPoint& NewEndPoint, 
		int32 NewEndTopologyID, bool bNewEndIsCorner, int32 NewCommonGroupID, int32 NewBoundaryIndex);

	void ClearPreview(bool bClearDrawnElements = true, bool bForce = false);

	void GetCornerTangent(int32 CornerID, int32 GroupID, int32 BoundaryIndex, FVector3d& TangentOut);

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
 * This should get emitted when selecting the first point in an edge insertion so that we can undo it.
 */
class MESHMODELINGTOOLS_API FGroupEdgeInsertionFirstPointChange : public FToolCommandChange
{
public:
	FGroupEdgeInsertionFirstPointChange(int32 CurrentChangeStamp)
		: ChangeStamp(CurrentChangeStamp)
	{};

	virtual void Apply(UObject* Object) override {}; 
	virtual void Revert(UObject* Object) override;
	virtual bool HasExpired(UObject* Object) const override
	{
		UGroupEdgeInsertionTool* Tool = Cast<UGroupEdgeInsertionTool>(Object);
		return bHaveDoneUndo || Tool->CurrentChangeStamp != ChangeStamp 
			|| Tool->ToolState != UGroupEdgeInsertionTool::EToolState::GettingEnd;
		// TODO: this is a bit of a hack in that we should probably have a separate stamp
		// for expiring these instead of letting the tool state help (they expire after
		// each new insertion unlike the other changes, which expire on tool close).
	}
	virtual FString ToString() const override
	{
		return TEXT("FGroupEdgeInsertionFirstPointChange");
	}

protected:
	int32 ChangeStamp;
	bool bHaveDoneUndo = false;
};

/**
 * This should get emitted on either side of the ComponentTarget that occurs when a second
 * point is successfully picked so that the tool can reload the current mesh from the changed
 * target.
 * TODO: This is a hack similar to FEdgeLoopInsertionChangeBookend but it works. Is there a cleaner way?
 */
class MESHMODELINGTOOLS_API FGroupEdgeInsertionChangeBookend : public FToolCommandChange
{
public:
	FGroupEdgeInsertionChangeBookend(int32 CurrentChangeStamp, bool bBeforeChangeIn)
		: ChangeStamp(CurrentChangeStamp)
		, bBeforeChange(bBeforeChangeIn)
	{};

	virtual void Apply(UObject* Object) override;
	virtual void Revert(UObject* Object) override;
	virtual bool HasExpired(UObject* Object) const override
	{
		return Cast<UGroupEdgeInsertionTool>(Object)->CurrentChangeStamp != ChangeStamp;
	}
	virtual FString ToString() const override
	{
		return TEXT("FGroupEdgeInsertionChangeBookend");
	}

protected:
	int32 ChangeStamp;
	bool bBeforeChange;
};

