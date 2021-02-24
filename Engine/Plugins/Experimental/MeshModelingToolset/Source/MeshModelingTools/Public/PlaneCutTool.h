// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "InteractiveGizmo.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "MeshOpPreviewHelpers.h"
#include "DynamicMesh3.h"
#include "Changes/DynamicMeshChangeTarget.h"
#include "BaseTools/SingleClickTool.h"
#include "PlaneCutTool.generated.h"


// predeclarations
class UTransformGizmo;
class UTransformProxy;





/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UPlaneCutToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IToolsContextAssetAPI* AssetAPI = nullptr;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};



/**
* Properties controlling how changes are baked out to static meshes on tool accept
*/
UCLASS()
class MESHMODELINGTOOLS_API UAcceptOutputProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:

	/** If true, meshes cut into multiple pieces will be saved as separate assets on 'accept'. */
	UPROPERTY(EditAnywhere, Category = ToolOutputOptions)
	bool bExportSeparatedPiecesAsNewMeshAssets = true;
};






/**
 * Standard properties of the plane cut operation
 */
UCLASS()
class MESHMODELINGTOOLS_API UPlaneCutToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPlaneCutToolProperties();

	/** Snap the cut plane to the world grid */
	UPROPERTY(EditAnywhere, Category = Snapping)
	bool bSnapToWorldGrid = false;

	/** If true, both halves of the cut are computed */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bKeepBothHalves;

	/** If keeping both halves, separate the two pieces by this amount */
	UPROPERTY(EditAnywhere, Category = Options, meta = (EditCondition = "bKeepBothHalves == true", UIMin = "0", ClampMin = "0") )
	float SpacingBetweenHalves;

	/** If true, the cut surface is filled with simple planar hole fill surface(s) */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bFillCutHole;

	UPROPERTY(EditAnywhere, Category = Options)
	bool bShowPreview;

	UPROPERTY(EditAnywhere, Category = Options, AdvancedDisplay)
	bool bFillSpans;
};




UCLASS()
class MESHMODELINGTOOLS_API UPlaneCutOperatorFactory : public UObject, public IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:
	// IDynamicMeshOperatorFactory API
	virtual TUniquePtr<FDynamicMeshOperator> MakeNewOperator() override;

	UPROPERTY()
	UPlaneCutTool *CutTool;

	int ComponentIndex;
};

/**
 * Simple Mesh Plane Cutting Tool
 */
UCLASS()
class MESHMODELINGTOOLS_API UPlaneCutTool : public UMultiSelectionTool, public IModifierToggleBehaviorTarget
{
	GENERATED_BODY()

public:

	friend UPlaneCutOperatorFactory;

	UPlaneCutTool();

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void SetWorld(UWorld* World);
	virtual void SetAssetAPI(IToolsContextAssetAPI* AssetAPI);

	virtual void RegisterActions(FInteractiveToolActionSet& ActionSet) override;

	virtual void OnTick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent &PropertyChangedEvent) override;
#endif

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;

	// IClickSequenceBehaviorTarget implementation
	virtual void OnUpdateModifierState(int ModifierID, bool bIsOn) override;


protected:

	UPROPERTY()
	UPlaneCutToolProperties* BasicProperties;

	UPROPERTY()
	UAcceptOutputProperties* AcceptProperties;

	/** Origin of cutting plane */
	UPROPERTY()
	FVector CutPlaneOrigin;

	/** Orientation of cutting plane */
	UPROPERTY()
	FQuat CutPlaneOrientation;

	UPROPERTY()
	TArray<UMeshOpPreviewWithBackgroundCompute*> Previews;

	/** Cut with the current plane without exiting the tool */
	UFUNCTION(CallInEditor, Category = Actions, meta = (DisplayName = "Cut"))
	void Cut();

protected:

	UPROPERTY()
	TArray<UDynamicMeshReplacementChangeTarget*> MeshesToCut;

	// UV Scale factor is cached based on the bounding box of the mesh before any cuts are performed, so you don't get inconsistent UVs if you multi-cut the object to smaller sizes
	TArray<float> MeshUVScaleFactor;

	UWorld* TargetWorld;
	IToolsContextAssetAPI* AssetAPI;

	FViewCameraState CameraState;

	// flags used to identify modifier keys/buttons
	static const int IgnoreSnappingModifier = 1;
	bool bIgnoreSnappingToggle = false;		// toggled by hotkey (shift)

	UPROPERTY()
	UTransformGizmo* PlaneTransformGizmo;

	UPROPERTY()
	UTransformProxy* PlaneTransformProxy;

	void TransformChanged(UTransformProxy* Proxy, FTransform Transform);
	void MeshChanged();

	void SetupPreviews();

	IClickBehaviorTarget* SetPointInWorldConnector = nullptr;

	virtual void SetCutPlaneFromWorldPos(const FVector& Position, const FVector& Normal, bool bIsInitializing);

	void GenerateAsset(const TArray<FDynamicMeshOpResult>& Results);
};
