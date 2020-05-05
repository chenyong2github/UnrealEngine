// Copyright Epic Games, Inc. All Rights Reserved.

// HoleFillTool: Fill one or more boundary loops on a selected mesh. Several hole-filling methods are available.

#pragma once

#include "CoreMinimal.h"
#include "InteractiveToolBuilder.h"
#include "SingleSelectionTool.h"
#include "CleaningOps/HoleFillOp.h"
#include "BaseTools/MeshSurfacePointTool.h"
#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "DynamicMeshAABBTree3.h"
#include "GroupTopology.h"
#include "HoleFillTool.generated.h"

class UPolygonSelectionMechanic;
class UDynamicMeshReplacementChangeTarget;
class UMeshOpPreviewWithBackgroundCompute;
class USimpleDynamicMeshComponent;
struct FDynamicMeshOpResult;
class UHoleFillTool;

/*
 * Tool builder
 */

UCLASS()
class MESHMODELINGTOOLS_API UHoleFillToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};

/*
 * Properties
 */

UCLASS()
class MESHMODELINGTOOLS_API UHoleFillToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Options)
	EHoleFillOpFillType FillType = EHoleFillOpFillType::PolygonEarClipping;
};


UENUM()
enum class EHoleFillToolActions
{
	NoAction,
	SelectAll,
	ClearSelection
};

UCLASS()
class MESHMODELINGTOOLS_API UHoleFillToolActions : public UInteractiveToolPropertySet
{
GENERATED_BODY()

	TWeakObjectPtr<UHoleFillTool> ParentTool;

public:

	void Initialize(UHoleFillTool* ParentToolIn)
	{
		ParentTool = ParentToolIn;
	}

	void PostAction(EHoleFillToolActions Action);

	UFUNCTION(CallInEditor, Category = SelectionEdits, meta = (DisplayName = "Select All", DisplayPriority = 1))
		void SelectAll()
	{
		PostAction(EHoleFillToolActions::SelectAll);
	}

	UFUNCTION(CallInEditor, Category = SelectionEdits, meta = (DisplayName = "Clear", DisplayPriority = 1))
		void Clear()
	{
		PostAction(EHoleFillToolActions::ClearSelection);
	}
};

/*
 * Operator factory
 */

UCLASS()
class MESHMODELINGTOOLS_API UHoleFillOperatorFactory : public UObject, public IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:

	TUniquePtr<FDynamicMeshOperator> MakeNewOperator() override;

	UPROPERTY()
	UHoleFillTool* FillTool;
};

/*
 * Tool
 * Inherit from IClickBehaviorTarget so we can click on boundary loops.
 */

UCLASS()
class MESHMODELINGTOOLS_API UHoleFillTool : public USingleSelectionTool, public IClickBehaviorTarget, public IHoverBehaviorTarget
{
	GENERATED_BODY()

public:

	// UMeshSurfacePointTool
	void Setup() override;
	void OnTick(float DeltaTime) override;
	void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;
	bool HasCancel() const override { return true; }
	bool HasAccept() const override { return true; }
	bool CanAccept() const override;
	void Shutdown(EToolShutdownType ShutdownType) override;

	// IClickBehaviorTarget
	FInputRayHit IsHitByClick(const FInputDeviceRay& ClickPos) override;
	void OnClicked(const FInputDeviceRay& ClickPos) override;

	// IHoverBehaviorTarget
	FInputRayHit BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos) override;
	void OnBeginHover(const FInputDeviceRay& DevicePos) override {}
	bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;
	void OnEndHover() override;

	virtual void RequestAction(EHoleFillToolActions Action);

protected:

	friend UHoleFillOperatorFactory;
	friend UHoleFillToolBuilder;

	UPROPERTY()
	UHoleFillToolProperties* Properties = nullptr;

	UPROPERTY()
	UHoleFillToolActions* Actions = nullptr;

	UPROPERTY()
	UMeshOpPreviewWithBackgroundCompute* Preview = nullptr;

	UPROPERTY()
	UPolygonSelectionMechanic* SelectionMechanic = nullptr;

	// Input mesh
	const FDynamicMesh3 OriginalMesh;

	// UV Scale factor is cached based on the bounding box of the mesh before any fills are performed
	float MeshUVScaleFactor = 0.0f;

	// World in which to create the Preview mesh actor
	UWorld* TargetWorld = nullptr;

	// Used for hit querying
	FDynamicMeshAABBTree3 MeshSpatial;

	TSet<int32> NewTriangleIDs;

	void SetWorld(UWorld* World);

	// Create the Preview object
	void SetupPreview();

	bool bHavePendingAction = false;
	EHoleFillToolActions PendingAction;
	virtual void ApplyAction(EHoleFillToolActions ActionType);
	void SelectAll();
	void ClearSelection();

	// Object used to get boundary loop information. All triangles return the same GroupID, so the only boundaries that 
	// are returned are the the actual mesh boundaries.
	// TODO: It seems like overkill to use a FGroupTopology subclass when we don't actually care about groups.
	class FBasicTopology : public FGroupTopology
	{
	public:
		FBasicTopology(const FDynamicMesh3* Mesh, bool bAutoBuild) :
			FGroupTopology(Mesh, bAutoBuild)
		{}

		int GetGroupID(int TriangleID) const override
		{
			return Mesh->IsTriangle(TriangleID) ? 1 : 0;
		}
	};
	TUniquePtr<FBasicTopology> Topology;

	struct FSelectedBoundaryLoop
	{
		int32 EdgeTopoID;
		TArray<int32> EdgeIDs;
	};
	TArray<FSelectedBoundaryLoop> ActiveBoundaryLoopSelection;
	void UpdateActiveBoundaryLoopSelection();

	// Just call the SelectionMechanism's Render function
	void Render(IToolsContextRenderAPI* RenderAPI) override;

	// Populate an array of Edge Loops to be processed by an FHoleFillOp. Returns the edge loops currently selected
	// by this tool.
	void GetLoopsToFill(TArray<FEdgeLoop>& OutLoops) const;

};
