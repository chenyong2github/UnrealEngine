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
 * Properties. This class reflects the parameters in FSmoothFillOptions, but is decorated to allow use in the UI system.
 */

UCLASS()
class MESHMODELINGTOOLS_API USmoothHoleFillProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:

	/** Allow smoothing and remeshing of triangles outside of the fill region */
	UPROPERTY(EditAnywhere, Category = SmoothHoleFillOptions)
	bool bConstrainToHoleInterior;

	/** Number of vertex rings outside of the fill region to allow remeshing */
	UPROPERTY(EditAnywhere, Category = SmoothHoleFillOptions, 
		meta = (UIMin = "0", ClampMin = "0", EditCondition = "!bConstrainToHoleInterior"))
	int RemeshingExteriorRegionWidth;

	/** Number of vertex rings outside of the fill region to perform smoothing */
	UPROPERTY(EditAnywhere, Category = SmoothHoleFillOptions, meta = (UIMin = "0", ClampMin = "0"))
	int SmoothingExteriorRegionWidth;

	/** Number of vertex rings away from the fill region boundary to constrain smoothing */
	UPROPERTY(EditAnywhere, Category = SmoothHoleFillOptions, meta = (UIMin = "0", ClampMin = "0"))
	int SmoothingInteriorRegionWidth;

	/** Desired Smoothness. This is not a linear quantity, but larger numbers produce smoother results */
	UPROPERTY(EditAnywhere, Category = SmoothHoleFillOptions, meta = (UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "100.0"))
	float InteriorSmoothness;

	/** Relative triangle density of fill region */
	UPROPERTY(EditAnywhere, Category = SmoothHoleFillOptions, meta = (UIMin = "0.001", UIMax = "10.0", ClampMin = "0.001", ClampMax = "10.0"))
	double FillDensityScalar = 1.0;

	/** 
	 * Whether to project to the original mesh during post-smooth remeshing. This can be expensive on large meshes with 
	 * many holes. 
	 */
	UPROPERTY(EditAnywhere, Category = SmoothHoleFillOptions)
	bool bProjectDuringRemesh = false;


	// Set default property values
	USmoothHoleFillProperties()
	{
		// Create a default FSmoothFillOptions and populate this class with its defaults.
		FSmoothFillOptions DefaultOptionsObject;
		bConstrainToHoleInterior = DefaultOptionsObject.bConstrainToHoleInterior;
		RemeshingExteriorRegionWidth = DefaultOptionsObject.RemeshingExteriorRegionWidth;
		SmoothingExteriorRegionWidth = DefaultOptionsObject.SmoothingExteriorRegionWidth;
		SmoothingInteriorRegionWidth = DefaultOptionsObject.SmoothingInteriorRegionWidth;
		FillDensityScalar = DefaultOptionsObject.FillDensityScalar;
		InteriorSmoothness = DefaultOptionsObject.InteriorSmoothness;
		bProjectDuringRemesh = DefaultOptionsObject.bProjectDuringRemesh;
	}

	FSmoothFillOptions ToSmoothFillOptions() const
	{
		FSmoothFillOptions Options;
		Options.bConstrainToHoleInterior = bConstrainToHoleInterior;
		Options.RemeshingExteriorRegionWidth = RemeshingExteriorRegionWidth;
		Options.SmoothingExteriorRegionWidth = SmoothingExteriorRegionWidth;
		Options.SmoothingInteriorRegionWidth = SmoothingInteriorRegionWidth;
		Options.InteriorSmoothness = InteriorSmoothness;
		Options.FillDensityScalar = FillDensityScalar;
		Options.bProjectDuringRemesh = bProjectDuringRemesh;
		return Options;
	}
};

UCLASS()
class MESHMODELINGTOOLS_API UHoleFillToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Options)
	EHoleFillOpFillType FillType = EHoleFillOpFillType::Minimal;

	/** Clean up triangles that have no neighbors */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bRemoveIsolatedTriangles = false;

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


UCLASS()
class MESHMODELINGTOOLS_API UHoleFillStatisticsProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:

	UPROPERTY(VisibleAnywhere, Category = HoleFillStatistics)
	FString InitialHoles;

	UPROPERTY(VisibleAnywhere, Category = HoleFillStatistics)
	FString SelectedHoles;

	UPROPERTY(VisibleAnywhere, Category = HoleFillStatistics)
	FString SuccessfulFills;

	UPROPERTY(VisibleAnywhere, Category = HoleFillStatistics)
	FString FailedFills;

	UPROPERTY(VisibleAnywhere, Category = HoleFillStatistics)
	FString RemainingHoles;

	void Initialize(const UHoleFillTool& HoleFillTool);

	void Update(const UHoleFillTool& HoleFillTool, const FHoleFillOp& HoleFillOp);
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
	friend UHoleFillStatisticsProperties;

	UPROPERTY()
	USmoothHoleFillProperties* SmoothHoleFillProperties = nullptr;

	UPROPERTY()
	UHoleFillToolProperties* Properties = nullptr;

	UPROPERTY()
	UHoleFillToolActions* Actions = nullptr;

	UPROPERTY()
	UHoleFillStatisticsProperties* Statistics = nullptr;

	UPROPERTY()
	UMeshOpPreviewWithBackgroundCompute* Preview = nullptr;

	UPROPERTY()
	UPolygonSelectionMechanic* SelectionMechanic = nullptr;

	// Input mesh. Ownership shared with Op.
	TSharedPtr<FDynamicMesh3> OriginalMesh;

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

	// Invalidate background compute result (some input changed)
	void InvalidatePreviewResult();

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
